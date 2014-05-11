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
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/serial.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#include "mos7703.h"
#include "16C50.h"
#define xyz 1

/* 1: Enables the debugging :: 0: Disable the debugging */
#define ASP_DEBUG 0

#if ASP_DEBUG
#define DPRINTK(fmt, args...) printk( "%s: " fmt, __FUNCTION__ , ## args)
#define dbg(args...) DPRINTK(args)
#define err(args...) DPRINTK(args)

#else
#define DPRINTK(fmt, args...)
#define dbg(args...)
#define err(args...)
#endif

#define DRIVER_AUTHOR "Moschip Semiconductor Tech. Ltd."
#define DRIVER_DESC "Moschip 7703 USB Serial Driver"

#define USB_VENDOR_ID_MOSCHIP		0x9710
#define MOSCHIP_DEVICE_ID_7703		0x7703

static struct usb_device_id id_table[] = {
	{USB_DEVICE(USB_VENDOR_ID_MOSCHIP, MOSCHIP_DEVICE_ID_7703)},
	{} /* terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

/* Defines used for sending commands to port */

#define WAIT_FOR_EVER   (HZ * 0)	/* timeout urb is wait for ever */
#define MOS_WDR_TIMEOUT (HZ * 5)	/* default urb timeout */

#define MOS_UART_REG    0x0300
#define MOS_VEN_REG     0x0000

#define MOS_WRITE       0x0E
#define MOS_READ        0x0D

#ifdef xyz
static struct usb_serial *get_usb_serial(struct usb_serial_port *port,
					 const char *function);
static int serial_paranoia_check(struct usb_serial *serial,
				 const char *function);
static int port_paranoia_check(struct usb_serial_port *port,
			       const char *function);
#endif

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
	struct moschip_port *mos7703_port;
	__u32 *data;

	if (!urb) {
		DPRINTK("%s", "Invalid Pointer !!!!:\n");
		return;
	}

	switch (urb->status) {

	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d", __FUNCTION__,
		    urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d\n", __FUNCTION__,
		    urb->status);
		goto exit;
	}

	length = urb->actual_length;
	data = urb->transfer_buffer;

	mos7703_port = (struct moschip_port *)urb->context;
	if (!mos7703_port) {
		DPRINTK("%s", "NULL mos7703_serial pointer \n");
		return;
	}

	/* Moschip get 4 bytes 
	 * Byte 1 IIR Port 1 (port.number is 0)
	 * Byte 2 IIR Port 2 (port.number is 1)
	 * Byte 3 --------------
	 * Byte 4 FIFO status for both 
	 */
	if (length && length > 4) {
		DPRINTK("%s \n", "Wrong data !!!!!!!!!!!!");
		return;
	}

 exit:
	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result) {
		dev_err(&urb->dev->dev,
			"%s - Error %d submitting control urb\n", __FUNCTION__,
			result);
	}
	return;
}

/*****************************************************************************
 * mos7703_bulk_in_callback
 * this is the callback function for when we have received data on the 
 * bulk in endpoint.
 * Input : 1 Input
 *   pointer to the URB packet,
 *
 *****************************************************************************/
static void mos7703_bulk_in_callback(struct urb *urb)
{
	int status;
	unsigned char *data;
	struct usb_serial *serial;
	struct usb_serial_port *port;
	struct moschip_serial *mos7703_serial;
	struct moschip_port *mos7703_port;
	struct tty_struct *tty;

	if (urb->status) {
		DPRINTK("nonzero read bulk status received: %d\n", urb->status);
		return;
	}

	mos7703_port = (struct moschip_port *)urb->context;
	if (!mos7703_port) {
		DPRINTK("%s", "NULL mos7703_port pointer \n");
		return;
	}
#ifdef xyz
	port = (struct usb_serial_port *)mos7703_port->port;
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Port Paranoia failed \n");
		return;
	}

	serial = get_usb_serial(port, __FUNCTION__);
	if (!serial) {
		DPRINTK("%s\n", "Bad serial pointer ");
		return;
	}
#endif
	data = urb->transfer_buffer;
	mos7703_serial = (struct moschip_serial *)serial->private;

	if (urb->actual_length) {
		tty = mos7703_port->tty;
		if (tty) {
			tty_insert_flip_string_fixed_flag(tty->port, data,
							  TTY_NORMAL,
							  urb->actual_length);
			tty_flip_buffer_push(tty->port);
		}
	}

	if (!mos7703_port->read_urb) {
		DPRINTK("%s", "URB KILLED !!!\n");
		return;
	}

	if (mos7703_port->read_urb->status != -EINPROGRESS) {
		mos7703_port->read_urb->dev = serial->dev;
		status = usb_submit_urb(mos7703_port->read_urb, GFP_ATOMIC);
		if (status) {
			DPRINTK
			    (" usb_submit_urb(read bulk) failed, status = %d\n",
			     status);
		}
	}
}

/*****************************************************************************
 * mos7703_bulk_out_data_callback
 * this is the callback function for when we have finished sending serial data
 * on the bulk out endpoint.
 * Input : 1 Input
 *   pointer to the URB packet,
 *
 *****************************************************************************/
static void mos7703_bulk_out_data_callback(struct urb *urb)
{
	struct moschip_port *mos7703_port;
	struct tty_struct *tty;

	if (urb->status) {
		DPRINTK("nonzero write bulk status received:%d\n", urb->status);
		return;
	}

	mos7703_port = (struct moschip_port *)urb->context;
	if (!mos7703_port) {
		DPRINTK("%s", "NULL mos7703_port pointer \n");
		return;
	}
#ifdef xyz
	if (port_paranoia_check(mos7703_port->port, __FUNCTION__)) {
		DPRINTK("%s", "Port Paranoia failed \n");
		return;
	}
#endif
	tty = mos7703_port->tty;
	if (tty) {
		/* let the tty driver wakeup if it has a special write_wakeup
		   function. */
		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
		    tty->ldisc->ops->write_wakeup) {

			(tty->ldisc->ops->write_wakeup) (tty);
		}

		/* tell the tty driver that something has changed */
		wake_up_interruptible(&tty->write_wait);
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

static int SendMosCmd(struct usb_serial *serial, __u8 request, __u16 value,
		      __u16 index, void *data)
{
	int timeout;
	int status;

	__u8 requesttype;
	__u16 size;
	unsigned int Pipe;
#ifdef xyz
	if (serial_paranoia_check(serial, __FUNCTION__)) {
		DPRINTK("%s", "Serial Paranoia failed \n");
		return -1;
	}
#endif

	size = 0x00;
	timeout = MOS_WDR_TIMEOUT;

	if (request == MOS_WRITE) {
		request = (__u8) MOS_WRITE;
		requesttype = (__u8) 0x40;
		if (data)
			value = value + (__u16) * ((unsigned char *)data);
		Pipe = usb_sndctrlpipe(serial->dev, 0);
	} else {
		request = (__u8) MOS_READ;
		requesttype = (__u8) 0xC0;
		size = 0x01;
		Pipe = usb_rcvctrlpipe(serial->dev, 0);
	}

	status = usb_control_msg(serial->dev,
				 Pipe, request,
				 requesttype, value,
				 index, data, size, timeout);

	if (status < 0) {
		DPRINTK("Command Write failed Value %x index %x\n", value,
			index);
		return status;
	}
	return status;
}


/************************************************************************/
/*       D R I V E R  T T Y  I N T E R F A C E  F U N C T I O N S       */
/************************************************************************/

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
	struct moschip_serial *mos7703_serial;
	struct moschip_port *mos7703_port;

	DPRINTK("%s \n", __FUNCTION__);
#ifdef xyz
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Port Paranoia failed,Returning with ENODEV \n");
		return -ENODEV;
	}
#endif
	serial = port->serial;
#ifdef xyz
	if (serial_paranoia_check(serial, __FUNCTION__)) {
		DPRINTK("%s",
			"Serial Paranoia failed,Returning with ENODEV \n");
		return -ENODEV;
	}
#endif
	mos7703_port = usb_get_serial_port_data(port);

	if (mos7703_port == NULL) {
		DPRINTK("%s", "Null port,Returning with ENODEV \n");
		return -ENODEV;
	}
	mos7703_port->tty = tty;

	port0 = serial->port[0];
	mos7703_serial = usb_get_serial_port_data(port);

	if (mos7703_serial == NULL || port0 == NULL) {
		DPRINTK("%s", "Null Serial n Port0,Returning with ENODEV \n");
		return -ENODEV;
	}

	/* Initialising the write urb pool */
	for (j = 0; j < NUM_URBS; ++j) {
		urb = usb_alloc_urb(0, GFP_KERNEL);
		mos7703_port->write_urb_pool[j] = urb;

		if (urb == NULL) {
			err(" ********** No more urbs???");
			continue;
		}

		urb->transfer_buffer = kmalloc(URB_TRANSFER_BUFFER_SIZE, GFP_KERNEL);
		if (!urb->transfer_buffer) {
			err(" **************** %s-out of memory for urb buffers.", __FUNCTION__);
			continue;
		}
	}

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

	/*
	 * 1: IER  2: FCR  3: LCR  4:MCR
	 */

	DPRINTK("%s", "Sending Command .......... \n");

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

        /* force low_latency on so that our tty_push actually forces
	 * the data through, otherwise it is scheduled, and with high
	 * data rates (like with OHCI) data can get lost.
	 */
	if (tty)
		tty->port->low_latency = 1;

	DPRINTK("port number is %d \n", port->port_number);
	DPRINTK("port bulk in endpoint is %x \n",
		port->bulk_in_endpointAddress);
	DPRINTK("port bulk out endpoint is %x \n",
		port->bulk_out_endpointAddress);
	DPRINTK("port int end point  %x \n",
		port->interrupt_in_endpointAddress);

	mos7703_port->bulk_in_buffer = port->bulk_in_buffer;
	mos7703_port->bulk_in_endpoint = port->bulk_in_endpointAddress;

	mos7703_port->read_urb = port->read_urb;

	mos7703_port->bulk_out_endpoint = port->bulk_out_endpointAddress;

	/* set up our bulk in urb */

	usb_fill_bulk_urb(mos7703_port->read_urb,
			  serial->dev,
			  usb_rcvbulkpipe(serial->dev,
					  port->bulk_in_endpointAddress),
			  port->bulk_in_buffer,
			  mos7703_port->read_urb->transfer_buffer_length,
			  mos7703_bulk_in_callback, mos7703_port);

	response = usb_submit_urb(mos7703_port->read_urb, GFP_KERNEL);

	if (response) {
		err("%s - Error %d submitting control urb", __FUNCTION__,
		    response);
	}

	/* initialize our wait queue */
	init_waitqueue_head(&mos7703_port->delta_msr_wait);

	/* initialize our icount structure */
	memset(&(mos7703_port->icount), 0x00, sizeof(mos7703_port->icount));

	/* initialize our port settings */
	mos7703_port->txCredits = 0;	/* Can't send any data yet */

	/* Must always set this bit to enable ints! */
	mos7703_port->shadowMCR = MCR_MASTER_IE;
	mos7703_port->chaseResponsePending = FALSE;

	/* send a open port command */
	mos7703_port->openPending = FALSE;
	mos7703_port->open = TRUE;

	change_port_settings(tty, mos7703_port, old_termios);
	mos7703_port->maxTxCredits = 4096;
	mos7703_port->rxBytesAvail = 0x0;

	DPRINTK("%s(%d) - Initialize TX fifo to %d bytes", __FUNCTION__,
		port->port_number, mos7703_port->maxTxCredits);

	return 0;
}

/*****************************************************************************
 * mos7703_close
 * this function is called by the tty driver when a port is closed
 *****************************************************************************/
static void mos7703_close(struct usb_serial_port *port)
{
	struct usb_serial *serial;
	struct moschip_serial *mos7703_serial;
	struct moschip_port *mos7703_port;
	int j;
	char data;

#ifdef xyz
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Port Paranoia failed \n");
		return;
	}

	serial = get_usb_serial(port, __FUNCTION__);
	if (!serial) {
		DPRINTK("%s", "Serial Paranoia failed \n");
		return;
	}
#endif

	mos7703_serial = usb_get_serial_data(serial);
	mos7703_port = usb_get_serial_port_data(port);

	if ((mos7703_serial == NULL) || (mos7703_port == NULL)) {
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

	if (serial->dev) {
                /* flush and block until tx is empty*/
		if (mos7703_port->write_urb) {
			DPRINTK("%s", "Shutdown bulk write\n");
			usb_kill_urb(mos7703_port->write_urb);
		}
		if (mos7703_port->read_urb) {
			DPRINTK("%s", "Shutdown bulk read\n");
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

	mos7703_port->open = FALSE;
	mos7703_port->closePending = FALSE;
	mos7703_port->openPending = FALSE;

	DPRINTK("%s \n", "Leaving ............");
}

/*****************************************************************************
 * SerialBreak
 * this function sends a break to the port
 *****************************************************************************/
static void mos7703_break(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial;
	struct moschip_serial *mos7703_serial;
	struct moschip_port *mos7703_port;

	DPRINTK("%s \n", "Entering ...........");

#ifdef xyz
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Port Paranoia failed \n");
		return;
	}

	serial = get_usb_serial(port, __FUNCTION__);
	if (!serial) {
		DPRINTK("%s", "Serial Paranoia failed \n");
		return;
	}
#endif
	mos7703_serial = usb_get_serial_data(serial);
	mos7703_port = usb_get_serial_port_data(port);

	if ((mos7703_serial == NULL) || (mos7703_port == NULL)) {
		return;
	}

	/* flush and chase */
	mos7703_port->chaseResponsePending = TRUE;

	return;
}

/*****************************************************************************
 * mos7703_write_room
 * this function is called by the tty driver when it wants to know how many
 * bytes of data we can accept for a specific port.
 * If successful, we return the amount of room that we have for this port
 * (the txCredits), 
 * Otherwise we return a negative error number.
 *****************************************************************************/
static int mos7703_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	int i;
	int room = 0;
	struct moschip_port *mos7703_port;

#ifdef xyz
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return -1;
	}
#endif
	mos7703_port = usb_get_serial_port_data(port);
	if (mos7703_port == NULL) {
		DPRINTK("%s", "Null port,Returning with ENODEV \n");
		return -ENODEV;
	}

	for (i = 0; i < NUM_URBS; ++i) {
		if (mos7703_port->write_urb_pool[i]->status != -EINPROGRESS) {
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
 * Otherwise we return a negative error number.
 *****************************************************************************/
static int mos7703_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	int i;
	int chars = 0;
	struct moschip_port *mos7703_port;

#ifdef xyz
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return -1;
	}
#endif
	mos7703_port = usb_get_serial_port_data(port);
	if (mos7703_port == NULL) {
		DPRINTK("%s", "Null port,Returning with ENODEV \n");
		return -ENODEV;
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
	struct usb_serial *serial;
	struct moschip_serial *mos7703_serial;
	struct urb *urb;
	const unsigned char *current_position = data;
	static long debugdata = 0;

#ifdef xyz
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Port Paranoia failed \n");
		return -1;
	}

	serial = port->serial;
	if (serial_paranoia_check(serial, __FUNCTION__)) {
		DPRINTK("%s", "Serial Paranoia failed \n");
		return -1;
	}
#endif
	mos7703_port = usb_get_serial_port_data(port);
	if (mos7703_port == NULL) {
		DPRINTK("%s", "mos7703_port is NULL\n");
		return -1;
	}

	mos7703_serial = usb_get_serial_data(serial);
	if (mos7703_serial == NULL) {
		DPRINTK("%s", "mos7703_serial is NULL \n");
		return -1;
	}

	/* try to find a free urb in our list of them */
	urb = NULL;
	for (i = 0; i < NUM_URBS; ++i) {
		if (mos7703_port->write_urb_pool[i]->status != -EINPROGRESS) {
			urb = mos7703_port->write_urb_pool[i];
			break;
		}
	}

	if (urb == NULL) {
		dbg("%s - ################ no more free urbs\n", __FUNCTION__);
		goto exit;
	}

	if (i > debugdata) {
		debugdata = i;
	}

	if (urb->transfer_buffer == NULL) {
		err("***** %s This should not happen", __FUNCTION__);
		urb->transfer_buffer =
		    kmalloc(URB_TRANSFER_BUFFER_SIZE, GFP_KERNEL);

		if (urb->transfer_buffer == NULL) {
			err("%s no more kernel memory...", __FUNCTION__);
			goto exit;
		}
	}

	transfer_size = min(count, URB_TRANSFER_BUFFER_SIZE);
	memcpy(urb->transfer_buffer, current_position, transfer_size);
	DPRINTK("transfer_size:%d	transfer_buffer:%s\n", transfer_size,
		(char *)urb->transfer_buffer);

	/* fill up the urb with all of our data and submit it */
	usb_fill_bulk_urb(urb, mos7703_serial->serial->dev,
			  usb_sndbulkpipe(mos7703_serial->serial->dev,
					  port->bulk_out_endpointAddress),
			  urb->transfer_buffer, transfer_size,
			  mos7703_bulk_out_data_callback, mos7703_port);

	/* send it down the pipe */
	status = usb_submit_urb(urb, GFP_KERNEL);
	if (status) {
		err("%s - usb_submit_urb(write bulk) failed with status = %d\n",
		    __FUNCTION__, status);
		bytes_sent = status;
		DPRINTK("URB Status Fail !!!!!!\n");
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

#ifdef xyz
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return;
	}
#endif
	DPRINTK("- port %d\n", port->port_number);

	mos7703_port = usb_get_serial_port_data(port);

	if (mos7703_port == NULL)
		return;
	if (!mos7703_port->open) {
		DPRINTK("%s\n", "port not opened");
		return;
	}

	if (!tty) {
		dbg("%s - no tty available", __FUNCTION__);
		return;
	}

	/* if we are implementing XON/XOFF, send the stop character */
	if (I_IXOFF(tty)) {
		unsigned char stop_char = STOP_CHAR(tty);
		status = mos7703_write(tty, port, &stop_char, 1);
		if (status <= 0) {
			return;
		}
	}

	/* if we are implementing RTS/CTS, toggle that line */
	if (tty->termios.c_cflag & CRTSCTS) {

		mos7703_port->shadowMCR &= ~MCR_RTS;
		status = SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, MCR,
				    &mos7703_port->shadowMCR);
		if (status != 0) {
			return;
		}
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

#ifdef xyz
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return;
	}
#endif
	if (mos7703_port == NULL)
		return;

	if (!mos7703_port->open) {
		dbg("%s - port not opened", __FUNCTION__);
		return;
	}
	if (!tty) {
		dbg("%s - no tty available", __FUNCTION__);
		return;
	}

	/* if we are implementing XON/XOFF, send the start character */
	if (I_IXOFF(tty)) {
		unsigned char start_char = START_CHAR(tty);
		status = mos7703_write(tty, port, &start_char, 1);
		if (status <= 0) {
			return;
		}
	}

	/* if we are implementing RTS/CTS, toggle that line */
	if (tty->termios.c_cflag & CRTSCTS) {
		mos7703_port->shadowMCR |= MCR_RTS;
		status = SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, MCR,
				    &mos7703_port->shadowMCR);
		if (status != 0) {
			return;
		}
	}

	return;
}

/*****************************************************************************
 * SerialSetTermios
 * this function is called by the tty driver when it wants to change the termios structure
 *****************************************************************************/
static void mos7703_set_termios(struct tty_struct *tty,
				struct usb_serial_port *port,
				struct ktermios *old_termios)
{
	int status;
	unsigned int cflag;
	struct usb_serial *serial;
	struct moschip_port *mos7703_port;

#ifdef xyz
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return;
	}

	serial = port->serial;

	if (serial_paranoia_check(serial, __FUNCTION__)) {
		DPRINTK("%s", "Invalid Serial \n");
		return;
	}
#endif
	mos7703_port = usb_get_serial_port_data(port);
	if (mos7703_port == NULL)
		return;

	if (!tty) {
		dbg("%s - no tty", __FUNCTION__);
		return;
	}

	if (!mos7703_port->open) {
		dbg("%s - port not opened", __FUNCTION__);
		return;
	}

	cflag = tty->termios.c_cflag;

	/* check that they really want us to change something */
	if (old_termios) {
		if ((cflag == old_termios->c_cflag) &&
		    (RELEVANT_IFLAG(tty->termios.c_iflag) ==
		     RELEVANT_IFLAG(old_termios->c_iflag))) {

			DPRINTK("%s\n", "Nothing to change");
			return;
		}
	}

	dbg("%s - clfag %08x iflag %08x", __FUNCTION__,
	    tty->termios.c_cflag, RELEVANT_IFLAG(tty->termios.c_iflag));

	if (old_termios) {
		dbg("%s - old clfag %08x old iflag %08x", __FUNCTION__,
		    old_termios->c_cflag, RELEVANT_IFLAG(old_termios->c_iflag));
	}

	dbg("%s - port %d", __FUNCTION__, port->port_number);

	/* change the port settings to the new ones specified */
	change_port_settings(tty, mos7703_port, old_termios);

	if (mos7703_port->read_urb->status != -EINPROGRESS) {

		mos7703_port->read_urb->dev = serial->dev;
		status = usb_submit_urb(mos7703_port->read_urb, GFP_KERNEL);
		if (status) {
			DPRINTK
			    (" usb_submit_urb(read bulk) failed, status = %d",
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

	if (mos7703_port->maxTxCredits == mos7703_port->txCredits) {

		dbg("%s -- Empty", __FUNCTION__);
		result = TIOCSER_TEMT;
	}

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;

	return 0;
}

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

#ifdef xyz
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return -1;
	}

	serial = (struct usb_serial *)port->serial;
	if (serial_paranoia_check(serial, __FUNCTION__)) {
		DPRINTK("%s", "Invalid Serial \n");
		return -1;
	}
#endif
	arg = *value;

	DPRINTK("%s", "Sending Setting Commands .......... \n");

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

	data = 0x001;		/*DLL */
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x00, &data);

	data = 0x000;		/*DLM */
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x01, &data);

	data = mos7703_port->shadowLCR & ~LCR_DL_ENABLE;
	DPRINTK("%s--%x", "value to be written to LCR", data);
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x03, &data);
	return 0;
}
#endif

static int set_high_rates(struct moschip_port *mos7703_port, int *value)
{
	int arg;
	unsigned char data, bypass_flag = 0;
	struct usb_serial_port *port;
	struct usb_serial *serial;
	char wValue = 0;

	if (mos7703_port == NULL)
		return -1;

#ifdef xyz
	port = (struct usb_serial_port *)mos7703_port->port;
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return -1;
	}

	serial = (struct usb_serial *)port->serial;
	if (serial_paranoia_check(serial, __FUNCTION__)) {
		DPRINTK("%s", "Invalid Serial \n");
		return -1;
	}
#endif
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
		bypass_flag = 1;	// To Enable bypass Clock 96 MHz Clock
		break;
	default:
		return -1;

	}

	/* HIGHER BAUD HERE */

        /* Clock multi register setting for above 1x baudrate */
	data = 0x40;
	SendMosCmd(port->serial, MOS_WRITE, MOS_VEN_REG, 0x02, &data);

	usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			0x0E, 0x40, wValue, 0x00, NULL, 0x00, 5 * HZ);

	SendMosCmd(port->serial, MOS_READ, MOS_VEN_REG, 0x01, &data);
	data |= 0x01;
	SendMosCmd(port->serial, MOS_WRITE, MOS_VEN_REG, 0x01, &data);

	if (bypass_flag) {
		/* If true, will write 0x02 in the control register to
		   enable the 96MHz Clock. This should be done only for 6 Mbps.
		*/
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

	data = 0x01;		/* DLL */
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x00, &data);

	data = 0x00;		/* DLM */
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x01, &data);

	data = mos7703_port->shadowLCR & ~LCR_DL_ENABLE;
	DPRINTK("%s--%x", "value to be written to LCR", data);
	SendMosCmd(port->serial, MOS_WRITE, MOS_UART_REG, 0x03, &data);

	return 0;
}

static int set_modem_info(struct moschip_port *mos7703_port, unsigned int cmd,
			  unsigned int *value)
{
	unsigned int mcr;
	unsigned int arg;
	unsigned char data;

	struct usb_serial_port *port;
	DPRINTK("%s \n", "set_modem_info:entering...........");

	if (mos7703_port == NULL)
		return -1;

#ifdef xyz
	port = (struct usb_serial_port *)mos7703_port->port;
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return -1;
	}
#endif
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
	DPRINTK("%s \n", "set_modem_info:leaving...........");
	return 0;
}

static int get_modem_info(struct moschip_port *mos7703_port,
			  unsigned int *value)
{
	unsigned int result = 0;
	unsigned int msr = mos7703_port->shadowMSR;
	unsigned int mcr = mos7703_port->shadowMCR;
	DPRINTK("%s \n", "get_modem_info:entering...........");

	result = ((mcr & MCR_DTR) ? TIOCM_DTR : 0)	     /* 0x002 */
	       | ((mcr & MCR_RTS) ? TIOCM_RTS : 0)	     /* 0x004 */
	       | ((msr & MOS7703_MSR_CTS) ? TIOCM_CTS : 0)   /* 0x020 */
	       | ((msr & MOS7703_MSR_CD) ? TIOCM_CAR : 0)    /* 0x040 */
	       | ((msr & MOS7703_MSR_RI) ? TIOCM_RI : 0)     /* 0x080 */
	       | ((msr & MOS7703_MSR_DSR) ? TIOCM_DSR : 0);  /* 0x100 */

	dbg("%s -- %x", __FUNCTION__, result);

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	DPRINTK("%s \n", "get_modem_info:leaving...........");
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
	tmp.xmit_fifo_size = mos7703_port->maxTxCredits;
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

#ifdef xyz
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return -1;
	}
#endif

	mos7703_port = usb_get_serial_port_data(port);
	if (mos7703_port == NULL)
		return -1;
	dbg("%s - port %d, cmd = 0x%x", __FUNCTION__, port->port_number, cmd);

	switch (cmd) {
	case TIOCSERGETLSR:
		dbg("%s (%d) TIOCSERGETLSR", __FUNCTION__, port->port_number);
		return get_lsr_info(tty, mos7703_port, (unsigned int *)arg);

	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
		dbg("%s (%d) TIOCMSET/TIOCMBIC/TIOCMSET", __FUNCTION__,
		    port->port_number);
		return set_modem_info(mos7703_port, cmd, (unsigned int *)arg);

	case TIOCMGET:
		dbg("%s (%d) TIOCMGET", __FUNCTION__, port->port_number);
		return get_modem_info(mos7703_port, (unsigned int *)arg);

	case TIOCGSERIAL:
		dbg("%s (%d) TIOCGSERIAL", __FUNCTION__, port->port_number);
		return get_serial_info(mos7703_port, (struct serial_struct *)arg);

	case TIOCSSERIAL:
		dbg("%s (%d) TIOCSSERIAL", __FUNCTION__, port->port_number);
		break;

	case TIOCMIWAIT:
		dbg("%s (%d) TIOCMIWAIT", __FUNCTION__, port->port_number);
		cprev = mos7703_port->icount;

		while (1) {
			mos7703_port->delta_msr_cond = 0;
			wait_event_interruptible(mos7703_port->delta_msr_wait,
						 (mos7703_port->
						  delta_msr_cond == 1));

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

		dbg("%s (%d) TIOCGICOUNT RX=%d, TX=%d", __FUNCTION__,
		    port->port_number, icount.rx, icount.tx);

		if (copy_to_user((void *)arg, &icount, sizeof(icount)))
			return -EFAULT;

		return 0;
	}

	return -ENOIOCTLCMD;
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
	struct usb_serial_port *port;

	if (mos7703_port == NULL)
		return -1;

#ifdef xyz
	port = (struct usb_serial_port *)mos7703_port->port;
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return -1;
	}

	if (serial_paranoia_check(port->serial, __FUNCTION__)) {
		DPRINTK("%s", "Invalid Serial \n");
		return -1;
	}
#endif

	number = mos7703_port->port->port_number - mos7703_port->port->minor;

	dbg("%s - port = %d, baud = %d", __FUNCTION__,
	    mos7703_port->port->port_number, baudRate);
	status = calc_baud_rate_divisor(baudRate, &divisor);
	if (status) {
		err("%s - bad baud rate", __FUNCTION__);
		DPRINTK("%s\n", "bad baud rate");
		return status;
	}

	/* Enable access to divisor latch */
	data = LCR_DL_ENABLE;
	SendMosCmd(mos7703_port->port->serial, MOS_WRITE,
		   MOS_UART_REG, LCR, &data);

	DPRINTK("%s\n", "DLL/DLM enabled...");

	/* Write the divisor itself */
	data = LOW8(divisor);
	SendMosCmd(mos7703_port->port->serial, MOS_WRITE,
		   MOS_UART_REG, 0x00, &data);

	DPRINTK("%s--value to DLL :%x\n", "DLL updated...", data);

	data = HIGH8(divisor);
	SendMosCmd(mos7703_port->port->serial, MOS_WRITE,
		   MOS_UART_REG, 0x01, &data);

	DPRINTK("%s--value to DLM :%x\n", "DLM updated...", data);

	/* Restore original value to disable access to divisor latch */
	data = mos7703_port->shadowLCR;
	SendMosCmd(mos7703_port->port->serial, MOS_WRITE,
		   MOS_UART_REG, 0x03, &data);

	DPRINTK("%s\n", "LCR Restored...");

	DPRINTK("%s\n", "Leaving FUNC <calc_baud_rate_divisor>");

	return status;
}

/*****************************************************************************
 * calc_baud_rate_divisor
 * this function calculates the proper baud rate divisor for the specified
 * baud rate.
 *****************************************************************************/
static int calc_baud_rate_divisor(int baudrate, int *divisor)
{
	int i;
	__u16 custom;
	__u16 round1;
	__u16 round;

	dbg("%s - %d", __FUNCTION__, baudrate);

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
		custom = (__u16) (230400L / baudrate);

		/* Check for round off */
		round1 = (__u16) (2304000L / baudrate);
		round = (__u16) (round1 - (custom * 10));
		if (round > 4) {
			custom++;
		}

		*divisor = custom;

		DPRINTK(" Baud %d = %d\n", baudrate, custom);
		return 0;
	}

	DPRINTK("%s\n", " Baud calculation Failed...");
	return -1;
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
	unsigned iflag;
	__u8 mask = 0xff;
	__u8 lData;
	__u8 lParity;
	__u8 lStop;
	int status;
	char data;

	struct usb_serial_port *port;

	if (mos7703_port == NULL)
		return;

#ifdef xyz
	port = (struct usb_serial_port *)mos7703_port->port;
	if (port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return;
	}

	if (serial_paranoia_check(port->serial, __FUNCTION__)) {
		DPRINTK("%s", "Invalid Serial \n");
		return;
	}
#endif
	dbg("%s - port %d", __FUNCTION__, mos7703_port->port->port_number);

	if ((!mos7703_port->open) && (!mos7703_port->openPending)) {
		dbg("%s - port not opened", __FUNCTION__);
		return;
	}

	if (!tty) {
		dbg("%s - no tty structure", __FUNCTION__);
		return;
	}

	lData = LCR_BITS_8;
	lStop = LCR_STOP_1;
	lParity = LCR_PAR_NONE;

	/* Change the data length */
	cflag = tty->termios.c_cflag;
	iflag = tty->termios.c_iflag;

	switch (cflag & CSIZE) {
	case CS5:
		lData = LCR_BITS_5;
		mask = 0x1f;
		DPRINTK("%s\n", " data bits = 5");
		break;

	case CS6:
		lData = LCR_BITS_6;
		mask = 0x3f;
		DPRINTK("%s\n", " data bits = 6");
		break;

	case CS7:
		lData = LCR_BITS_7;
		mask = 0x7f;
		dbg("%s - data bits = 7", __FUNCTION__);
		DPRINTK("%s\n", " data bits = 7");
		break;

	default:
	case CS8:
		lData = LCR_BITS_8;
		dbg("%s - data bits = 8", __FUNCTION__);
		DPRINTK("%s\n", " data bits = 8");
		break;
	}

	/* Change the Parity bit */
	if (cflag & PARENB) {
		if (cflag & PARODD) {
			lParity = LCR_PAR_ODD;
			dbg("%s - parity = odd", __FUNCTION__);
			DPRINTK("%s\n", " parity = odd");
		} else {
			lParity = LCR_PAR_EVEN;
			dbg("%s - parity = even", __FUNCTION__);
			DPRINTK("%s\n", " parity = even");
		}
	} else {
		dbg("%s - parity = none", __FUNCTION__);
		DPRINTK("%s\n", " parity = none");
	}

	/* Change the Stop bit */
	if (cflag & CSTOPB) {
		lStop = LCR_STOP_2;
		dbg("%s - stop bits = 2", __FUNCTION__);
		DPRINTK("%s\n", " stop bits = 2");
	} else {
		lStop = LCR_STOP_1;
		dbg("%s - stop bits = 1", __FUNCTION__);
		DPRINTK("%s\n", " stop bits = 1");
	}

	if (cflag & CMSPAR) {
		lParity = lParity | 0x20;
	}

	/* update the LCR with the correct LCR value */
	mos7703_port->shadowLCR &=
	    ~(LCR_BITS_MASK | LCR_STOP_MASK | LCR_PAR_MASK);
	mos7703_port->shadowLCR |= (lData | lParity | lStop);
	mos7703_port->validDataMask = mask;

        /* Disable Interrupts */
	data = 0x00;
	SendMosCmd(mos7703_port->port->serial, MOS_WRITE, MOS_UART_REG, IER,
		   &data);
	data = 0x00;
	SendMosCmd(mos7703_port->port->serial, MOS_WRITE, MOS_UART_REG, FCR,
		   &data);
	data = 0xcf;
	SendMosCmd(mos7703_port->port->serial, MOS_WRITE, MOS_UART_REG, FCR,
		   &data);

        /* Send the updated LCR value to the mos7703 */
	data = mos7703_port->shadowLCR;
	SendMosCmd(mos7703_port->port->serial, MOS_WRITE,
		   MOS_UART_REG, LCR, &data);

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
	SendMosCmd(mos7703_port->port->serial, MOS_WRITE,
		   MOS_UART_REG, MCR, &data);

	/* Determine divisor based on baud rate */
	baud = tty_get_baud_rate(tty);
	DPRINTK("%s-- baud = %d\n", "Back from <tty_get_baud_rate>...", baud);

	if (baud > 115200) {
		set_high_rates(mos7703_port, &baud);
		/* Enable Interrupts */
		data = 0x0c;
		SendMosCmd(mos7703_port->port->serial, MOS_WRITE, MOS_UART_REG,
			   IER, &data);
		return;
	}
	if (!baud) {
		DPRINTK("%s\n", "Picked default baud...");
		baud = 9600;
	}

	dbg("%s - baud rate = %d", __FUNCTION__, baud);
	status = send_cmd_write_baud_rate(mos7703_port, baud);
	wake_up(&mos7703_port->delta_msr_wait);
	mos7703_port->delta_msr_cond = 1;
	return;
}

/****************************************************************************
 * mos7703_startup
 ****************************************************************************/
static int mos7703_startup(struct usb_serial *serial)
{
	struct moschip_serial *mos7703_serial;
	struct moschip_port *mos7703_port;
	struct usb_device *dev;
	int i;

	if (!serial) {
		DPRINTK("%s\n", "Invalid Handler");
		return -1;
	}

	dev = serial->dev;

	/* create our private serial structure */
	mos7703_serial = kmalloc(sizeof(struct moschip_serial), GFP_KERNEL);
	if (mos7703_serial == NULL) {
		err("%s - Out of memory", __FUNCTION__);
		return -ENOMEM;
	}

	/* resetting the private structure field values to zero */
	memset(mos7703_serial, 0, sizeof(struct moschip_serial));

	mos7703_serial->serial = serial;
	serial->private = mos7703_serial;

	/* we set up the pointers to the endpoints in the mos7703_open function, 
	 * as the structures aren't created yet. */

	/* set up our port private structures */
	for (i = 0; i < serial->num_ports; ++i) {

		mos7703_port = kmalloc(sizeof(struct moschip_port), GFP_KERNEL);

		if (mos7703_port == NULL) {
			usb_set_serial_data(serial, NULL);
			kfree(mos7703_serial);
			err("%s - Out of memory", __FUNCTION__);
			return -ENOMEM;
		}

		memset(mos7703_port, 0, sizeof(struct moschip_port));

		/* Initialize all port interrupt end point to port 0 int endpoint
		 * Our device has only one interrupt end point comman to all port */

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
			(__u8) 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 5 * HZ);

	return 0;

}


#ifdef xyz
static struct usb_serial *get_usb_serial(struct usb_serial_port *port,
					 const char *function)
{
	/* if no port was specified, or it fails a paranoia check */
	if (!port ||
	    port_paranoia_check(port, function) ||
	    serial_paranoia_check(port->serial, function)) {
		/* then say that we don't have a valid usb_serial thing, which will
		 * end up genrating -ENODEV return values */
		return NULL;
	}

	return port->serial;
}

/* Inline functions to check the sanity of a pointer that is passed to us */
static int serial_paranoia_check(struct usb_serial *serial,
				 const char *function)
{
	if (!serial) {
		dbg("%s - serial == NULL", function);
		return -1;
	}
	if (!serial->type) {
		dbg("%s - serial->type == NULL!", function);
		return -1;
	}

	return 0;
}

static int port_paranoia_check(struct usb_serial_port *port,
			       const char *function)
{
	if (!port) {
		dbg("%s - port == NULL", function);
		return -1;
	}
	if (!port->serial) {
		dbg("%s - port->serial == NULL", function);
		return -1;
	}

	return 0;
}

#endif

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
	.break_ctl = mos7703_break,
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
