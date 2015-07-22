# $Id: Makefile 27 2015-07-13 17:38:25Z Don $

REMOVE = rm -f

# change the value of this macro to be the path prefix for the .o files
# or comment it out to have the .o files in the current directory
OBJDIR = obj/

GENDEPFLAGS = -Wp,-M,-MP,-MT,$(OBJDIR)$(*F).o,-MF,.dep/$(@F).d
CFLAGS=-g -Wall -Wno-unused-function -pipe $(GENDEPFLAGS)
LDFLAGS=-lstdc++ -lrt
LD=g++
TARGET = esp_tool

SRC = \
	esp_tool.cpp \
	esp.cpp \
	elf.cpp \
	serial.cpp \
	${LAST}

OBJLIST = $(SRC:.cpp=.o)
ifdef OBJDIR
OBJ = $(addprefix $(OBJDIR),$(OBJLIST))
else
OBJ = $(OBJLIST)
endif

MSG_LINKING = Linking:
MSG_COMPILING = Compiling:
MSG_CLEANING = Cleaning project:

all : objdir $(TARGET)

$(TARGET) : $(OBJ)
	@echo
	@echo $(MSG_LINKING) $@
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

# rules to create the object file directory (if other than the current directory)
ifdef OBJDIR
objdir : $(OBJDIR)

$(OBJDIR) :
	mkdir $(OBJDIR)
else
objdir :
endif

ifdef OBJDIR
$(addprefix $(OBJDIR),%.o) : %.cpp
else
%.o : %.cpp
endif
	@echo
	@echo $(MSG_COMPILING) $<
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	@echo
	@echo $(MSG_CLEANING)
	$(REMOVE) $(TARGET)
	$(REMOVE) $(OBJ)
	$(REMOVE) .dep/*

# Include the dependency files.
-include $(shell mkdir .dep 2>/dev/null) $(wildcard .dep/*)

# Listing of phony targets.
.PHONY : \
	all \
	clean \
	objdir \
	${LAST}

