// $Id: serial.h 76 2015-08-24 23:08:40Z Don $

/*
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
 */
#if	!defined(SERIAL_H__)
#define SERIAL_H__

#if defined(WIN32)
  #include <windows.h>
  #include <conio.h>
#endif

/****************************************************************************/

// low level serial functions

#if defined(WIN32)
typedef HANDLE SerialHandle_t;
#define IS_VALID_SERIAL_HANDLE(h)		((h) != INVALID_HANDLE_VALUE)
#define INVALID_SERIAL_HANDLE			INVALID_HANDLE_VALUE
#else
typedef int SerialHandle_t;
#define IS_VALID_SERIAL_HANDLE(h)		((h) >= 0)
#define INVALID_SERIAL_HANDLE			-1
#endif

// bit values for the 'flags parameter to SerialOpen()
#define SERIAL_NO_FLAGS					0x0000

#define SERIAL_BITS_8					0x0000
#define SERIAL_BITS_7					0x0001
#define SERIAL_BITS_6					0x0002
#define SERIAL_BITS_5					0x0003
#define SERIAL_BITS_MASK				0x0003

#define SERIAL_PARITY_NONE				0x0000
#define SERIAL_PARITY_EVEN				0x0008
#define SERIAL_PARITY_ODD				0x000c
#define SERIAL_PARITY_MASK				0x000c

#define SERIAL_STOPBITS_1				0x0000
#define SERIAL_STOPBITS_1_HALF			0x0010
#define SERIAL_STOPBITS_2				0x0011
#define SERIAL_STOPBITS_MASK			0x0010

// N.B.: low and high here (and for RTS) refer to RS232 voltage levels -
//   for USB devices the logic state will the the opposite
#define SERIAL_DTR_NONE					0x0000
#define SERIAL_DTR_LOW					0x0200
#define SERIAL_DTR_HIGH					0x0300
#define SERIAL_DTR_MASK					0x0300

#define SERIAL_RTS_NONE					0x0000
#define SERIAL_RTS_LOW					0x2000
#define SERIAL_RTS_HIGH					0x3000
#define SERIAL_RTS_MASK					0x3000

SerialHandle_t SerialOpen(const char *desc, unsigned long baud, unsigned flags);
int SerialClose(SerialHandle_t hand);
int SerialSetSpeed(SerialHandle_t hand, unsigned long speed);
unsigned long SerialGetSpeed(SerialHandle_t hand);
unsigned SerialAvailable(SerialHandle_t hand);
unsigned SerialRead(SerialHandle_t hand, unsigned char *buf, unsigned count);
unsigned SerialWrite(SerialHandle_t hand, const unsigned char *buf, unsigned count);
unsigned SerialWriteByte(SerialHandle_t hand, unsigned char b);
int SerialControl(SerialHandle_t hand, unsigned flags);
int SerialBreak(SerialHandle_t hand, unsigned msBreakTime);
int SerialFlush(SerialHandle_t hand);

/****************************************************************************/

//
// A class to manage a queue associated with a serial port.
//
class SerialQueue
{
public:
	SerialQueue(SerialHandle_t hand = INVALID_SERIAL_HANDLE, unsigned maxSize = 0, unsigned initialSize = 0);
	~SerialQueue();

	void Init(SerialHandle_t hand = INVALID_SERIAL_HANDLE);
	void SetHandle(SerialHandle_t hand) { m_handle = hand; }
	void SetMaxSize(unsigned maxSize) { m_maxSize = maxSize; }
	unsigned Available();
	unsigned Count() const { return(m_count); }
	SerialHandle_t GetHandle() const { return(m_handle); }
	unsigned Refresh();
	void Flush();
	unsigned GetData(unsigned char *buf, unsigned count);

protected:

private:
	SerialQueue(const SerialQueue&);
	SerialQueue& operator=(const SerialQueue&);
	SerialHandle_t m_handle;		// the associated serial port
	unsigned m_maxSize;				// the maximum data space to use (0 for unlimited)
	unsigned m_curSize;				// current number of bytes of space in the queue
	unsigned m_count;				// current number of bytes of data in the queue
	unsigned m_head;				// the index to the first byte to be removed
	unsigned char *m_data;			// space for the data, allocated as needed
};

/****************************************************************************/

//
// A class to manage a serial port, incorporating a queue.
//
class SerialChannel
{
public:
	SerialChannel();
	~SerialChannel();

	bool IsOpen() const { return(IS_VALID_SERIAL_HANDLE(m_handle)); }
	int Open(const char *desc, unsigned long baud, unsigned flags = SERIAL_NO_FLAGS);
	int Close();
	int SetSpeed(unsigned long speed) { return(SerialSetSpeed(m_handle, speed)); }
	unsigned long GetSpeed() const { return(SerialGetSpeed(m_handle)); }
	unsigned Read(unsigned char *buf, unsigned count);
	unsigned char ReadByte() { unsigned char b; return(Read(&b, 1) ? b : 0); }
	int ReadByte(unsigned char& data, bool slipDecode = false);
	unsigned Write(const unsigned char *buf, unsigned count) const;
	unsigned WriteByte(unsigned char b, bool slipEncode = false) const;
	int Break(unsigned msBreakTime) { return(SerialBreak(m_handle, msBreakTime)); }
	int Control(unsigned flags) { return(SerialControl(m_handle, flags)); }

	unsigned Available() { return(m_queue.Available()); }
	void Flush() { m_queue.Flush(); }

protected:

private:
	SerialChannel(const SerialChannel&);
	SerialChannel& operator=(const SerialChannel&);
	SerialHandle_t m_handle;			// the associated serial port
	SerialQueue m_queue;				// the queue
};

#endif	// defined(SERIAL_H__)
