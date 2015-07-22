// $Id: esp.cpp 67 2015-07-22 21:23:28Z Don $

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

/** include files **/
#include "esp.h"

/** local definitions **/

typedef struct FileData_tag
{
	char name[MAX_FILENAME];
	uint32_t addr;
	VFile vfile;
	FileData_tag() { name[0] = '\0'; addr = 0; }
} FileData_t;

/** private data **/

//
// This data is actually code that is downloaded to RAM and executed in order
// to read out the contents of Flash.  Note that the first section comprises
// data elements used by the code and it has two parts.  The first twelve bytes
// are the parameters that control which part and how much of Flash is read
// while the remainder is constant data.
//
static uint8_t flashReadStub[] =
{
	// variable data modified on each use
									// data:
	LE_BYTES(0),					// 0 - start address
	LE_BYTES(0),					// 4 - block size
	LE_BYTES(0),					// 8 - block count

	// constant data
	LE_BYTES(SEND_PACKET_ADDR),		// 12  &send_packet
	LE_BYTES(SPI_READ_ADDR),		// 16  &SPIRead
	LE_BYTES(USER_DATA_RAM_ADDR),	// 20  RAM buffer address

	// code (offset 0x18 into flashReadStub)
	0xc1, 0xfc, 0xff,				//		l32r	a12, data + 8
	0xd1, 0xf9, 0xff,				//		l32r	a13, data + 0
									// 1:
	0x2d, 0x0d,						//		mov.n	a2, a13
	0x31, 0xfd, 0xff,				//		l32r	a3, data + 20
	0x41, 0xf8, 0xff,				//		l32r	a4, data + 4
	0x4a, 0xdd,						//		add.n	a13, a13, a4
	0x51, 0xfa, 0xff,				//		l32r	a5, data + 16
	0xc0, 0x05, 0x00,				//		callx0	a5
	0x21, 0xf9, 0xff,				//		l32r	a2, data + 20
	0x31, 0xf4, 0xff,				//		l32r	a3, data + 4
	0x41, 0xf6, 0xff,				//		l32r	a4, data + 12
	0xc0, 0x04, 0x00,				//		callx0	a4
	0x0b, 0xcc,						//		addi.n	a12, a12, -1
	0x56, 0xec, 0xfd,				//		bnez	a12, 1b
									// 2:
	0x06, 0xff, 0xff,				//		j		2b
	// filler
	0x00, 0x00, 0x00
};

// designators for Flash mode
static const NameValue_t flashModeList[] =
{
	{ "QIO",		0x0000 },	// default
	{ "QOUT",		0x0001 },
	{ "DIO",		0x0002 },
	{ "DOUT",		0x0003 },
	{ NULL,			0 }
};

// designators for Flash size (in KB or MB)
static const NameValue_t flashSizeList[] =
{
	{ "512K",		0x0000 },	// default
	{ "256K",		0x1000 },
	{ "1M",			0x2000 },
	{ "2M",			0x3000 },
	{ "4M",			0x4000 },
	{ "8M",			0x5000 },
	{ "16M",		0x6000 },
	{ "32M",		0x7000 },
	{ NULL,			0 }
};

// designators for Flash frequency (in megaHertz)
static const NameValue_t flashFreqList[] =
{
	{ "40M",		0x0000 },	// default
	{ "26M",		0x0100 },
	{ "20M",		0x0200 },
	{ "80M",		0x0f00 },
	{ NULL,			0 }
};

/** internal functions **/
static void putData(uint32_t val, unsigned byteCnt, uint8_t *buf, int ofst = 0);
static uint32_t getData(unsigned byteCnt, const uint8_t *buf, int ofst = 0);
static const NameValue_t *findNameValueEntry(const NameValue_t *tbl, const char *name, bool ignCase = true);
static const NameValue_t *findNameValueEntry(const NameValue_t *tbl, uint32_t val);

/** public functions **/

/** class implementations **/

ESP::
ESP()
{
	m_connected = false;
	m_flags = ESP_AUTO_RUN;
	m_address = ESP_NO_ADDRESS;
	m_size = 0;
	m_imageSize = 0;
}

ESP::
~ESP()
{
}

/*
 ** OpenComm
 *
 * Open a serial port if not already opened.  If the port can't be opened
 * an error message is output and the app is exited.
 *
 * For Windows systems, the serial channel descriptor should be in the
 * form "//./COMn" where n is a numeric value greater than zero.
 *
 */
void ESP::
OpenComm(const char *portStr, unsigned speed, unsigned flags)
{
	if (!m_serial.IsOpen())
	{
		if (m_serial.Open(portStr, speed, flags) != 0)
		{
			fprintf(stderr, "Can't open port %s.\n", portStr);
			exit(1);
		}
	}
}

//
// Send a synchronizing packet to the serial port in an attempt to induce
// the ESP8266 to auto-baud lock on the baud rate.
//
int ESP::
Sync(uint16_t timeout)
{
	int stat;
	uint8_t buf[36];

	// compose the data for the sync attempt
	memset(buf, 0x55, sizeof(buf));
	buf[0] = 0x07;
	buf[1] = 0x07;
	buf[2] = 0x12;
	buf[3] = 0x20;

	if ((stat = doCommand(ESP_SYNC, buf, sizeof(buf))) != 0)
	{
		// sync failed
		msDelay(100);
		FlushComm();
	}
	else
	{
		// read and discard additional replies
		while (readPacket(ESP_SYNC) == 2)
			;
	}
	return(stat);
}

//
// Attempt to establish a connection to the ESP8266.
//
int ESP::
Connect(ResetMode_t resetMode)
{
	if (m_connected)
		return(ESP_SUCCESS);

	uint16_t i, j;
	const char *sep = "";

	if ((m_flags & ESP_QUIET) == 0)
	{
		fprintf(stdout, "Connecting ");
		fflush(stdout);
	}
	for (i = 0; i < 4; i++)
	{
		resetDevice(resetMode);

		for (j = 0; j < 4; j++)
		{
			if (Sync(500) == ESP_SUCCESS)
			{
				fprintf(stdout, "%sconnection established\n", sep);
				fflush(stdout);
				m_connected = true;
				return(0);
			}
			fputc('.', stdout);
			fflush(stdout);
			sep = " ";
		}
	}
	if ((m_flags & ESP_QUIET) == 0)
		fprintf(stdout, "%sconnection attempt failed\n", sep);
	fflush(stdout);
	return(ESP_ERROR_CONNECT);
}

//
// Cause the device to run.
//
int ESP::
Run(bool reboot)
{
	int stat;

	if ((stat = flashBegin(0, 0)) == 0)
		stat = flashFinish(reboot);
	return(stat);
}

//
// Read the ID of the Flash chip on the device.
//
int ESP::
GetFlashID(uint32_t& flashID)
{
	int stat;

	if ((stat = flashBegin(0, 0)) != 0)
		return(stat);
	if ((stat = WriteReg(0x60000240, 0x00000000)) != 0)
		return(stat);
	if ((stat = WriteReg(0x60000200, 0x10000000)) != 0)
		return(stat);
	stat = ReadReg(0x60000240, flashID);
	return(stat);
}

//
// Send a command to the device to erase Flash memory.  The technique
// used is described in esptool.py.
//
int ESP::
FlashErase()
{
	int stat;

	if (((stat = flashBegin(0, 0)) == 0) &&
			((stat = ramBegin(IRAM_ADDR, 0, 0, 0)) == 0))
		stat = ramFinish(ERASE_CHIP_ADDR);
	return(stat);
}
//
// Send a command to the device to erase a block of Flash memory.
//
int ESP::
FlashErase(uint32_t addr, uint32_t size)
{
	int stat = -1;

	if (size)
	{
		const uint32_t blkSize = ESP_FLASH_BLK_SIZE;
		uint32_t blkCnt = (size + blkSize - 1) / blkSize;

		addr &= ~(blkSize - 1);
		if ((m_flags & ESP_QUIET) == 0)
		{
			fprintf(stdout, "Erasing %u bytes at 0x%06x ...\n", size, addr);
			fflush(stdout);
		}
		stat = flashBegin(addr, blkCnt * blkSize);
	}
	else
		stat = ESP_ERROR_PARAM;
	return(stat);
}


//
// Read data from Flash, write to a file.
//
int ESP::
FlashRead(VFile& vf, uint32_t address, uint32_t length)
{
	int stat = -1;

	if (!vf.IsOpen() || (length == 0))
		return(ESP_ERROR_PARAM);

	uint32_t stubLen = sizeof(flashReadStub) & 0xfffffffc;		// truncated to a multiple of 4 bytes

	// compute the block size to use
	uint32_t blkSize;
	uint32_t blkCnt;
	if (length <= ESP_FLASH_BLK_SIZE)
	{
		// read one block only of the exact size requested
		blkSize = length;
	 	blkCnt = 1;
	}
	else
	{
		// read multiple blocks
		blkSize = ESP_FLASH_BLK_SIZE;
	 	blkCnt = (length + blkSize - 1) / blkSize;
	}

	// set the parameters in the stub code
	putData(address, 4, flashReadStub, 0);
	putData(blkSize, 4, flashReadStub, 4);
	putData(blkCnt, 4, flashReadStub, 8);

	// download the stub
	if (((stat = flashBegin(0, 0)) == 0) &&
			((stat = ramBegin(IRAM_ADDR, stubLen, stubLen)) == 0) &&
			((stat = ramData(flashReadStub, stubLen)) == 0) &&
			((stat = ramFinish(FLASH_READ_STUB_BEGIN)) == 0))
	{
		// read back the data
		uint32_t dataLen = 0;
		for (unsigned i = 0; i < blkCnt; i++)
		{
			uint8_t data;

			// read the start marker
			if ((stat = readByte(data, false, DEF_TIMEOUT)) != 0)
				break;
			if (data != 0xc0)
			{
				stat = ESP_ERROR_SLIP_START;
				break;
			}

			// read a block of data
			for (unsigned j = 0; j < blkSize; j++)
			{
				// read a byte with SLIP decoding
				if ((stat = readByte(data, true, DEF_TIMEOUT)) != 0)
					return(stat);

				// store the data if the total doesn't exceed the requested length
				if (dataLen++ < length)
					vf.PutChar(data);
			}

			// read the end marker
			if ((stat = readByte(data, false, DEF_TIMEOUT)) != 0)
				break;
			if (data != 0xc0)
			{
				stat = ESP_ERROR_SLIP_END;
				break;
			}
		}
	}
	if ((stat == 0) && !(m_flags & ESP_QUIET))
		fprintf(stdout, "%u bytes written to \"%s\".\n", length, vf.Name());
	return(stat);
}

//
// Send the content of a file to the device.  Note that the file
// must be open in binary mode to avoid EOL translation.  This metho
// handles both combined image files and individual image files.
//
int ESP::
FlashWrite(VFile& vf, uint32_t addr, uint16_t flashParmVal, uint16_t flashParmMask)
{
	int stat = -1;
	if (!vf.IsOpen())
		return(ESP_ERROR_PARAM);

	// determine the length of the file and the number of blocks
	size_t fileSize = vf.Size();
	if (fileSize < 0)
	{
		fprintf(stderr, "Can't determine the size of the download file \"%s\".\n", vf.Name());
		return(ESP_ERROR_FILE_SIZE);
	}
	if (fileSize == 0)
	{
		fprintf(stderr, "The download file \"%s\" is zero length.\n", vf.Name());
		return(ESP_ERROR_FILE_SEEK);
	}

	// read in the header of the file, check for a combined image file
	uint8_t buf[4];
	if ((vf.Position(0) != 0) || (vf.Read(buf, 1, sizeof(buf)) != sizeof(buf)))
	{
		fprintf(stderr, "Can't read the download file \"%s\".\n", vf.Name());
		return(ESP_ERROR_FILE_READ);
	}
	if (memcmp(buf, COMPOSITE_SIG, 3) != 0)
		// not a combined image file - write the entire image
		stat = flashWrite(vf, 0, fileSize, addr, flashParmVal, flashParmMask);
	else
	{
		// download the individual images
		uint16_t imageCnt = buf[3];
		for (uint16_t i = 0; i < imageCnt; i++)
		{
			// read the segment descriptor
			uint8_t hdrBuf[8];
			if (vf.Read(hdrBuf, 1, sizeof(hdrBuf)) != sizeof(hdrBuf))
			{
				fprintf(stderr, "An error occurred while reading the image file \"%s\".\n", vf.Name());
				return(ESP_ERROR_FILE_READ);
			}
			uint32_t addr = getData(4, hdrBuf, 0);
			uint32_t len = getData(4, hdrBuf, 4);
			uint32_t pos = vf.Position();
			if ((stat = flashWrite(vf, pos, len, addr, flashParmVal, flashParmMask)) != 0)
				break;
			if (vf.Position(pos + len) < 0)
			{
				fprintf(stderr, "An error occurred while reading the image file \"%s\".\n", vf.Name());
				return(ESP_ERROR_FILE_SEEK);
			}
		}
	}
	return(stat);
}

//
// Send the content of a file (with the given size at the given offset in the file)
// to the device.
//
int ESP::
flashWrite(VFile& vf, uint32_t ofst, uint32_t size, uint32_t addr, uint16_t flashParmVal, uint16_t flashParmMask)
{
	int stat;
	const uint32_t blkSize = ESP_FLASH_BLK_SIZE;
	uint8_t *blkBuf = NULL;
	uint32_t blkCnt = (size + blkSize - 1) / blkSize;

	// move the file pointer to the start of the image
	if (vf.Position(ofst) < 0)
		return(ESP_ERROR_FILE_SEEK);

	// attempt to enter download mode
	if ((m_flags & ESP_QUIET) == 0)
	{
		fprintf(stdout, "Erasing %u bytes...\n", size);
		fflush(stdout);
	}
	bool needEOL = false;
	if ((stat = flashBegin(addr, blkCnt * blkSize)) == 0)
	{
		// allocate a data buffer for the combined header and block data
		const uint16_t hdrOfst = 0;
		const uint16_t dataOfst = 16;
		const uint16_t blkBufSize = dataOfst + blkSize;
		blkBuf = new uint8_t[blkBufSize];

		// send the blocks of the file
		for (uint32_t blkIdx = 0; blkIdx < blkCnt; blkIdx++)
		{
			// prepare the header for the block
			putData(blkSize, 4, blkBuf, hdrOfst + 0);
			putData(blkIdx, 4, blkBuf, hdrOfst + 4);
			putData(0, 4, blkBuf, hdrOfst + 8);
			putData(0, 4, blkBuf, hdrOfst + 12);

			// read the data for the block
			size_t cnt = vf.Read(blkBuf + dataOfst, 1, blkSize);
			if (cnt != ESP_FLASH_BLK_SIZE)
			{
				if (vf.EndOfFile())
					// partial last block, fill the remainder
					memset(blkBuf + dataOfst + cnt, 0xff, blkSize - cnt);
				else
				{
					// read error
					stat = ESP_ERROR_FILE_READ;
					goto done;
				}
			}

			// patch the flash parameters into the first block if it is loaded at address 0
			if ((blkIdx == 0) && (addr == 0) && (blkBuf[dataOfst] == ESP_IMAGE_MAGIC) && flashParmMask)
			{
				// update the Flash parameters
				uint32_t flashParm = getData(2, blkBuf + dataOfst + 2) & ~(uint32_t)flashParmMask;
				putData(flashParm | flashParmVal, 2, blkBuf + dataOfst + 2);
			}

			// calculate the block checksum
			uint16_t cksum = checksum(blkBuf + dataOfst, blkSize);

			if ((m_flags & ESP_QUIET) == 0)
			{
				fprintf(stdout, "\rWriting block %u of %u at 0x%06x", blkIdx + 1, blkCnt, addr + (blkIdx * blkSize));
				fflush(stdout);
				needEOL = true;
			}
			for (int i = 0; i < 3; i++)
			{
				if ((stat = doCommand(ESP_FLASH_DATA, blkBuf, blkBufSize, cksum)) == 0)
					break;
			}
			if (stat != 0)
				goto done;
		}
		if ((m_flags & ESP_QUIET) == 0)
		{
			fprintf(stdout, "\n%u bytes written successfully.\n", size);
			fflush(stdout);
			needEOL = false;
		}
	}

done:
	if (needEOL && !(m_flags & ESP_QUIET))
	{
		fputs("\n", stdout);
		fflush(stdout);
	}
	delete[] blkBuf;
	return(stat);
}

bool ESP::
FlashMode(const char *desc, uint16_t& flashMode) const
{
	if ((desc == NULL) || (*desc == '\0'))
		return(false);
	const NameValue_t *nvp;
	if ((nvp = findNameValueEntry(flashModeList, desc)) == NULL)
		return(false);
	flashMode = (uint16_t)nvp->value;
	return(true);
}

bool ESP::
FlashSize(const char *desc, uint16_t& flashSize) const
{
	if ((desc == NULL) || (*desc == '\0'))
		return(false);
	const NameValue_t *nvp;
	if ((nvp = findNameValueEntry(flashSizeList, desc)) == NULL)
		return(false);
	flashSize = (uint16_t)nvp->value;
	return(true);
}

bool ESP::
FlashFreq(const char *desc, uint16_t& flashFreq) const
{
	if ((desc == NULL) || (*desc == '\0'))
		return(false);
	const NameValue_t *nvp;
	if ((nvp = findNameValueEntry(flashFreqList, desc)) == NULL)
		return(false);
	flashFreq = (uint16_t)nvp->value;
	return(true);
}

//
// Write data from one or more sections from the currently open ELF file
// to the given virtual file.  If the 'sectName' parameter contains one
// or more commas, it is taken to be a list of sections names to be written
// to an ESP formatted load image file.  Otherwise, a raw binary file is
// written containing only the section content.
//
int ESP::
WriteSections(VFile& vf, const char *sectName, uint16_t flashParm)
{
	int stat = -1;
	uint8_t cksum = ESP_CHECKSUM_MAGIC;
	int sectNum;
	if (strchr(sectName, ',') != NULL)
	{
		char *p;

		// copy the list of section names to a local buffer, limiting the length
		unsigned len = strlen(sectName);
		char sectList[200];
		if (len >= sizeof(sectList))
			len = sizeof(sectList) - 1;
		memcpy(sectList, sectName, len);
		sectList[len] = '\0';

		// count the number of section names in the list
		int sectCnt = 1;
		sectName = sectList;
		for (p = sectList; (p = strchr(p, ',')) != NULL; sectCnt++)
			*p++ = '\0';

		// write the file header
		uint32_t imageSize = 0;
		uint8_t buf[8];
		buf[0] = ESP_IMAGE_MAGIC;
		buf[1] = (uint8_t)sectCnt;
		putData(flashParm, 2, buf, 2);
		putData(m_elf.GetEntry(), 4, buf, 4);
		if (vf.Write(buf, 1, sizeof(buf)) != sizeof(buf))
		{
			fprintf(stderr, "An error occurred writing the image header from \"%s\".\n", vf.Name());
			return(ESP_ERROR_FILE_WRITE);
		}
		imageSize += sizeof(buf);

		// output the information for each segment
		p = sectList;
		for (int i = 0; i < sectCnt; i++)
		{
			bool lastSeg = (i == (sectCnt - 1));

			// get the section number
			if ((sectNum = m_elf.GetSectionNum(p)) == 0)
			{
				fprintf(stderr, "Can't find section \"%s\" in the ELF file \"%s\".\n", p, m_elf.Filename());
				return(ESP_ERROR_PARAM);
			}
			int sectIdx = sectNum - 1;
			p += (strlen(p) + 1);

			// make the padded size a multiple of 4
			uint32_t segSize = m_elf.GetSectionSize(sectIdx);
			uint32_t paddedSize = (segSize + 3) & 0xfffffffc;

			// write the segment header
			putData(m_elf.GetSectionAddress(sectIdx), 4, buf, 0);
			putData(paddedSize, 4, buf, 4);
			if (vf.Write(buf, sizeof(buf)) != sizeof(buf))
			{
				fprintf(stderr, "An error occurred writing a section header to \"%s\".\n", vf.Name());
				return(ESP_ERROR_FILE_WRITE);
			}
			imageSize += sizeof(buf);

			// write the section data to the file
			if ((stat = m_elf.WriteSection(sectIdx, vf, cksum, paddedSize)) < 0)
			{
				fprintf(stderr, "An error occurred writing a section data to \"%s\".\n", vf.Name());
				return(stat);
			}
			imageSize += paddedSize;

			// write the final padding with the checksum
			if (lastSeg)
			{
				paddedSize = (imageSize + 16) & 0xfffffff0;
				uint32_t padSize = paddedSize - imageSize;
				uint8_t padBuf[16];
				memset(padBuf, 0, sizeof(padBuf));
				padBuf[padSize - 1] = cksum;
				if (vf.Write(padBuf, padSize) != padSize)
				{
					fprintf(stderr, "An error occurred writing the image padding to \"%s\".\n", vf.Name());
					return(ESP_ERROR_FILE_WRITE);
				}
			}
		}
		stat = 0;
	}
	else if ((sectNum = m_elf.GetSectionNum(sectName)) == 0)
	{
		fprintf(stderr, "Can't find section \"%s\" in the ELF file \"%s\".\n", sectName, m_elf.Filename());
		return(ESP_ERROR_PARAM);
	}
	else if ((stat = m_elf.WriteSection(sectNum - 1, vf, cksum)) < 0)
		fprintf(stderr, "An error occurred writing the image file \"%s\".\n", vf.Name());
	vf.Flush();
	return(stat);
}

//
// Extract information from an ELF file to create two binary files.  The
// first image created will contain the .text, .data and .rodata sections
// and will have the form of an ESP8266 boot image.  The second image
// created will be a raw binary image of the .irom0.text section.
//
// If the 'vfCombine' parameter is open, write the resulting image
// files to that file in either padded or sparse mode as indicated by
// the 'padded' parameter.
//
// If combining and the 'filename' parameter isn't null, the image contained
// in that file is added to the combined image at the indicated load address.
//
int ESP::
AutoExtract(VFile& vfCombine, uint16_t flashParm, bool padded, const char *imageFile, uint32_t imageAddr)
{
	int stat = -1;
	FileData_t imageData[3];
	int imageCnt = 0;

	if (!vfCombine.IsOpen() || ((imageFile != NULL) && (*imageFile == '\0')))
		imageFile = NULL;
	if ((imageFile != NULL) && ((strlen(imageFile) + 1) > sizeof(imageData[0].name)))
		return(ESP_ERROR_FILENAME_LENGTH);

	// create the boot image, sections .text, .data, and .rodata
	const char *fname = m_elf.Filename();
	const char *p;
	int baseLen;
	if (fname == NULL)
		return(ESP_ERROR_PARAM);

	if ((p = strrchr(fname, '.')) != NULL)
		baseLen = p - fname;
	else
		baseLen = strlen(fname);
	if ((baseLen + 13) > (int)sizeof(imageData[0].name))
		return(ESP_ERROR_FILENAME_LENGTH);

	// create the filename for the first image
	char *file = imageData[imageCnt].name;
	memcpy(file, fname, baseLen);
	sprintf(file + baseLen, "_0x%05x.bin", 0);
	const char *fmode = vfCombine.IsOpen() ? VFileOpenVirt : "wb";
	if (imageData[imageCnt].vfile.Open(file, fmode) != 0)
	{
		fprintf(stderr, "Can't create image file \"%s\".\n", file);
		return(-1);
	}
	stat = WriteSections(imageData[imageCnt].vfile, ".text,.data,.rodata", flashParm);
	if (stat != 0)
		return(stat);
	if ((m_flags & ESP_QUIET) == 0)
		fprintf(stdout, "Created image file \"%s\".\n", file);
	imageCnt++;

	// create the image for .irom0.text
	if (stat == 0)
	{
		int sectNum;
		const char *sectName = ".irom0.text";

		if ((sectNum = m_elf.GetSectionNum(sectName)) == 0)
		{
			fprintf(stderr, "Can't find section \"%s\" in the ELF file \"%s\".\n", sectName, m_elf.Filename());
			return(-1);
		}
		int sectIdx = sectNum - 1;

		// compute the offset into Flash for this section
		uint32_t sectAddr = m_elf.GetSectionAddress(sectIdx);
		if (sectAddr <= FLASH_ADDR)
		{
			fprintf(stderr, "Invalid start address for section %s - 0x%08x\n", sectName, sectAddr);
			return(-1);
		}
		sectAddr -= FLASH_ADDR;

		if ((imageFile != NULL) && (imageAddr < sectAddr))
		{
			strcpy(imageData[imageCnt].name, imageFile);
			imageData[imageCnt].addr = imageAddr;
			imageCnt++;
		}

		// open the file for this section
		file = imageData[imageCnt].name;
		memcpy(file, imageData[0].name, baseLen);
		sprintf(file + baseLen, "_0x%05x.bin", sectAddr);
		imageData[imageCnt].addr = sectAddr;
		if (imageData[imageCnt].vfile.Open(file, fmode) != 0)
		{
			fprintf(stderr, "Can't create image file \"%s\".\n", file);
			return(-1);
		}
		stat = m_elf.WriteSection(sectIdx, imageData[imageCnt].vfile);
		imageCnt++;
		if (stat < 0)
		{
			fprintf(stderr, "An error occurred writing the image file \"%s\".\n", file);
			return(-1);
		}
		if ((m_flags & ESP_QUIET) == 0)
			fprintf(stdout, "Created image file \"%s\".\n", file);

		if (vfCombine.IsOpen())
		{
			if ((imageCnt < 3) && (imageFile != NULL))
			{
				strcpy(imageData[imageCnt].name, imageFile);
				imageData[imageCnt].addr = imageAddr;
				imageCnt++;
			}

			for (int i = 0; i < imageCnt; i++)
			{
				file = imageData[i].name;
				VFile& vf = imageData[i].vfile;
				if (!vf.IsOpen() && (vf.Open(file, "rb") != 0))
				{
					fprintf(stderr, "Can't open the image file \"%s\".\n", file);
					stat = ESP_ERROR_FILE_OPEN;
				}
				else if (vf.Position(0) != 0)
				{
					fprintf(stderr, "Can't reposition the image file \"%s\".\n", file);
					stat = ESP_ERROR_FILE_SEEK;
				}
				else
					stat = AddImage(vfCombine, vf, imageData[i].addr, padded);
				if (stat)
					break;
			}
			if ((stat == 0) && ((m_flags & ESP_QUIET) == 0))
			{
				fprintf(stdout, "Combined \"%s\"", imageData[0].name);
				if (imageCnt == 2)
					fprintf(stdout, " and \"%s\"", imageData[1].name);
				else
					fprintf(stdout, ", \"%s\" and \"%s\"", imageData[1].name, imageData[2].name);
				fprintf(stdout, " %s.\n", padded ? "with padding" : "sparsely");
			}
		}
		else
			stat = 0;
	}
	return(stat);
}

//
// Copy the contents of the ESP8266 memory to a file.
//
int ESP::
DumpMem(VFile& vf, uint32_t address, uint32_t size, FILE *fpProgress)
{
	int stat = 0;
	unsigned dotCnt = 0;
	uint32_t ofst;
	address &= 0xfffffffc;
	for (ofst = 0; ofst < size; ofst += 4)
	{
		uint32_t val;

		// read from memory
		if ((stat = ReadReg(address + ofst, val)) != 0)
		{
			fprintf(stderr, "%sAn error occurred while reading memory at 0x%08x (%d).\n",
					dotCnt ? "\n" : "", address + ofst, stat);
			exit(1);
		}

		// write to the file
		if (vf.Write(&val, sizeof(val), 1) != 1)
		{
			fprintf(stderr, "%sAn error occurred while writing to \"%s\".\n", dotCnt ? "\n" : "", vf.Name());
			exit(1);
		}

		// output a progress indicator
		if (ofst && !(ofst & 0x00ff) && !(m_flags & ESP_QUIET) && (fpProgress != NULL))
		{
			if (++dotCnt >= 70)
			{
				dotCnt = 0;
				fputc('\n', fpProgress);
			}
			fputc('.', fpProgress);
			fflush(fpProgress);
		}
	}
	if (fpProgress != NULL)
	{
		if (dotCnt)
			fputc('\n', fpProgress);
		fflush(fpProgress);
	}
	if ((stat == 0) && !(m_flags & ESP_QUIET))
		fprintf(stdout, "%u bytes written to \"%s\".\n", ofst, vf.Name());
	return(stat);
}

//
// Attempt to read the station and, optionally, the AP MAC.  Return 0 if
// successful. The 'mac' parameter should point to a buffer with at least
// six bytes of space, if the AP MAC is also required at least 12 byte
// is needed.
//
int ESP::
ReadMAC(uint8_t *mac, int len)
{
	int stat;
	uint32_t mac0;
	uint32_t mac1;
	uint32_t mac2;
	uint32_t mac3;

	if ((mac == NULL) || (len < 6))
		return(ESP_ERROR_PARAM);
	bool apAlso = (len >= 12);

	if (((stat = ReadReg(ESP_OTP_MAC0, mac0)) == 0) &&
			((stat = ReadReg(ESP_OTP_MAC1, mac1)) == 0) &&
			((stat = ReadReg(ESP_OTP_MAC2, mac2)) == 0) &&
			((stat = ReadReg(ESP_OTP_MAC3, mac3)) == 0))
	{
		if ((mac2 & 0x00008000) == 0)
			return(ESP_ERROR_DEVICE);

		// determine the OUI
		uint8_t id = (uint8_t)(mac1 >> 16);
		if (id == 0)
		{
			memcpy(mac + 0, "\x18\xfe\x34", 3);
			if (apAlso)
				memcpy(mac + 6, "\x1a\xfe\x34", 3);
		}
		else if (id == 1)
		{
			memcpy(mac + 0, "\xac\xd0\x74", 3);
			if (apAlso)
				memcpy(mac + 6, "\xac\xd0\x74", 3);
		}
		else
		{
			mac[0] = id;
			return(ESP_ERROR_UNKNOWN_OUI);
		}

		// fill in the remainder of the MAC
		mac[3] = (uint8_t)(mac1 >> 8);
		mac[4] = (uint8_t)(mac1 >> 0);
		mac[5] = (uint8_t)(mac0 >> 24);
		if (apAlso)
			memcpy(mac + 9, mac + 3, 3);
	}
	return(stat);
}

//
// Send a command to the device to read a register.
//
int ESP::
ReadReg(uint32_t addr, uint32_t& val)
{
	uint8_t buf[4];

	putData(addr, 4, buf);
	return(doCommand(ESP_READ_REG, buf, sizeof(buf), 0, &val));
}

//
// Send a command to the device to write a register.
//
int ESP::
WriteReg(uint32_t addr, uint32_t value, uint32_t mask, uint32_t delay)
{
	uint8_t buf[16];

	addr &= 0xfffffffc;
	putData(addr, 4, buf, 0);
	putData(value, 4, buf, 4);
	putData(mask, 4, buf, 8);
	putData(delay, 4, buf, 12);
	return(doCommand(ESP_WRITE_REG, buf, sizeof(buf)));
}

//
// Add an ESP8266 image file to a combined file.  If the
// 'padded' parameter is true, a padded image is built otherwise a
// sparse image is built.
//
int ESP::
AddImage(VFile& vfOut, VFile& vfImage, uint32_t addr, bool padded)
{
	if (!vfOut.IsOpen() || !vfImage.IsOpen())
		return(ESP_ERROR_PARAM);

	size_t sizeOut = vfOut.Size();
	if (sizeOut < 0)
	{
		fprintf(stderr, "Can't determine the size of the combined file \"%s\".\n", vfOut.Name());
		return(ESP_ERROR_FILE_SIZE);
	}
	if (sizeOut == 0)
		m_imageSize = 0;

	size_t sizeIn = vfImage.Size();
	if (sizeIn < 0)
	{
		fprintf(stderr, "Can't determine the size of the image file \"%s\".\n", vfImage.Name());
		return(ESP_ERROR_FILE_SIZE);
	}
	if (sizeIn == 0)
	{
		fprintf(stderr, "The image file \"%s\" is zero length.\n", vfImage.Name());
		return(ESP_ERROR_IMAGE_SIZE);
	}

	// confirm that the address for the image to be added is not below the current size
	if (addr < m_imageSize)
	{
		fprintf(stderr, "The address specified for the image file \"%s\" is less than the current image size.\n", vfImage.Name());
		return(ESP_ERROR_FILE_READ);
	}

	uint32_t imageSize = m_imageSize;
	if (padded)
	{
		// creating a padded image, pad up to the address for the image to be added
		if (imageSize < addr)
		{
			vfOut.Fill(0xff, addr - imageSize);
			imageSize = addr;
		}
	}
	else
	{
		// creating a sparse combine image
		uint8_t buf[4];
		uint32_t pos;
		if (sizeOut == 0)
		{
			// the output file is currently empty, add the "combined image" header
			memcpy(buf, COMPOSITE_SIG, 3);
			buf[3] = 1;
			pos = sizeof(buf);
		}
		else if (sizeOut & 0x03)
		{
			// this shouldn't happen
			fprintf(stderr, "The combined file \"%s\" is not a multiple of 4 bytes in size.\n", vfOut.Name());
			return(ESP_ERROR_FILE_READ);
		}
		else
		{
			// update the "combined image" header for the additional image
			if ((vfOut.Position(0) < 0) || (vfOut.Peek(buf, sizeof(buf)) != sizeof(buf)))
			{
				fprintf(stderr, "An error occurred while reading the combined file \"%s\".\n", vfOut.Name());
				return(ESP_ERROR_FILE_READ);
			}
			if (memcmp(buf, COMPOSITE_SIG, 3) != 0)
			{
				fprintf(stderr, "The combined file \"%s\" does not have the correct header.\n", vfOut.Name());
				return(ESP_ERROR_PARAM);
			}
			buf[3]++;
			pos = sizeOut;
		}

		// update the header, prepare to append the new image
		if ((vfOut.Write(buf, sizeof(buf)) != sizeof(buf)) || (vfOut.Position(pos) < 0))
		{
combinedWriteErr:
			fprintf(stderr, "An error occurred while writing the combined file \"%s\".\n", vfOut.Name());
			return(ESP_ERROR_FILE_WRITE);
		}

		// append a header for the new image consisting of the load address and size
		uint8_t hdrBuf[8];
		putData(addr, 4, hdrBuf, 0);
		putData((sizeIn + 3) & 0xfffffffc, 4, hdrBuf, 4);
		if (vfOut.Write(hdrBuf, 1, sizeof(hdrBuf)) != sizeof(hdrBuf))
			goto combinedWriteErr;
	}

	// append the new image to the combined image
	uint32_t bytesAdded;
	vfOut.NeedSpace(sizeIn);
	for (bytesAdded = 0; bytesAdded < sizeIn; )
	{
		uint8_t tbuf[1024];
		uint32_t part = sizeIn - bytesAdded;
		if (part > sizeof(tbuf))
			part = sizeof(tbuf);
		if (vfImage.Read(tbuf, 1, part) != part)
		{
			fprintf(stderr, "An error occurred while reading the image file \"%s\".\n", vfImage.Name());
			return(ESP_ERROR_FILE_READ);
		}
		if (vfOut.Write(tbuf, 1, part) != part)
			goto combinedWriteErr;
		bytesAdded += part;
	}

	if (padded)
		// update the image size
		m_imageSize = imageSize + bytesAdded;
	else
	{
		// pad the image to a multiple of four bytes
		uint32_t cnt = bytesAdded & 0x03;
		if (cnt)
		{
			if (vfOut.Fill(0, 4 - cnt) != 0)
				goto combinedWriteErr;
			bytesAdded += cnt;
		}

		// update the image size
		m_imageSize = addr + bytesAdded;
	}
	vfOut.Flush();

	if ((m_flags & ESP_QUIET) == 0)
	{
		fprintf(stdout, "Added \"%s\" at 0x%08x, %u bytes.\n", vfImage.Name(), addr, bytesAdded);
		fflush(stdout);
	}
	return(0);
}

//
// Output information about an ESP8266 executable image file.  Both
// standard and "combined image" files are supported.
//
int ESP::
ImageInfo(VFile& vf, FILE *fpOut)
{
	int stat;

	if (!vf.IsOpen() || (fpOut == NULL))
		return(ESP_ERROR_PARAM);

	size_t fileSize = vf.Size();
	if (fileSize < 0)
	{
		fprintf(stderr, "Can't determine the size of the image file \"%s\".\n", vf.Name());
		return(ESP_ERROR_FILE_SIZE);
	}

	// read in the header of the file, check for the magic number
	uint8_t buf[4];
	if ((vf.Position(0) != 0) || (vf.Read(buf, 1, sizeof(buf)) != sizeof(buf)))
		return(ESP_ERROR_FILE_READ);
	if (buf[0] == ESP_IMAGE_MAGIC)
	{
		fprintf(fpOut, "%s:\n", vf.Name());
		stat = stdImageInfo(vf, 0, fileSize, "", fpOut);
		return(stat);
	}
	if (memcmp(buf, COMPOSITE_SIG, 3) != 0)
	{
		fprintf(stderr, "The file \"%s\" is neither a standard ESP image nor a combined image.\n", vf.Name());
		return(ESP_ERROR_GENERAL);
	}

	// output information about the images in a combined image file
	uint16_t imageCnt = buf[3];
	fprintf(fpOut, "%s:\n", vf.Name());
	fprintf(fpOut, "Combined image file containing %u images:\n", imageCnt);
	for (uint16_t i = 0; i < imageCnt; i++)
	{
		uint8_t hdrBuf[8];

		// read the segment descriptor
		if (vf.Read(hdrBuf, 1, sizeof(hdrBuf)) != sizeof(hdrBuf))
		{
			fprintf(stderr, "An error occurred reading the image file \"%s\".\n", vf.Name());
			return(ESP_ERROR_FILE_READ);
		}
		uint32_t addr = getData(4, hdrBuf, 0);
		uint32_t len = getData(4, hdrBuf, 4);
		uint32_t pos = vf.Position();

		// output the segment information
		fprintf(fpOut, "  Image %2u: Flash address 0x%06x, size 0x%06x\n", i, addr, len);

		if (vf.Read(buf, 1, sizeof(buf)) != sizeof(buf))
			return(ESP_ERROR_FILE_READ);
		if ((buf[0] == ESP_IMAGE_MAGIC) && ((stat = stdImageInfo(vf, pos, len, "    ", fpOut)) != 0))
			return(stat);
		if (vf.Position(pos + len) < 0)
		{
			fprintf(stderr, "An error occurred while reading the image file \"%s\".\n", vf.Name());
			return(ESP_ERROR_FILE_SEEK);
		}
	}
	return(0);
}

//
// Output information about a standard ESP8266 load image.
//
int ESP::
stdImageInfo(VFile& vf, uint32_t ofst, uint32_t size, const char *prefix, FILE *fpOut)
{
	if (!vf.IsOpen() || (fpOut == NULL) || (size == 0))
		return(ESP_ERROR_PARAM);

	// seek to the offset of the file
	if (vf.Position(ofst) < 0)
	{
		fprintf(stderr, "An error occurred while reading the image file \"%s\".\n", vf.Name());
		return(ESP_ERROR_FILE_SEEK);
	}

	// read in the header of the file, check for the magic number
	uint8_t buf[8];
	if (vf.Read(buf, 1, sizeof(buf)) != sizeof(buf))
		return(ESP_ERROR_FILE_READ);
	if (buf[0] != ESP_IMAGE_MAGIC)
	{
		fprintf(stderr, "The file \"%s\" is not a valid ESP image.\n", vf.Name());
		return(ESP_ERROR_GENERAL);
	}

	// get the Flash parameters
	uint16_t flashParm = (uint16_t)getData(2, buf, 2);
	fprintf(fpOut, "%sFlash parameters: ", prefix);
	const NameValue_t *nvp;
	char valStr[20];
	if ((nvp = findNameValueEntry(flashSizeList, flashParm & FLASH_SIZE_MASK)) != NULL)
		sprintf(valStr, "%sB", nvp->name);
	else
		strcpy(valStr, "<unknown>");
	fprintf(fpOut, "size=%s, ", valStr);
	if ((nvp = findNameValueEntry(flashModeList, flashParm & FLASH_MODE_MASK)) != NULL)
		sprintf(valStr, "%s", nvp->name);
	else
		strcpy(valStr, "<unknown>");
	fprintf(fpOut, "mode=%s, ", valStr);
	if ((nvp = findNameValueEntry(flashFreqList, flashParm & FLASH_FREQ_MASK)) != NULL)
		sprintf(valStr, "%sHz", nvp->name);
	else
		strcpy(valStr, "<unknown>");
	fprintf(fpOut, "freq=%s\n", valStr);

	// display segment information, while calculating the checksum
	uint8_t cksum = ESP_CHECKSUM_MAGIC;
	uint8_t b;
	uint16_t segCnt = buf[1];
	for (uint16_t i = 0; i < segCnt; i++)
	{
		// read the segment descriptor
		if (vf.Read(buf, 1, sizeof(buf)) != sizeof(buf))
		{
			fprintf(stderr, "An error occurred reading the image file \"%s\".\n", vf.Name());
			return(ESP_ERROR_FILE_READ);
		}
		uint32_t addr = getData(4, buf, 0);
		uint32_t len = getData(4, buf, 4);

		// output the segment information
		fprintf(fpOut, "%ssegment %2u: address 0x%08x, size 0x%06x\n", prefix, i, addr, len);

		// process the segment data
		while (len--)
		{
			if (vf.Read(&b, 1, 1) != 1)
			{
				fprintf(stderr, "An error occurred reading the image file \"%s\".\n", vf.Name());
				return(ESP_ERROR_FILE_READ);
			}
			cksum ^= b;
		}
	}

	// read the padding and the checksum byte
	uint32_t pos = vf.Position() - ofst;
	uint8_t lastByte;
	while ((pos++ & 0x0f) != 0)
	{
		if (vf.Read(&lastByte, 1, 1) != 1)
		{
			fprintf(stderr, "An error occurred reading the image file \"%s\".\n", vf.Name());
			return(ESP_ERROR_FILE_READ);
		}
		cksum ^= lastByte;
	}
	fprintf(fpOut, "%sThe checksum is %scorrect: 0x%02x\n", prefix, (cksum == 0) ? "" : "in", lastByte);

	if ((pos &= 0xfffffff0) < size)
	{
		fprintf(fpOut, "\n%sAdditional Flash data:\n", prefix);
		fprintf(fpOut, "%s              address 0x%06x, size 0x%06x\n", prefix, pos, size - pos);
	}
	return(0);
}

//
// Effect a device reset.
//
void ESP::
resetDevice(ResetMode_t resetMode)
{
	if (IsCommOpen())
	{
		switch (resetMode)
		{
		case ResetAuto:		// DTR controls RST via a capacitor, RTS pulls down GPIO0
			// ensure that DTR is high and RTS is low
			m_serial.Control(SERIAL_DTR_LOW | SERIAL_RTS_HIGH);

			// send a reset pulse
			m_serial.Control(SERIAL_DTR_HIGH);
			msDelay(5);
			m_serial.Control(SERIAL_DTR_LOW);

			// delay a bit and then release GPIO0
			msDelay(250);
			m_serial.Control(SERIAL_RTS_LOW);
			break;

		case ResetCK:		// DTR pulls down GPIO0, RTS pulls down reset
			m_serial.Control(SERIAL_DTR_HIGH | SERIAL_RTS_HIGH);	// set RST and GPIO0 to zero
			msDelay(5);
			m_serial.Control(SERIAL_RTS_LOW);						// release RST
			msDelay(75);
			m_serial.Control(SERIAL_DTR_LOW);						// release GPIO0
			break;

		case ResetWifio:	// TxD controls GPIO0 via a PNP, and DTR controls RST via a capacitor
			// ensure that DTR is high
			m_serial.Control(SERIAL_DTR_LOW);

			// send a reset pulse
			m_serial.Control(SERIAL_DTR_HIGH);
			msDelay(5);
			m_serial.Control(SERIAL_DTR_LOW);

			// send a break and wait for it to complete
			m_serial.Break(250);
			msDelay(250);
			break;

		default:			// manual reset, nothing to do
			break;
		}
	}
}

//
// Send a command to the device to begin the Flash process.
//
int ESP::
flashBegin(uint32_t addr, uint32_t size)
{
	int stat;

	// determine the number of blocks represented by the size
	uint32_t blkCnt;
	blkCnt = (size + ESP_FLASH_BLK_SIZE - 1) / ESP_FLASH_BLK_SIZE;

	// ensure that the address is on a block boundary
	addr &= ~(ESP_FLASH_BLK_SIZE - 1);

	// begin the Flash process
	uint8_t buf[16];
	putData(size, 4, buf, 0);
	putData(blkCnt, 4, buf, 4);
	putData(ESP_FLASH_BLK_SIZE, 4, buf, 8);
	putData(addr, 4, buf, 12);

	unsigned timeout = size ? 10000 : DEF_TIMEOUT;
	stat = doCommand(ESP_FLASH_BEGIN, buf, sizeof(buf), 0, NULL, timeout);
	return(stat);
}

//
// Send a command to the device to terminate the Flash process.
//
int ESP::
flashFinish(bool reboot)
{
	uint8_t buf[4];

	putData(reboot ? 0 : 1, 4, buf);
	return(doCommand(ESP_FLASH_END, buf, sizeof(buf)));
}

//
// Send a command to the device to begin writing to RAM.
//
int ESP::
ramBegin(uint32_t addr, uint32_t size, uint32_t blkSize, uint32_t blkCnt)
{
	uint8_t buf[16];

	// populate the header
	putData(size, 4, buf, 0);
	putData(blkCnt, 4, buf, 4);
	putData(blkSize, 4, buf, 8);
	putData(addr, 4, buf, 12);
	return(doCommand(ESP_MEM_BEGIN, buf, sizeof(buf)));
}

//
// Send a block of data to the device to be stored in RAM.
//
int ESP::
ramData(const uint8_t *data, unsigned dataLen, unsigned seq)
{
	// populate the data header
	uint8_t buf[16];
	putData(dataLen, 4, buf, 0);
	putData(seq, 4, buf, 4);
	putData(0, 4, buf, 8);
	putData(0, 4, buf, 12);

	// populate the block list
	DataBlock_t blockList[2];
	blockList[0].dataLen = sizeof(buf);
	blockList[0].data = buf;
	blockList[1].dataLen = dataLen;
	blockList[1].data = data;

	// calculate the block checksum
	unsigned cksum = checksum(data, dataLen);

	// execute the command
	return(doCommand(ESP_MEM_DATA, blockList, 2, cksum));
}

//
// Send a command to the device to finish writing to RAM.
//
int ESP::
ramFinish(uint32_t entryPoint)
{
	uint8_t buf[8];

	putData((entryPoint == 0), 4, buf, 0);
	putData(entryPoint, 4, buf, 4);
	return(doCommand(ESP_MEM_END, buf, sizeof(buf)));
}

//
// Compute the checksum of a block of data.
//
uint16_t ESP::
checksum(const uint8_t *data, uint16_t dataLen, uint16_t cksum) const
{
	if (data != NULL)
	{
		while (dataLen--)
			cksum ^= (uint16_t)*data++;
	}
	return(cksum);
}

//
// Send a block of data performing SLIP encoding of the content.
//
int ESP::
writePacket(const uint8_t *data, unsigned len)
{
	if (!data || !len)
		return(ESP_ERROR_PARAM);

	// send the data
	do
	{
		unsigned cnt = m_serial.WriteByte(*data++, true);
		if ((cnt != 1) && (cnt != 2))
			return(ESP_ERROR_COMM_WRITE);
	} while (--len);

	return(0);
}

//
// Send a packet to the serial port while performing SLIP framing.  The packet's
// data comprises a header and zero or more data blocks.
//
// A SLIP packet begins and ends with 0xc0.  The data encapsulated has the bytes
// 0xc0 and 0xdb replaced by the two-byte sequences {0xdb, 0xdc} and {0xdb, 0xdd},
// respectively.
//
int ESP::
writePacket(const uint8_t *hdr, unsigned hdrLen, const DataBlock_t *blockList, unsigned blockCnt)
{
	int stat;

	if ((hdr == NULL) || !hdrLen)
		return(ESP_ERROR_PARAM);

	// send the packet start character
	if (!m_serial.WriteByte(0xc0))
		return(ESP_ERROR_COMM_WRITE);

	// send the header
	if (hdr && hdrLen && ((stat = writePacket(hdr, hdrLen)) != 0))
		return(stat);

	// send the data blocks, if any
	if (blockList)
	{
		for (unsigned i = 0; i < blockCnt; i++)
		{
			// send a data block
			if ((stat = writePacket(blockList[i].data, blockList[i].dataLen)) != 0)
				return(stat);
		}
	}

	// send the packet end character
	stat = (m_serial.WriteByte(0xc0) ? 0 : ESP_ERROR_COMM_WRITE);

	return(stat);
}

int ESP::
writePacket(const uint8_t *hdr, unsigned hdrLen, const uint8_t *data, unsigned dataLen)
{
	DataBlock_t dataBlock;

	dataBlock.data = data;
	dataBlock.dataLen = dataLen;

	return(writePacket(hdr, hdrLen, &dataBlock, 1));
}

//
// Read a byte from the serial port with optional SLIP decoding and an optional timeout.
//
int ESP::
readByte(uint8_t& data, bool slipDecode, unsigned msTimeout)
{
	int stat = ESP_ERROR_GENERAL;
	unsigned tick = getTickCount() + msTimeout;
	unsigned needBytes = slipDecode ? 2 : 1;
	while (1)
	{
		if (BytesAvailable() >= needBytes)
		{
			stat = m_serial.ReadByte(data, slipDecode);
			if ((stat == 1) || (stat == 2))
				stat = ESP_SUCCESS;
			else if ((stat == 0) || (stat == -2))
				stat = ESP_ERROR_SLIP_DATA;
			else
				stat = ESP_ERROR_SLIP_FRAME;
			break;
		}

		// no data yet, check for timeout
		if (msTimeout && !(diagCode & DIAG_NO_TIME_LIMIT) && (getTickCount() > tick))
		{
			stat = ESP_ERROR_TIMEOUT;
			break;
		}
	}
	return(stat);
}

//
// Wait for a data packet to be returned.  If the body of the packet is
// non-zero length, return an allocated buffer indirectly containing the
// data and return the data length.  Note that if the pointer for returning
// the data buffer is NULL, the response is expected to be two bytes of
// zero.
//
// If an error occurs, return a negative value.  Otherwise, return the number
// of bytes in the response (or zero if the response was not the standard
// "two bytes of zero").
//
// Note that if the value of 'msTimeout' is zero, the routine will never
// time out.
//
int ESP::
readPacket(uint8_t op, uint32_t *valp, uint8_t **bufpp, unsigned msTimeout)
{
	// these values are the states for the state machine
	#define PKT_BEGIN		0
	#define PKT_HEADER		1
	#define PKT_BODY		2
	#define PKT_END			3
	#define PKT_DONE		4

	#define HDR_LEN			8

	unsigned tickEnd = getTickCount() + msTimeout;
	uint8_t hdr[HDR_LEN];
	uint16_t hdrIdx = 0;
	uint16_t bodyLen = 0;
	uint16_t bodyIdx = 0;
	uint8_t *dbuf = NULL;
	uint8_t respBuf[2];
	uint8_t *body = NULL;

	if (bufpp != NULL)
		*bufpp = NULL;

	// wait for the response
	uint16_t needBytes = 1;
	unsigned state = PKT_BEGIN;
	while (state < PKT_DONE)
	{
		uint8_t c;
		int stat;

		if (msTimeout && !(diagCode & DIAG_NO_TIME_LIMIT) && (getTickCount() > tickEnd))
			return(ESP_ERROR_TIMEOUT);

		if (BytesAvailable() < needBytes)
		{
			// insufficient data available
#if defined(WIN32)
			Yield();
#endif
			continue;
		}

		// sufficient bytes have been received for the curent state, process them
		switch(state)
		{
		case PKT_BEGIN:		// expecting frame start
		case PKT_END:		// expecting frame end
			c = m_serial.ReadByte();
			if (c != 0xc0)
			{
				delete[] dbuf;
				return(ESP_ERROR_SLIP_FRAME);
			}
			if (state == PKT_BEGIN)
			{
				state = PKT_HEADER;
				needBytes = 2;
			}
			else
				state = PKT_DONE;
			break;

		case PKT_HEADER:	// reading an 8-byte header
		case PKT_BODY:		// reading the response body
			// retrieve a byte with SLIP decoding
			stat = m_serial.ReadByte(c, true);
			if ((stat != 1) && (stat != 2))
			{
				// some error occurred
				if ((stat == 0) || (stat == -2))
					stat = ESP_ERROR_SLIP_DATA;
				else
					stat = ESP_ERROR_SLIP_FRAME;
				delete[] dbuf;
				return(stat);
			}
			else if (state == PKT_HEADER)
			{
				//store the header byte
				hdr[hdrIdx++] = c;
				if (hdrIdx >= HDR_LEN)
				{
					uint8_t resp;
					uint8_t opRet;

					// the header has been read, extract elements from it
					resp = (uint8_t)getData(1, hdr, 0);
					opRet = (uint8_t)getData(1, hdr, 1);
					if ((resp != 0x01) || (op && (opRet != op)))
						return(ESP_ERROR_RESP_HDR);

					// get the body length, prepare a buffer for it
					bodyLen = (uint16_t)getData(2, hdr, 2);
					if ((bufpp == NULL) && (bodyLen <= sizeof(respBuf)))
						// use a local buffer
						body = respBuf;
					else
						// allocate space for the data
						body = dbuf = new uint8_t[needBytes];

					// extract the value, if requested
					if (valp != NULL)
						*valp = getData(4, hdr, 4);

					if (bodyLen)
						state = PKT_BODY;
					else
					{
						needBytes = 1;
						state = PKT_END;
					}
				}
			}
			else
			{
				//store the response body byte, check for completion
				body[bodyIdx++] = c;
				if (bodyIdx >= bodyLen)
				{
					needBytes = 1;
					state = PKT_END;
				}
			}
			break;

		default:		// this shouldn't happen
			delete[] dbuf;
			return(ESP_ERROR_SLIP_STATE);
			break;
		}
	}

	if (bufpp != NULL)
		*bufpp = dbuf;
	else
	{
		// return of the data buffer isn't requested, just check size and content
		if ((body == NULL) || (bodyLen != 2) || body[0] || body[1])
			bodyLen = 0;
		delete[] dbuf;
	}
	return(bodyLen);

	#undef HDR_LEN
	#undef PKT_BEGIN
	#undef PKT_HEADER
	#undef PKT_BODY
	#undef PKT_END
	#undef PKT_DONE
}

//
// Send a command to the attached device together with the supplied data, if any.
// The data is supplied via a list of one or more seqments.
//
int ESP::
sendCommand(uint8_t op, uint32_t checkVal, const DataBlock_t *blockList, unsigned dataBlockCnt)
{
	int stat = 0;

	if (op)
	{
		// compute the total data length
		unsigned dataLen = 0;
		if (blockList)
		{
			for (unsigned i = 0; i < dataBlockCnt; i++)
			{
				if (blockList[i].data)
					dataLen += blockList[i].dataLen;
			}
		}

		// populate the header
		uint8_t hdr[8];
		putData(0, 1, hdr, 0);
		putData(op, 1, hdr, 1);
		putData(dataLen, 2, hdr, 2);
		putData(checkVal, 4, hdr, 4);

		// send the packet
		FlushComm();
		stat = writePacket(hdr, sizeof(hdr), blockList, dataBlockCnt);
	}
	return(stat);
}

int ESP::
sendCommand(uint8_t op, uint32_t checkVal, const uint8_t *data, unsigned dataLen)
{
	DataBlock_t dataBlock;

	dataBlock.data = data;
	dataBlock.dataLen = dataLen;

	return(sendCommand(op, checkVal, &dataBlock, 1));
}

//
// Send a command to the attached device together with the supplied data, if any, and
// get the response.
//
int ESP::
doCommand(uint8_t op, const DataBlock_t *blockList, unsigned blockCnt, uint32_t checkVal, uint32_t *valp, unsigned msTimeout)
{
	int stat;

	if ((stat = sendCommand(op, checkVal, blockList, blockCnt)) == 0)
	{
		// command sent successfully, read the reply
		stat = readPacket(op, valp, NULL, msTimeout);
		if (stat == 2)
			stat = ESP_SUCCESS;
		else
			stat = ESP_ERROR_REPLY;
	}
	return(stat);
}

int ESP::
doCommand(uint8_t op, const uint8_t *data, unsigned dataLen, uint32_t checkVal, uint32_t *valp, unsigned msTimeout)
{
	int stat;
	DataBlock_t dataBlock;

	dataBlock.data = data;
	dataBlock.dataLen = dataLen;
	stat = doCommand(op, &dataBlock, 1, checkVal, valp, msTimeout);
	return(stat);
}

/** private functions **/

//
// Extract 1-4 bytes of a value in little endian order from a buffer
// beginning at a specified offset.
//
static uint32_t
getData(unsigned byteCnt, const uint8_t *buf, int ofst)
{
	uint32_t val = 0;

	if (buf && byteCnt)
	{
		int shiftCnt = 0;
		if (byteCnt > 4)
			byteCnt = 4;
		do
		{
			val |= (uint32_t)buf[ofst++] << shiftCnt;
			shiftCnt += 8;
		} while (--byteCnt);
	}
	return(val);
}

//
// Put 1-4 bytes of a value in little endian order into a buffer
// beginning at a specified offset.
//
static void
putData(uint32_t val, unsigned byteCnt, uint8_t *buf, int ofst)
{
	if (buf && byteCnt)
	{
		if (byteCnt > 4)
			byteCnt = 4;
		do
		{
			buf[ofst++] = (uint8_t)(val & 0xff);
			val >>= 8;
		} while (--byteCnt);
	}
}

/*
 ** findNameValueEntry
 *
 * Attempt to locate a name in a name-value list.  The list is terminated
 * with a null name entry.  If found, a pointer to the entry is returned,
 * otherwise, NULL.
 *
 */
static const NameValue_t *
findNameValueEntry(const NameValue_t *tbl, const char *name, bool ignCase)
{
	if ((tbl != NULL) && (name != NULL) && (*name != '\0'))
	{
		for ( ; tbl->name != NULL; tbl++)
		{
			if (ignCase)
			{
				if (_stricmp(tbl->name, name) == 0)
					return(tbl);
			}
			else
			{
				if (strcmp(tbl->name, name) == 0)
					return(tbl);
			}
		}
	}
	return(NULL);
}

/*
 ** findNameValueEntry
 *
 * Attempt to locate a value in a name-value list.  The list is terminated
 * with a null name entry.  If found, a pointer to the entry is returned,
 * otherwise, NULL.
 *
 */
static const NameValue_t *
findNameValueEntry(const NameValue_t *tbl, uint32_t val)
{
	if (tbl != NULL)
	{
		for ( ; tbl->name != NULL; tbl++)
		{
			if (tbl->value == val)
				return(tbl);
		}
	}
	return(NULL);
}

