#
# "Makefile.am" for "libupnp/upnp/sample"
#
# Copyright (C) 2007 Marcelo Roberto Jimenez <mroberto@users.sourceforge.net>
#

AM_CPPFLAGS = \
	-I$(top_srcdir)/upnp/inc \
	-I$(top_srcdir)/util/inc \
	-I$(top_srcdir)/threadutil/inc

LDADD = \
	$(top_builddir)/upnp/libupnp.la \
	$(top_builddir)/util/libutil.la \
	$(top_builddir)/threadutil/libthreadutil.la

# samples
noinst_PROGRAMS =
if ENABLE_SAMPLES
if ENABLE_CLIENT
noinst_PROGRAMS += ctrlpt
ctrlpt_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(srcdir)/common/ \
	-I$(srcdir)/ctrlpt
endif
endif

ctrlpt_SOURCES = \
	ctrlpt.c \
	ctrlpt.h

if WITH_DOCUMENTATION
examplesdir = $(docdir)/examples
examples_DATA = \
	$(ctrlpt_SOURCES) \
	$(tv_device_SOURCES)
endif

