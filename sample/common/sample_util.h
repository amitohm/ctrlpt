#ifndef SAMPLE_UTIL_H
#define SAMPLE_UTIL_H

/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer. 
 * - Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * - Neither name of Intel Corporation nor the names of its contributors 
 * may be used to endorse or promote products derived from this software 
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY 
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/

/*!
 * \defgroup UpnpSamples Sample Code
 *
 * @{
 *
 * \file
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "ithread.h"
#include "upnp.h" /* for Upnp_EventType */
#include "upnptools.h"

#include <stdlib.h>
#include <string.h>

#ifdef SAMPLE_UTIL_C
	/*! Service types for tv services. */
	const char *TvServiceType[] = {
		"urn:upnp:service:tvcontrol:1",
		"urn:upnp:service:tvpicture:1"
	};
#else /* SAMPLE_UTIL_C */
	extern const char *TvServiceType[];
#endif /* SAMPLE_UTIL_C */

/* mutex to control displaying of events */
extern ithread_mutex_t display_mutex;

typedef enum {
	STATE_UPDATE = 0,
	DEVICE_ADDED = 1,
	DEVICE_REMOVED = 2,
	GET_VAR_COMPLETE = 3
} eventType;

/*!
 * \brief Prints a callback event type as a string.
 */
void SampleUtil_PrintEventType(
	/*! [in] The callback event. */
	Upnp_EventType S);

/*!
 * \brief Prints callback event structure details.
 */
int SampleUtil_PrintEvent(
	/*! [in] The type of callback event. */
	Upnp_EventType EventType, 
	/*! [in] The callback event structure. */
	void *Event);

/*!
 * \brief Prototype for displaying strings. All printing done by the device,
 * control point, and sample util, ultimately use this to display strings 
 * to the user.
 */
typedef void (*print_string)(
	/*! [in] Format. */
	const char *string,
	/*! [in] Arguments. */
	...)
#if (__GNUC__ >= 3)
	/* This enables printf like format checking by the compiler */
	__attribute__((format (__printf__, 1, 2)))
#endif
;

/*! global print function used by sample util */
extern print_string gPrintFun;

/*!
 * \brief Prototype for passing back state changes.
 */
typedef void (*state_update)(
	/*! [in] . */
	const char *varName,
	/*! [in] . */
	const char *varValue,
	/*! [in] . */
	const char *UDN,
	/*! [in] . */
	eventType type);

/*! global state update function used by smaple util */
extern state_update gStateUpdateFun;

/*!
 * \brief Initializes the sample util. Must be called before any sample util
 * functions. May be called multiple times.
 */
int SampleUtil_Initialize(
	/*! [in] Print function to use in SampleUtil_Print. */
	print_string print_function);

/*!
 * \brief Releases Resources held by sample util.
 */
int SampleUtil_Finish();

/*!
 * \brief Function emulating printf that ultimately calls the registered print
 * function with the formatted string.
 *
 * Provides platform-specific print functionality.  This function should be
 * called when you want to print content suitable for console output (i.e.,
 * in a large text box or on a screen).  If your device/operating system is 
 * not supported here, you should add a port.
 *
 * \return The same as printf.
 */
int SampleUtil_Print(
	/*! [in] Format (see printf). */
	const char *fmt,
	/*! [in] Format data. */
	...)
#if (__GNUC__ >= 3)
	/* This enables printf like format checking by the compiler */
	__attribute__((format (__printf__, 1, 2)))
#endif
;

/*!
 * \brief
 */
int SampleUtil_RegisterUpdateFunction(
	/*! [in] . */
	state_update update_function);

/*!
 * \brief
 */
void SampleUtil_StateUpdate(
	/*! [in] . */
	const char *varName,
	/*! [in] . */
	const char *varValue,
	/*! [in] . */
	const char *UDN,
	/*! [in] . */
	eventType type);

/*!
 * \brief Prints a string to standard out.
 */
void linux_print(const char *format, ...)
#if (__GNUC__ >= 3)
	/* This enables printf like format checking by the compiler */
	__attribute__((format (__printf__, 1, 2)))
#endif
;

#ifdef __cplusplus
};
#endif /* __cplusplus */

#ifdef WIN32
	#define snprintf	_snprintf
	#define strcasecmp	stricmp
#endif

/*! @} UpnpSamples */

#endif /* SAMPLE_UTIL_H */

