// $Id: elf.h 67 2015-07-22 21:23:28Z Don $

/*
 * Based on ideas in esptool_elf.h, esptool_elf_enums.h and esptool_elf_object.h
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

#if !defined(ELF_H__)
#define ELF_H__

#include "sysdep.h"
#include <stdio.h>
#if defined(HAVE_STDINT_H)
  #include <stdint.h>
#endif

enum {
	EI_MAG0			= 0,
	EI_MAG1			= 1,
	EI_MAG2			= 2,
	EI_MAG3			= 3,
	EI_CLASS		= 4,
	EI_DATA			= 5,
	EI_VERSION		= 6,
	EI_PAD			= 7,
	EI_NIDENT		= 16
};

#define Elf32_Addr			uint32_t
#define Elf32_Half			uint16_t
#define Elf32_Off			uint32_t
#define Elf32_Sword			int32_t
#define Elf32_Word			uint32_t

#define SIZE_EI_NIDENT		16

typedef struct {
	uint8_t			e_ident[SIZE_EI_NIDENT];
	Elf32_Half		e_type;
	Elf32_Half		e_machine;
	Elf32_Word		e_version;
	Elf32_Addr		e_entry;
	Elf32_Off		e_phoff;
	Elf32_Off		e_shoff;
	Elf32_Word		e_flags;
	Elf32_Half		e_ehsize;
	Elf32_Half		e_phentsize;
	Elf32_Half		e_phnum;
	Elf32_Half		e_shentsize;
	Elf32_Half		e_shnum;
	Elf32_Half		e_shstrndx;
} Elf32_Ehdr;

typedef struct {
	Elf32_Word		sh_name;
	Elf32_Word		sh_type;
	Elf32_Word		sh_flags;
	Elf32_Addr		sh_addr;
	Elf32_Off		sh_offset;
	Elf32_Word		sh_size;
	Elf32_Word		sh_link;
	Elf32_Word		sh_info;
	Elf32_Word		sh_addralign;
	Elf32_Word		sh_entsize;
} Elf32_Shdr;

/*
** struct holding  the information of a given section according to
** what is found in the ELF file
*/
typedef struct {
	const char		*name;
	Elf32_Word		offset;
	Elf32_Addr		address;
	Elf32_Word		size;
} ELF_section;


#define VFileOpenVirt			"v"

class VFile
{
public:
	VFile(const char *name, const char *mode = VFileOpenVirt) { init(); Open(name, mode); }
	VFile(FILE *fp, const char *name = NULL) { init(); Open(fp, name); }
	VFile() { init(); }
	~VFile() { deinit(); }

	int Open(const char *name, const char *mode = VFileOpenVirt);
	int Open(FILE *fp, const char *name = NULL);

	size_t Peek(void *buf, size_t count);
	size_t Read(void *buf, size_t size, size_t count);
	size_t Write(const void *buf, size_t size, size_t count);
	size_t Read(void *buf, size_t count) { return(Read(buf, 1, count)); }
	size_t Write(const void *buf, size_t count) { return(Write(buf, 1, count)); }
	int GetChar() { char c; return((Read(&c, 1, 1) == 1) ? (int)(uint8_t)c : -1); }
	int PutChar(char c) { return((Write(&c, 1, 1) == 1) ? c : -1); }
	int Fill(uint8_t c, uint32_t count);

	size_t Size() const;
	size_t Position() const;
	int Position(long pos) { return(Seek(pos, SEEK_SET)); }
	int Seek(long offset, int whence);
	void Flush() { if (m_fp != NULL) fflush(m_fp); }

	int Close() { deinit(); return(0); }
	bool IsOpen() const { return((m_fp != NULL) || (m_buf != NULL)); }
	bool EndOfFile() const { return((m_fp != NULL) ? !!feof(m_fp) : (m_pos >= m_size)); }

	void NeedSpace(uint32_t space);
	void Increment(size_t incr) { m_increment = incr; }
	size_t Increment() const { return(m_increment); }
	void Name(const char *name);
	const char *Name() const { return(m_name ? m_name : ""); }

private:
	void init();
	void deinit();

	// for a virtual file
	char *m_buf;			// the buffer holding the file data
	uint32_t m_bufSize;		// the current size of m_buf
	uint32_t m_size;		// the current size of the file
	uint32_t m_pos;			// the current position of the file pointer
	uint32_t m_increment;	// the minimum increment for growing m_buf

	// for a real file
	FILE *m_fp;				// the stream
	bool m_fpClose;			// if the stream should be closed

	char *m_name;			// the associated name
};

class ELF
{
public:
	ELF();
	~ELF();

	bool IsOpen() const { return(m_fp != NULL); }
	int Open(const char *file);
	void Close() { deinit(); }

	const char *Filename() const { return(m_fname ? m_fname : ""); }
	int GetSectionNum(const char *sectName) const;
	Elf32_Word GetEntry(void) const { return(m_header.e_entry); }
	Elf32_Word GetSectionSize(Elf32_Half secIdx) const;
	Elf32_Word GetSectionAddress(Elf32_Half secIdx) const;

	int WriteSection(int sectIdx, VFile& vf, uint8_t& cksum, uint32_t paddedSize = 0) const;
	int WriteSection(int sectIdx, VFile& vf) const { uint8_t cksum = 0; return(WriteSection(sectIdx, vf, cksum)); }

	int SectionInfo(FILE *fp = stdout);

private:
	void init();
	void deinit();
	void getStrings();
	void collectSections();

	FILE *m_fp;					// file handle for the ELF file
	char *m_fname;				// name of the ELF file
	Elf32_Ehdr m_header;
	ELF_section *m_sections;
	char *m_strings;
};

#endif	// !defined(ELF_H__)
