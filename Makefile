##############################################################################
##                                  gfx2o™                                  ##
##                                                                          ##
##                         Copyright © 2020 Aquefir                         ##
##                           Released under MPL2.                           ##
##############################################################################

include etc/base.mk

# name of project. used in output binary naming
PROJECT := gfx2o

# put a ‘1’ for the desired target types to compile
EXEFILE := 1
SOFILE  :=
AFILE   :=

# space-separated path list for #includes
# <system> includes
INCLUDES := include $(TROOT)/include/glib-2.0 $(TROOT)/lib/glib-2.0/include
# "local" includes
INCLUDEL := src

# space-separated library name list
LIBS      := glib-2.0
LIBDIRS   := $(TROOT)/lib

# ‘3P’ are in-tree 3rd-party dependencies
# 3PLIBDIR is the base directory
# 3PLIBS is the folder names in the base directory for each library
3PLIBDIR := 3rdparty
3PLIBS   :=

# frameworks (macOS target only)
FWORKS :=

# sources
CFILES    := \
	src/main.c
HFILES    :=
CPPFILES  :=
HPPFILES  :=

# test suite sources
TES_CFILES   :=
TES_HFILES   :=
TES_CPPFILES :=
TES_HPPFILES :=

# force C++ linker because of 3rd-party library
CCLD := $(CXX)

# this defines all our usual targets
include etc/targets.mk
