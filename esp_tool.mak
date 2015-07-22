# $Id: esp_tool.mak 67 2015-07-22 21:23:28Z Don $

# This is a makefile for Microsoft nmake.

!IF "$(CFG)" == ""
CFG=Debug
!ENDIF

# validate the configuration
!IF "$(CFG)" != "Release" && "$(CFG)" != "Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE
!MESSAGE nmake /f "esp_tool.mak" CFG="Debug"
!MESSAGE
!MESSAGE Possible choices for configuration are:
!MESSAGE
!MESSAGE "Release"
!MESSAGE "Debug"
!MESSAGE
!ERROR An invalid configuration is specified.
!ENDIF

!IF "$(COMPILER)" == ""
# uncomment this for building with MSVC6
#COMPILER=MSVC6

# uncomment this to build with Visual Studio 12
COMPILER=VS12
!ENDIF

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE
NULL=nul
!ENDIF

# specify version-specific options for compiling and linking
!IF "$(COMPILER)" == "MSVC6"
CC_COM_OPTS=/YX /GX
CC_DBG_OPTS=/MLd /GZ
CC_REL_OPTS=/ML
LD_COM_OPTS=/pdb:"$(BLDDIR)\$(TARG).pdb"
LD_DBG_OPTS=/pdbtype:sept /incremental:yes
LD_REL_OPTS=/incremental:no
!ELSEIF "$(COMPILER)" == "VS12"
CC_COM_OPTS=/EHsc /Gd /GS /WX- /Oy- /TP
CC_DBG_OPTS=/MTd /RTC1
CC_REL_OPTS=/MT
LD_COM_OPTS=/OPT:NOICF /pdb:"$(BLDDIR)\$(TARG).pdb" /INCREMENTAL /NXCOMPAT
LD_DBG_OPTS=
LD_REL_OPTS=
!ELSE
!ERROR No recognized compiler is specified.
!ENDIF

# specify build-specific options for compiling and linking
!IF  "$(CFG)" == "Release"
BLDDIR=.\Release
CFLAGS=$(CC_COM_OPTS) $(CC_REL_OPTS) /W3 /D NDEBUG /O2
LFLAGS=$(LD_COM_OPTS) $(LD_REL_OPTS)
!ELSEIF "$(CFG)" == "Debug"
BLDDIR=.\Debug
CFLAGS=$(CC_COM_OPTS) $(CC_DBG_OPTS) /W3 /D _DEBUG /Od /Gm /ZI
LFLAGS=$(LD_COM_OPTS) $(LD_DBG_OPTS) /debug
!ENDIF
OBJDIR=$(BLDDIR)

TARG=esp_tool

# specify commands
CC=cl.exe
LD=link.exe
RM=erase

# specify the compile command line options
CPPFLAGS=/nologo $(CFLAGS) /c /D "WIN32" /D "_CONSOLE" /D "_MBCS" /Fp"$(OBJDIR)\$(TARG).pch" /Fo"$(OBJDIR)\\" /Fd"$(OBJDIR)\\" /FD

# specify the linker command line options
LIBS=kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib
LDFLAGS=/nologo $(LFLAGS) /machine:I386 /subsystem:console $(LIBS) /out:"$(BLDDIR)\$(TARG).exe"

# specify the objects to be built
OBJS="$(OBJDIR)\esp_tool.obj" "$(OBJDIR)\esp.obj" "$(OBJDIR)\elf.obj" "$(OBJDIR)\serial.obj"

first : all

all : "$(BLDDIR)\$(TARG).exe"

clean :
	-@if exist "$(BLDDIR)\*.obj"       $(RM) "$(BLDDIR)\*.obj"
	-@if exist "$(BLDDIR)\*.idb"       $(RM) "$(BLDDIR)\*.idb"
	-@if exist "$(BLDDIR)\*.ilk"       $(RM) "$(BLDDIR)\*.ilk"
	-@if exist "$(BLDDIR)\*.pdb"       $(RM) "$(BLDDIR)\*.pdb"
	-@if exist "$(BLDDIR)\*.idb"       $(RM) "$(BLDDIR)\*.idb"
	-@if exist "$(BLDDIR)\$(TARG).exe" $(RM) "$(BLDDIR)\$(TARG).exe"

"$(BLDDIR)" :
    if not exist "$(BLDDIR)/$(NULL)" mkdir "$(BLDDIR)"

!IF "$(OBJDIR)" != "$(BLDDIR)"
"$(OBJDIR)" :
    if not exist "$(OBJDIR)/$(NULL)" mkdir "$(OBJDIR)"
!ENDIF

.cpp{$(OBJDIR)}.obj::
   $(CC) $(CPPFLAGS) $<

"$(BLDDIR)\$(TARG).exe" : "$(BLDDIR)" "$(OBJDIR)" $(OBJS)
    $(LD) $(LDFLAGS) $(OBJS)

$(OBJDIR)\esp_tool.obj : esp_tool.cpp esp.h elf.h serial.h sysdep.h
$(OBJDIR)\esp.obj : esp.cpp esp.h elf.h serial.h sysdep.h
$(OBJDIR)\elf.obj : elf.cpp elf.h sysdep.h
$(OBJDIR)\serial.obj : serial.cpp serial.h

