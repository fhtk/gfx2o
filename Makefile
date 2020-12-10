##############################################################################
##                                  gfx2o™                                  ##
##                                                                          ##
##                         Copyright © 2020 Aquefir                         ##
##                           Released under MPL2.                           ##
##############################################################################

include $(AQ)/lib/slick/base.mk

# name of project. used in output binary naming
PROJECT := gfx2o

# put a ‘1’ for the desired target types to compile
EXEFILE := 1
SOFILE  :=
AFILE   :=

# space-separated path list for #includes
# <system> includes
INCLUDES := include $(TROOT)/include
# "local" includes
INCLUDEL := src

# space-separated library name list
LIBS      := uni_log uni_err uni_himem uni_arr uni_str mangledeggs
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
	src/excall.c \
	src/main.c
CPPFILES  :=
PUBHFILES :=
PRVHFILES := \
	src/excall.h

# test suite sources
TES_CFILES    :=
TES_CPPFILES  :=
TES_PUBHFILES :=
TES_PRVHFILES :=

# this defines all our usual targets
include $(AQ)/lib/slick/targets.mk
