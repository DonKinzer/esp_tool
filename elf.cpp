// $Id: elf.cpp 67 2015-07-22 21:23:28Z Don $

/*
 * Based on ideas in esptool_elf.c and esptool_elf_object.c
 *    Copyright (C) 2014 Christian Klippel <ck@atelier-klippel.de>
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
 */

#include "elf.h"
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

ELF::
ELF()
{
	init();
}

ELF::
~ELF()
{
	deinit();
}

//
// Initialize the class members.
//
void ELF::
init()
{
	m_fp = NULL;
	m_fname = NULL;
	memset(&m_header, 0, sizeof(m_header));
	m_strings = NULL;
	m_sections = NULL;
}

//
// De-initialize the class members.
//
void ELF::
deinit()
{
	if (m_fp != NULL)
		fclose(m_fp);
	delete[] m_fname;
	delete[] m_strings;
	delete[] m_sections;
	init();
}

//
// Open an ELF file and collection information about its content.
//
int ELF::
Open(const char *file)
{
	int stat = -1;
	deinit();

	int len;
	if ((file == NULL) || ((len = strlen(file)) == 0))
		return(-1);
	if ((m_fp = fopen(file, "rb")) == NULL)
		return(-1);

	// validate the ELF header
   	if ((fread((char *)&m_header, 1, sizeof(m_header), m_fp) == sizeof(m_header)) &&
			(m_header.e_ident[EI_MAG0] == 0x7F) &&
			(m_header.e_ident[EI_MAG1] == 'E')  &&
			(m_header.e_ident[EI_MAG2] == 'L')  &&
			(m_header.e_ident[EI_MAG3] == 'F'))
	{
		// extract information from the ELF file
		getStrings();
		collectSections();

		// save the filename for later use
		m_fname = new char[len + 1];
		strcpy(m_fname, file);
		stat = 0;
	}
	if (stat != 0)
		deinit();
	return(0);
}

//
// Write a section given by its ordinal number (0-based) to a file.  If the 'sizeMult'
// parameter is non-zero, the file is padded with zeroes to be a multiple of that value.
//
int ELF::
WriteSection(int sectIdx, VFile& vf, uint8_t& cksum, uint32_t paddedSize) const
{
	if ((m_sections == NULL) || !vf.IsOpen())
		return(-1);

	uint32_t sectSize = m_sections[sectIdx].size;
	if (sectSize && (fseek(m_fp, m_sections[sectIdx].offset, SEEK_SET) == 0))
	{
		uint32_t len = sectSize;
		if (len < paddedSize)
			len = paddedSize;
		vf.NeedSpace(len);
		char b;
		for (len = 0; len < sectSize; len++)
		{
			if ((fread(&b, 1, 1, m_fp) != 1) || (vf.Write(&b, 1) != 1))
				return(-1);
			cksum ^= b;
		}

		// pad the file to the desired length
		b = 0;
		while (len < paddedSize)
		{
			if (vf.Write(&b, 1) != 1)
				return(-1);
			len++;
		}
		sectSize = len;
	}
	return(sectSize);
}

int ELF::
SectionInfo(FILE *fp)
{
	int stat = -1;
	if ((fp != NULL) && (m_fp != NULL))
	{
		fprintf(fp, "Address     Size        Name\n");
		for (unsigned cnt = 1; cnt < m_header.e_shnum; cnt++)
		{
			unsigned idx = cnt - 1;
			fprintf(fp, "0x%08x  0x%08x  %s\n", m_sections[idx].address, m_sections[idx].size, m_sections[idx].name);
		}
		stat = 0;
	}
	return(stat);
}

//
// Locate a named section in the ELF file.  If found, return the 1-based
// section number.
//
int ELF::
GetSectionNum(const char *sectName) const
{
	if ((sectName != NULL) && (*sectName != '\0'))
	{
		for (unsigned cnt = 1; cnt < m_header.e_shnum; cnt++)
		{
			if (strcmp(m_sections[cnt - 1].name, sectName) == 0)
				return(cnt);
		}
	}
	return(0);
}

//
// Given a 0-based section index, return the section size.
//
Elf32_Word ELF::
GetSectionSize(Elf32_Half secIdx) const
{
	if (secIdx < m_header.e_shnum)
		return(m_sections[secIdx].size);
	return(0);
}

//
// Given a 0-based section index, return the section load address.
//
Elf32_Word ELF::
GetSectionAddress(Elf32_Half secIdx) const
{
	if (secIdx < m_header.e_shnum)
		return(m_sections[secIdx].address);
	return(0);
}

//
// Get the string table from the ELF file.
//
void ELF::
getStrings()
{
	if (m_header.e_shstrndx)
	{
		if (fseek(m_fp, m_header.e_shoff + (m_header.e_shstrndx * m_header.e_shentsize), SEEK_SET) != 0)
			return;

	    Elf32_Shdr m_section;
		if (fread((char *)&m_section, 1, sizeof(m_section), m_fp) != sizeof(m_section) )
			return;

		if (m_section.sh_size)
		{
			m_strings = new char[m_section.sh_size];
			if (m_strings == NULL)
				return;

			if (fseek(m_fp, m_section.sh_offset, SEEK_SET) != 0)
				return;

			if (fread(m_strings, 1, m_section.sh_size, m_fp) != m_section.sh_size)
				return;
		}
	}
}

//
// Get section information from the ELF file.
//
void ELF::
collectSections()
{
	if (m_header.e_shnum)
	{
		// allocate space for the section information table
		m_sections = new ELF_section[m_header.e_shnum];

		// populate the table
		for (unsigned cnt = 1; cnt < m_header.e_shnum; cnt++)
		{
			if (fseek(m_fp, m_header.e_shoff + (cnt * m_header.e_shentsize), SEEK_SET) != 0)
				return;

		    Elf32_Shdr m_section;
			if (fread((char *)&m_section, 1, sizeof(m_section), m_fp) != sizeof(m_section))
				return;

			unsigned idx = cnt - 1;
			if (m_section.sh_name)
				m_sections[idx].name = m_strings + m_section.sh_name;
			else
				m_sections[idx].name = 0;

			m_sections[idx].offset = m_section.sh_offset;
			m_sections[idx].address = m_section.sh_addr;
			m_sections[idx].size = m_section.sh_size;
		}
	}
}

//-----------------------------------------------------------------------------

//
// Initialize the data members of a virtual file.
//
void VFile::
init()
{
	m_buf = NULL;
	m_bufSize = 0;
	m_size = 0;
	m_pos = 0;
	m_increment = 100;
	m_fp = NULL;
	m_fpClose = false;
	m_name = NULL;
}

//
// De-initialize the data members of a virtual file.
//
void VFile::
deinit()
{
	if (m_buf != NULL)
		free(m_buf);
	if ((m_fp != NULL) && m_fpClose)
		fclose(m_fp);
	delete[] m_name;
	init();
}

//
// Prepare a virtual file for operations.  If the mode is "v", the file is
// operated in virtual mode.  Otherwise, it is operated in physical mode.
// The return value is zero on success, non-zero otherwise.
//
int VFile::
Open(const char *name, const char *mode)
{
	Close();
	if ((name != NULL) && (*name != '\0'))
	{
		if (strcmp(mode, VFileOpenVirt) == 0)
		{
			// make the initial allocation for the virtual file
			m_buf = (char *)malloc(m_bufSize = m_increment);
			if (m_buf == NULL)
			{
				fprintf(stderr, "Can't allocate block of %u bytes\n", m_bufSize);
				exit(1);
			}
		}
		else
		{
			// physical file
			m_fpClose = true;
			m_fp = fopen(name, mode);
		}
		Name(name);
	}
	return(IsOpen() ? 0 : -1);
}

//
// Prepare a virtual file to use a previously opened stream.
// The return value is zero on success, non-zero otherwise.
//
int VFile::
Open(FILE *fp, const char *name)
{
	Close();
	if (fp == NULL)
		return(-1);
	m_fp = fp;
	Name(name);
	return(0);
}

//
// Set the name associated with the virtual file.
//
void VFile::
Name(const char *name)
{
	delete[] m_name;
	if ((name != NULL) && (*name != '\0'))
	{
		m_name = new char[strlen(name) + 1];
		strcpy(m_name, name);
	}
	else
		m_name = NULL;
}

//
// Read a number of elements of a given size from a virtual file.  Return
// the number of elements read or <0 on error.
//
size_t VFile::
Read(void *buf, size_t size, size_t count)
{
	size_t total = size * count;
	if (total <= 0)
		return(0);

	if (m_fp != NULL)
		return(fread(buf, size, count, m_fp));
	if (m_buf == NULL)
		return(-1);

	if ((m_pos + total) >= m_size)
	{
		// not enough data exist to satisfy the request
		total = m_size - m_pos;
		if ((count = (total / size) * size) <= 0)
			return(0);
	}

	// copy the data
	memcpy(buf, m_buf + m_pos, total);
	m_pos += total;
	return(count);
}

//
// Read a number of bytes from a virtual file and then reposition
// the file.  Return the number of bytes read or <0 on error.
//
size_t VFile::
Peek(void *buf, size_t count)
{
	if (count <= 0)
		return(0);

	if (m_fp != NULL)
	{
		long pos = Position();
		count = fread(buf, 1, count, m_fp);
		if (Position(pos) < 0)
			count = -1;
		return(count);
	}
	if (m_buf == NULL)
		return(-1);

	// limit the request to the data available
	if ((m_pos + count) > m_size)
		count = m_size - m_pos;

	// copy the data
	memcpy(buf, m_buf + m_pos, count);
	return(count);
}

//
// Write a number of elements of a given size to a virtual file.  Return
// the number of elements written or <0 on error.
//
size_t VFile::
Write(const void *buf, size_t size, size_t count)
{
	size_t total = size * count;
	if (total <= 0)
		return(0);

	if (m_fp != NULL)
		return(fwrite(buf, size, count, m_fp));
	else if (m_buf == NULL)
		return(-1);

	// copy the data, update the position and size
	NeedSpace(total);
	memcpy(m_buf + m_pos, buf, total);
	m_pos += total;
	if (m_size < m_pos)
		m_size = m_pos;

	return(count);
}

//
// Write a filler to a virtual file.  Return zero on success.
//
int VFile::
Fill(uint8_t c, uint32_t count)
{
	if (count > 0)
	{
		if (m_fp != NULL)
		{
			while (count--)
				if (fputc((int)c, m_fp) < 0)
					return(-1);
		}
		else
		{
			NeedSpace(count);
			memset(m_buf + m_pos, c, count);
			m_pos += count;
			if (m_size < m_pos)
				m_size = m_pos;
		}
	}
	return(0);
}

//
// Request additional space for future writes.
//
void VFile::
NeedSpace(uint32_t space)
{
	if ((m_fp != NULL) || (space == 0))
		return;
	if ((m_pos + space) <= m_bufSize)
		return;

	// use the minimum increment
	if (space < m_increment)
		space = m_increment;

	// not enough space in the buffer, enlarge it
	uint32_t newSize = m_pos + space;
	m_buf = (char *)realloc(m_buf, newSize);
	if (m_buf == NULL)
	{
		fprintf(stderr, "Can't reallocate memory from %u to %u bytes\n", m_bufSize, newSize);
		exit(1);
	}
	m_bufSize = newSize;
}

//
// Get the current position of a virtual file.  If successful, the
// return value will be zero or greater.
//
size_t VFile::
Position() const
{
	if (m_fp != NULL)
		return(ftell(m_fp));
	else if (m_buf != NULL)
		return(m_pos);
	return(-1);
}

//
// Reposition a virtual file.  If successful, the return value will be
// zero or greater.
//
int VFile::
Seek(long offset, int whence)
{
	if (m_fp != NULL)
		return(fseek(m_fp, offset, whence));
	if (m_buf != NULL)
	{
		size_t pos;

		// compute the new position
		if (whence == SEEK_CUR)
			pos = m_pos + offset;
		else if (whence == SEEK_END)
			pos = m_size + offset;
		else if (whence == SEEK_SET)
			pos = offset;
		else
			return(-1);

		// validate the new position
		if (pos < 0)
			return(-1);
		else if (pos > m_size)
			return(-1);

		// set the new position
		m_pos = pos;
		return(0);
	}
	return(-1);
}

//
// Get the current size of a virtual file.  If successful, the return value
// will be zero or greater.
//
size_t VFile::
Size() const
{
	if (m_fp != NULL)
	{
		struct stat fs;
		if (fstat(FILENO(m_fp), &fs) == 0)
			return(fs.st_size);
	}
	else if (m_buf != NULL)
		return(m_size);
	return(-1);
}

