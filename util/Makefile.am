#
# "Makefile.am" for "libupnp/util"
#
# (C) Copyright 2005 Remi Turboult <r3mi@users.sourceforge.net>
#

AM_CPPFLAGS		= \
			  -I./inc \
			  -I$(top_srcdir)/upnp/inc \
			  -I$(top_srcdir)/upnp/src/inc \
			  -I$(top_srcdir)/threadutil/inc

if ENABLE_DEBUG
    AM_CPPFLAGS		+= -DDEBUG -DSTATS
else
    AM_CPPFLAGS		+= -DNO_DEBUG -DNDEBUG
endif


lib_LTLIBRARIES		= libutil.la

libutil_la_LDFLAGS = -version-info $(LT_VERSION_THREADUTIL)

libutil_la_SOURCES = \
			src/debug.c \
			src/os_unix.c

