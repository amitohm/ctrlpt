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
 * \file
 */

#include "config.h"

#include "ithread.h"
#include "upnp.h"
#include "debug.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/timeb.h>
#include <string.h>
#include <time.h>

#define LOGFILE	"/home/amit/projects/UPnP/private/upnp_device/log.txt"

/*! Mutex to synchronize all the log file opeartions in the debug mode */
static ithread_mutex_t GlobalDebugMutex;

/*! Global log level */
static Dbg_LogLevel g_log_level = DEFAULT_LOG_LEVEL;

int DbgInitLog(void)
{
    ithread_mutex_init(&GlobalDebugMutex, NULL);
#ifdef __DAEMONIZE__
    FILE *fp = fopen(LOGFILE,"wb");
    if(NULL==fp) {
	/* Failed to open log file */
	return 0;
    }
    fprintf(fp,"Start logging\n");
    fclose(fp);
#endif /* __DAEMONIZE__ */
    return UPNP_E_SUCCESS;
}

void DbgSetLogLevel(Dbg_LogLevel log_level)
{
    g_log_level = log_level;
}

void DbgCloseLog(void)
{
    ithread_mutex_destroy(&GlobalDebugMutex);
}

void DbgPrintf(Dbg_LogLevel DLevel, const char *TagName, const char *FmtStr, ...)
{
    struct timeb tp;
    struct tm *tm;
    va_list ArgList;
    FILE *fp;

    if(DLevel > g_log_level)
	return;

    ithread_mutex_lock(&GlobalDebugMutex);
#ifdef __DAEMONIZE__
    fp = fopen(LOGFILE,"ab");
    if(NULL==fp) {
	/* Failed to open log file */
	return;
    }
#else /* __DAEMONIZE__ */
    fp = stdout;
#endif /* __DAEMONIZE__ */
    va_start(ArgList, FmtStr);

    ftime(&tp);
    tm = localtime(&tp.time);

    fprintf(fp,"%02d:%02d:%02d:%03d   %d (%ld): %s: ",tm->tm_hour,tm->tm_min,tm->tm_sec,tp.millitm,getpid(),syscall(SYS_gettid), TagName);
    vfprintf(fp, FmtStr, ArgList);
    fflush(fp);

    va_end(ArgList);
#ifdef __DAEMONIZE__
    fclose(fp);
#endif /* __DAEMONIZE__ */

    ithread_mutex_unlock(&GlobalDebugMutex);

    return;
}

