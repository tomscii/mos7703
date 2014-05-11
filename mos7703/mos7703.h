/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*************************************************************************
 *** Project Name: MosChip
 ***
 *** Module Name: Mos7703
 ***
 *** File: mos7703.h 
 ***		
 ***
 *** File Revision: 0.0
 ***
 *** Revision Date:  20/06/05 
 ***
 *** Author       : Eshwar Danduri
 ***			  
 ***
 *** Purpose	  : It contains defines that will be used by the corresponding mosc7703.c file
 ***
 *** Change History:
 ***
 ***
 *** LEGEND		:
 ***
 *** 
 ***
 *************************************************************************/

#if !defined(_MOS_CIP_H_)
#define	_MOS_CIP_H_

/* typedefs that the insideout headers need */

#ifndef TRUE
#define TRUE		(1)
#endif

#ifndef FALSE
#define FALSE		(0)
#endif

#ifndef LOW8
#define LOW8(val)	((unsigned char)(val & 0xff))
#endif

#ifndef HIGH8
#define HIGH8(val)	((unsigned char)((val & 0xff00) >> 8))
#endif

#ifndef NUM_ENTRIES
#define NUM_ENTRIES(x)	(sizeof(x)/sizeof((x)[0]))
#endif

#define NUM_URBS                        32	/* URB Count */
#define URB_TRANSFER_BUFFER_SIZE        32	/* URB Size  */

/* This structure holds all of the local port information */
struct moschip_port {
	struct urb *write_urb;	/* write URB for this port */
	struct urb *read_urb;	/* read URB for this port */

	__u8 shadowLCR;		/* last LCR value received */
	__u8 shadowMCR;		/* last MCR value received */
	__u8 shadowMSR;		/* last MSR value received */

	char open;
	char openPending;
	char closePending;

	wait_queue_head_t delta_msr_wait; /* for handling sleeping while waiting for msr change to happen */
	int delta_msr_cond;

	struct async_icount icount;
	struct usb_serial_port *port;	/* loop back to the owner of this object */
	struct usb_serial *serial;	/* loop back to the owner of this object */
	struct tty_struct *tty;
	struct urb *write_urb_pool[NUM_URBS];
};

/* baud rate information */
struct divisor_table_entry {
	__u32 BaudRate;
	__u16 Divisor;
};

//
// Define table of divisors for Rev A moschip 7703 hardware
// These assume a 3.6864MHz crystal, the standard /16, and
// MCR.7 = 0.
//
static struct divisor_table_entry divisor_table[] = {
	{50, 2304},
	{110, 1047},		/* 2094.545455 => 230450   => .0217 % over */
	{134, 857},		/* 1713.011152 => 230398.5 => .00065% under */
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

/* local function prototypes */

/* function prototypes for all URB callbacks */
static void mos7703_interrupt_callback(struct urb *urb);
static void mos7703_bulk_in_callback(struct urb *urb);
static void mos7703_bulk_out_data_callback(struct urb *urb);

/* function prototypes for the usbserial callbacks */
static int mos7703_open(struct tty_struct *tty, struct usb_serial_port *port);
static void mos7703_close(struct usb_serial_port *port);
static int mos7703_write(struct tty_struct *tty, struct usb_serial_port *port,
			 const unsigned char *data, int count);
static int mos7703_write_room(struct tty_struct *tty);
static int mos7703_chars_in_buffer(struct tty_struct *tty);
static void mos7703_throttle(struct tty_struct *tty);
static void mos7703_unthrottle(struct tty_struct *tty);
static void mos7703_set_termios(struct tty_struct *tty,
				struct usb_serial_port *port,
				struct ktermios *old_termios);
static int mos7703_ioctl(struct tty_struct *tty,
			 unsigned int cmd, unsigned long arg);
static int mos7703_startup(struct usb_serial *serial);

/* function prototypes for all of our local functions */
static int SendMosCmd(struct usb_serial *serial, __u8 request, __u16 value,
		      __u16 index, void *data);
static int calc_baud_rate_divisor(int baud_rate, int *divisor);
static int send_cmd_write_baud_rate(struct moschip_port *mos7703_port,
				    int baudRate);
static void change_port_settings(struct tty_struct *tty,
				 struct moschip_port *mos7703_port,
				 struct ktermios *old_termios);

#endif
