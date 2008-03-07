#
# Makefile for a Video Disk Recorder plugin
#
# $Id: Makefile 1.7 2003/12/21 15:47:41 kls Exp $

# The official name of this plugin.
# This name will be used in the '-P...' option of VDR to load the plugin.
# By default the main source file also carries this name.
#
PLUGIN = lcdproc

### The version number of this plugin (taken from the main source file):

VERSION = $(shell grep 'static const char \*VERSION *=' $(PLUGIN).c | awk '{ print $$6 }' | sed -e 's/[";]//g')

### The C++ compiler and options:

CXX      ?= g++
CXXFLAGS ?= -O2 -Wall -Woverloaded-virtual

### The directory environment:

DVBDIR = ../../../../DVB
VDRDIR = ../../..
LIBDIR = ../../lib
TMPDIR = /tmp

### Allow user defined options to overwrite defaults:

-include $(VDRDIR)/Make.config

### The version number of VDR (taken from VDR's "config.h"):

APIVERSION = $(shell grep 'define APIVERSION ' $(VDRDIR)/config.h | awk '{ print $$3 }' | sed -e 's/"//g')
VDRVERSNUM = $(shell grep 'define VDRVERSNUM ' $(VDRDIR)/config.h | awk '{ print $$3 }' | sed -e 's/"//g')
USE_GETTEXT= $(shell test $(VDRVERSNUM) -ge 10507 && echo yes)
USE_GETTEXT_V2= $(shell test $(VDRVERSNUM) -ge 10508 && echo yes)

ifeq ($(USE_GETTEXT_V2),yes)
I18N_PREFIX = vdr-
endif

### The name of the distribution archive:

ARCHIVE = $(PLUGIN)-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### Includes and Defines (add further entries here):

INCLUDES += -I$(VDRDIR)/include -I$(DVBDIR)/include

DEFINES += -D_GNU_SOURCE -DPLUGIN_NAME_I18N='"$(PLUGIN)"'

ifeq ($(shell echo $(APIVERSION)|sed -e 's/\([0-9]\.[0-9]\)\..*/\1/' ),1.2)
  DEFINES += -DOLDVDR
endif

ifdef LCDKEYCONF
  DEFINES += -DLCD_EXT_KEY_CONF="\"$(LCDKEYCONF)\""
endif



### The object files (add further files here):

OBJS = $(PLUGIN).o lcd.o sockets.o setup.o

ifneq ($(USE_GETTEXT),yes)
OBJS += i18n.o
endif

### Targets:
ifeq ($(USE_GETTEXT),yes)
all: libvdr-$(PLUGIN).so i18n
else
all: libvdr-$(PLUGIN).so
endif

%.o: %.c
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) $<

libvdr-$(PLUGIN).so: $(OBJS)
	$(CXX) $(CXXFLAGS) -shared $(OBJS) -o $@
	@cp $@ $(LIBDIR)/$@.$(APIVERSION)

dist: clean
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@tar czf $(PACKAGE).tgz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(PACKAGE).tgz

clean:
	@-rm -f $(PODIR)/*.mo $(PODIR)/*.pot
	@-rm -f $(OBJS) $(DEPFILE) *.so *.tgz core* *~

# Dependencies:

MAKEDEP = g++ -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.c) > $@

-include $(DEPFILE)

### Internationalization (I18N):

PODIR     = po
LOCALEDIR = $(VDRDIR)/locale
I18Npo    = $(wildcard $(PODIR)/*.po)
I18Nmo    = $(addsuffix .mo, $(foreach file, $(I18Npo), $(basename $(file))))
I18Ndirs  = $(notdir $(foreach file, $(I18Npo), $(basename $(file))))
I18Npot   = $(PODIR)/$(PLUGIN).pot

%.mo: %.po
	msgfmt -c -o $@ $<

$(I18Npot): $(wildcard *.c)
	xgettext -C -cTRANSLATORS --no-wrap -F -k -ktr -ktrNOOP --msgid-bugs-address='<vdr@joachim-wilke.de>' -o $@ $(wildcard *.c)

$(I18Npo): $(I18Npot)
	msgmerge -U --no-wrap -F --backup=none -q $@ $<

i18n: $(I18Nmo)
	@mkdir -p $(LOCALEDIR)
	for i in $(I18Ndirs); do\
	    mkdir -p $(LOCALEDIR)/$$i/LC_MESSAGES;\
	    cp $(PODIR)/$$i.mo $(LOCALEDIR)/$$i/LC_MESSAGES/$(I18N_PREFIX)$(PLUGIN).mo;\
	    done

