/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation
 * Copyright (c) 2006 Rémi Turboult <r3mi@users.sourceforge.net>
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

#ifndef OHM_DEBUG_H
#define OHM_DEBUG_H

/*!
 * \file
 */

#include "ThreadPool.h"
#include "upnpconfig.h"
#include "UpnpGlobal.h"		/* for _INLINE */

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! \name Other debugging features
 *
 * The UPnP SDK contains other features to aid in debugging.
 */
/*@{*/

/*! \name Dbg_LogLevel
 *  The user has the option to select 4 different types of debugging levels,
 *  see \c DbgSetLogLevel.
 *  The critical level will show only those messages 
 *  which can halt the normal processing of the library, like memory 
 *  allocation errors. The remaining three levels are just for debugging 
 *  purposes.  Packet level will display all the incoming and outgoing 
 *  packets that are flowing between the control point and the device. 
 *  Info Level displays the other important operational information 
 *  regarding the working of the library. If the user selects All, 
 *  then the library displays all the debugging information that it has.
 *    \li \c OHM_CRITICAL [0]
 *    \li \c OHM_PACKET [1]
 *    \li \c DBG_INFO [2]
 *    \li \c DBG_ALL [3]
 */

/*@{*/
typedef enum Dbg_LogLevel_e {
    DBG_ERROR,
    DBG_WARN,
    DBG_INFO,
    DBG_ALL
} Dbg_LogLevel;
/*@}*/

/*!
 * Default log level : see \c Dbg_LogLevel
 */
#define DEFAULT_LOG_LEVEL	DBG_ERROR

#define CDBG_ERROR(fmt, args...)	DbgPrintf(DBG_ERROR, DBG_TAG, fmt, ##args)
#define CDBG_WARN(fmt, args...)		DbgPrintf(DBG_WARN, DBG_TAG, fmt, ##args)
#define CDBG_INFO(fmt, args...)		DbgPrintf(DBG_INFO, DBG_TAG, fmt, ##args)
#define CDBG(fmt, args...)		DbgPrintf(DBG_ALL, DBG_TAG, fmt, ##args)

/*!
 * \brief Initialize the log files.
 *
 * \return -1 if fails or UPNP_E_SUCCESS if succeeds.
 */
int DbgInitLog(void);
/*!
 * \brief Set the log level (see \c Dbg_LogLevel).
 */
void DbgSetLogLevel(
	/*! [in] Log level. */
	Dbg_LogLevel log_level);

/*!
 * \brief Closes the log files.
 */
void DbgCloseLog(void);

/*!
 * \brief Prints the debug statement either on the standard output or log file
 * along with the information from where this debug statement is coming.
 */
void DbgPrintf(
	/*! [in] The level of the debug logging. It will decide whether debug
	 * statement will go to standard output, or any of the log files. */
	Dbg_LogLevel DLevel,
	/*! [in] debug will go in the name of this module. */
	const char *TagName,
	/*! [in] Printf like format specification. */
	const char *FmtStr,
	/*! [in] Printf like Variable number of arguments that will go in the
	 * debug statement. */
	...)
#if (__GNUC__ >= 3)
	/* This enables printf like format checking by the compiler. */
	__attribute__ ((format(__printf__, 3, 4)))
#endif
	;

/*@}*/

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_H */
