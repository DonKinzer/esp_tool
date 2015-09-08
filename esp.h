// $Id: esp.h 78 2015-09-08 17:54:33Z Don $

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
#if	!defined(ESP__H__)
#define ESP__H__

#include "sysdep.h"
#include <stdio.h>
#include <sys/stat.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#if defined(WIN32)
  #include <io.h>
#elif defined(__linux__)
  #include <string.h>
  #include <unistd.h>
#endif
#if defined(HAVE_STDINT_H)
  #include <stdint.h>
#endif
#include "serial.h"
#include "elf.h"

#define MAX_FILENAME				1024		// the longest filename that can be handled

#define DEF_TIMEOUT					500			// default timeout in milliseconds

// debugging code values
#define DIAG_NO_TIME_LIMIT			0x0001

// ESP8266 command codes
#define ESP_FLASH_BEGIN				0x02
#define ESP_FLASH_DATA				0x03
#define ESP_FLASH_END				0x04
#define ESP_MEM_BEGIN				0x05
#define ESP_MEM_END					0x06
#define ESP_MEM_DATA				0x07
#define ESP_SYNC					0x08
#define ESP_WRITE_REG				0x09
#define ESP_READ_REG				0x0a

// MAC address storage locations
#define ESP_OTP_MAC0				0x3ff00050
#define ESP_OTP_MAC1				0x3ff00054
#define ESP_OTP_MAC2				0x3ff00058
#define ESP_OTP_MAC3				0x3ff0005c

#define FLASH_MODE_MASK				0x0003
#define FLASH_SIZE_MASK				0xf000
#define FLASH_FREQ_MASK				0x0f00

#define ESP_FLASH_BLK_SIZE			0x0400		// 1K byte blocks
#define ESP_RAM_BLOCK_SIZE			0x0400		// 1K byte blocks

#define ESP_NO_ADDRESS				(uint32_t)(~(ESP_FLASH_BLK_SIZE - 1))

#define COMPOSITE_SIG				"esp"
#define ESP_IMAGE_MAGIC				0xe9
#define ESP_CHECKSUM_MAGIC			0xef

#define ERASE_CHIP_ADDR				0x40004984	// &SPIEraseChip
#define SEND_PACKET_ADDR			0x40003c80	// &send_packet
#define SPI_READ_ADDR				0x40004b1c	// &SPIRead
#define UNKNOWN_ADDR				0x40001121	// not used
#define USER_DATA_RAM_ADDR			0x3ffe8000	// &user data ram
#define IRAM_ADDR					0x40100000	// instruction RAM
#define FLASH_ADDR					0x40200000	// address of start of Flash
#define FLASH_READ_STUB_BEGIN		IRAM_ADDR + 0x18

// this macro expands a value to four bytes in little-endian order
#define LE_BYTES(v)					(((v) >> 0) & 0xff), (((v) >> 8) & 0xff), (((v) >> 16) & 0xff), (((v) >> 24) & 0xff)

// flags to control operation
#define ESP_QUIET					0x0001
#define ESP_AUTO_RUN				0x0002

// error codes
#define ESP_SUCCESS					0
#define ESP_ERROR_GENERAL			-1
#define ESP_ERROR_TIMEOUT			-2
#define ESP_ERROR_ALLOC				-3
#define ESP_ERROR_PARAM				-4
#define ESP_ERROR_COMM_OPEN			-5
#define ESP_ERROR_COMM_READ			-6
#define ESP_ERROR_COMM_WRITE		-7
#define ESP_ERROR_CONNECT			-8
#define ESP_ERROR_REPLY				-9
#define ESP_ERROR_FILE_OPEN			-10
#define ESP_ERROR_FILE_CREATE		-11
#define ESP_ERROR_FILE_READ			-12
#define ESP_ERROR_FILE_WRITE		-13
#define ESP_ERROR_FILE_SEEK			-14
#define ESP_ERROR_FILE_SIZE			-15
#define ESP_ERROR_FILE_STAT			-16
#define ESP_ERROR_RESP_HDR			-17
#define ESP_ERROR_SLIP_START		-18
#define ESP_ERROR_SLIP_FRAME		-19
#define ESP_ERROR_SLIP_STATE		-20
#define ESP_ERROR_SLIP_DATA			-21
#define ESP_ERROR_SLIP_END			-22
#define ESP_ERROR_UNKNOWN_OUI		-23
#define ESP_ERROR_IMAGE_SIZE		-24
#define ESP_ERROR_DEVICE			-25
#define ESP_ERROR_FILENAME_LENGTH	-26

// structure for associating name-value pairs
typedef struct
{
	const char *name;			// the name
	uint32_t value;				// the associated value
} NameValue_t;

typedef struct
{
	unsigned dataLen;			// the length of the data
	const uint8_t *data;		// the data block
} DataBlock_t;

extern uint16_t diagCode;

typedef enum
{
	ResetNone = 0,				// manual reset configuration
	ResetAuto,					// DTR controls RST via a capacitor, RTS pulls down GPIO0
	ResetDTROnly,				// DTR controls RST via a capacitor and pulls down  GPIO0
	ResetCK,					// DTR pulls down GPIO0, RTS pulls down reset
	ResetWifio,					// DTR controls RST via a capacitor, TxD controls GPIO0 via a PNP 
	ResetNodeMCU,				// DTR and RTS control RST and GPIO0 via transistors
} ResetMode_t;

class ESP
{
public:
	ESP();
	~ESP();

	int Sync(uint16_t timeout);
	int Connect(ResetMode_t resetMode = ResetNone);
	int Run(bool reboot = false);
	void ResetDevice(ResetMode_t resetMode, bool forApp = false);
	int GetFlashID(uint32_t& flashID);
	int FlashErase();
	int FlashErase(uint32_t addr, uint32_t length);
	int FlashRead(VFile& vf, uint32_t addr, uint32_t length);
	int FlashWrite(VFile& vf, uint32_t addr, uint16_t flashParmVal, uint16_t flashParmMask);
	int ReadMAC(uint8_t *macp, int len);
	int ReadReg(uint32_t addr, uint32_t& valp);
	int WriteReg(uint32_t addr, uint32_t value, uint32_t mask = 0xffffffff, uint32_t delay = 0);
	int DumpMem(VFile& vf, uint32_t address, uint32_t size, FILE *fpProgress = stderr);
	int ImageInfo(VFile& vf, FILE *fpOut = stdout);
	int AddImage(VFile& vfOut, VFile& vfImage, uint32_t addr, bool padded);

	bool IsCommOpen() { return(m_serial.IsOpen()); }
	void OpenComm(const char *portStr, unsigned baud, unsigned flags = 0);
	void FlushComm() { m_serial.Flush(); }
	int CloseComm() { return(m_serial.Close()); }
	int SetCommSpeed(unsigned long speed) { return(m_serial.SetSpeed(speed)); }
	unsigned BytesAvailable() { return(m_serial.Available()); }
	void WriteByte(uint8_t b) { m_serial.WriteByte(b); }
	uint8_t ReadByte() { uint8_t b; return((m_serial.ReadByte(b) == 1) ? b : 0); }

	bool FlashMode(const char *desc, uint16_t& flashMode) const;
	bool FlashSize(const char *desc, uint16_t& flashSize) const;
	bool FlashFreq(const char *desc, uint16_t& flashFreq) const;

	unsigned GetFlags() const { return(m_flags); }
	void SetFlags(unsigned mask) { m_flags |= mask; }
	void ClearFlags(unsigned mask) { m_flags &= ~mask; }

	int OpenELF(const char *name) { return(m_elf.Open(name)); }
	int WriteSections(VFile& vf, const char *sectName, uint16_t flashParm = 0, bool forceESP = false);
	int AutoExtract(VFile& vfCombine, uint16_t flashParm = 0, bool padded = false, const char *filename = NULL, uint32_t addr = 0);
	bool HaveELF() const { return(m_elf.IsOpen()); }
	int SectionInfo(FILE *fp = stdout) { return(m_elf.SectionInfo(fp)); }

	void SetAddress(uint32_t addr) { m_address = addr; }
	uint32_t GetAddress() const { return(m_address); }
	void SetSize(uint32_t size) { m_size = size; }
	uint32_t GetSize() const { return(m_size); }

private:
	ESP(const ESP&);
	ESP& operator=(const ESP&);
	uint16_t checksum(const uint8_t *data, uint16_t dataLen, uint16_t cksum = ESP_CHECKSUM_MAGIC) const;

	int ramBegin(uint32_t addr, uint32_t size, uint32_t blkSize, uint32_t blkCnt = 1);
	int ramData(const uint8_t *data, unsigned dataLen, unsigned seq = 0);
	int ramFinish(uint32_t entryPoint = 0);
	int flashBegin(uint32_t addr, uint32_t size);
	int flashFinish(bool reboot = false);
	int flashWrite(VFile& vf, uint32_t ofst, uint32_t size, uint32_t addr, uint16_t flashParmVal, uint16_t flashParmMask);

	int writePacket(const uint8_t *data, unsigned len);
	int writePacket(const uint8_t *hdr, unsigned hdrLen, const DataBlock_t *blockList, unsigned dataBlockCnt);
	int writePacket(const uint8_t *hdr, unsigned hdrLen, const uint8_t *data, unsigned dataLen);
	int readPacket(uint8_t op, uint32_t *valp = NULL, uint8_t **bufpp = NULL, unsigned msTimeout = DEF_TIMEOUT);
	int readByte(uint8_t& data, bool slipDecode = false, unsigned msTimeout = 0);
	int sendCommand(uint8_t op, uint32_t checkVal, const DataBlock_t *blockList, unsigned dataBlockCnt);
	int sendCommand(uint8_t op, uint32_t checkVal, const uint8_t *data, unsigned dataLen);
	int doCommand(uint8_t op, const uint8_t *data, unsigned dataLen, uint32_t checkVal = 0, uint32_t *valp = NULL, unsigned msTimeout = DEF_TIMEOUT);
	int doCommand(uint8_t op, const DataBlock_t *blockList, unsigned dataBlockCnt, uint32_t checkVal = 0, uint32_t *valp = NULL, unsigned msTimeout = DEF_TIMEOUT);

	int stdImageInfo(VFile& vf, uint32_t ofst, uint32_t size, const char *prefix, FILE *fpOut = stdout);

	SerialChannel m_serial;
	ELF m_elf;
	bool m_connected;
	unsigned m_flags;
	uint32_t m_address;
	uint32_t m_size;
	uint32_t m_imageSize;
};

void usDelay(uint32_t us);
void msDelay(unsigned ms);
unsigned getTickCount(void);

#endif	// defined(ESP__H__)
