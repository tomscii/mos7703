/*
 * mos7703.c
 *   Controls the Moschip 7703 usb to single port serial converter
 *
 * Copyright 2005 Moschip Semiconductor Tech. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Developed by:
 *      Eshwar Danduri
 *	Ravikanth G
 *	Sandilya Bhagi
 *
 * Cleaned up from the original and ported to latest Linux kernels by:
 *	Tom Szilagyi <tomszilagyi@gmail.com>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/sched/signal.h>


#define DRIVER_AUTHOR "Moschip Semiconductor Tech. Ltd."
#define DRIVER_DESC "Moschip 7703 USB Serial Driver"

#define USB_VENDOR_ID_MOSCHIP		0x9710
#define MOSCHIP_DEVICE_ID_7703		0x7703

static struct usb_device_id id_table[] = {
	{USB_DEVICE(USB_VENDOR_ID_MOSCHIP, MOSCHIP_DEVICE_ID_7703)},
	{} /* terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);


#ifndef NUM_ENTRIES
#define NUM_ENTRIES(x)	(sizeof(x)/sizeof((x)[0]))
#endif

#define NUM_URBS                        32	/* URB Count */
#define URB_TRANSFER_BUFFER_SIZE        32	/* URB Size  */

/* This structure holds all of the local port information */
struct moschip_port {
	struct urb *write_urb;	/* write URB for this port */
	struct urb *read_urb;	/* read URB for this port */

	u8 shadowLCR;		/* last LCR value received */
	u8 shadowMCR;		/* last MCR value received */
	u8 shadowMSR;		/* last MSR value received */

	char open;
	char openPending;
	char closePending;

        /* for handling sleeping while waiting for msr change to happen */
	wait_queue_head_t delta_msr_wait;
	int delta_msr_cond;

	struct async_icount icount;
	struct usb_serial_port *port; /* loop back to the owner of this object */
	struct tty_struct *tty;
	struct urb *write_urb_pool[NUM_URBS];
};

struct divisor_table_entry {
	u32 BaudRate;
	u16 Divisor;
};

/* Define table of divisors for Rev A moschip 7703 hardware
 * These assume a 3.6864MHz crystal, the standard /16, and
 * MCR.7 = 0.
 */
static struct divisor_table_entry divisor_table[] = {
	{50, 2304},
	{110, 1047}, /* 2094.545455 => 230450   => .0217 % over */
	{134, 857},  /* 1713.011152 => 230398.5 => .00065% under */
	{150, 768},
	{300, 384},
	{600, 192},
	{1200, 96},
	{1800, 64},
	{2400, 48},
	{4800, 24},
	{7200, 16},
	{9600, 12},
	{19200, 6},
	{38400, 3},
	{57600, 2},
	{115200, 1},
};

/* Defines used for sending commands to port */

#define WAIT_FOR_EVER   (HZ * 0)	/* timeout urb is wait for ever */
#define MOS_WDR_TIMEOUT (HZ * 5)	/* default urb timeout */

#define MOS_UART_REG    0x0300
#define MOS_VEN_REG     0x0000

#define MOS_WRITE       0x0E
#define MOS_READ        0x0D

/* UART register constants */

#define IER			1	// ! Interrupt Enable Register
#define FCR			2	// ! Fifo Control Register (Write)
#define LCR			3	// Line Control Register
#define MCR			4	// Modem Control Register

#define LCR_BITS_5		0x00	// 5 bits/char
#define LCR_BITS_6		0x01	// 6 bits/char
#define LCR_BITS_7		0x02	// 7 bits/char
#define LCR_BITS_8		0x03	// 8 bits/char
#define LCR_BITS_MASK		0x03	// Mask for bits/char field

#define LCR_STOP_1		0x00	// 1 stop bit
                                        // 1.5 stop bits (if 5   bits/char)
#define LCR_STOP_2		0x04	// 2 stop bits   (if 6-8 bits/char)
#define LCR_STOP_MASK		0x04	// Mask for stop bits field

#define LCR_PAR_NONE		0x00	// No parity
#define LCR_PAR_ODD		0x08	// Odd parity
#define LCR_PAR_EVEN		0x18	// Even parity
#define LCR_PAR_MASK		0x38	// Mask for parity field

#define LCR_DL_ENABLE		0x80	// Enable access to divisor latch

#define MCR_DTR			0x01	// Assert DTR
#define MCR_RTS			0x02	// Assert RTS
#define MCR_MASTER_IE		0x08	// Enable interrupt outputs
#define MCR_LOOPBACK		0x10	// Set internal (digital) loopback mode
#define MCR_XON_ANY		0x20	// Enable any char to exit XOFF mode

#define MOS7703_MSR_CTS		0x10	// Current state of CTS
#define MOS7703_MSR_DSR		0x20	// Current state of DSR
#define MOS7703_MSR_RI		0x40	// Current state of RI
#define MOS7703_MSR_CD		0x80	// Current state of CD


/************************************************************************/
/*            U S B  C A L L B A C K   F U N C T I O N S                */
/************************************************************************/

/*****************************************************************************
 * mos7703_interrupt_callback
 * this is the callback function for when we have received data on the 
 * interrupt endpoint.
 * Input : 1 Input
 *   pointer to the URB packet,
 *****************************************************************************/
static void mos7703_interrupt_callback(struct urb *urb)
{
	int length;
	int result;
	struct device *dev = &urb->dev->dev;
	int status = urb->status;
	u32 *data;

	switch (status) {
	case 0:	break; /* success */
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(dev, "%s - urb shutting down with status: %d\n", __func__, status);
		return;
	default:
		dev_dbg(dev, "%s - nonzero urb status received: %d\n", __func__, status);
		goto exit;
	}

	length = urb->actual_length;
	data = urb->transfer_buffer;


	/* Moschip get 4 bytes 
	 * Byte 1 IIR Port 1 (port.number is 0)
	 * Byte 2 IIR Port 2 (port.number is 1)
	 * Byte 3 --------------
	 * Byte 4 FIFO status for both 
	 */
	if (unlikely(length && length > 4)) {
		dev_dbg(dev, "%s: Wrong data!\n", __func__);
		return;
	}

 exit:
	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result) {
		dev_err(dev, "%s - Error %d submitting control urb\n", __func__, result);
	}
	return;
}

/*****************************************************************************
 * mos7703_bulk_in_callback
 * this is the callback function for when we have received data on the 
 * bulk in endpoint.
 * Input : 1 Input
 *   pointer to the URB packet,
 *****************************************************************************/
static void mos7703_bulk_in_callback(struct urb *urb)
{
	int retval;
	int status = urb->status;
	unsigned char *data;
	struct moschip_port *mos7703_port = (struct moschip_port *)urb->context;
	struct device *dev = &urb->dev->dev;
	struct tty_struct *tty;

	if (status) {
		dev_dbg(dev, "nonzero read bulk status received: %d\n", status);
		return;
	}

	data = urb->transfer_buffer;
	if (urb->actual_length) {
		tty = mos7703_port->tty;
		tty_insert_flip_string(tty->port, data, urb->actual_length);
		tty_flip_buffer_push(tty->port);
	}

	if (!mos7703_port->read_urb) {
		dev_dbg(dev, "%s: URB killed!\n", __func__);
		return;
	}

	if (mos7703_port->read_urb->status != -EINPROGRESS) {
		mos7703_port->read_urb->dev = mos7703_port->port->serial->dev;
		retval = usb_submit_urb(mos7703_port->read_urb, GFP_ATOMIC);
		if (retval) {
			dev_dbg(dev, "usb_submit_urb(read bulk) failed, retval = %d\n", retval);
		}
	}
}

/*****************************************************************************
 * mos7703_bulk_out_data_callback
 * this is the callback function for when we have finished sending serial data
 * on the bulk out endpoint.
 * Input : 1 Input
 *   pointer to the URB packet,
 *****************************************************************************/
static void mos7703_bulk_out_data_callback(struct urb *urb)
{
	int status = urb->status;
	struct moschip_port *mos7703_port = (struct moschip_port *)urb->context;
	struct device *dev = &urb->dev->dev;
	struct tty_struct *tty;

	if (status) {
		dev_dbg(dev, "nonzero write bulk status received: %d\n", status);
		return;
	}

	tty = mos7703_port->tty;
	if (mos7703_port->open) {
		tty_port_tty_wakeup(tty->port);
	}
}

/*****************************************************************************
 * SendMosCmd
 * this function will be used for sending command to device
 * Input : 5 Input
 *   pointer to the serial device,
 *   request type
 *   value
 *   register index
 *   pointer to data/buffer
 *****************************************************************************/
static int SendMosCmd(struct usb_serial *serial, u8 request, u16 value,
		      u16 index, void *data)
{
	int timeout;
	int status;
	u8 requesttype;
	u16 size;
	unsigned int Pipe;

	size = 0x00;
	timeout = MOS_WDR_TIMEOUT;

	if (request == MOS_WRITE) {
		request = (u8) MOS_WRITE;
		requesttype = (u8) 0x40;
		if (data)
			value = value + (u16) * ((unsigned char *)data);
		Pipe = usb_sndctrlpipe(serial->dev, 0);
	} else {
		request = (u8) MOS_READ;
		requesttype = (u8) 0xC0;
		size = 0x01;
		Pipe = usb_rcvctrlpipe(serial->dev, 0);
	}

	status = usb_control_msg(serial->dev,
				 Pipe, request,
				 requesttype, value,
				 index, data, size, timeout);

	if (status < 0) {
		dev_err(&serial->dev->dev, "%s failed: value %x index %x\n",
			__func__, value, index);
		return status;
	}
	return status;
}


/************************************************************************/
/*       D R I V E R  T T Y  I N T E R F A C E  F U N C T I O N S       */
/************************************************************************/

#if 0 // this is unused, but may be useful to compare w/ set_high_rates() below
static int set_higher_rates(struct moschip_port *mos7703_port, int *value)
{
	unsigned int arg;
	unsigned char data;

	struct usb_serial_port *port;
	struct usb_serial *serial;

	if (mos7703_port == NULL)
		return -1;

	port = (struct usb_serial_port *)mos7703_port->port;

	arg = *value;

	dev_dbg(&port->dev, "Sending Setting Commands\n");

	data = 0x00;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x01, &data);

	data = 0x00;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x02, &data);

	data = 0xCF;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x02, &data);

	data = 0x0b;
	mos7703_port->shadowMCR = data;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x04, &data);

	data = 0x0b;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x04, &data);

	data = 0x2b;
	mos7703_port->shadowMCR = data;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x04, &data);
	data = 0x2b;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x04, &data);

	/*
	 * SET BAUD DLL/DLM
	 */

	data = mos7703_port->shadowLCR | LCR_DL_ENABLE;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x03, &data);

	data = 0x001; /*DLL */
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x00, &data);

	data = 0x000; /*DLM */
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x01, &data);

	data = mos7703_port->shadowLCR & ~LCR_DL_ENABLE;
	dev_dbg(&port->dev, "value to be written to LCR: %x\n", data);
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x03, &data);
	return 0;
}
#endif

static int set_high_rates(struct moschip_port *mos7703_port, int *value)
{
	int arg;
	unsigned char data, bypass_flag = 0;
	struct usb_serial_port *port;
	char wValue = 0;

	if (mos7703_port == NULL)
		return -1;

	port = (struct usb_serial_port *)mos7703_port->port;

	arg = *value;

	switch (arg) {
	case 115200:
		wValue = 0x00;
		break;
	case 230400:
		wValue = 0x10;
		break;
	case 403200:
		wValue = 0x20;
		break;
	case 460800:
		wValue = 0x30;
		break;
	case 806400:
		wValue = 0x40;
		break;
	case 921600:
		wValue = 0x50;
		break;
	case 1500000:
		wValue = 0x62;
		break;
	case 3000000:
		wValue = 0x70;
		break;
	case 6000000:
		bypass_flag = 1; // To Enable bypass Clock 96 MHz Clock
		break;
	default:
		return -1;

	}

	/* HIGHER BAUD HERE */

        /* Clock multi register setting for above 1x baudrate */
	data = 0x40;
	SendMosCmd(port->serial, MOS_WRITE, MOS_VEN_REG, 0x02, &data);

	usb_control_msg(port->serial->dev, usb_sndctrlpipe(port->serial->dev, 0),
			0x0E, 0x40, wValue, 0x00, NULL, 0x00, 5 * HZ);

	SendMosCmd(port->serial, MOS_READ, MOS_VEN_REG, 0x01, &data);
	data |= 0x01;
	SendMosCmd(port->serial, MOS_WRITE, MOS_VEN_REG, 0x01, &data);

	if (bypass_flag) {
		/* If true, will write 0x02 in the control register to
		   enable the 96MHz Clock. This should be done only for 6 Mbps. */
		SendMosCmd(port->serial, MOS_READ, MOS_VEN_REG, 0x01, &data);
		data |= 0x02;
		SendMosCmd(port->serial, MOS_WRITE, MOS_VEN_REG, 0x01, &data);
	}

	/* DCR0 register */
	SendMosCmd(port->serial, MOS_READ, MOS_VEN_REG, 0x04, &data);
	data |= 0x20;
	SendMosCmd(port->serial, MOS_WRITE, MOS_VEN_REG, 0x04, &data);

	data = 0x2b;
	mos7703_port->shadowMCR = data;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x04, &data);
	data = 0x2b;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x04, &data);

	/* SET BAUD DLL/DLM */
	data = mos7703_port->shadowLCR | LCR_DL_ENABLE;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x03, &data);

	data = 0x01; /* DLL */
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x00, &data);

	data = 0x00; /* DLM */
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x01, &data);

	data = mos7703_port->shadowLCR & ~LCR_DL_ENABLE;
	dev_dbg(&port->dev, "value to be written to LCR: %x\n", data);
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x03, &data);

	return 0;
}

/*****************************************************************************
 * calc_baud_rate_divisor
 * this function calculates the proper baud rate divisor for the specified
 * baud rate.
 *****************************************************************************/
static int calc_baud_rate_divisor(struct device *dev, int baudrate, int *divisor)
{
	int i;
	u16 custom;
	u16 round1;
	u16 round;

	dev_dbg(dev, "%s: baudrate=%d\n", __func__, baudrate);

	for (i = 0; i < NUM_ENTRIES(divisor_table); i++) {
		if (divisor_table[i].BaudRate == baudrate) {
			*divisor = divisor_table[i].Divisor;
			return 0;
		}
	}

	/* We have tried all of the standard baud rates;
	 * let's try to calculate the divisor for this baud rate.
	 * Make sure the baud rate is reasonable.
	 */
	if (baudrate > 75 && baudrate < 230400) {
		/* get divisor */
		custom = (u16) (230400L / baudrate);

		/* Check for round off */
		round1 = (u16) (2304000L / baudrate);
		round = (u16) (round1 - (custom * 10));
		if (round > 4) {
			custom++;
		}
		*divisor = custom;

		dev_dbg(dev, "baudrate=%d custom=%d\n", baudrate, custom);
		return 0;
	}

	dev_err(dev, "Baudrate calculation failed for baudrate=%d\n", baudrate);
	return -1;
}

/*****************************************************************************
 * send_cmd_write_baud_rate
 * this function sends the proper command to change the baud rate of the
 * specified port.
 *****************************************************************************/
static int send_cmd_write_baud_rate(struct moschip_port *mos7703_port,
				    int baudRate)
{
	int divisor;
	int status;
	unsigned char data;
	unsigned char number;
	struct usb_serial_port * port;

	if (mos7703_port == NULL)
		return -1;

	port = mos7703_port->port;
	number = port->port_number - port->minor;

	dev_dbg(&port->dev, "%s - port=%d, baud=%d\n", __func__,
		port->port_number, baudRate);
	status = calc_baud_rate_divisor(&port->dev, baudRate, &divisor);
	if (status) {
		dev_err(&port->dev, "%s - bad baud rate\n", __func__);
		return status;
	}

	/* Enable access to divisor latch */
	data = LCR_DL_ENABLE;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, LCR, &data);
	dev_dbg(&port->dev, "DLL/DLM enabled\n");

	/* Write the divisor itself */
	data = divisor & 0xff; /* LOW byte */
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x00, &data);
	dev_dbg(&port->dev, "DLL updated: value=%x\n", data);

	data = (divisor & 0xff00) >> 8; /* HIGH byte */
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x01, &data);
	dev_dbg(&port->dev, "DLM updated: value=%x\n", data);

	/* Restore original value to disable access to divisor latch */
	data = mos7703_port->shadowLCR;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x03, &data);
	dev_dbg(&port->dev, "LCR restored\n");

	return status;
}

/*****************************************************************************
 * change_port_settings
 * This routine is called to set the UART on the device to match 
 * the specified new settings.
 *****************************************************************************/
static void change_port_settings(struct tty_struct *tty,
				 struct moschip_port *mos7703_port,
				 struct ktermios *old_termios)
{
	int baud;
	unsigned cflag;
	u8 mask = 0xff;
	u8 lData;
	u8 lParity;
	u8 lStop;
	int status;
	char data;

	struct usb_serial_port *port;

	if (mos7703_port == NULL)
		return;

	port = (struct usb_serial_port *)mos7703_port->port;
	dev_dbg(&port->dev, "%s - port %d\n", __func__, port->port_number);

	if ((!mos7703_port->open) && (!mos7703_port->openPending)) {
		dev_dbg(&port->dev, "%s - port not opened\n", __func__);
		return;
	}

	lData = LCR_BITS_8;
	lStop = LCR_STOP_1;

	/* Change the data length */
	cflag = tty->termios.c_cflag;

	switch (cflag & CSIZE) {
	case CS5:
		lData = LCR_BITS_5;
		mask = 0x1f;
		dev_dbg(&port->dev, "data bits = 5\n");
		break;

	case CS6:
		lData = LCR_BITS_6;
		mask = 0x3f;
		dev_dbg(&port->dev, "data bits = 6\n");
		break;

	case CS7:
		lData = LCR_BITS_7;
		mask = 0x7f;
		dev_dbg(&port->dev, "data bits = 7\n");
		break;

	default:
	case CS8:
		lData = LCR_BITS_8;
		dev_dbg(&port->dev, "data bits = 8\n");
		break;
	}

	/* Change the Parity bit */
	if (cflag & PARENB) {
		if (cflag & PARODD) {
			lParity = LCR_PAR_ODD;
			dev_dbg(&port->dev, "parity: odd\n");
		} else {
			lParity = LCR_PAR_EVEN;
			dev_dbg(&port->dev, "parity: even\n");
		}
	} else {
		lParity = LCR_PAR_NONE;
		dev_dbg(&port->dev, "parity: none\n");
	}

	/* Change the Stop bit */
	if (cflag & CSTOPB) {
		lStop = LCR_STOP_2;
		dev_dbg(&port->dev, "stop bits: 2\n");
	} else {
		lStop = LCR_STOP_1;
		dev_dbg(&port->dev, "stop bits: 1\n");
	}

	if (cflag & CMSPAR) {
		lParity = lParity | 0x20;
	}

	/* update the LCR with the correct LCR value */
	mos7703_port->shadowLCR &= ~(LCR_BITS_MASK | LCR_STOP_MASK | LCR_PAR_MASK);
	mos7703_port->shadowLCR |= (lData | lParity | lStop);

        /* Disable Interrupts */
	data = 0x00;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, IER, &data);
	data = 0x00;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, FCR, &data);
	data = 0xcf;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, FCR, &data);

        /* Send the updated LCR value to the mos7703 */
	data = mos7703_port->shadowLCR;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, LCR, &data);

	data = 0x00b;
	mos7703_port->shadowMCR = data;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x04, &data);
	data = 0x00b;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x04, &data);

	/* set up the MCR register and send it to the mos7703 */
	mos7703_port->shadowMCR = MCR_MASTER_IE;
	if (cflag & CBAUD) {
		mos7703_port->shadowMCR |= (MCR_DTR | MCR_RTS);
	}

	if (cflag & CRTSCTS) {
		mos7703_port->shadowMCR |= (MCR_XON_ANY);
	} else {
		mos7703_port->shadowMCR &= ~(MCR_XON_ANY);
	}

	data = mos7703_port->shadowMCR;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, MCR, &data);

	/* Determine divisor based on baud rate */
	baud = tty_get_baud_rate(tty);
	dev_dbg(&port->dev, "got baud=%d from tty_get_baud_rate()\n", baud);

	if (baud > 115200) {
		set_high_rates(mos7703_port, &baud);
		/* Enable Interrupts */
		data = 0x0c;
		SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, IER, &data);
		return;
	}
	if (!baud) {
		baud = 9600;
		dev_dbg(&port->dev, "Picked default baudrate %d\n", baud);
	}

	dev_dbg(&port->dev, "%s - baud rate = %d\n", __func__, baud);
	status = send_cmd_write_baud_rate(mos7703_port, baud);
	mos7703_port->delta_msr_cond = 1;
	wake_up(&mos7703_port->delta_msr_wait);
	return;
}

/*****************************************************************************
 * SerialOpen
 * this function is called by the tty driver when a port is opened
 * If successful, we return 0
 * Otherwise we return a negative error number.
 *****************************************************************************/
static int mos7703_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	int response;
	char data;
	int j;
	struct usb_serial *serial;
	struct usb_serial_port *port0;
	struct urb *urb;
	struct ktermios *old_termios;
	struct moschip_port *mos7703_port;
	int allocated_urbs = 0;

	serial = port->serial;
	mos7703_port = usb_get_serial_port_data(port);
	if (mos7703_port == NULL) {
		return -ENODEV;
	}
	mos7703_port->tty = tty;

	port0 = serial->port[0];
	if (port0 == NULL) {
		dev_err(&port->dev, "Null serial->port[0], returning ENODEV\n");
		return -ENODEV;
	}

	/* Initialising the write urb pool */
	for (j = 0; j < NUM_URBS; ++j) {
		urb = usb_alloc_urb(0, GFP_KERNEL);
		mos7703_port->write_urb_pool[j] = urb;
		if (!urb)
			continue;

		urb->transfer_buffer = kmalloc(URB_TRANSFER_BUFFER_SIZE, GFP_KERNEL);
		if (!urb->transfer_buffer) {
			usb_free_urb(mos7703_port->write_urb_pool[j]);
			mos7703_port->write_urb_pool[j] = NULL;
			continue;
		}
		++allocated_urbs;
	}

	if (!allocated_urbs)
		return -ENOMEM;

        /* update DCR0 DCR1 DCR2 DCR3 */

	SendMosCmd(port->serial, MOS_READ, MOS_VEN_REG, 0x04, &data);
	data = 0x01;
	SendMosCmd(port->serial, MOS_WRITE, MOS_VEN_REG, 0x04, &data);

	SendMosCmd(port->serial, MOS_READ, MOS_VEN_REG, 0x05, &data);
	data = 0x05;
	SendMosCmd(port->serial, MOS_WRITE, MOS_VEN_REG, 0x05, &data);

	SendMosCmd(port->serial, MOS_READ, MOS_VEN_REG, 0x06, &data);
	data = 0x24;
	SendMosCmd(port->serial, MOS_WRITE, MOS_VEN_REG, 0x06, &data);

	data = 0x00;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x07, &data);
	SendMosCmd(port->serial, MOS_WRITE, MOS_VEN_REG, 0x07, &data);

	/* 1: IER  2: FCR  3: LCR  4:MCR */

	data = 0x02;
	SendMosCmd(port->serial, MOS_WRITE, MOS_VEN_REG, 0x00, &data);

	data = 0x00;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x01, &data);

	data = 0x00;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x02, &data);

	data = 0xCF;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x02, &data);

	data = 0x03;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x03, &data);

	data = 0x0b;
	mos7703_port->shadowMCR = data;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x04, &data);

	data = 0x83;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x03, &data);

	data = 0x0c;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x00, &data);

	data = 0x00;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x01, &data);

	data = 0x03;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x03, &data);

	/* Enable Interrupt Registers */
	data = 0x0c;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x01, &data);

	/* see if we've set up our endpoint info yet (can't set it up in 
	 * mos7703_startup as the structures were not set up at that time.)
	 */

	dev_dbg(&port->dev, "port number: %d, bulk in ep: %x, "
		"bulk out ep: %x, int in ep: %x\n",
		port->port_number, port->bulk_in_endpointAddress,
		port->bulk_out_endpointAddress, port->interrupt_in_endpointAddress);

	mos7703_port->read_urb = port->read_urb;

	/* set up our bulk in urb */
	usb_fill_bulk_urb(mos7703_port->read_urb, serial->dev,
			  usb_rcvbulkpipe(serial->dev,
					  port->bulk_in_endpointAddress),
			  port->bulk_in_buffer,
			  mos7703_port->read_urb->transfer_buffer_length,
			  mos7703_bulk_in_callback, mos7703_port);

	response = usb_submit_urb(mos7703_port->read_urb, GFP_KERNEL);
	if (response) {
		dev_err(&port->dev, "%s - Error %d submitting control urb\n",
			__func__, response);
	}

	/* initialize our wait queue */
	init_waitqueue_head(&mos7703_port->delta_msr_wait);

	/* initialize our icount structure */
	memset(&(mos7703_port->icount), 0x00, sizeof(mos7703_port->icount));

	/* Must always set this bit to enable ints! */
	mos7703_port->shadowMCR = MCR_MASTER_IE;
	mos7703_port->openPending = 0;
	mos7703_port->open = 1;

	change_port_settings(tty, mos7703_port, old_termios);

	return 0;
}

/*****************************************************************************
 * mos7703_close
 * this function is called by the tty driver when a port is closed
 *****************************************************************************/
static void mos7703_close(struct usb_serial_port *port)
{
	struct moschip_port *mos7703_port;
	int j;
	char data;

	mos7703_port = usb_get_serial_port_data(port);
	if (mos7703_port == NULL) {
		return;
	}

	for (j = 0; j < NUM_URBS; ++j)
		usb_kill_urb(mos7703_port->write_urb_pool[j]);

	/* Freeing Write URBs */
	for (j = 0; j < NUM_URBS; ++j) {
		if (mos7703_port->write_urb_pool[j]) {
			if (mos7703_port->write_urb_pool[j]->transfer_buffer)
				kfree(mos7703_port->write_urb_pool[j]->
				      transfer_buffer);

			usb_free_urb(mos7703_port->write_urb_pool[j]);
		}
	}

	/* While closing port shutdown all bulk read write and interrupt read if
	   they exist */

	if (mos7703_port->port->serial->dev) {
                /* flush and block until tx is empty*/
		if (mos7703_port->write_urb) {
			dev_dbg(&port->dev, "Shutdown bulk write\n");
			usb_kill_urb(mos7703_port->write_urb);
		}
		if (mos7703_port->read_urb) {
			dev_dbg(&port->dev, "Shutdown bulk read\n");
			usb_kill_urb(mos7703_port->read_urb);
		}
	}

	if (mos7703_port->write_urb) {
		/* if this urb had a transfer buffer already (old tx) free it */
		if (mos7703_port->write_urb->transfer_buffer != NULL) {
			kfree(mos7703_port->write_urb->transfer_buffer);
		}
		usb_free_urb(mos7703_port->write_urb);
	}

	data = 0x00;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x04, &data);

	data = 0x00;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x01, &data);

	mos7703_port->open = 0;
	mos7703_port->openPending = 0;
}

/*****************************************************************************
 * mos7703_write_room
 * this function is called by the tty driver when it wants to know how many
 * bytes of data we can accept for a specific port.
 * If successful, we return the amount of room that we have for this port
 * Otherwise we return a negative error number.
 *****************************************************************************/
static unsigned int mos7703_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	int i;
	int room = 0;
	struct moschip_port *mos7703_port;

	mos7703_port = usb_get_serial_port_data(port);
	if (mos7703_port == NULL) {
		dev_err(&port->dev, "%s: Null port, return 0\n", __func__);
		return 0;
	}

	for (i = 0; i < NUM_URBS; ++i) {
		if (mos7703_port->write_urb_pool[i] &&
		    mos7703_port->write_urb_pool[i]->status != -EINPROGRESS) {
			room += URB_TRANSFER_BUFFER_SIZE;
		}
	}

	return (room);
}

/*****************************************************************************
 * mos7703_chars_in_buffer
 * this function is called by the tty driver when it wants to know how many
 * bytes of data we currently have outstanding in the port (data that has
 * been written, but hasn't made it out the port yet)
 * If successful, we return the number of bytes left to be written in the 
 * system,
 * Otherwise we return 0.
 *****************************************************************************/
static unsigned int mos7703_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	int i;
	int chars = 0;
	struct moschip_port *mos7703_port;

	mos7703_port = usb_get_serial_port_data(port);
	if (mos7703_port == NULL) {
		dev_err(&port->dev, "%s: Null port, return ENODEV\n", __func__);
		return 0;
	}
	for (i = 0; i < NUM_URBS; ++i) {
		if (mos7703_port->write_urb_pool[i]->status == -EINPROGRESS) {
			chars += URB_TRANSFER_BUFFER_SIZE;
		}
	}

	return (chars);
}

/*****************************************************************************
 * SerialWrite
 * this function is called by the tty driver when data should be written to
 * the port.
 * If successful, we return the number of bytes written, otherwise we return
 * a negative error number.
 *****************************************************************************/
static int mos7703_write(struct tty_struct *tty, struct usb_serial_port *port,
			 const unsigned char *data, int count)
{
	int i;
	int bytes_sent = 0;
	int transfer_size;
	int status;
	struct moschip_port *mos7703_port;
	struct urb *urb;
	const unsigned char *current_position = data;

	mos7703_port = usb_get_serial_port_data(port);
	if (mos7703_port == NULL) {
		return -ENODEV;
	}

	/* try to find a free urb in our list */
	urb = NULL;
	for (i = 0; i < NUM_URBS; ++i) {
		if (mos7703_port->write_urb_pool[i] &&
		    mos7703_port->write_urb_pool[i]->status != -EINPROGRESS) {
			urb = mos7703_port->write_urb_pool[i];
			dev_dbg(&port->dev, "URB:%d\n", i);
			break;
		}
	}

	if (urb == NULL) {
		dev_dbg(&port->dev, "%s - no more free urbs\n", __func__);
		goto exit;
	}

	if (urb->transfer_buffer == NULL) {
		urb->transfer_buffer = kmalloc(URB_TRANSFER_BUFFER_SIZE, GFP_KERNEL);
		if (urb->transfer_buffer == NULL) {
			goto exit;
		}
	}

	transfer_size = min(count, URB_TRANSFER_BUFFER_SIZE);
	memcpy(urb->transfer_buffer, current_position, transfer_size);
	usb_serial_debug_data(&port->dev, __func__, transfer_size,
			      urb->transfer_buffer);

	/* fill up the urb with all of our data and submit it */
	usb_fill_bulk_urb(urb, mos7703_port->port->serial->dev,
			  usb_sndbulkpipe(mos7703_port->port->serial->dev,
					  port->bulk_out_endpointAddress),
			  urb->transfer_buffer, transfer_size,
			  mos7703_bulk_out_data_callback, mos7703_port);

	/* send it down the pipe */
	status = usb_submit_urb(urb, GFP_KERNEL);
	if (status) {
		dev_err(&port->dev, "%s - usb_submit_urb(write bulk) failed "
			"with status = %d\n", __func__, status);
		bytes_sent = status;
		goto exit;
	}
	bytes_sent = transfer_size;
 exit:
	return bytes_sent;
}

/*****************************************************************************
 * SerialThrottle
 * this function is called by the tty driver when it wants to stop the data
 * being read from the port.
 *****************************************************************************/
static void mos7703_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7703_port;
	int status;

	mos7703_port = usb_get_serial_port_data(port);
	if (mos7703_port == NULL)
		return;
	if (!mos7703_port->open) {
		dev_dbg(&port->dev, "%s - port not opened\n", __func__);
		return;
	}

	/* if we are implementing XON/XOFF, send the stop character */
	if (I_IXOFF(tty)) {
		unsigned char stop_char = STOP_CHAR(tty);
		status = mos7703_write(tty, port, &stop_char, 1);
		if (status <= 0)
			return;
	}

	/* if we are implementing RTS/CTS, toggle that line */
	if (tty->termios.c_cflag & CRTSCTS) {
		mos7703_port->shadowMCR &= ~MCR_RTS;
		status = SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, MCR,
				    &mos7703_port->shadowMCR);
		if (status != 0)
			return;
	}
	return;
}

/*****************************************************************************
 * mos7703_unthrottle
 * this function is called by the tty driver when it wants to resume the data
 * being read from the port (called after SerialThrottle is called)
 *****************************************************************************/
static void mos7703_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7703_port = usb_get_serial_port_data(port);
	int status;

	if (mos7703_port == NULL)
		return;

	if (!mos7703_port->open) {
		dev_dbg(&port->dev, "%s - port not opened\n", __func__);
		return;
	}

	/* if we are implementing XON/XOFF, send the start character */
	if (I_IXOFF(tty)) {
		unsigned char start_char = START_CHAR(tty);
		status = mos7703_write(tty, port, &start_char, 1);
		if (status <= 0)
			return;
	}

	/* if we are implementing RTS/CTS, toggle that line */
	if (tty->termios.c_cflag & CRTSCTS) {
		mos7703_port->shadowMCR |= MCR_RTS;
		status = SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, MCR,
				    &mos7703_port->shadowMCR);
		if (status != 0)
			return;
	}
	return;
}

/*****************************************************************************
 * SerialSetTermios
 * this function is called by the tty driver when it wants to change the
 * termios structure
 *****************************************************************************/
static void mos7703_set_termios(struct tty_struct *tty,
				struct usb_serial_port *port,
				struct ktermios *old_termios)
{
	int status;
	unsigned int cflag;
	struct moschip_port *mos7703_port;

	mos7703_port = usb_get_serial_port_data(port);
	if (mos7703_port == NULL)
		return;

	if (!mos7703_port->open) {
		dev_dbg(&port->dev, "%s - port not opened\n", __func__);
		return;
	}

	cflag = tty->termios.c_cflag;

	/* check that they really want us to change something */
	if (old_termios) {
		if (cflag == old_termios->c_cflag) {
			return;
		}
	}

	dev_dbg(&port->dev, "%s - cflag=%08x\n", __func__,
		tty->termios.c_cflag);

	if (old_termios) {
		dev_dbg(&port->dev, "%s - old cflag=%08x\n", __func__,
		    old_termios->c_cflag);
	}

	/* change the port settings to the new ones specified */
	change_port_settings(tty, mos7703_port, old_termios);

	if (mos7703_port->read_urb->status != -EINPROGRESS) {
		mos7703_port->read_urb->dev = mos7703_port->port->serial->dev;
		status = usb_submit_urb(mos7703_port->read_urb, GFP_KERNEL);
		if (status) {
			dev_dbg(&port->dev,
				"usb_submit_urb(read bulk) failed, status = %d\n",
				status);
		}
	}
	return;
}

/*****************************************************************************
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 *      is emptied.  On bus types like RS485, the transmitter must
 *      release the bus after transmitting. This must be done when
 *      the transmit shift register is empty, not be done when the
 *      transmit holding register is empty.  This functionality
 *      allows an RS485 driver to be written in user space. 
 *****************************************************************************/
static int get_lsr_info(struct tty_struct *tty,
		struct moschip_port *mos7703_port, unsigned int __user *value)
{
	unsigned int result = 0;
	int count = mos7703_chars_in_buffer(tty);
	if (count == 0) {
		result = TIOCSER_TEMT;
	}
	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}

static int set_modem_info(struct moschip_port *mos7703_port, unsigned int cmd,
			  unsigned int *value)
{
	unsigned int mcr;
	unsigned int arg;
	unsigned char data;
	struct usb_serial_port *port;

	if (mos7703_port == NULL)
		return -1;

	port = (struct usb_serial_port *)mos7703_port->port;
	mcr = mos7703_port->shadowMCR;

	if (copy_from_user(&arg, value, sizeof(int)))
		return -EFAULT;

	switch (cmd) {
	case TIOCMBIS:
		if (arg & TIOCM_RTS)
			mcr |= MCR_RTS;
		if (arg & TIOCM_DTR)
			mcr |= MCR_RTS;
		if (arg & TIOCM_LOOP)
			mcr |= MCR_LOOPBACK;
		break;

	case TIOCMBIC:
		if (arg & TIOCM_RTS)
			mcr &= ~MCR_RTS;
		if (arg & TIOCM_DTR)
			mcr &= ~MCR_RTS;
		if (arg & TIOCM_LOOP)
			mcr &= ~MCR_LOOPBACK;
		break;

	case TIOCMSET:
		/* turn off the RTS and DTR and LOOPBACK 
		 * and then only turn on what was asked to */
		mcr &= ~(MCR_RTS | MCR_DTR | MCR_LOOPBACK);
		mcr |= ((arg & TIOCM_RTS) ? MCR_RTS : 0);
		mcr |= ((arg & TIOCM_DTR) ? MCR_DTR : 0);
		mcr |= ((arg & TIOCM_LOOP) ? MCR_LOOPBACK : 0);
		break;
	}

	mos7703_port->shadowMCR = mcr;
	data = mos7703_port->shadowMCR;
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, MCR, &data);
	return 0;
}

static int get_modem_info(struct moschip_port *mos7703_port,
			  unsigned int *value)
{
	unsigned int result = 0;
	unsigned int msr = mos7703_port->shadowMSR;
	unsigned int mcr = mos7703_port->shadowMCR;

	result = ((mcr & MCR_DTR) ? TIOCM_DTR : 0)	     /* 0x002 */
	       | ((mcr & MCR_RTS) ? TIOCM_RTS : 0)	     /* 0x004 */
	       | ((msr & MOS7703_MSR_CTS) ? TIOCM_CTS : 0)   /* 0x020 */
	       | ((msr & MOS7703_MSR_CD) ? TIOCM_CAR : 0)    /* 0x040 */
	       | ((msr & MOS7703_MSR_RI) ? TIOCM_RI : 0)     /* 0x080 */
	       | ((msr & MOS7703_MSR_DSR) ? TIOCM_DSR : 0);  /* 0x100 */

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}

static int get_serial_info(struct moschip_port *mos7703_port,
			   struct serial_struct *retinfo)
{
	struct serial_struct tmp;

	if (mos7703_port == NULL)
		return -1;

	if (!retinfo)
		return -EFAULT;

	memset(&tmp, 0, sizeof(tmp));

	tmp.type = PORT_16550A;
	tmp.line = mos7703_port->port->minor;
	tmp.port = mos7703_port->port->port_number;
	tmp.irq = 0;
	tmp.flags = ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ;
	tmp.xmit_fifo_size = 4096;
	tmp.baud_base = 9600;
	tmp.close_delay = 5 * HZ;
	tmp.closing_wait = 30 * HZ;

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

/*****************************************************************************
 * SerialIoctl
 * this function handles any ioctl calls to the driver
 *****************************************************************************/
static int mos7703_ioctl(struct tty_struct *tty,
			 unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct moschip_port *mos7703_port;

	struct async_icount cnow;
	struct async_icount cprev;
	struct serial_icounter_struct icount;

	mos7703_port = usb_get_serial_port_data(port);
	if (mos7703_port == NULL)
		return -1;

	switch (cmd) {
	case TIOCSERGETLSR:
		dev_dbg(&port->dev, "%s (%d) TIOCSERGETLSR\n", __func__,
			port->port_number);
		return get_lsr_info(tty, mos7703_port, (unsigned int *)arg);
	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
		dev_dbg(&port->dev, "%s (%d) TIOCMSET/TIOCMBIC/TIOCMSET\n",
			__func__, port->port_number);
		return set_modem_info(mos7703_port, cmd, (unsigned int *)arg);
	case TIOCMGET:
		dev_dbg(&port->dev, "%s (%d) TIOCMGET\n", __func__,
			port->port_number);
		return get_modem_info(mos7703_port, (unsigned int *)arg);
	case TIOCGSERIAL:
		dev_dbg(&port->dev, "%s (%d) TIOCGSERIAL\n", __func__,
			port->port_number);
		return get_serial_info(mos7703_port, (struct serial_struct *)arg);
	case TIOCSSERIAL:
		dev_dbg(&port->dev, "%s (%d) TIOCSSERIAL\n", __func__,
			port->port_number);
		break;
	case TIOCMIWAIT:
		dev_dbg(&port->dev, "%s (%d) TIOCMIWAIT\n", __func__,
			port->port_number);
		cprev = mos7703_port->icount;
		while (1) {
			mos7703_port->delta_msr_cond = 0;
			wait_event_interruptible(mos7703_port->delta_msr_wait,
					 (mos7703_port->delta_msr_cond == 1));
			/* see if a signal did it */
			if (signal_pending(current))
				return -ERESTARTSYS;
			cnow = mos7703_port->icount;
			if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr &&
			    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts)
				return -EIO;	/* no change => error */

			if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
			    ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
			    ((arg & TIOCM_CD) && (cnow.dcd != cprev.dcd)) ||
			    ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts))) {
				return 0;
			}
			cprev = cnow;
		}
		/* NOTREACHED */
		break;
	case TIOCGICOUNT:
		cnow = mos7703_port->icount;
		icount.cts = cnow.cts;
		icount.dsr = cnow.dsr;
		icount.rng = cnow.rng;
		icount.dcd = cnow.dcd;
		icount.rx = cnow.rx;
		icount.tx = cnow.tx;
		icount.frame = cnow.frame;
		icount.overrun = cnow.overrun;
		icount.parity = cnow.parity;
		icount.brk = cnow.brk;
		icount.buf_overrun = cnow.buf_overrun;

		dev_dbg(&port->dev, "%s (%d) TIOCGICOUNT RX=%d, TX=%d\n",
			__func__, port->port_number, icount.rx, icount.tx);

		if (copy_to_user((void *)arg, &icount, sizeof(icount)))
			return -EFAULT;
		return 0;
	}
	return -ENOIOCTLCMD;
}

/****************************************************************************
 * mos7703_startup
 ****************************************************************************/
static int mos7703_startup(struct usb_serial *serial)
{
	struct moschip_port *mos7703_port;
	struct usb_device *dev = serial->dev;
	int i;

	/* we set up the pointers to the endpoints in the mos7703_open function, 
	 * as the structures aren't created yet. */

	/* set up our port private structures */
	for (i = 0; i < serial->num_ports; ++i) {
		mos7703_port = kmalloc(sizeof(struct moschip_port), GFP_KERNEL);
		if (mos7703_port == NULL) {
			usb_set_serial_data(serial, NULL);
			dev_err(&dev->dev, "%s - Out of memory\n", __func__);
			return -ENOMEM;
		}
		memset(mos7703_port, 0, sizeof(struct moschip_port));

		/* Initialize all port interrupt end point to port 0 int endpoint
		 * Our device has only one interrupt end point common to all ports */
		serial->port[i]->interrupt_in_endpointAddress =
		    serial->port[0]->interrupt_in_endpointAddress;
		mos7703_port->port = serial->port[i];
		usb_set_serial_port_data(serial->port[i], mos7703_port);
	}

	/* Set Driver Done Bit */
	usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			0x0E, 0x40, 0x08, 0x01, NULL, 0x00, 5 * HZ);

	/* setting configuration feature to one */
	usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			(u8) 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 5 * HZ);
	return 0;
}

static struct usb_serial_driver mcs7703_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name =	"moschip7703",
	},
	.description = "Moschip 7703 usb-serial",
	.id_table = id_table,
	.num_ports = 1,

	.attach = mos7703_startup,

	.open = mos7703_open,
	.close = mos7703_close,
	.write = mos7703_write,

	.write_room = mos7703_write_room,
	.ioctl = mos7703_ioctl,
	.set_termios = mos7703_set_termios,
	.chars_in_buffer = mos7703_chars_in_buffer,
	.throttle = mos7703_throttle,
	.unthrottle = mos7703_unthrottle,
	.read_bulk_callback = mos7703_bulk_in_callback,
	.read_int_callback = mos7703_interrupt_callback
};

static struct usb_serial_driver * const serial_drivers[] = {
	&mcs7703_driver, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
