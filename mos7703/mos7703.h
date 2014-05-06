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

#define MAX_RS232_PORTS		2	/* Max # of RS-232 ports per device */

/* 
 *  All typedef goes here
 */

/* typedefs that the insideout headers need */

#ifndef TRUE
#define TRUE		(1)
#endif

#ifndef FALSE
#define FALSE		(0)
#endif

#ifndef LOW8
#define LOW8(val)		((unsigned char)(val & 0xff))
#endif

#ifndef HIGH8
#define HIGH8(val)	((unsigned char)((val & 0xff00) >> 8))
#endif

#ifndef NUM_ENTRIES
#define NUM_ENTRIES(x)	(sizeof(x)/sizeof((x)[0]))
#endif

#ifndef __KERNEL__
#define __KERNEL__
#endif

#define MAX_SERIALNUMBER_LEN 12

/* The following table is used to map the USBx port number to 
 * the device serial number (or physical USB path), */
#define MAX_MOSPORTS	2

#define MAX_NAME_LEN		64

//++higher baud

#define TIOCEXBAUD	0x5462	//IOCTL command to set higher baud rate

#define BAUD_1152	0	//115200bps  * 1
#define BAUD_2304	1	//230400bps  * 2
#define BAUD_4032	2	//403200bps  * 3.5
#define BAUD_4608	3	//460800bps  * 4
#define BAUD_8064	4	//806400bps  * 7
#define BAUD_9216	5	//921600bps  * 8

//--higher baud

#define CHASE_TIMEOUT		(5*HZ)	/* 5 seconds */
#define OPEN_TIMEOUT		(5*HZ)	/* 5 seconds */
#define COMMAND_TIMEOUT		(5*HZ)	/* 5 seconds */

#ifndef SERIAL_MAGIC
#define SERIAL_MAGIC	0x6702
#endif

#define PORT_MAGIC		0x7301

#define MOSPORT_CONFIG_DEVICE "/proc/mos7703"

/* /proc/mos7703 Interface
 * This interface uses read/write/lseek interface to talk to the moschip driver
 * the following read functions are supported: */
#define PROC_GET_MAPPING_TO_PATH 		1
#define PROC_GET_COM_ENTRY				2

#define PROC_GET_EDGE_MANUF_DESCRIPTOR	3
#define PROC_GET_BOOT_DESCRIPTOR		4
#define PROC_GET_PRODUCT_INFO			5

#define PROC_GET_STRINGS				6
#define PROC_GET_CURRENT_COM_MAPPING	7

/* The parameters to the lseek() for the read is: */
#define PROC_READ_SETUP(Command, Argument)	((Command) + ((Argument)<<8))

/* the following write functions are supported: */
#define PROC_SET_COM_MAPPING 		1
#define PROC_SET_COM_ENTRY			2

/* vendor id and device id defines */
#define USB_VENDOR_ID_MOSCHIP		0x9710
#define MOSCHIP_DEVICE_ID_7703		0x7703

/* 
 *  All structure defination goes here
 */

/* Interrupt Rotinue Defines	*/

#define SERIAL_IIR_RLS      0x06
#define SERIAL_IIR_RDA      0x04
#define SERIAL_IIR_CTI      0x0c
#define SERIAL_IIR_THR      0x02
#define SERIAL_IIR_MS       0x00
/*
 *  Emulation of the bit mask on the LINE STATUS REGISTER.
 */
#define SERIAL_LSR_DR       0x0001
#define SERIAL_LSR_OE       0x0002
#define SERIAL_LSR_PE       0x0004
#define SERIAL_LSR_FE       0x0008
#define SERIAL_LSR_BI       0x0010
#define SERIAL_LSR_THRE     0x0020
#define SERIAL_LSR_TEMT     0x0040
#define SERIAL_LSR_FIFOERR  0x0080

/*
 * URB POOL related defines
 */
#define NUM_URBS                        32	/* URB Count */
#define URB_TRANSFER_BUFFER_SIZE        32	/* URB Size  */

struct comMapper {
	char SerialNumber[MAX_SERIALNUMBER_LEN + 1];	/* Serial number/usb path */
	int numPorts;		/* Number of ports */
	int Original[MAX_RS232_PORTS];	/* Port numbers set by IOCTL */
	int Port[MAX_RS232_PORTS];	/* Actual used port numbers */
};

/* 
 * The following sturcture is passed to the write 
 */
struct procWrite {
	int Command;
	union {
		struct comMapper Entry;
		int ComMappingBasedOnUSBPort;	/* Boolean value */
	} u;
};

/*
 *	Product information read from the Moschip. Provided for later upgrade
 */
struct moschip_product_info {
	__u16 ProductId;	/* Product Identifier */
	__u8 NumPorts;		/* Number of ports on moschip */
	__u8 ProdInfoVer;	/* What version of structure is this? */

	__u32 IsServer:1;	/* Set if Server */
	__u32 IsRS232:1;	/* Set if RS-232 ports exist */
	__u32 IsRS422:1;	/* Set if RS-422 ports exist */
	__u32 IsRS485:1;	/* Set if RS-485 ports exist */
	__u32 IsReserved:28;	/* Reserved for later expansion */

	__u8 CpuRev;		/* CPU revision level (chg only if s/w visible) */
	__u8 BoardRev;		/* PCB revision level (chg only if s/w visible) */

	__u8 ManufactureDescDate[3];	/* MM/DD/YY when descriptor template was compiled */
	__u8 Unused1[1];	/* Available */

};

/*
static struct usb_device_id moschip_port_id_table [] = {
	{ USB_DEVICE(USB_VENDOR_ID_MOSCHIP,MOSCHIP_DEVICE_ID_7703) },
	{ } 
}; */

//static __devinitdata struct usb_device_id id_table_combined [] = {
static struct usb_device_id id_table_combined[] = {
	{USB_DEVICE(USB_VENDOR_ID_MOSCHIP, MOSCHIP_DEVICE_ID_7703)},
	{}			/* terminating entry */
};

MODULE_DEVICE_TABLE(usb, id_table_combined);

#if 0

/* receive port state */
enum RXSTATE {
	EXPECT_HDR1 = 0,	/* Expect header byte 1 */
	EXPECT_HDR2 = 1,	/* Expect header byte 2 */
	EXPECT_DATA = 2,	/* Expect 'RxBytesRemaining' data */
	EXPECT_HDR3 = 3,	/* Expect header byte 3 (for status hdrs only) */
};

#endif

/* Transmit Fifo 
 * This Transmit queue is an extension of the moschip Rx buffer. 
 * The maximum amount of data buffered in both the moschip 
 * Rx buffer (maxTxCredits) and this buffer will never exceed maxTxCredits.
 */
struct TxFifo {
	unsigned int head;	/* index to head pointer (write) */
	unsigned int tail;	/* index to tail pointer (read)  */
	unsigned int count;	/* Bytes in queue */
	unsigned int size;	/* Max size of queue (equal to Max number of TxCredits) */
	unsigned char *fifo;	/* allocated Buffer */
};

#if 0

/* Reciever Fifo 
 * This Transmit queue is an extension of the moschip Rx buffer. 
 * The maximum amount of data buffered in both the moschip 
 * Rx buffer (maxTxCredits) and this buffer will never exceed maxTxCredits.
 */
struct RxFifo {
	unsigned int head;	/* index to head pointer (write) */
	unsigned int tail;	/* index to tail pointer (read)  */
	unsigned int count;	/* Bytes in queue */
	unsigned int size;	/* Max size of queue (equal to Max number of TxCredits) */
	unsigned char *fifo;	/* allocated Buffer */
};

#endif

/* This structure holds all of the local port information */
struct moschip_port {
	__u16 txCredits;	/* our current credits for this port */
	__u16 maxTxCredits;	/* the max size of the port */

	struct TxFifo txfifo;	/* transmit fifo -- size will be maxTxCredits */

	__u8 bulk_out_endpoint;	/* the bulk out endpoint handle */
	unsigned char *bulk_out_buffer;	/* the buffer we use for the bulk out endpoint */
	struct urb *write_urb;	/* write URB for this port */

	__u8 bulk_in_endpoint;	/* the bulk in endpoint handle */
	unsigned char *bulk_in_buffer;	/* the buffer we use for the bulk in endpoint */
	struct urb *read_urb;	/* read URB for this port */

#if 0
	struct RxFifo rxfifo;
#endif

	__s16 rxBytesAvail;	/* the number of bytes that we need to read from this device */
	__s16 rxBytesRemaining;	/* the number of port bytes left to read */

	char write_in_progress;	/* TRUE while a write URB is outstanding */

	__u8 shadowLCR;		/* last LCR value received */
	__u8 shadowMCR;		/* last MCR value received */
	__u8 shadowMSR;		/* last MSR value received */
	__u8 shadowLSR;		/* last LSR value received */
	__u8 shadowXonChar;	/* last value set as XON char in moschip */
	__u8 shadowXoffChar;	/* last value set as XOFF char in moschip */

	__u8 validDataMask;

	__u32 baudRate;

	char open;
	char openPending;

	char commandPending;

	char closePending;

	char chaseResponsePending;

	wait_queue_head_t wait_chase;	/* for handling sleeping while waiting for chase to finish */
	wait_queue_head_t wait_open;	/* for handling sleeping while waiting for open to finish */
	wait_queue_head_t wait_command;	/* for handling sleeping while waiting for command to finish */
	wait_queue_head_t delta_msr_wait;	/* for handling sleeping while waiting for msr change to happen */
	int delta_msr_cond;

	struct async_icount icount;
	struct usb_serial_port *port;	/* loop back to the owner of this object */
	struct urb *write_urb_pool[NUM_URBS];
};

/* This structure holds all of the individual serial device information */
struct moschip_serial {
	char name[MAX_NAME_LEN + 1];	/* string name of this device */

	struct moschip_product_info product_info;	/* Product Info */

	__u8 interrupt_in_endpoint;	/* the interrupt endpoint handle */
	unsigned char *interrupt_in_buffer;	/* the buffer we use for the interrupt endpoint */
	struct urb *interrupt_read_urb;	/* our interrupt urb */

	__u8 bulk_in_endpoint;	/* the bulk in endpoint handle */
	unsigned char *bulk_in_buffer;	/* the buffer we use for the bulk in endpoint */
	struct urb *read_urb;	/* our bulk read urb */

	__u8 bulk_out_endpoint;	/* the bulk out endpoint handle */

	__s16 rxBytesAvail;	/* the number of bytes that we need to read from this device */

#if 0
	enum RXSTATE rxState;	/* the current state of the bulk receive processor */
	__u8 rxHeader1;		/* receive header byte 1 */
	__u8 rxHeader2;		/* receive header byte 2 */
	__u8 rxHeader3;		/* receive header byte 3 */
#endif

	__u8 rxPort;		/* the port that we are currently receiving data for */
	__u8 rxStatusCode;	/* the receive status code */
	__u8 rxStatusParam;	/* the receive status paramater */
	__s16 rxBytesRemaining;	/* the number of port bytes left to read */
	struct usb_serial *serial;	/* loop back to the owner of this object */
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
static void mos7703_interrupt_callback(struct urb *urb, struct pt_regs *regs);
static void mos7703_bulk_in_callback(struct urb *urb, struct pt_regs *regs);
static void mos7703_bulk_out_data_callback(struct urb *urb,
					   struct pt_regs *regs);

/* function prototypes for the usbserial callbacks */
static int mos7703_open(struct usb_serial_port *port, struct file *filp);
static void mos7703_close(struct usb_serial_port *port, struct file *filp);
//static int  mos7703_write                     (struct usb_serial_port *port, int from_user, const unsigned char *buf, int count);
static int mos7703_write(struct usb_serial_port *port, const unsigned char *buf,
			 int count);
static int mos7703_write_room(struct usb_serial_port *port);
static int mos7703_chars_in_buffer(struct usb_serial_port *port);
static void mos7703_throttle(struct usb_serial_port *port);
static void mos7703_unthrottle(struct usb_serial_port *port);
static void mos7703_set_termios(struct usb_serial_port *port,
				struct termios *old_termios);
static int mos7703_ioctl(struct usb_serial_port *port, struct file *file,
			 unsigned int cmd, unsigned long arg);
static void mos7703_break(struct usb_serial_port *port, int break_state);
static int mos7703_startup(struct usb_serial *serial);
static void mos7703_shutdown(struct usb_serial *serial);

/* function prototypes for all of our local functions */
static int SendMosCmd(struct usb_serial *serial, __u8 request, __u16 value,
		      __u16 index, void *data);
static int calc_baud_rate_divisor(int baud_rate, int *divisor);
static int send_cmd_write_baud_rate(struct moschip_port *mos7703_port,
				    int baudRate);
static void change_port_settings(struct moschip_port *mos7703_port,
				 struct termios *old_termios);

#endif
