// $Id: esp_tool.cpp 78 2015-09-08 17:54:33Z Don $

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

/*
 *
 * This code implements a downloader/utility program that works with the
 * ESP8266. The various options allow sending an executable image to the device
 * and querying the device for different types of data.  The code was inspired
 * by Fredrik Ahlberg's esptool.py (available on github) and it was created
 * to eliminate the need for the Python interpreter and the need for the Visual
 * C runtime that is needed to convert a Python script to an executable using
 * py2exe.  Some inspiration was also drawn from Christian Kippel's esptool-ck
 * (also available on github) which is implemented in C.
 *
 * The code was originally written to be compiled with the VC6 compiler and
 * to run on a Windows box but it has been compiled successfully using Visual
 * Studio V12 as well.  It has also been compiled and run on Debian running in
 * a VirtualBox.  The code has some conditionals for OS X but it has not been
 * compiled in that environment.
 *
 * This utility is capable of creating and downloading a "sparse combined image"
 * file. Such a file is a composite containing one or more ESP8266 image files
 * but without padding between the component images.  Using a sparse combined
 * image file is preferable to using a padded combined image file because the
 * download time can be substantially smaller.
 *
 * A sparse combined image file begins with the three bytes 'e', 's', 'p'
 * followed by a byte giving the number of images contained therein.  Each
 * original image is represented by an 8-byte header giving the load address
 * and length of the image (each four bytes in little-endian order) followed
 * by the image itself, padded with zero bytes to be an integral multiple of
 * four bytes.  The length given in the header preceding the image indicates
 * the zero-padded length.
 *
 */

/** include files **/
#include "esp.h"
#if defined(__linux__)
  #include <time.h>
  #include <sys/ioctl.h>
#endif

/** local definitions **/

#define DEF_DL_SPEED				115200
#define DEF_RUN_SPEED				0

#if defined(WIN32)
#define DEF_COMM_CHANNEL			"COM1"
#elif defined(__linux__)
#define DEF_COMM_CHANNEL			"/dev/ttyS0"
#endif

#define DEF_MON_ESCAPE				0x04

// operating modes
typedef enum
{
	ModeWriteFlash,				// write files to Flash
	ModeReadFlash,				// read Flash, write to file
	ModeDumpMem,				// dump ESP8266 memory region to a file
	ModeImageCombine,			// combine images sparsely
	ModeImageAppend,			// append images to sparse combined file
	ModeImageInfo,				// output information about an image
	ModeElfSection,				// extract sections from an ELF file
	ModeNone
} Mode_t;

// option processing modes
typedef enum
{
	OptionNone,
	OptionSetPort,
	OptionSetSpeed,
	OptionSetAddress,
	OptionSetSize,
	OptionSetElf,
	OptionProcessFile,
	OptionFlashMode,
	OptionFlashSize,
	OptionFlashParm,
	OptionFlashFreq,
	OptionReadFlash,
	OptionWriteFlash,
	OptionEraseFlash,
	OptionDumpMem,
	OptionElfSections,
	OptionAutoExtract,
	OptionReadMAC,
	OptionGetFlashID,
	OptionImageInfo,
	OptionSparseImage,
	OptionAppendSparse,
	OptionPaddedImage,
	OptionAppendPadded,
	OptionMonitor,
	OptionMonitorExit,
	OptionLog,
	OptionSections,
	OptionSetQuiet,
	OptionSetRun,
	OptionSetNoRun,
	OptionResetMode,
	OptionHelp,
	OptionSetDiagCode,
	OptionInvalid,
	OptionInvalidValue,
	OptionBadForm,
	OptionModeExit
} Option_t;

// structure to hold parameter values
typedef struct Parameter_tag
{
	const char *portStr;		// the serial port designator (e.g. COM2 or /dev/ttyS1)
	uint32_t dlSpeed;			// the desired baud rate for downloading
	uint32_t runSpeed;			// the desired baud rate for the monitor (0 implies dlSpeed)
	uint8_t monExit;			// the monitor exit character code
	Mode_t mode;				// the operating mode
	ResetMode_t resetMode;		// the reset mode
	uint32_t address;			// the target address
	uint32_t size;				// the region size
	uint16_t flashParmVal;		// combined Flash parameters (as it appears in an image header)
	uint16_t flashParmMask;		// mask bits indicating which parameters have been specified
	uint16_t dlCount;			// the number of images downloaded
	VFile vfCombine;			// file descriptor for a combined image file
	const char *sectName;		// one or more ELF section names, comma separated
	bool padded;				// indicates padded (or sparse) mode
	bool termMode;				// if monitor mode should be entered
	const char *logFile;		// name of a file to which to log device output
	bool longOpt;

	Parameter_tag()
	{
		// set the default values
		portStr = DEF_COMM_CHANNEL;
		dlSpeed = DEF_DL_SPEED;
		runSpeed = DEF_RUN_SPEED;
		monExit = DEF_MON_ESCAPE;
		mode = ModeWriteFlash;
		resetMode = ResetNone;
		address = ESP_NO_ADDRESS;
		size = 0;
		flashParmVal = 0;
		flashParmMask = 0;
		dlCount = 0;
		sectName = NULL;
		padded = false;
		termMode = false;
		logFile = NULL;
		longOpt = false;
	}
} Parameter_t;

typedef struct
{
	const char *opt;				// the option
	Option_t value;					// the associated option value
} OptWord_t;


/** public data **/
uint16_t diagCode = 0;

/** private data **/
static const unsigned verMajor = 0;
static const unsigned verMinor = 1;
static const unsigned verVariant = 0;

#if defined(WIN32)
// storage for the serial port string, e.g "//./COM2"
static char commPortStr[20];
#endif

//
// Long form options (preceeded by '--'), and their corresponding values.
// N.B.: this table is scanned sequentially for an entry being a substring
//       of an option.  Consequently, an entry that is a prefix of another
//       entry must follow the longer entry.
//
static OptWord_t optWords[] =
{
	{ "address=",		OptionSetAddress },
	{ "baud=",			OptionSetSpeed },
	{ "diagCode=",		OptionSetDiagCode },
	{ "dump-mem",		OptionDumpMem },
	{ "elf-file=",		OptionSetElf },
	{ "elf-info",		OptionElfSections },
	{ "erase-flash",	OptionEraseFlash },
	{ "erase",			OptionEraseFlash },
	{ "exit=",			OptionMonitorExit },
	{ "extract",		OptionAutoExtract },
	{ "file=",			OptionProcessFile },
	{ "flash-freq=",	OptionFlashFreq },
	{ "flash-id",		OptionGetFlashID },
	{ "flash-mode=",	OptionFlashMode },
	{ "flash-parm=",	OptionFlashParm },
	{ "flash-size=",	OptionFlashSize },
	{ "help",			OptionHelp },
	{ "image-info",		OptionImageInfo },
	{ "no-run",			OptionSetNoRun },
	{ "padded=",		OptionPaddedImage },
	{ "padded+=",		OptionAppendPadded },
	{ "port=",			OptionSetPort },
	{ "quiet",			OptionSetQuiet },
	{ "read-mac",		OptionReadMAC },
	{ "read-flash",		OptionReadFlash },
	{ "read",			OptionReadFlash },
	{ "reset=",			OptionResetMode },
	{ "run",			OptionSetRun },
	{ "section=",		OptionSections },
	{ "sections=",		OptionSections },
	{ "size=",			OptionSetSize },
	{ "sparse=",		OptionSparseImage },
	{ "sparse+=",		OptionAppendSparse },
	{ "write-flash",	OptionWriteFlash },
	{ "write",			OptionWriteFlash },
	{ NULL,				OptionInvalid }
};

/** internal functions **/
static void displayHelp(bool doExit = true);
static char *skipWhite(char *p);
static void processArgString(ESP& esp, Parameter_t& parm, char *argList, bool observeQuotes = true);
static void processArg(ESP& esp, Parameter_t& parms, const char *argp);
static void processFile(ESP& esp, Parameter_t& parm, const char *file);
static int openComm(ESP& esp, Parameter_t& parms, bool forDownload = true);
static int getOptionVal(const char *& p, uint32_t& valp, bool suffixK = true);
static int getVal(const char *& p, uint32_t& valp, int radix = 10);
static bool extractAddress(const char *& file, uint32_t& addr);

/** public functions **/

int
main(int argc, char **argv)
{
	ESP esp;
	Parameter_t parms;
	char *envStr = NULL;

	// process args contained in an environment variable
	if ((envStr = getenv("ESP_TOOL")) != NULL)
	{
		// make a local copy of the string
		char *p = new char[strlen(envStr) + 1];
		strcpy(p, envStr);
		processArgString(esp, parms, envStr = p);
	}

	// detect invocation with no arguments
	if (argc == 1)
	{
		displayHelp();
		exit(0);
	}

	// process command line arguments
	argv++;
	while (--argc > 0)
		processArg(esp, parms, *argv++);

	parms.vfCombine.Close();
	if (esp.GetFlags() & ESP_AUTO_RUN)
	{
		if (parms.resetMode == ResetNone)
			esp.Run(true);
		else
			esp.ResetDevice(parms.resetMode, true);
	}

	if (parms.termMode)
	{
		FILE *fpLog = NULL;

		if ((parms.logFile != NULL) && ((fpLog = fopen(parms.logFile, "w")) == NULL))
			fprintf(stderr, "Can't create monitor log file \"%s\".\n", parms.logFile);

		// make sure the comm port is open
		openComm(esp, parms, false);

		// echo incoming characters to the console
		fflush(stdin);
		while (1)
		{
			uint8_t c;

			if (esp.BytesAvailable() == 0)
			{
				// check for a console key being available
#if defined(WIN32)
				if (_kbhit())
				{
					if ((c = _getch()) == '\r')
						c = '\n';
					if (c == parms.monExit)
						break;
					esp.WriteByte(c);
				}
				Yield();
#elif defined(__linux__)
				int byteCnt;
				if ((ioctl(0, FIONREAD, &byteCnt) >= 0) && byteCnt)
				{
					read(0, &c, 1);
					if (c == parms.monExit)
						break;
					esp.WriteByte(c);
				}
#endif
			}
			else
			{
				if ((c = esp.ReadByte()) != '\r')
				{
					fputc(c, stdout);
					fflush(stdout);

					if (fpLog != NULL)
					{
						fputc(c, fpLog);
						fflush(fpLog);
					}
				}
			}
		}
		if (fpLog != NULL)
			fclose(fpLog);
	}
	delete[] envStr;
	exit(0);
	return(0);
}

/*
 ** usDelay
 *
 * Delay for a specified number of microseconds.
 *
 */
void
usDelay(uint32_t us)
{
#if defined(WIN32)
	__int64 end;

	if (QueryPerformanceCounter((LARGE_INTEGER *)&end))
	{
		__int64 freq, tick;

		// compute the number of counts to equal the requested delay
		QueryPerformanceFrequency((LARGE_INTEGER *)&freq);
		end += ((__int64)us * freq / 1000000);

		// implement the delay
		do
		{
			QueryPerformanceCounter((LARGE_INTEGER *)&tick);
		}
		while (tick <= end);
	}
#elif defined(__linux__)
	// perhaps nanosleep() should be used
    usleep(us);
#elif defined(ERROR_MISSING_IMPLEMENTATION)
	#error missing implementation of isDelay()
#endif
}

/*
 ** msDelay
 *
 * Delay for a specified number of milliseconds.
 *
 */
void
msDelay(unsigned ms)
{
#if defined(WIN32)
	if (ms > 65)
	{
		for (unsigned tick = GetTickCount() + ms; GetTickCount() < tick; )
			;
	}
	else
		// for smaller values, to get better resolution
		usDelay(ms * 1000);
#elif defined(__linux__)
    usleep(ms * 1000);
#elif defined(ERROR_MISSING_IMPLEMENTATION)
	#error missing implementation of msDelay()
#endif
}

/*
 ** getTickCount
 *
 * Retrieve a tick count from the OS representing elapsed milliseconds.
 *
 */
unsigned
getTickCount(void)
{
	unsigned tick = 0;

#if defined(WIN32)
	tick = GetTickCount();
#elif defined(__linux__)
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	tick = (ts.tv_sec * 1000) + ((ts.tv_nsec + 500000) / 1000000);
#elif defined(ERROR_MISSING_IMPLEMENTATION)
	#error missing implementation of getTickCount()
#endif
	return(tick);
}

#if defined(NEED_MEMICMP)
//
// Case-insensitive memory block comparison.
//
int
_memicmp(const void *_p1, const void *_p2, unsigned len)
{
	const uint8_t *p1 = (const uint8_t *)_p1;
	const uint8_t *p2 = (const uint8_t *)_p2;

	while (len--)
	{
		unsigned char c1, c2;

		// fetch the next byte, change to upper case if alphabetic
		c1 = *p1++;
		if (isalpha(c1) && islower(c1))
			c1 = toupper(c1);
		c2 = *p2++;
		if (isalpha(c2) && islower(c2))
			c2 = toupper(c2);
		if (c1 != c2)
			return((int)c1 - (int)c2);
	}
	return(0);
}
#endif

#if defined(NEED_STRICMP)
//
// Case-insensitive string comparison.
//
int
_stricmp(const char *s1, const char *s2)
{
	int len1, len2;

	// guard against null pointers, compute string lengths (including the trailing null)
	if (s1 == NULL)
		s1 = "";
	len1 = strlen(s1) + 1;
	if (s2 == NULL)
		s2 = "";
	len2 = strlen(s2) + 1;

	// do the comparison
	return(_memicmp(s1, s2, (len1 < len2) ? len1 : len2));
}
#endif

/** class implementations **/

/** private functions **/

/*
 ** displayHelp
 *
 * Display invocation help on stdout.
 *
 */
static void
displayHelp(bool doExit)
{
	char verStr[20];
	int len;

	// compose the version number
	len = sprintf(verStr, "%u.%u", verMajor, verMinor);
	if (verVariant)
		sprintf(verStr + len, ".%u", verVariant);

	fprintf(stdout, "Invocation:            (V%s)\n", verStr);
	fprintf(stdout, "esp_tool [[<options>] [<operation>] [<file>]]...\n");
	fprintf(stdout, " where <options> are:\n");
	fprintf(stdout, " -h          --help                 display this information\n");
	fprintf(stdout, " -p<port>    --port=<port>          specify the COM port, e.g. COM1 or 1\n");
	fprintf(stdout, " -b<speed>   --baud=<speed>         specify the baud rate\n");
	fprintf(stdout, " -a<addr>    --address=<addr>       specify the address for a later operation\n");
	fprintf(stdout, " -s<size>    --size=<size>          specify the size for a later operation\n");
	fprintf(stdout, " -e<elf>     --elf-file=<elf>       specify an ELF file to process\n");
	fprintf(stdout, " -fs<size>   --flash-size=<size>    Flash size (256K, 512K, 1M, 2M, 4M, 8M)\n");
	fprintf(stdout, " -ff<freq>   --flash-freq=<freq>    Flash frequency (20M, 26M, 40M, 80M)\n");
	fprintf(stdout, " -fm<mode>   --flash-mode=<mode>    Flash mode (QIO, DIO, QOUT, DOUT)\n");
	fprintf(stdout, " -fp<val>    --flash-parm=<val>     combined Flash parameters\n");
	fprintf(stdout, " -l<file>    --log=<file>           log device output in monitor mode\n");
	fprintf(stdout, " -m[<speed>] --monitor[=<speed>]    after operations, enter monitor mode\n");
	fprintf(stdout, " -r<reset>   --reset=<reset>        set the reset mode (none, auto, ck, wifio)\n");
	fprintf(stdout, " -r0         --no-run               do not run device after operations\n");
	fprintf(stdout, " -r1         --run                  run device after operations (default)\n");
	fprintf(stdout, " -q          --quiet                suppress progress reporting\n");
	fprintf(stdout, " -x<code>    --exit=<code>          set the character code for monitor exit\n");
	fprintf(stdout, "\n");
	fprintf(stdout, " where <operation> is one of:\n");
	fprintf(stdout, " -cp<file>   --padded=<file>        combine images into a padded image file\n");
	fprintf(stdout, " -cp+<file>  --padded+=<file>       append images to an existing padded file\n");
	fprintf(stdout, " -cs<file>   --sparse=<file>        combine images into a sparse image file\n");
	fprintf(stdout, " -cs+<file>  --sparse+=<file>       append images to an existing sparse file\n");
	fprintf(stdout, " -od         --dump-mem             write the content of memory to a file\n");
	fprintf(stdout, " -oe[<size>] --erase-flash[=<size>] erase all or part of Flash memory\n");
	fprintf(stdout, " -of         --flash-id             report Flash identification information\n");
	fprintf(stdout, " -oi         --image-info           output information about an image\n");
	fprintf(stdout, " -om         --read-mac             report the station MAC address\n");
	fprintf(stdout, " -or         --read-flash           read Flash memory, write to a file\n");
	fprintf(stdout, " -os         --elf-info             output section information from ELF file\n");
	fprintf(stdout, " -os<sect>   --section=<sect>       extract data from sections of ELF file\n");
	fprintf(stdout, " -ow         --write-flash          write files to Flash memory (default)\n");
	fprintf(stdout, " -ox[<file>] --extract[=<file>]     extract ELF file sections to create images\n");

	if (doExit)
		exit(0);
}

//
// Advance a pointer to the first non-whitespace character.
//
static char *
skipWhite(char *p)
{
	char c;

	if (p == NULL)
		return(p);
	while ((c = *p) != '\0')
	{
		if ((c != ' ') && (c != '\t'))
			break;
		p++;
	}
	return(p);
}

//
// Process whitespace-separated arguments in a string.  Note that the
// string is modified, adding NULL characters to delineate the args.  If
// the 'observeQuotes' parameter is true whitespace is allowed within
// quoted sequences.
//
static void
processArgString(ESP& esp, Parameter_t& parm, char *argList, bool observeQuotes)
{
	if (argList == NULL)
		return;

	char *p;
	for (; *argList != '\0'; argList = p)
	{
		// skip over leading whitespace, if any
		argList = skipWhite(argList);

		// find the next whitespace break, optionally observing quotes
		char c;
		if (observeQuotes && (((c = *argList) == '"') || (c == '\'')))
		{
			// find the matching quote
			if ((p = strchr(++argList, c)) != NULL)
				*p++ = '\0';
			else
				p = argList + strlen(argList);
		}
		else if ((p = strpbrk(argList, " \t")) != NULL)
			*p++ = '\0';
		else
			p = argList + strlen(argList);

		// process the arg
		processArg(esp, parm, argList);
	}
}

//
// Process an argument string.
//
static void
processArg(ESP& esp, Parameter_t& parm, const char *argp)
{
	char c;
	int stat;
	const char *p;
	uint32_t val;
	uint16_t flashVal;
	uint16_t flashMask = 0;
	Option_t option = OptionInvalid;

	parm.longOpt = false;
	if (((p = argp) == NULL) || ((c = *p++) == '\0'))
		return;
	if (c  == '-')
	{
		switch(*p++)
		{
		case '#':			option = OptionSetDiagCode;		break;

		case '?':
		case 'H':
		case 'h':			option = OptionHelp;			break;

		case 'a':			option = OptionSetAddress;		break;

		case 'b':			option = OptionSetSpeed;		break;

		case 'c':			// specify a combined image file
			switch (*p++)
			{
			case 'p':		option = OptionPaddedImage;		break;
			case 's':		option = OptionSparseImage;		break;
			}
			if ((option != OptionInvalid) && (*p == '+'))
			{
				p++;
				if (option == OptionPaddedImage)
					option = OptionAppendPadded;
				else
					option = OptionAppendSparse;
			}
			break;

		case 'e':			option = OptionSetElf;			break;

		case 'f':			// specify a Flash parameter
			switch (*p++)
			{
			case 'f':		option = OptionFlashFreq; 		break;
			case 'm':		option = OptionFlashMode; 		break;
			case 'p':		option = OptionFlashParm;		break;
			case 's':		option = OptionFlashSize; 		break;
			}
			break;

		case 'l':			option = OptionLog;				break;

		case 'm':			option = OptionMonitor;			break;

		case 'o':			// specify an operation
			switch (*p++)
			{
			case 'b':		option = OptionEraseFlash;		break;
			case 'd':		option = OptionDumpMem;			break;
			case 'e':		option = OptionEraseFlash;		break;
			case 'f':		option = OptionGetFlashID;		break;
			case 'i':		option = OptionImageInfo;		break;
			case 'm':		option = OptionReadMAC;			break;
			case 'r':		option = OptionReadFlash;		break;
			case 's':		option = (*p == '\0') ?
								OptionElfSections :
								OptionSections;				break;
			case 'w':		option = OptionWriteFlash;		break;
			case 'x':		option = OptionAutoExtract;		break;
			}
			break;

		case 'p':			option = OptionSetPort;			break;

		case 'q':			option = OptionSetQuiet;		break;

		case 'r':
			switch (*p++)
			{
			case '0':		option = OptionSetNoRun;		break;
			case '1':		option = OptionSetRun;			break;
			default:		option = OptionResetMode; p--;	break;
			}
			break;

		case 's':			option = OptionSetSize;			break;

		case 'x':			option = OptionMonitorExit;		break;

		case '-':
			{
				// possible long-form option, look up the option in a table
				for (OptWord_t *owp = optWords; owp->opt != NULL; owp++)
				{
					int optLen = strlen(owp->opt);
					if (strncmp(p, owp->opt, optLen) == 0)
					{
						option = owp->value;
						p += optLen;
						parm.longOpt = true;
						break;
					}
				}
			}
			break;

		default:
			break;
		}
	}
#if defined(WIN32)
	// support additional help options on Windows
	else if ((c == '/') && (((c = argp[1]) == 'H') || (c == 'h') || (c == '?')) && (argp[2] == '\0'))
		option = OptionHelp;
#endif
	else
	{
		// assume it is a file to be processed
		option = OptionProcessFile;
		p = argp;
	}

	// take action based on the option seen
	switch (option)
	{
	case OptionHelp:
		displayHelp();
		break;

	//--------------------------------------------------------------------------
	// Options that save data for later use.
	//--------------------------------------------------------------------------
	case OptionSetDiagCode:
		if (getOptionVal(p, val) != 0)
			option = OptionInvalidValue;
		else
			diagCode = (uint16_t)val;
		break;

	case OptionSetQuiet:
		if (*p == '\0')
			esp.SetFlags(ESP_QUIET);
		else
			option = OptionBadForm;
		break;

	case OptionSetRun:
		if (*p == '\0')
			esp.SetFlags(ESP_AUTO_RUN);
		else
			option = OptionBadForm;
		break;

	case OptionSetNoRun:
		if (*p == '\0')
			esp.ClearFlags(ESP_AUTO_RUN);
		else
			option = OptionBadForm;
		break;

	case OptionSetPort:
#if defined(WIN32)
		{
			uint16_t ofst = 0;		// serial port number adjustment

			// accept comm port specification as a numeric value, COMn, or /dev/ttySn
			if ((_memicmp(p, "COM", 3) == 0) && isdigit(p[3]))
				p += 3;
			else if ((_memicmp(p, "/dev/ttyS", 9) == 0) && isdigit(p[9]))
				p += 9, ofst = 1;
			if (isdigit(*p))
			{
				if (getVal(p, val) != 0)
				{
					option = OptionInvalidValue;
					break;
				}
				if (((val += ofst) == 0) || (val > 99))
				{
					fprintf(stderr, "Invalid serial channel: \"%s\".\n", argp);
					exit(1);
				}

				// produce a port descriptor string using the port number
				sprintf(commPortStr, "//./COM%u", val);
				parm.portStr = commPortStr;
				break;
			}
		}
#else
		// accept any non-empty string, validated when attempting open
		if (*p != '\0')
		{
			parm.portStr = p;
			break;
		}
#endif
		option = OptionBadForm;
		break;

	case OptionSetSpeed:
		if (isdigit(*p) && (getOptionVal(p, val) == 0))
		{
			if (*p == '\0')
			{
				parm.dlSpeed = val;
				break;
			}
		}
		option = OptionInvalidValue;
		break;

	case OptionResetMode:
		if (*p == '\0')
		{
			fprintf(stderr, "Missing reset mode designator: \"%s\".\n", argp);
			exit(1);
		}
		if (_stricmp(p, "none") == 0)
			parm.resetMode = ResetNone;
		else if (_stricmp(p, "auto") == 0)
			parm.resetMode = ResetAuto;
		else if (_stricmp(p, "dtronly") == 0)
			parm.resetMode = ResetDTROnly;
		else if (_stricmp(p, "ck") == 0)
			parm.resetMode = ResetCK;
		else if (_stricmp(p, "wifio") == 0)
			parm.resetMode = ResetWifio;
		else if (_stricmp(p, "nodemcu") == 0)
			parm.resetMode = ResetNodeMCU;
		else
		{
			fprintf(stderr, "Unrecognized reset mode designator: \"%s\".\n", argp);
			exit(1);
		}
		break;

	case OptionFlashMode:
		flashMask = FLASH_MODE_MASK;
		if (esp.FlashMode(p, flashVal))
			break;
		fprintf(stderr, "Invalid flash mode designator: \"%s\".\n", argp);
		exit(1);
		break;

	case OptionFlashSize:
		flashMask = FLASH_SIZE_MASK;
		if (esp.FlashSize(p, flashVal))
			break;
		fprintf(stderr, "Invalid flash size designator: \"%s\".\n", argp);
		exit(1);
		break;

	case OptionFlashFreq:
		flashMask = FLASH_FREQ_MASK;
		if (esp.FlashFreq(p, flashVal))
			break;
		fprintf(stderr, "Invalid flash frequency designator: \"%s\".\n", argp);
		exit(1);
		break;

	case OptionFlashParm:
		if (getOptionVal(p, val) != 0)
		{
			option = OptionInvalidValue;
			break;
		}
		flashVal = (uint16_t)val;
		flashMask = FLASH_FREQ_MASK | FLASH_SIZE_MASK | FLASH_MODE_MASK;
		break;

	case OptionSetAddress:
		if (getOptionVal(p, val) == 0)
		{
			parm.address = val;
			break;
		}
		option = OptionInvalidValue;
		break;

	case OptionSetSize:
		if (getOptionVal(p, val) != 0)
		{
			option = OptionInvalidValue;
			break;
		}
		if (val == 0)
		{
			fprintf(stderr, "The size must be non-zero - \"%s\".\n", argp);
			exit(1);
		}
		parm.size = val;
		break;

	case OptionSections:
		if (*p == '\0')
		{
			fprintf(stderr, "Missing section name - \"%s\".\n", argp);
			exit(1);
		}
		parm.mode = ModeElfSection;
		parm.sectName = p;
		break;

	case OptionMonitor:
		if (*p == '=')
		{
			if (!parm.longOpt)
			{
				option = OptionBadForm;
				break;
			}
			if (*++p == '\0')
			{
				fprintf(stderr, "Missing run speed - \"%s\".\n", argp);
				exit(1);
			}
		}
		if (*p != '\0')
		{
			if (getOptionVal(p, val) != 0)
			{
				option = OptionInvalidValue;
				break;
			}
			if (val == 0)
			{
				fprintf(stderr, "The run speed must be non-zero - \"%s\".\n", argp);
				exit(1);
			}
			parm.runSpeed = val;
		}
		parm.termMode = true;
		break;

	case OptionMonitorExit:
		if (*p != '\0')
		{
			if (getOptionVal(p, val) != 0)
			{
				option = OptionInvalidValue;
				break;
			}
			if (val > 0xff)
			{
				fprintf(stderr, "The monitor exit code must be a byte value - \"%s\".\n", argp);
				exit(1);
			}
			parm.monExit = (uint8_t)val;
		}
		else
			option = OptionBadForm;
		break;

	case OptionLog:
		if (*p != '\0')
			parm.logFile = p;
		else
			option = OptionBadForm;
		break;

	//--------------------------------------------------------------------------
	// Options that set a mode for later operations.
	//--------------------------------------------------------------------------
	case OptionReadFlash:
		if (*p == '\0')
			parm.mode = ModeReadFlash;
		else
			option = OptionBadForm;
		break;

	case OptionWriteFlash:
		if (*p == '\0')
			parm.mode = ModeWriteFlash;
		else
			option = OptionBadForm;
		break;

	case OptionDumpMem:
		if (*p == '\0')
			parm.mode = ModeDumpMem;
		else
			option = OptionBadForm;
		break;

	case OptionImageInfo:
		if (*p == '\0')
			parm.mode = ModeImageInfo;
		else
			option = OptionBadForm;
		break;

	//--------------------------------------------------------------------------
	// Options that prepare files for later operations.
	//--------------------------------------------------------------------------
	case OptionSparseImage:
	case OptionAppendSparse:
	case OptionPaddedImage:
	case OptionAppendPadded:
		if (*p == '\0')
		{
			fprintf(stderr, "Missing filename for the combined image - \"%s\".\n", argp);
			exit(1);
		}
		else
		{
			bool append = ((option == OptionAppendSparse) || (option == OptionAppendPadded));
			parm.padded = ((option == OptionAppendPadded) || (option == OptionPaddedImage));
			parm.mode = append ? ModeImageAppend : ModeImageCombine;

			const char *modeStr = (append ? "r+b" : "w+b");
			if (parm.vfCombine.Open(p, modeStr) != 0)
			{
				fprintf(stderr, "Can't open file \"%s\" for %s.\n", p, append ? "appending" : "writing");
				exit(1);
			}
		}
		break;

	case OptionSetElf:
		if (esp.OpenELF(p) != 0)
		{
			fprintf(stderr, "An error occurred attempting to open the ELF file \"%s\".\n", p);
			exit(1);
		}
		break;

	//--------------------------------------------------------------------------
	// Options that are executed immediately.
	//--------------------------------------------------------------------------
	case OptionReadMAC:
		if (*p == '\0')
		{
			if (openComm(esp, parm) != 0)
				break;

			uint8_t mac[12];
			if ((stat = esp.ReadMAC(mac, sizeof(mac))) == 0)
			{
				int i;
				fprintf(stdout, "Station MAC is");
				for (i = 0; i < 6; i++)
					fprintf(stdout, "%c%2x", (i ? ':' : ' '), mac[i + 0]);
				fputs("\n", stdout);
				fprintf(stdout, "     AP MAC is");
				for (i = 0; i < 6; i++)
					fprintf(stdout, "%c%2x", (i ? ':' : ' '), mac[i + 6]);
				fputs("\n", stdout);
			}
			else if (stat == ESP_ERROR_UNKNOWN_OUI)
				fprintf(stderr, "Unable to determine the OUI (code 0x%02x).\n", mac[0]);
			else
				fprintf(stderr, "An error occurred attempting to read the MAC address (%d).\n", stat);
			if (stat != 0)
				exit(1);
		}
		else
			option = OptionBadForm;
		break;

	case OptionGetFlashID:
		if (*p == '\0')
		{
			if (openComm(esp, parm) != 0)
				break;
			uint32_t flashID;
			if ((stat = esp.GetFlashID(flashID)) == 0)
				fprintf(stdout, "Manufacturer: %02x, Device: %02x%02x.\n", flashID & 0xff,
						(flashID >> 8) & 0xff, (flashID >> 16) & 0xff);
			else
			{
				fprintf(stderr, "Failed to get Flash ID (%d).\n", stat);
				exit(1);
			}
		}
		else
			option = OptionBadForm;
		break;

	case OptionEraseFlash:
		{
			uint32_t eraseSize = 0;

			if (*p == '=')
			{
				if (!parm.longOpt)
				{
					option = OptionBadForm;
					break;
				}
				if (*++p == '\0')
				{
					fprintf(stderr, "Missing erase size - \"%s\".\n", argp);
					exit(1);
				}
			}
			if (*p != '\0')
			{
				p++;
				if (getOptionVal(p, val) != 0)
				{
					option = OptionInvalidValue;
					break;
				}
				if (val == 0)
				{
					fprintf(stderr, "The size to erase must be non-zero - \"%s\".\n", argp);
					exit(1);
				}
				eraseSize = val;
			}

			if (option == OptionEraseFlash)
			{
				if (openComm(esp, parm) != 0)
					break;
				if (eraseSize)
					stat = esp.FlashErase(parm.address, eraseSize);
				else
					stat = esp.FlashErase();
				if (stat != 0)
				{
					fprintf(stderr, "Flash erase failed (%d).\n", stat);
					exit(1);
				}
			}
		}
		break;

	case OptionElfSections:
		if (*p == '\0')
		{
			if (!esp.HaveELF())
			{
				fprintf(stderr, "No ELF file was specified.\n");
				exit(1);
			}
			esp.SectionInfo();
		}
		else
			option = OptionBadForm;
		break;

	case OptionAutoExtract:
		{
			const char *image = NULL;
			uint32_t addr = parm.address;

			if (*p == '=')
			{
				if (!parm.longOpt)
				{
					option = OptionBadForm;
					break;
				}
				if (*++p == '\0')
				{
					fprintf(stderr, "Missing additional image filename - \"%s\".\n", argp);
					exit(1);
				}
			}
			if (*p != '\0')
			{
				// an additional image is present, confirm that a combined file was named
				if (!parm.vfCombine.IsOpen())
				{
					fprintf(stderr, "An additional image file is allowed only when combining the extracted images - \"%s\".\n", argp);
					exit(1);
				}

				parm.address = ESP_NO_ADDRESS;
				if (addr == ESP_NO_ADDRESS)
					extractAddress(p, addr);
				image = p;
			}
			if (addr == ESP_NO_ADDRESS)
				addr = 0;
			if (!esp.HaveELF())
			{
				fprintf(stderr, "No ELF file was specified.\n");
				exit(1);
			}
			esp.AutoExtract(parm.vfCombine, parm.flashParmVal, parm.padded, image, addr);
		}
		break;

	case OptionProcessFile:
#if defined(WIN32)
		{
			// locate the base part of the filename
			int len = strlen(p);
			const char *base = p + len;
			for (base = p + len; len--; )
			{
				char c = *--base;
				if ((c == '\\') || (c == '/'))
				{
					base++;
					break;
				}
			}

			// see if the base name contains wild card characters
			if (strpbrk(base, "*?") != NULL)
			{
				HANDLE hFind;
				WIN32_FIND_DATA ffd;

				if ((hFind = FindFirstFile(p, &ffd)) != INVALID_HANDLE_VALUE)
				{
					int baseLen = base - p;
					do
					{
						if ((ffd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)) == 0)
						{
							char filename[MAX_FILENAME];
							const char *file = ffd.cFileName;
							if (baseLen)
							{
								// skip the file if the resulting name is too long (very unlikely)
								if ((strlen(ffd.cFileName) + baseLen) > sizeof(filename))
									continue;

								// construct path+filename
								memcpy(filename, p, baseLen);
								strcpy(filename + baseLen, file);
								file = filename;
							}

							// process the matching file
							processFile(esp, parm, file);
						}
					} while(FindNextFile(hFind, &ffd));
					FindClose(hFind);
				}
				break;
			}
		}
#endif
		processFile(esp, parm, p);
		break;

	default:
		break;
	}

	if (option == OptionInvalid)
	{
		fprintf(stderr, "Unrecognized option: \"%s\".\n", argp);
		exit(1);
	}
	if (option == OptionBadForm)
	{
		fprintf(stderr, "Badly formed option: \"%s\".\n", argp);
		exit(1);
	}
	if (option == OptionInvalidValue)
	{
		fprintf(stderr, "Invalid character in option value: \"%s\".\n", argp);
		exit(1);
	}
	if (flashMask)
	{
		// incorporate the Flash parameter value
		parm.flashParmVal = (parm.flashParmVal & ~flashMask) | (flashVal & flashMask);
		parm.flashParmMask |= flashMask;
	}
}

//
// Process a file using the accumulated option values.
//
static void
processFile(ESP& esp, Parameter_t& parm, const char *file)
{
	int stat;

	if ((file == NULL) || (*file == '\0'))
		return;

	if ((parm.mode == ModeWriteFlash) || (parm.mode == ModeReadFlash) || (parm.mode == ModeDumpMem))
	{
		// prepare to communicate with the ESP8266
		if ((stat = openComm(esp, parm)) != 0)
			return;
	}

	// prepare the target file, if applicable
	VFile vf;
	switch (parm.mode)
	{
	case ModeWriteFlash:
	case ModeImageCombine:
	case ModeImageAppend:
		// check for auto-address extraction
		if (parm.address == ESP_NO_ADDRESS)
			extractAddress(file, parm.address);

		// check that the address is on a block boundary
		if ((parm.address & (ESP_FLASH_BLK_SIZE - 1)) != 0)
		{
			fprintf(stderr, "The address 0x%x is not an integral multiple of the block size (%u).\n",
					parm.address, ESP_FLASH_BLK_SIZE);
			exit(1);
		}
		// continue into the next case

	case ModeImageInfo:
		if (vf.Open(file, "rb") != 0)
		{
			fprintf(stderr, "Can't open file \"%s\" for reading.\n", file);
			exit(1);
		}
		break;

	case ModeReadFlash:
	case ModeDumpMem:
		if (parm.address == ESP_NO_ADDRESS)
			parm.address = 0;
		if (parm.size == 0)
		{
			fprintf(stderr, "The size to %s must be specified.\n", (parm.mode == ModeReadFlash) ? "read" : "dump");
			exit(1);
		}
		// continue into the next case

	case ModeElfSection:
		if (vf.Open(file, "wb") != 0)
		{
			fprintf(stderr, "Can't open file \"%s\" for writing.\n", file);
			exit(1);
		}
		break;

	default:
		break;
	}

	// perform the operation
	switch (parm.mode)
	{
	case ModeWriteFlash:
		// download the file
		if (parm.address == ESP_NO_ADDRESS)
			parm.address = 0;
		if ((stat = esp.FlashWrite(vf, parm.address, parm.flashParmVal, parm.flashParmMask)) != 0)
		{
			fprintf(stderr, "Download of file \"%s\" failed (%d).\n", file, stat);
			exit(1);
		}
		parm.dlCount++;
		parm.address = ESP_NO_ADDRESS;
		break;

	case ModeReadFlash:
		// read Flash, write to file
		if ((stat = esp.FlashRead(vf, parm.address, parm.size)) != 0)
		{
			fprintf(stderr, "An error occurred while reading Flash (%d).\n", stat);
			exit(1);
		}
		parm.address = ESP_NO_ADDRESS;
		break;

	case ModeDumpMem:
		// dump memory to a file
		if (parm.address == 0)
		{
			fprintf(stderr, "The starting address to dump must be non-zero.\n");
			exit(1);
		}
		esp.DumpMem(vf, parm.address, parm.size);
		parm.address = ESP_NO_ADDRESS;
		break;

	case ModeElfSection:
		// write one or mroe sections of an ELF file to an image file
		if (!esp.HaveELF())
		{
			fprintf(stderr, "No ELF file was specified.\n");
			exit(1);
		}
		if ((stat = esp.WriteSections(vf, parm.sectName, parm.flashParmVal)) < 0)
			exit(1);
		break;

	case ModeImageInfo:
		// output information about a load image
		stat = esp.ImageInfo(vf);
		break;

	case ModeImageCombine:
	case ModeImageAppend:
		// create or append to a combined image file
		if (parm.address == ESP_NO_ADDRESS)
		{
			size_t curSize = parm.vfCombine.Size();
			if (curSize < 0)
			{
				fprintf(stderr, "Can't determine the current size of the combined image file \"%s\".\n", parm.vfCombine.Name());
				exit(1);
			}
			if (curSize == 0)
				// default to offset zero for the first file
				parm.address = 0;
			else
			{
				fprintf(stderr, "No Flash address was specified for the image file \"%s\".\n", file);
				exit(1);
			}
		}

		// add a load image to a combined image file
		stat = esp.AddImage(parm.vfCombine, vf, parm.address, parm.padded);
		parm.address = ESP_NO_ADDRESS;
		break;

	default:
		break;
	}
	vf.Close();
}

//
// Ensure that the serial port is opened and prepared for communication.
//
static int
openComm(ESP& esp, Parameter_t& parm, bool forDownload)
{
	int stat;
	unsigned speed = parm.dlSpeed;

	if (!forDownload && parm.runSpeed)
		speed = parm.runSpeed;

	if (!esp.IsCommOpen())
	{
		unsigned flags;
		switch (parm.resetMode)
		{
		case ResetAuto:
		case ResetNodeMCU:
		case ResetCK:		flags = SERIAL_DTR_LOW | SERIAL_RTS_LOW;	break;
		case ResetDTROnly:
		case ResetWifio:	flags = SERIAL_DTR_LOW;						break;
		default:			flags = 0;									break;
		}
		esp.OpenComm(parm.portStr, speed, flags);
	}
	else
		esp.SetCommSpeed(speed);

	// for download mode, make sure a connection is established
	if (forDownload)
		stat = esp.Connect(parm.resetMode);
	else
		stat = 0;
	return(stat);
}

/*
 ** getOptionVal
 *
 * Get the value of a decimal or hexadecimal parameter.  If the 'suffixK'
 * parameter is true, an upper or lower case K suffix is accepted and
 * causes the preceding value to multiplied by 1024.  If a value
 * is extracted successfully, return zero, otherwise non-zero.
 *
 */
static int
getOptionVal(const char *& p, uint32_t& val, bool suffixK)
{
	int stat = -1;
	val = 0;
	if (p != NULL)
	{
		char c;
		int radix = 10;

		// check for a hexadecimal indicator (leading x, X, 0x or 0X)
		if (((c = *p) == 'x') || (c == 'X'))
			p += 1, radix = 16;
		else if ((c == '0') && (((c = p[1]) == 'x') || (c == 'X')))
			p += 2, radix = 16;
		if ((stat = getVal(p, val, radix)) != 0)
		{
			if (!suffixK || (((c = *p) != 'k') && (c != 'K')) || (p[1] != '\0'))
				return(stat);

			// accept multiplier
			val *= 1024;
			p++;
			stat = 0;
		}
	}
	return(stat);
}

/*
 ** getVal
 *
 * Read a numeric value from a buffer using the specified radix.  Return
 * the value and a pointer to the first invalid character indirectly.
 * Return zero if one or more valid digits were read before encountering
 * a null in the buffer.  Return non-zero if an unsupported radix is
 * specified, or an invalid character or an invalid digit for the radix
 * is encountered, or if an null buffer is provided.
 *
 * Radices between 2 and 10 (inclusively) are supported as well as 16.
 *
 */
static int
getVal(const char *& p, uint32_t& val, int radix)
{
	int stat = -1;

	val = 0;

	// validate the requested radix
	if ((radix < 2) || ((radix > 10) && (radix != 16)))
		return(stat);
	if (p == NULL)
		return(stat);

	for (int digitCnt = 0; ; digitCnt++, p++)
	{
		char c;
		if ((c = *p) == '\0')
		{
			if (digitCnt > 0)
				stat = 0;
			break;
		}
		if (isdigit(c))
			c -= '0';
		else if ((radix == 16) && isxdigit(c))
		{
			if (isupper(c))
				c -= ('A' - 10);
			else
				c -= ('a' - 10);
		}
		else
			// invalid character
			break;

		if (c >= radix)
			// invalid digit for the radix
			break;

		// add the digit to the value accumulator
		val = (val * radix) + (uint8_t)c;
	}
	return(stat);
}

//
// Attempt to extract a hexadecimal address from a filename that is prefixed
// with and at sign.  If successful, return the extracted address indirectly
// and bump the filename pointer past the at sign.
//
static bool
extractAddress(const char *& file, uint32_t& addr)
{
	const char *p;
	if ((file == NULL) || (*file != '@') ||
			(((p = strstr(file, "0x")) == NULL) && ((p = strstr(file, "0X")) == NULL)))
		return(false);

	// attempt to extract an address
	uint32_t val;
	const char *pp = (p += 2);
	getVal(pp, val, 16);
	if (pp == p)
	{
		fprintf(stderr, "Unable to extract an address from filename - \"%s\".\n", file);
		exit(1);
	}

	// extraction successful
	addr = val;
	file++;
	return(true);
}
