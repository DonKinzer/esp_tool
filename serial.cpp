// $Id: serial.cpp 67 2015-07-22 21:23:28Z Don $

/*
 ** Module: serial.cpp
 *
 * Copyright 2015 Don Kinzer
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
 * Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * This module implements serial I/O functions.
 *
 * The code is written to be compiled with the VC6 compiler and run on a Windows box.
 * Some changes may be required to compile with later version of Visual C.  It
 * may be possible to modify it to compile/run on Linux boxes.
 *
 */

#if defined(__APPLE__) && defined(__GNUC__) && !defined(__linux__)
  #define __linux__ 1
#endif

/** include files **/
#include <limits.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#if defined(WIN32)
  #include <memory.h>
  #include <io.h>
#elif defined(__linux__)
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <sys/ioctl.h>
  #include <fcntl.h>
  #include <termios.h>
  #include <unistd.h>
#endif
#include "serial.h"

/** local definitions **/

/** private data **/
#if defined(__linux__)
static struct termios term;
#endif

/** internal functions **/

/** public functions **/

/*
 ** SerialOpen
 *
 * Open a serial channel described by the first parameter and set the baud rate
 * and other characteristics.  If successful, a SerialHandle_t is returned otherwise
 * the value INVALID_SERIAL_HANDLE is returned.
 *
 * For Windows systems, the serial channel descriptor should be in the
 * form "//./COMn" where n is a numeric value greater than zero.
 *
 */
SerialHandle_t
SerialOpen(const char *desc, unsigned long baud, unsigned flags)
{
	int stat = -1;
	SerialHandle_t hand = INVALID_SERIAL_HANDLE;

	if ((desc != NULL) && (*desc != '\0') && baud)
	{
#if defined(WIN32)
		// attempt to open the channel
		hand = CreateFile(desc, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
		if (IS_VALID_SERIAL_HANDLE(hand))
		{
			// set the device driver queue sizes
			SetupComm(hand, 200, 200);

			// initialize the DCB
			DCB dcb;
			memset(&dcb, 0, sizeof(dcb));
			dcb.DCBlength = sizeof(dcb);

			// configure the serial channel
			dcb.fBinary = 1;
			dcb.BaudRate = baud;
			switch(flags & SERIAL_PARITY_MASK)
			{
			case SERIAL_PARITY_EVEN:	dcb.Parity = EVENPARITY;	break;
			case SERIAL_PARITY_ODD:		dcb.Parity = ODDPARITY;		break;
			default:					dcb.Parity = NOPARITY;		break;
			}
			dcb.ByteSize = 8 - (flags & SERIAL_BITS_MASK);
			switch(flags & SERIAL_STOPBITS_MASK)
			{
			case SERIAL_STOPBITS_1:		dcb.StopBits = ONESTOPBIT;	break;
			case SERIAL_STOPBITS_1_HALF:dcb.StopBits = ONE5STOPBITS;break;
			default:					dcb.StopBits = TWOSTOPBITS;	break;
			}
			if (SetCommState(hand, &dcb))
				stat = 0;
		}
#elif defined(__linux__)
		int dflags = O_RDWR | O_NOCTTY;
  #ifdef __APPLE__
		dflags |= O_NONBLOCK;
  #endif
		if ((hand = open(desc, dflags)) >= 0)
		{
  #ifdef __APPLE__
			dflags = fcntl(hand, F_GETFL, 0);
			fcntl(hand, F_SETFL, dflags & ~O_NONBLOCK);
  #endif
			tcgetattr(hand, &term);
			SerialSetSpeed(hand, baud);

			// configure the serial channel
			term.c_cflag |= CLOCAL | CREAD;
			term.c_cflag &= ~(PARENB | PARODD);
			switch(flags & SERIAL_PARITY_MASK)
			{
			case SERIAL_PARITY_EVEN:	term.c_cflag |= PARENB;		break;
			case SERIAL_PARITY_ODD:		term.c_cflag |= PARENB | PARODD; break;
			default:												break;
			}

			term.c_cflag &= ~CSIZE;
			switch(flags & SERIAL_BITS_MASK)
			{
			case SERIAL_BITS_5:			term.c_cflag |= CS5;		break;
			case SERIAL_BITS_6:			term.c_cflag |= CS6;		break;
			case SERIAL_BITS_7:			term.c_cflag |= CS7;		break;
			default:					term.c_cflag |= CS8;		break;
			}

			if ((flags & SERIAL_STOPBITS_MASK) == SERIAL_STOPBITS_1)
				term.c_cflag &= ~CSTOPB;
			else
				term.c_cflag |= CSTOPB;

			term.c_iflag = IGNBRK;
			term.c_iflag &= ~(IXON | IXOFF);
			term.c_lflag = 0;
			term.c_oflag = 0;
			term.c_cc[VMIN]=0;
			term.c_cc[VTIME]=1;
			if ((tcsetattr(hand, TCSANOW, &term) == 0) &&
					(tcgetattr(hand, &term) == 0))
			{
  #if defined(CRTSCTS)
				term.c_cflag &= ~CRTSCTS;
				if (tcsetattr(hand, TCSANOW, &term) == 0)
					stat = 0;
  #else
				stat = 0;
#endif
			}
		}
#elif defined(ERROR_MISSING_IMPLEMENTATION)
	#error missing implementation of SerialOpen()
#endif


		// set the DTR and RTS lines as requested
		if (stat == 0)
			stat = SerialControl(hand, flags);

		// close the handle if any configuration error occurred
		if (IS_VALID_SERIAL_HANDLE(hand) &&	(stat != 0))
		{
			// configuration failed, close the channel
			SerialClose(hand);
			hand = INVALID_SERIAL_HANDLE;
		}
	}
	return(hand);
}

/*
 ** SerialControl
 *
 * Set serial control signals, e.g. RTS, DTR.
 *
 */
int
SerialControl(SerialHandle_t hand, unsigned flags)
{
	int stat = -1;
	if (IS_VALID_SERIAL_HANDLE(hand))
	{
#if defined(WIN32)
		stat = 0;

		if ((flags & SERIAL_DTR_MASK) && !stat)
		{
			switch (flags & SERIAL_DTR_MASK)
			{
			case SERIAL_DTR_LOW:	stat = !EscapeCommFunction(hand, CLRDTR); break;
			case SERIAL_DTR_HIGH:	stat = !EscapeCommFunction(hand, SETDTR); break;
			}
		}
		if ((flags & SERIAL_RTS_MASK) && !stat)
		{
			switch (flags & SERIAL_RTS_MASK)
			{
			case SERIAL_RTS_LOW:	stat = !EscapeCommFunction(hand, CLRRTS); break;
			case SERIAL_RTS_HIGH:	stat = !EscapeCommFunction(hand, SETRTS); break;
			}
		}
#elif defined(__linux__)
		int tcm;
		if (ioctl(hand, TIOCMGET, &tcm) == 0)
		{
			unsigned sel;
			if ((sel = flags & SERIAL_DTR_MASK) != 0)
			{
				if (sel == SERIAL_DTR_HIGH)
					tcm |= TIOCM_DTR;
				else
					tcm &= ~TIOCM_DTR;
			}
			if ((sel = flags & SERIAL_RTS_MASK) != 0)
			{
				if (sel == SERIAL_RTS_HIGH)
					tcm |= TIOCM_RTS;
				else
					tcm &= ~TIOCM_RTS;
			}
			if (ioctl(hand, TIOCMSET, &tcm) == 0)
				stat = 0;
		}
#elif defined(ERROR_MISSING_IMPLEMENTATION)
	#error missing implementation of SerialControl()
#endif
	}
	return(stat);
}

/*
 ** SerialBreak
 *
 * Set a break condition on the serial transmit line for the
 * indicate time (in milliseconds).
 *
 */
int
SerialBreak(SerialHandle_t hand, unsigned msBreakTime)
{
	if (!IS_VALID_SERIAL_HANDLE(hand))
		return(-1);

	int stat = -1;
#if defined(WIN32)
	if (EscapeCommFunction(hand, SETBREAK))
	{
		Sleep(msBreakTime);
		if (EscapeCommFunction(hand, CLRBREAK))
			stat = 0;
	}
#elif defined(__linux__)
	// 'msBreakTime' parameter not used
	if (tcsendbreak(hand, 0) == 0)
		stat = 0;
#elif defined(ERROR_MISSING_IMPLEMENTATION)
	#error missing implementation of SerialControl()
#endif
	return(stat);
}

/*
 ** SerialFlush
 *
 * Flush the device driver's input queue. Return zero if successful.
 *
 */
int
SerialFlush(SerialHandle_t hand)
{
	int stat = -1;
	if (IS_VALID_SERIAL_HANDLE(hand))
	{
#if defined(WIN32)
		if (PurgeComm(hand, PURGE_RXCLEAR))
			stat = 0;
#elif defined(__linux__)
		if (tcflush(hand, TCIOFLUSH) == 0)
			stat = 0;
#elif defined(ERROR_MISSING_IMPLEMENTATION)
	#error missing implementation of SerialControl()
#endif
	}
	return(stat);
}

/*
 ** SerialAvailable
 *
 * Get the current speed of a serial channel, returning zero on error.
 *
 */
unsigned
SerialAvailable(SerialHandle_t hand)
{
	unsigned count = 0;

	if (IS_VALID_SERIAL_HANDLE(hand))
	{
#if defined(WIN32)
		unsigned long cnt;
		COMSTAT cs;

		if (ClearCommError(hand, &cnt, &cs))
			count = cs.cbInQue;
#elif defined(__linux__)
		int byteCnt;
		if (ioctl(hand, FIONREAD, &byteCnt) >= 0)
			count = (unsigned)byteCnt;
#elif defined(ERROR_MISSING_IMPLEMENTATION)
	#error missing implementation of SerialAvailable()
#endif
	}
	return(count);
}

/*
 ** SerialRead
 *
 * Read data from the serial channel.  If the requested amount is greater
 * than the amount available, the smaller amount will be read.  The return
 * value is the number of bytes placed in the buffer.
 *
 */
unsigned
SerialRead(SerialHandle_t hand, unsigned char *buf, unsigned count)
{
	unsigned actual = 0;

	if (IS_VALID_SERIAL_HANDLE(hand) && buf && count)
	{
#if defined(WIN32)
		unsigned long cnt;
		COMSTAT cs;

		if (ClearCommError(hand, &cnt, &cs))
		{
			if (count > cs.cbInQue)
				count = cs.cbInQue;
			if (ReadFile(hand, buf, count, &cnt, NULL))
				actual = cnt;
		}
#elif defined(__linux__)
		size_t stat = read(hand, buf, count);
		if (stat > 0)
			actual = (unsigned)stat;
#elif defined(ERROR_MISSING_IMPLEMENTATION)
	#error missing implementation of SerialRead()
#endif
	}
	return(actual);
}

/*
 ** SerialWrite
 *
 * Write bytes to a serial port.  The value returned is the number of
 * bytes actually written.
 *
 */
unsigned
SerialWrite(SerialHandle_t hand, const unsigned char *buf, unsigned count)
{
	unsigned actual = 0;

	if (IS_VALID_SERIAL_HANDLE(hand) && buf && count)
	{
#if defined(WIN32)
		unsigned long cnt;

		if (WriteFile(hand, buf, count, &cnt, NULL))
			actual = cnt;
#elif defined(__linux__)
		size_t stat = write(hand, buf, count);
		if (stat > 0)
			actual = (unsigned)stat;
#elif defined(ERROR_MISSING_IMPLEMENTATION)
	#error missing implementation of SerialWrite()
#endif
	}
	return(actual);
}

/*
 ** SerialWriteByte
 *
 * Write a byte to a serial port.  The value returned is the number of
 * bytes actually written.
 *
 */
unsigned
SerialWriteByte(SerialHandle_t hand, unsigned char b)
{
	return(SerialWrite(hand, &b, 1));
}

/*
 ** SerialClose
 *
 * Close a serial channel given a SerialHandle_t.  If successful, return zero otherwise
 * non-zero.
 *
 */
int
SerialClose(SerialHandle_t hand)
{
	int stat = -1;
	if (IS_VALID_SERIAL_HANDLE(hand))
	{
#if defined(WIN32)
		if (CloseHandle(hand))
			stat = 0;
#elif defined(__linux__)
		tcdrain(hand);
		tcflush(hand, TCIOFLUSH);
		if (close(hand) == 0)
			stat = 0;
#elif defined(ERROR_MISSING_IMPLEMENTATION)
	#error missing implementation of SerialClose()
#endif
	}
	return(stat);
}

/*
 ** SerialSetSpeed
 *
 * Set the speed of a serial channel.  Return zero if successful,
 * non-zero otherwise.
 *
 */
int
SerialSetSpeed(SerialHandle_t hand, unsigned long speed)
{
	int stat = -1;

	if (IS_VALID_SERIAL_HANDLE(hand))
	{
#if defined(WIN32)
		DCB dcb;
		memset(&dcb, 0, sizeof(dcb));
		dcb.DCBlength = sizeof(dcb);
		if (GetCommState(hand, &dcb))
		{
			dcb.BaudRate = speed;
			if (SetCommState(hand, &dcb))
				stat = 0;
		}
#elif defined(__linux__)
		cfsetispeed(&term, speed);
		cfsetospeed(&term, speed);
#elif defined(ERROR_MISSING_IMPLEMENTATION)
	#error missing implementation of SerialSetSpeed()
#endif
	}
	return(stat);
}

/*
 ** SerialGetSpeed
 *
 * Get the current speed of a serial channel, returning zero on error.
 *
 */
unsigned long
SerialGetSpeed(SerialHandle_t hand)
{
	unsigned long speed = 0;

	if (IS_VALID_SERIAL_HANDLE(hand))
	{
#if defined(WIN32)
		DCB dcb;
		memset(&dcb, 0, sizeof(dcb));
		dcb.DCBlength = sizeof(dcb);
		if (GetCommState(hand, &dcb))
			speed = dcb.BaudRate;
#elif defined(__linux__)
		speed = cfgetispeed(&term);
#elif defined(ERROR_MISSING_IMPLEMENTATION)
	#error missing implementation of SerialGetSpeed()
#endif
	}
	return(speed);
}

/** class implementations **/

SerialChannel::
SerialChannel()
{
	m_handle = INVALID_SERIAL_HANDLE;
}

SerialChannel::
~SerialChannel()
{
	Close();
}

//
// Open the specified serial channel.
//
int SerialChannel::
Open(const char *desc, unsigned long baud, unsigned flags)
{
	int stat = -1;

	Close();
	SerialHandle_t handle = SerialOpen(desc, baud, flags);
	if (IS_VALID_SERIAL_HANDLE(handle))
	{
		m_handle = handle;
		m_queue.Init(handle);
		stat = 0;
	}
	return(stat);
}

//
// Close the serial channel.
//
int SerialChannel::
Close()
{
	int stat = -1;

	m_queue.Init();
	stat = SerialClose(m_handle);
	m_handle = INVALID_SERIAL_HANDLE;
	return(stat);
}

//
// Read data from the queue, return the number of bytes placed in the buffer.
//
unsigned SerialChannel::
Read(unsigned char *buf, unsigned count)
{
	unsigned byteCnt = m_queue.Refresh();
	if (count > byteCnt)
		count = byteCnt;
	return(m_queue.GetData(buf, count));
}

//
// Read a byte optionally performing SLIP decoding.  The return values are:
//
//	2 - an escaped byte was read successfully
//	1 - a non-escaped byte was read successfully
//	0 - no data was available
//   -1 - the value 0xc0 was encountered (shouldn't happen)
//   -2 - a SLIP escape byte was found but the following byte wasn't available
//   -3 - a SLIP escape byte was followed by an invalid byte
//
int SerialChannel::
ReadByte(unsigned char& data, bool slipDecode)
{
	if (!Available())
		return(0);

	// at least one byte is available
	data = ReadByte();
	if (!slipDecode)
		return(1);
	if (data == 0xc0)
		// this shouldn't happen
		return(-1);

	// if not the SLIP escape, we're done
	if (data != 0xdb)
		return(1);

	// SLIP escape, check availability of subsequent byte
	if (!Available())
		return(-2);

	// process the escaped byte
	data = ReadByte();
	if (data == 0xdc)
	{
		data = 0xc0;
		return(2);
	}
	if (data == 0xdd)
	{
		data = 0xdb;
		return(2);
	}
	// invalid
	return(-3);
}

//
// Write a block of data to the serial port.
//
unsigned SerialChannel::
Write(const unsigned char *buf, unsigned count) const
{
	return(SerialWrite(m_handle, buf, count));
}

//
// Write a byte to the serial port optionally SLIP encoding.  The return value
// represents the number of bytes actually written.
//
unsigned SerialChannel::
WriteByte(unsigned char b, bool slipEncode) const
{
	unsigned cnt = 1;
	unsigned char buf[2];

	buf[0] = b;
	if (slipEncode)
	{
		if (b == 0xc0)
			buf[0] = 0xdb, buf[1] = 0xdc, cnt = 2;
		else if (b == 0xdb)
			buf[0] = 0xdb, buf[1] = 0xdd, cnt = 2;
	}
	return(Write(buf, cnt));
}

/*************************************************************************/

SerialQueue::
SerialQueue(SerialHandle_t handle, unsigned maxSize, unsigned initialSize)
{
	Init(handle);
	m_maxSize = maxSize;
	if (initialSize)
	{
		// create the initial data space
		m_data = new unsigned char[initialSize];
		m_curSize = initialSize;
	}
	else
	{
		m_data = NULL;
		m_curSize = 0;
	}
}

SerialQueue::
~SerialQueue()
{
	if (m_data != NULL)
		delete[] m_data;
	m_data = NULL;
}

//
// Initialize a queue.
//
void SerialQueue::
Init(SerialHandle_t handle)
{
	m_handle = handle;
	m_head = 0;
	m_count = 0;
}

//
// Extract data from a queue.  The return value is the number of bytes
// copied to the buffer.  N.B.: this routine will not return until
// the number of bytes specified has been read.
//
unsigned SerialQueue::
GetData(unsigned char *buf, unsigned count)
{
	unsigned actual = 0;

	if (buf && count)
	{
		do
		{
			Refresh();

			// copy from the local queue
			unsigned part = count;
			if (part > m_count)
				part = m_count;
			if (part > 0)
			{
				memcpy(buf + actual, m_data + m_head, part);
				actual += part;
				m_head += part;
				m_count -= part;
				count -= part;
			}
		} while (count);
	}
	return(actual);
}

//
// Determine the total number of bytes of data that is available - both the
// data that is queued locally as well as that queued in the device driver.
//
unsigned SerialQueue::
Available()
{
	Refresh();
	return(m_count + SerialAvailable(m_handle));
}

//
// Refill a queue from the serial port up to the maximum size subject
// to the number of characters available.
//
unsigned SerialQueue::
Refresh()
{
	// see if there is more data queued in the driver
	unsigned count;
	if ((count = SerialAvailable(m_handle)) != 0)
	{
		unsigned part;

		// more serial data is available, determine how much to read
		if (m_maxSize == 0)
		{
			// grow the queue, if necessary, to accommodate the available data
			if ((part = count) > (m_curSize - m_count))
			{
				unsigned newSize = count + m_count;
				unsigned char *p = new unsigned char[newSize];
				if (m_count)
					memcpy(p, m_data + m_head, m_count);
				delete[] m_data;
				m_data = p;
				m_curSize = newSize;
				m_head = 0;
			}
		}
		else if ((m_curSize < m_maxSize) && (m_count < m_curSize))
			// limit the amount to be read to the available space
			part = m_curSize - m_count;
		else
			// no space left
			part = 0;

		// move the existing data to the beginning of the queue before adding more
		if (m_count && m_head)
			memcpy(m_data, m_data + m_head, m_count);
		m_head = 0;

		// add data to the queue
		if (part > count)
			part = count;
		if (part)
			m_count += SerialRead(m_handle, m_data + m_count, part);
	}
	return(m_count);
}

//
// Remove all characters from the input queue and the associated
// serial channel.
//
void SerialQueue::
Flush()
{
	// delete from the local queue
	m_count = 0;
	m_head = 0;

	// flush the device driver's queue
	if (IS_VALID_SERIAL_HANDLE(m_handle))
		SerialFlush(m_handle);
}

/** private functions **/
