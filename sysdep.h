// $Id: sysdep.h 66 2015-07-22 21:22:25Z Don $

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
#if	!defined(SYSDEP_H__)
#define SYSDEP_H__

#if defined(__APPLE__) && defined(__GNUC__) && !defined(__linux__)
  #define __linux__ 1
#endif

#if defined(WIN32)
  #define _CRT_SECURE_NO_WARNINGS
  #if (defined(_MSC_VER) && (_MSC_VER <= 1200))
	// stdint.h not available on VC6, provide what's needed
	typedef unsigned char uint8_t;
	typedef short int16_t;
	typedef unsigned short uint16_t;
	typedef unsigned long uint32_t;
	typedef long int32_t;
  #else
	#define HAVE_STDINT_H
  #endif
#elif defined(__linux__)
  #define HAVE_STDINT_H
  #define _stricmp strcasecmp
  #define NEED_MEMICMP
#endif

//#define NEED_STRICMP

#if defined(NEED_MEMICMP)
  int _memicmp(const void *_p1, const void *_p2, unsigned len);
#endif
#if defined(NEED_STRICMP)
  int _stricmp(const char *s1, const char *s2);
#endif

#if defined(WIN32)
  #define FILENO _fileno
#elif defined(__linux__)
  #define FILENO fileno
#endif

// uncomment this line to produce errors for missing implementation
#define ERROR_MISSING_IMPLEMENTATION

#endif	// defined(SYSDEP_H__)
