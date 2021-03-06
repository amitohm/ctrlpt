/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
 * All rights reserved. 
 * Copyright (C) 2011-2012 France Telecom All rights reserved. 
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
 * \addtogroup UPnPAPI
 * 
 * @{
 * 
 * \file
 */

#include "config.h"

#include "upnpapi.h"

#include "httpreadwrite.h"
#include "membuffer.h"
#include "ssdplib.h"
#include "soaplib.h"
#include "sysdep.h"
#include "ThreadPool.h"
#include "UpnpStdInt.h"
#include "UpnpUniStd.h" /* for close() */
#include "uuid.h"

/* Needed for GENA */
#include "gena.h"
#include "miniserver.h"

#include <sys/stat.h>

#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
	/* Do not include these files */
#else
	#include <sys/ioctl.h>
	#include <sys/param.h>
	#include <sys/types.h>
#endif

#undef DBG_TAG
#define DBG_TAG "API"

#ifndef IN6_IS_ADDR_GLOBAL
#define IN6_IS_ADDR_GLOBAL(a) \
		((((__const uint32_t *) (a))[0] & htonl((uint32_t)0x70000000)) \
		== htonl ((uint32_t)0x20000000))
#endif /* IS ADDR GLOBAL */

#ifndef IN6_IS_ADDR_ULA
#define IN6_IS_ADDR_ULA(a) \
		((((__const uint32_t *) (a))[0] & htonl((uint32_t)0xfe000000)) \
		== htonl ((uint32_t)0xfc000000))
#endif /* IS ADDR ULA */

/*! This structure is for virtual directory callbacks */
struct VirtualDirCallbacks virtualDirCallback;

/*! Pointer to the virtual directory list. */
virtualDirList *pVirtualDirList;

#ifdef INCLUDE_CLIENT_APIS
/*! Mutex to synchronize the subscription handling at the client side. */
ithread_mutex_t GlobalClientSubscribeMutex;
#endif /* INCLUDE_CLIENT_APIS */

/*! rwlock to synchronize handles (root device or control point handle). */
ithread_rwlock_t GlobalHndRWLock;


/*! Mutex to synchronize the uuid creation process. */
ithread_mutex_t gUUIDMutex;

/*! Initialization mutex. */
ithread_mutex_t gSDKInitMutex = PTHREAD_MUTEX_INITIALIZER;

/*! Global timer thread. */
TimerThread gTimerThread;

/*! Send thread pool. */
ThreadPool gSendThreadPool;

/*! Receive thread pool. */
ThreadPool gRecvThreadPool;

/*! Mini server thread pool. */
ThreadPool gMiniServerThreadPool;

/*! Flag to indicate the state of web server */
WebServerState bWebServerState = WEB_SERVER_DISABLED;

/*! Static buffer to contain interface name. (extern'ed in upnp.h) */
char gIF_NAME[LINE_SIZE] = { '\0' };

/*! Static buffer to contain interface IPv4 address. (extern'ed in upnp.h) */
char gIF_IPV4[INET_ADDRSTRLEN] = { '\0' };

/*! Static buffer to contain interface IPv6 address. (extern'ed in upnp.h) */
char gIF_IPV6[INET6_ADDRSTRLEN] = { '\0' };

/*! Static buffer to contain interface ULA or GUA IPv6 address. (extern'ed in upnp.h) */
char gIF_IPV6_ULA_GUA[INET6_ADDRSTRLEN] = { '\0' };

/*! Contains interface index. (extern'ed in upnp.h) */
unsigned gIF_INDEX = (unsigned)-1;

/*! local IPv4 port for the mini-server */
unsigned short LOCAL_PORT_V4;

/*! local IPv6 port for the mini-server */
unsigned short LOCAL_PORT_V6;

/*! UPnP device and control point handle table  */
#define NUM_HANDLE 200
static void *HandleTable[NUM_HANDLE];

/*! a local dir which serves as webserver root */
extern membuffer gDocumentRootDir;

/*! Maximum content-length (in bytes) that the SDK will process on an incoming
 * packet. Content-Length exceeding this size will be not processed and
 * error 413 (HTTP Error Code) will be returned to the remote end point. */
size_t g_maxContentLength = DEFAULT_SOAP_CONTENT_LENGTH;

/*! Global variable to denote the state of Upnp SDK == 0 if uninitialized,
 * == 1 if initialized. */
int UpnpSdkInit = 0;

/*! Global variable to denote the state of Upnp SDK client registration.
 * == 0 if unregistered, == 1 if registered. */
int UpnpSdkClientRegistered = 0;

/*! Global variable to denote the state of Upnp SDK IPv4 device registration.
 * == 0 if unregistered, == 1 if registered. */
int UpnpSdkDeviceRegisteredV4 = 0;

/*! Global variable to denote the state of Upnp SDK IPv6 device registration.
 * == 0 if unregistered, == 1 if registered. */
int UpnpSdkDeviceregisteredV6 = 0;

#ifdef UPNP_HAVE_OPTSSDP
/*! Global variable used in discovery notifications. */
Upnp_SID gUpnpSdkNLSuuid;
#endif /* UPNP_HAVE_OPTSSDP */


/*!
 * \brief (Windows Only) Initializes the Windows Winsock library.
 *
 * \return UPNP_E_SUCCESS on success, UPNP_E_INIT_FAILED on failure.
 */
static int WinsockInit(void)
{
	int retVal = UPNP_E_SUCCESS;
#ifdef WIN32
	WORD wVersionRequested;
	WSADATA wsaData;
	int err;

	wVersionRequested = MAKEWORD(2, 2);
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		/* Tell the user that we could not find a usable */
		/* WinSock DLL.                                  */
		retVal = UPNP_E_INIT_FAILED;
		goto exit_function;
	}
	/* Confirm that the WinSock DLL supports 2.2.
	 * Note that if the DLL supports versions greater
	 * than 2.2 in addition to 2.2, it will still return
	 * 2.2 in wVersion since that is the version we
	 * requested. */
	if (LOBYTE(wsaData.wVersion) != 2 ||
	    HIBYTE(wsaData.wVersion) != 2) {
		/* Tell the user that we could not find a usable
		 * WinSock DLL. */
		WSACleanup();
		retVal = UPNP_E_INIT_FAILED; 
		goto exit_function;
	}
	/* The WinSock DLL is acceptable. Proceed. */
exit_function:
#else
#endif
	return retVal;
}


/*!
 * \brief Initializes the global mutexes used by the UPnP SDK.
 *
 * \return UPNP_E_SUCCESS on success or UPNP_E_INIT_FAILED if a mutex could not
 * 	be initialized.
 */
static int UpnpInitMutexes(void)
{
#ifdef __CYGWIN__
	/* On Cygwin, pthread_mutex_init() fails without this memset. */
	/* TODO: Fix Cygwin so we don't need this memset(). */
	memset(&GlobalHndRWLock, 0, sizeof(GlobalHndRWLock));
#endif
	if (ithread_rwlock_init(&GlobalHndRWLock, NULL) != 0) {
		return UPNP_E_INIT_FAILED;
	}

	if (ithread_mutex_init(&gUUIDMutex, NULL) != 0) {
		return UPNP_E_INIT_FAILED;
	}
	/* initialize subscribe mutex. */
#ifdef INCLUDE_CLIENT_APIS
	if (ithread_mutex_init(&GlobalClientSubscribeMutex, NULL) != 0) {
		return UPNP_E_INIT_FAILED;
	}
#endif
	return UPNP_E_SUCCESS;
}


/*!
 * \brief Initializes the global threadm pools used by the UPnP SDK.
 *
 * \return UPNP_E_SUCCESS on success or UPNP_E_INIT_FAILED if a mutex could not
 * 	be initialized.
 */
static int UpnpInitThreadPools(void)
{
	int ret = UPNP_E_SUCCESS;
	ThreadPoolAttr attr;

	TPAttrInit(&attr);
	TPAttrSetMaxThreads(&attr, MAX_THREADS);
	TPAttrSetMinThreads(&attr, MIN_THREADS);
	TPAttrSetStackSize(&attr, THREAD_STACK_SIZE);
	TPAttrSetJobsPerThread(&attr, JOBS_PER_THREAD);
	TPAttrSetIdleTime(&attr, THREAD_IDLE_TIME);
	TPAttrSetMaxJobsTotal(&attr, MAX_JOBS_TOTAL);

	if (ThreadPoolInit(&gSendThreadPool, &attr) != UPNP_E_SUCCESS) {
		ret = UPNP_E_INIT_FAILED;
		goto exit_function;
	}

	if (ThreadPoolInit(&gRecvThreadPool, &attr) != UPNP_E_SUCCESS) {
		ret = UPNP_E_INIT_FAILED;
		goto exit_function;
	}

	if (ThreadPoolInit(&gMiniServerThreadPool, &attr) != UPNP_E_SUCCESS) {
		ret = UPNP_E_INIT_FAILED;
		goto exit_function;
	}

exit_function:
	if (ret != UPNP_E_SUCCESS) {
		UpnpSdkInit = 0;
		UpnpFinish();
	}

	return ret;
}


/*!
 * \brief Performs the initial steps in initializing the UPnP SDK.
 *
 *	\li Winsock library is initialized for the process (Windows specific).
 *	\li The logging (for debug messages) is initialized.
 *	\li Mutexes, Handle table and thread pools are allocated and initialized.
 *	\li Callback functions for SOAP and GENA are set, if they're enabled.
 *	\li The SDK timer thread is initialized.
 *
 * \return UPNP_E_SUCCESS on success.
 */
static int UpnpInitPreamble(void)
{
	int retVal = UPNP_E_SUCCESS;
	int i;
#ifdef UPNP_HAVE_OPTSSDP
	uuid_upnp nls_uuid;
#endif /* UPNP_HAVE_OPTSSDP */

	retVal = WinsockInit();
	if (retVal != UPNP_E_SUCCESS) {
		return retVal;
	}

	/* needed by SSDP or other parts. */
	srand((unsigned int)time(NULL));

	/* Initialize debug output. */
	retVal = DbgInitLog();
	if (retVal != UPNP_E_SUCCESS) {
		/* UpnpInitLog does not return a valid UPNP_E_*. */
		return UPNP_E_INIT_FAILED;
	}

	CDBG_INFO("Inside UpnpInitPreamble\n" );

	/* Initialize SDK global mutexes. */
	retVal = UpnpInitMutexes();
	if (retVal != UPNP_E_SUCCESS) {
		return retVal;
	}

#ifdef UPNP_HAVE_OPTSSDP
	/* Create the NLS uuid. */
	uuid_create(&nls_uuid);
	uuid_unpack(&nls_uuid, gUpnpSdkNLSuuid);
#endif /* UPNP_HAVE_OPTSSDP */

	/* Initializes the handle list. */
	HandleLock();
	for (i = 0; i < NUM_HANDLE; ++i) {
		HandleTable[i] = NULL;
	}
	HandleUnlock();

	/* Initialize SDK global thread pools. */
	retVal = UpnpInitThreadPools();
	if (retVal != UPNP_E_SUCCESS) {
		return retVal;
	}

	/* Initialize the SDK timer thread. */
	retVal = TimerThreadInit( &gTimerThread, &gSendThreadPool );
	if (retVal != UPNP_E_SUCCESS) {
		UpnpFinish();

		return retVal;
	}

	return UPNP_E_SUCCESS;
}


/*!
 * \brief Finishes initializing the UPnP SDK.
 *	\li The MiniServer is started, if enabled.
 *	\li The WebServer is started, if enabled.
 * 
 * \return UPNP_E_SUCCESS on success or  UPNP_E_INIT_FAILED if a mutex could not
 * 	be initialized.
 */
static int UpnpInitStartServers(
	/*! [in] Local Port to listen for incoming connections. */
	unsigned short DestPort)
{
#if EXCLUDE_MINISERVER == 0 || EXCLUDE_WEB_SERVER == 0
	int retVal = 0;
#endif

	CDBG_INFO("Entering UpnpInitStartServers\n" );

#if EXCLUDE_MINISERVER == 0
	LOCAL_PORT_V4 = DestPort;
	LOCAL_PORT_V6 = DestPort;
	retVal = StartMiniServer(&LOCAL_PORT_V4, &LOCAL_PORT_V6);
	if (retVal != UPNP_E_SUCCESS) {
		CDBG_INFO(
			"Miniserver failed to start");
		UpnpFinish();
		return retVal;
	}
#endif

#if EXCLUDE_WEB_SERVER == 0
	membuffer_init(&gDocumentRootDir);
	retVal = UpnpEnableWebserver(WEB_SERVER_ENABLED);
	if (retVal != UPNP_E_SUCCESS) {
		UpnpFinish();
		return retVal;
	}
#endif

	CDBG_INFO(
		"Exiting UpnpInitStartServers\n");

	return UPNP_E_SUCCESS;
}


int UpnpInit(const char *HostIP, unsigned short DestPort)
{
	int retVal = UPNP_E_SUCCESS;

	/* Initializes the ithread library */
	ithread_initialize_library();

	ithread_mutex_lock(&gSDKInitMutex);

	/* Check if we're already initialized. */
	if (UpnpSdkInit == 1) {
		retVal = UPNP_E_INIT;
		goto exit_function;
	}

	/* Perform initialization preamble. */
	retVal = UpnpInitPreamble();
	if (retVal != UPNP_E_SUCCESS) {
		goto exit_function;
	}

	CDBG_INFO(
		"UpnpInit with HostIP=%s, DestPort=%d.\n", 
		HostIP ? HostIP : "", (int)DestPort);

	/* Verify HostIP, if provided, or find it ourselves. */
	memset(gIF_IPV4, 0, sizeof(gIF_IPV4));
	if (HostIP != NULL) {
		strncpy(gIF_IPV4, HostIP, sizeof(gIF_IPV4) - 1);
	} else {
		if( getlocalhostname( gIF_IPV4, sizeof(gIF_IPV4) - 1 ) != UPNP_E_SUCCESS ) {
			retVal = UPNP_E_INIT_FAILED;
			goto exit_function;
		}
	}

	/* Set the UpnpSdkInit flag to 1 to indicate we're successfully initialized. */
	UpnpSdkInit = 1;

	/* Finish initializing the SDK. */
	retVal = UpnpInitStartServers(DestPort);
	if (retVal != UPNP_E_SUCCESS) {
		UpnpSdkInit = 0;
		goto exit_function;
	}

	CDBG_INFO(
		"Host Ip: %s Host Port: %d\n", gIF_IPV4,
		(int)LOCAL_PORT_V4);

exit_function:
	ithread_mutex_unlock(&gSDKInitMutex);

	return retVal;
}

#ifdef UPNP_ENABLE_IPV6
int UpnpInit2(const char *IfName, unsigned short DestPort)
{
	int retVal;

	/* Initializes the ithread library */
	ithread_initialize_library();

	ithread_mutex_lock(&gSDKInitMutex);

	/* Check if we're already initialized. */
	if (UpnpSdkInit == 1) {
		retVal = UPNP_E_INIT;
		goto exit_function;
	}

	/* Perform initialization preamble. */
	retVal = UpnpInitPreamble();
	if (retVal != UPNP_E_SUCCESS) {
		goto exit_function;
	}

	CDBG_INFO(
		"UpnpInit2 with IfName=%s, DestPort=%d.\n", 
		IfName ? IfName : "", DestPort);

	/* Retrieve interface information (Addresses, index, etc). */
	retVal = UpnpGetIfInfo( IfName );
	if (retVal != UPNP_E_SUCCESS) {
		goto exit_function;
	}

	/* Set the UpnpSdkInit flag to 1 to indicate we're successfully initialized. */
	UpnpSdkInit = 1;

	/* Finish initializing the SDK. */
	retVal = UpnpInitStartServers(DestPort);
	if (retVal != UPNP_E_SUCCESS) {
		UpnpSdkInit = 0;
		goto exit_function;
	}

exit_function:
	ithread_mutex_unlock(&gSDKInitMutex);

	return retVal;
}
#endif

#ifdef DEBUG
/*!
 * \brief Prints thread pool statistics.
 */
void PrintThreadPoolStats(
	/*! [in] The thread pool. */
	ThreadPool *tp,
	/*! [in] The file name that called this function, use the macro
	 * __FILE__. */
	const char *DbgFileName,
	/*! [in] The line number that the function was called, use the macro
	 * __LINE__. */
	int DbgLineNo,
	/*! [in] The message. */
	const char *msg)
{
	ThreadPoolStats stats;
	ThreadPoolGetStats(tp, &stats);
	CDBG_INFO(
		"%s\n"
		"High Jobs pending: %d\n"
		"Med Jobs Pending: %d\n"
		"Low Jobs Pending: %d\n"
		"Average wait in High Q in milliseconds: %lf\n"
		"Average wait in Med Q in milliseconds: %lf\n"
		"Average wait in Low Q in milliseconds: %lf\n"
		"Max Threads Used: %d\n"
		"Worker Threads: %d\n"
		"Persistent Threads: %d\n"
		"Idle Threads: %d\n"
		"Total Threads: %d\n"
		"Total Work Time: %lf\n"
		"Total Idle Time: %lf\n",
		msg,
		stats.currentJobsHQ,
		stats.currentJobsMQ,
		stats.currentJobsLQ,
		stats.avgWaitHQ,
		stats.avgWaitMQ,
		stats.avgWaitLQ,
		stats.maxThreads,
		stats.workerThreads,
		stats.persistentThreads,
		stats.idleThreads,
		stats.totalThreads,
		stats.totalWorkTime,
		stats.totalIdleTime);
}
#else
static UPNP_INLINE void PrintThreadPoolStats(ThreadPool *tp,
	const char *DbgFileName, int DbgLineNo, const char *msg)
{
	return;
	tp = tp;
	DbgFileName = DbgFileName;
	DbgLineNo = DbgLineNo;
	msg = msg;
}
#endif /* DEBUG */

int UpnpFinish(void)
{
#ifdef INCLUDE_CLIENT_APIS
	UpnpClient_Handle client_handle;
#endif
	struct Handle_Info *temp;

	if (UpnpSdkInit != 1)
		return UPNP_E_FINISH;
	CDBG_INFO("Inside UpnpFinish: UpnpSdkInit is %d\n", UpnpSdkInit);
	if (UpnpSdkInit == 1)
		CDBG_INFO(
			"UpnpFinish: UpnpSdkInit is ONE\n");
	PrintThreadPoolStats(&gSendThreadPool, __FILE__, __LINE__,
		"Send Thread Pool");
	PrintThreadPoolStats(&gRecvThreadPool, __FILE__, __LINE__,
		"Recv Thread Pool");
	PrintThreadPoolStats(&gMiniServerThreadPool, __FILE__, __LINE__,
		"MiniServer Thread Pool");
#ifdef INCLUDE_CLIENT_APIS
	switch (GetClientHandleInfo(&client_handle, &temp)) {
	case HND_CLIENT:
		UpnpUnRegisterClient(client_handle);
		break;
	default:
		break;
	}
#endif
	TimerThreadShutdown(&gTimerThread);
#if EXCLUDE_MINISERVER == 0
	StopMiniServer();
#endif
#if EXCLUDE_WEB_SERVER == 0
	web_server_destroy();
#endif
	ThreadPoolShutdown(&gMiniServerThreadPool);
	PrintThreadPoolStats(&gMiniServerThreadPool, __FILE__, __LINE__,
		"MiniServer Thread Pool");
	ThreadPoolShutdown(&gRecvThreadPool);
	PrintThreadPoolStats(&gSendThreadPool, __FILE__, __LINE__,
		"Send Thread Pool");
	ThreadPoolShutdown(&gSendThreadPool);
	PrintThreadPoolStats(&gRecvThreadPool, __FILE__, __LINE__,
		"Recv Thread Pool");
#ifdef INCLUDE_CLIENT_APIS
	ithread_mutex_destroy(&GlobalClientSubscribeMutex);
#endif
	ithread_rwlock_destroy(&GlobalHndRWLock);
	ithread_mutex_destroy(&gUUIDMutex);
	/* remove all virtual dirs */
	UpnpRemoveAllVirtualDirs();
	UpnpSdkInit = 0;
	CDBG_INFO("Exiting UpnpFinish: UpnpSdkInit is :%d:\n", UpnpSdkInit);
	DbgCloseLog();
	/* Clean-up ithread library resources */
	ithread_cleanup_library();

	return UPNP_E_SUCCESS;
}

unsigned short UpnpGetServerPort(void)
{
	if (UpnpSdkInit != 1)
		return 0u;

	return LOCAL_PORT_V4;
}

#ifdef UPNP_ENABLE_IPV6
unsigned short UpnpGetServerPort6(void)
{
	if (UpnpSdkInit != 1)
		return 0u;

	return LOCAL_PORT_V6;
}
#endif

char *UpnpGetServerIpAddress(void)
{
	if (UpnpSdkInit != 1)
		return NULL;

	return gIF_IPV4;
}

char *UpnpGetServerIp6Address(void)
{
	if (UpnpSdkInit != 1)
		return NULL;

	return gIF_IPV6;
}

char *UpnpGetServerUlaGuaIp6Address(void)
{
	if (UpnpSdkInit != 1)
		return NULL;

	return gIF_IPV6_ULA_GUA;
}

/*!
 * \brief Get a free handle.
 *
 * \return On success, an integer greater than zero or UPNP_E_OUTOF_HANDLE on
 * 	failure.
 */
static int GetFreeHandle()
{
	/* Handle 0 is not used as NULL translates to 0 when passed as a handle */
	int i = 1;

	while (i < NUM_HANDLE && HandleTable[i] != NULL)
		++i;
	if (i == NUM_HANDLE)
		return UPNP_E_OUTOF_HANDLE;
	else
		return i;
}

/*!
 * \brief Free handle.
 *
 * \return UPNP_E_SUCCESS if successful or UPNP_E_INVALID_HANDLE if not
 */
static int FreeHandle(
	/*! [in] Handle index. */
	int Upnp_Handle)
{
	int ret = UPNP_E_INVALID_HANDLE;

	CDBG_INFO(
		"FreeHandle: entering, Handle is %d\n", Upnp_Handle);
	if (Upnp_Handle < 1 || Upnp_Handle >= NUM_HANDLE) {
		CDBG_INFO(
			"FreeHandle: Handle %d is out of range\n",
			Upnp_Handle);
	} else if (HandleTable[Upnp_Handle] == NULL) {
		CDBG_INFO(
			"FreeHandle: HandleTable[%d] is NULL\n",
			Upnp_Handle);
	} else {
		free( HandleTable[Upnp_Handle] );
		HandleTable[Upnp_Handle] = NULL;
		ret = UPNP_E_SUCCESS;
	}
	CDBG_INFO(
		"FreeHandle: exiting, ret = %d.\n", ret);

	return ret;
}

#ifdef INCLUDE_CLIENT_APIS
int UpnpRegisterClient(Upnp_FunPtr Fun, const void *Cookie,
	UpnpClient_Handle *Hnd)
{
	struct Handle_Info *HInfo;

	if (UpnpSdkInit != 1)
		return UPNP_E_FINISH;
	CDBG_INFO(
		   "Inside UpnpRegisterClient \n");
	if (Fun == NULL || Hnd == NULL)
		return UPNP_E_INVALID_PARAM;

	HandleLock();
	if (UpnpSdkClientRegistered) {
		HandleUnlock();
		return UPNP_E_ALREADY_REGISTERED;
	}
	if ((*Hnd = GetFreeHandle()) == UPNP_E_OUTOF_HANDLE) {
		HandleUnlock();
		return UPNP_E_OUTOF_MEMORY;
	}
	HInfo = (struct Handle_Info *)malloc(sizeof(struct Handle_Info));
	if (HInfo == NULL) {
		HandleUnlock();
		return UPNP_E_OUTOF_MEMORY;
	}
	HInfo->HType = HND_CLIENT;
	HInfo->Callback = Fun;
	HInfo->Cookie = (void *)Cookie;
	HInfo->ClientSubList = NULL;
	ListInit(&HInfo->SsdpSearchList, NULL, NULL);
	HandleTable[*Hnd] = HInfo;
	UpnpSdkClientRegistered = 1;
	HandleUnlock();

	CDBG_INFO(
		   "Exiting UpnpRegisterClient \n");

	return UPNP_E_SUCCESS;
}
#endif /* INCLUDE_CLIENT_APIS */

#ifdef INCLUDE_CLIENT_APIS
int UpnpUnRegisterClient(UpnpClient_Handle Hnd)
{
	struct Handle_Info *HInfo;
	ListNode *node = NULL;
	SsdpSearchArg *searchArg = NULL;

	if (UpnpSdkInit != 1)
		return UPNP_E_FINISH;
	CDBG_INFO(
		   "Inside UpnpUnRegisterClient \n");

	HandleLock();
	if (!UpnpSdkClientRegistered) {
		HandleUnlock();
		return UPNP_E_INVALID_HANDLE;
	}
	HandleUnlock();

#if EXCLUDE_GENA == 0
	if (genaUnregisterClient(Hnd) != UPNP_E_SUCCESS)
		return UPNP_E_INVALID_HANDLE;
#endif
	HandleLock();
	switch (GetHandleInfo(Hnd, &HInfo)) {
	case HND_INVALID:
		HandleUnlock();
		return UPNP_E_INVALID_HANDLE;
	default:
		break;
	}
	/* clean up search list */
	node = ListHead(&HInfo->SsdpSearchList);
	while (node != NULL) {
		searchArg = (SsdpSearchArg *) node->item;
		if (searchArg) {
			free(searchArg->searchTarget);
			free(searchArg);
		}
		ListDelNode(&HInfo->SsdpSearchList, node, 0);
		node = ListHead(&HInfo->SsdpSearchList);
	}
	ListDestroy(&HInfo->SsdpSearchList, 0);
	FreeHandle(Hnd);
	UpnpSdkClientRegistered = 0;
	HandleUnlock();

	CDBG_INFO(
		   "Exiting UpnpUnRegisterClient \n");

	return UPNP_E_SUCCESS;
}
#endif /* INCLUDE_CLIENT_APIS */

/*******************************************************************************
 *
 *                                  SSDP interface
 *
 ******************************************************************************/


#if EXCLUDE_SSDP == 0
#ifdef INCLUDE_CLIENT_APIS


int UpnpSearchAsync(
	UpnpClient_Handle Hnd,
	int Mx,
	const char *Target_const,
	const void *Cookie_const )
{
    struct Handle_Info *SInfo = NULL;
    char *Target = ( char * )Target_const;
    int retVal;

    if( UpnpSdkInit != 1 ) {
        return UPNP_E_FINISH;
    }

    CDBG_INFO(
        "Inside UpnpSearchAsync\n" );

    HandleReadLock();
    switch( GetHandleInfo( Hnd, &SInfo ) ) {
    case HND_CLIENT:
        break;
    default:
        HandleUnlock();
        return UPNP_E_INVALID_HANDLE;
    }
    if( Mx < 1 )
        Mx = DEFAULT_MX;

    if( Target == NULL ) {
        HandleUnlock();
        return UPNP_E_INVALID_PARAM;
    }

    HandleUnlock();
    retVal = SearchByTarget( Mx, Target, ( void * )Cookie_const );
    if (retVal != 1)
        return retVal;

    CDBG_INFO(
        "Exiting UpnpSearchAsync \n" );

    return UPNP_E_SUCCESS;

}
#endif /* INCLUDE_CLIENT_APIS */
#endif


/*******************************************************************************
 *
 *                                  GENA interface
 *
 ******************************************************************************/


#if EXCLUDE_GENA == 0

#ifdef INCLUDE_CLIENT_APIS
int UpnpSubscribeAsync(
	UpnpClient_Handle Hnd,
	const char *EvtUrl_const,
	int TimeOut,
	Upnp_FunPtr Fun,
	const void *Cookie_const)
{
    struct Handle_Info *SInfo = NULL;
    struct UpnpNonblockParam *Param;
    char *EvtUrl = ( char * )EvtUrl_const;
    ThreadPoolJob job;

    memset(&job, 0, sizeof(job));

    if( UpnpSdkInit != 1 ) {
        return UPNP_E_FINISH;
    }

    CDBG_INFO(
        "Inside UpnpSubscribeAsync\n");

    HandleReadLock();
    switch( GetHandleInfo( Hnd, &SInfo ) ) {
    case HND_CLIENT:
        break;
    default:
        HandleUnlock();
        return UPNP_E_INVALID_HANDLE;
    }
    if( EvtUrl == NULL ) {
        HandleUnlock();
        return UPNP_E_INVALID_PARAM;
    }
    if( TimeOut != UPNP_INFINITE && TimeOut < 1 ) {
        HandleUnlock();
        return UPNP_E_INVALID_PARAM;
    }
    if( Fun == NULL ) {
        HandleUnlock();
        return UPNP_E_INVALID_PARAM;
    }
    HandleUnlock();

    Param = (struct UpnpNonblockParam *)
        malloc(sizeof (struct UpnpNonblockParam));
    if( Param == NULL ) {
        return UPNP_E_OUTOF_MEMORY;
    }
    memset( Param, 0, sizeof( struct UpnpNonblockParam ) );

    Param->FunName = SUBSCRIBE;
    Param->Handle = Hnd;
    strncpy( Param->Url, EvtUrl, sizeof( Param->Url ) - 1 );
    Param->TimeOut = TimeOut;
    Param->Fun = Fun;
    Param->Cookie = (void *)Cookie_const;

    TPJobInit(&job, (start_routine)UpnpThreadDistribution, Param);
    TPJobSetFreeFunction(&job, (free_routine)free);
    TPJobSetPriority(&job, MED_PRIORITY);
    if (ThreadPoolAdd(&gSendThreadPool, &job, NULL) != 0) {
	free(Param);
    }

    CDBG_INFO(
        "Exiting UpnpSubscribeAsync\n");

    return UPNP_E_SUCCESS;

}
#endif /* INCLUDE_CLIENT_APIS */


#ifdef INCLUDE_CLIENT_APIS
int UpnpSubscribe(
	UpnpClient_Handle Hnd,
	const char *EvtUrl_const,
	int *TimeOut,
	Upnp_SID SubsId)
{
	int retVal;
	struct Handle_Info *SInfo = NULL;
	UpnpString *EvtUrl = UpnpString_new();
	UpnpString *SubsIdTmp = UpnpString_new();
	
	CDBG_INFO( "Inside UpnpSubscribe\n");

	if (UpnpSdkInit != 1) {
		retVal = UPNP_E_FINISH;
		goto exit_function;
	}

	if (EvtUrl == NULL) {
		retVal = UPNP_E_OUTOF_MEMORY;
		goto exit_function;
	}
	if (EvtUrl_const == NULL) {
		retVal = UPNP_E_INVALID_PARAM;
		goto exit_function;
	}
	UpnpString_set_String(EvtUrl, EvtUrl_const);

	if (SubsIdTmp == NULL) {
		retVal = UPNP_E_OUTOF_MEMORY;
		goto exit_function;
	}
	if (SubsId == NULL) {
		retVal = UPNP_E_INVALID_PARAM;
		goto exit_function;
	}
	UpnpString_set_String(SubsIdTmp, SubsId);

	if (TimeOut == NULL) {
		retVal = UPNP_E_INVALID_PARAM;
		goto exit_function;
	}

	HandleReadLock();
	switch (GetHandleInfo(Hnd, &SInfo)) {
	case HND_CLIENT:
		break;
	default:
		HandleUnlock();
		retVal = UPNP_E_INVALID_HANDLE;
		goto exit_function;
	}
	HandleUnlock();

	retVal = genaSubscribe(Hnd, EvtUrl, TimeOut, SubsIdTmp);
	memset(SubsId, 0, sizeof(Upnp_SID));
	strncpy(SubsId, UpnpString_get_String(SubsIdTmp), sizeof(Upnp_SID) - 1);

exit_function:
	CDBG_INFO(
		"Exiting UpnpSubscribe, retVal=%d\n", retVal);

	UpnpString_delete(SubsIdTmp);
	UpnpString_delete(EvtUrl);

	return retVal;
}
#endif /* INCLUDE_CLIENT_APIS */


#ifdef INCLUDE_CLIENT_APIS
int UpnpUnSubscribe(UpnpClient_Handle Hnd, const Upnp_SID SubsId)
{
	struct Handle_Info *SInfo = NULL;
	int retVal;
	UpnpString *SubsIdTmp = UpnpString_new();

	CDBG_INFO( "Inside UpnpUnSubscribe\n");

	if (UpnpSdkInit != 1) {
		retVal = UPNP_E_FINISH;
		goto exit_function;
	}

	if (SubsIdTmp == NULL) {
		retVal = UPNP_E_OUTOF_MEMORY;
		goto exit_function;
	}
	if (SubsId == NULL) {
		retVal = UPNP_E_INVALID_PARAM;
		goto exit_function;
	}
	UpnpString_set_String(SubsIdTmp, SubsId);

	HandleReadLock();
	switch (GetHandleInfo(Hnd, &SInfo)) {
	case HND_CLIENT:
		break;
	default:
		HandleUnlock();
		retVal = UPNP_E_INVALID_HANDLE;
		goto exit_function;
	}
	HandleUnlock();

	retVal = genaUnSubscribe(Hnd, SubsIdTmp);

exit_function:
	CDBG_INFO(
		"Exiting UpnpUnSubscribe, retVal=%d\n", retVal);

	UpnpString_delete(SubsIdTmp);

	return retVal;
}
#endif /* INCLUDE_CLIENT_APIS */


#ifdef INCLUDE_CLIENT_APIS
int UpnpUnSubscribeAsync(
	UpnpClient_Handle Hnd,
	Upnp_SID SubsId,
	Upnp_FunPtr Fun,
	const void *Cookie_const)
{
	int retVal = UPNP_E_SUCCESS;
	ThreadPoolJob job;
	struct Handle_Info *SInfo = NULL;
	struct UpnpNonblockParam *Param;

	memset(&job, 0, sizeof(job));

	CDBG_INFO( "Inside UpnpUnSubscribeAsync\n");

	if (UpnpSdkInit != 1) {
		retVal = UPNP_E_FINISH;
		goto exit_function;
	}

	if (SubsId == NULL) {
		retVal = UPNP_E_INVALID_PARAM;
		goto exit_function;
	}
	if (Fun == NULL) {
		retVal = UPNP_E_INVALID_PARAM;
		goto exit_function;
	}

	HandleReadLock();
	switch (GetHandleInfo(Hnd, &SInfo)) {
	case HND_CLIENT:
		break;
	default:
		HandleUnlock();
		retVal = UPNP_E_INVALID_HANDLE;
		goto exit_function;
	}
	HandleUnlock();

	Param = (struct UpnpNonblockParam *)malloc(sizeof(struct UpnpNonblockParam));
	if (Param == NULL) {
		retVal = UPNP_E_OUTOF_MEMORY;
		goto exit_function;
	}
	memset( Param, 0, sizeof( struct UpnpNonblockParam ) );

	Param->FunName = UNSUBSCRIBE;
	Param->Handle = Hnd;
	strncpy( Param->SubsId, SubsId, sizeof( Param->SubsId ) - 1 );
	Param->Fun = Fun;
	Param->Cookie = (void *)Cookie_const;
	TPJobInit( &job, ( start_routine ) UpnpThreadDistribution, Param );
	TPJobSetFreeFunction( &job, ( free_routine ) free );
	TPJobSetPriority( &job, MED_PRIORITY );
	if (ThreadPoolAdd( &gSendThreadPool, &job, NULL ) != 0) {
		free(Param);
	}

exit_function:
	CDBG_INFO( "Exiting UpnpUnSubscribeAsync\n");

	return retVal;
}
#endif /* INCLUDE_CLIENT_APIS */


#ifdef INCLUDE_CLIENT_APIS
int UpnpRenewSubscription(
	UpnpClient_Handle Hnd,
	int *TimeOut,
	const Upnp_SID SubsId)
{
	struct Handle_Info *SInfo = NULL;
	int retVal;
	UpnpString *SubsIdTmp = UpnpString_new();

	CDBG_INFO( "Inside UpnpRenewSubscription\n");

	if (UpnpSdkInit != 1) {
		retVal = UPNP_E_FINISH;
		goto exit_function;
	}

	if (SubsIdTmp == NULL) {
		retVal = UPNP_E_OUTOF_MEMORY;
		goto exit_function;
	}
	if (SubsId == NULL) {
		retVal = UPNP_E_INVALID_PARAM;
		goto exit_function;
	}
	UpnpString_set_String(SubsIdTmp, SubsId);

	if (TimeOut == NULL) {
		retVal = UPNP_E_INVALID_PARAM;
		goto exit_function;
	}

	HandleReadLock();
	switch (GetHandleInfo(Hnd, &SInfo)) {
	case HND_CLIENT:
		break;
	default:
		HandleUnlock();
		retVal = UPNP_E_INVALID_HANDLE;
		goto exit_function;
	}
	HandleUnlock();

	retVal = genaRenewSubscription(Hnd, SubsIdTmp, TimeOut);

exit_function:
	CDBG_INFO(
		"Exiting UpnpRenewSubscription, retVal=%d\n", retVal);

	UpnpString_delete(SubsIdTmp);

	return retVal;
}
#endif /* INCLUDE_CLIENT_APIS */


#ifdef INCLUDE_CLIENT_APIS
int UpnpRenewSubscriptionAsync(
	UpnpClient_Handle Hnd,
	int TimeOut,
	Upnp_SID SubsId,
	Upnp_FunPtr Fun,
	const void *Cookie_const)
{
    ThreadPoolJob job;
    struct Handle_Info *SInfo = NULL;
    struct UpnpNonblockParam *Param;

    memset(&job, 0, sizeof(job));

    if( UpnpSdkInit != 1 ) {
        return UPNP_E_FINISH;
    }

    CDBG_INFO(
        "Inside UpnpRenewSubscriptionAsync\n");
    HandleReadLock();
    switch( GetHandleInfo( Hnd, &SInfo ) ) {
    case HND_CLIENT:
        break;
    default:
        HandleUnlock();
        return UPNP_E_INVALID_HANDLE;
    }
    if( TimeOut != UPNP_INFINITE && TimeOut < 1 ) {
        HandleUnlock();
        return UPNP_E_INVALID_PARAM;
    }
    if( SubsId == NULL ) {
        HandleUnlock();
        return UPNP_E_INVALID_PARAM;
    }
    if( Fun == NULL ) {
        HandleUnlock();
        return UPNP_E_INVALID_PARAM;
    }
    HandleUnlock();

    Param =
        ( struct UpnpNonblockParam * )
        malloc( sizeof( struct UpnpNonblockParam ) );
    if( Param == NULL ) {
        return UPNP_E_OUTOF_MEMORY;
    }
    memset(Param, 0, sizeof( struct UpnpNonblockParam ) );

    Param->FunName = RENEW;
    Param->Handle = Hnd;
    strncpy( Param->SubsId, SubsId, sizeof( Param->SubsId ) - 1 );
    Param->Fun = Fun;
    Param->Cookie = ( void * )Cookie_const;
    Param->TimeOut = TimeOut;

    TPJobInit( &job, ( start_routine ) UpnpThreadDistribution, Param );
    TPJobSetFreeFunction( &job, ( free_routine ) free );
    TPJobSetPriority( &job, MED_PRIORITY );
    if (ThreadPoolAdd( &gSendThreadPool, &job, NULL ) != 0) {
	free(Param);
    }

    CDBG_INFO(
        "Exiting UpnpRenewSubscriptionAsync\n");

    return UPNP_E_SUCCESS;
}
#endif /* INCLUDE_CLIENT_APIS */

#endif /* EXCLUDE_GENA == 0 */


/*******************************************************************************
 *
 *                                  SOAP interface
 *
 ******************************************************************************/


#if EXCLUDE_SOAP == 0
#ifdef INCLUDE_CLIENT_APIS
int UpnpSendActionAsync(
	UpnpClient_Handle Hnd,
	const char *ActionURL_const,
	const char *ServiceType_const,
	const char *DevUDN_const,
	Upnp_FunPtr Fun,
	const void *Cookie_const)
{
    ThreadPoolJob job;
    struct Handle_Info *SInfo = NULL;
    struct UpnpNonblockParam *Param;
    char *ActionURL = (char *)ActionURL_const;
    char *ServiceType = (char *)ServiceType_const;
    /* udn not used? */
    /*char *DevUDN = (char *)DevUDN_const;*/

    memset(&job, 0, sizeof(job));

    if(UpnpSdkInit != 1) {
	return UPNP_E_FINISH;
    }
    CDBG_INFO(
	    "Inside UpnpSendActionAsync\n");

    HandleReadLock();
    switch( GetHandleInfo( Hnd, &SInfo ) ) {
	case HND_CLIENT:
	    break;
	default:
	    HandleUnlock();
	    return UPNP_E_INVALID_HANDLE;
    }
    HandleUnlock();

    if( ActionURL == NULL ) {
	return UPNP_E_INVALID_PARAM;
    }
    if( ServiceType == NULL ||
	    Fun == NULL || DevUDN_const != NULL ) {
	return UPNP_E_INVALID_PARAM;
    }

    Param =
	( struct UpnpNonblockParam * )
	malloc( sizeof( struct UpnpNonblockParam ) );
    if( Param == NULL ) {
	return UPNP_E_OUTOF_MEMORY;
    }
    memset( Param, 0, sizeof( struct UpnpNonblockParam ) );

    Param->FunName = ACTION;
    Param->Handle = Hnd;
    strncpy( Param->Url, ActionURL, sizeof ( Param->Url ) - 1 );
    strncpy( Param->ServiceType, ServiceType,
	    sizeof ( Param->ServiceType ) - 1 );

    Param->Cookie = ( void * )Cookie_const;
    Param->Fun = Fun;

    TPJobInit( &job, ( start_routine ) UpnpThreadDistribution, Param );
    TPJobSetFreeFunction( &job, ( free_routine ) free );
    TPJobSetPriority( &job, MED_PRIORITY );

    if (ThreadPoolAdd( &gSendThreadPool, &job, NULL ) != 0) {
	free(Param);
    }

    CDBG_INFO(
	    "Exiting UpnpSendActionAsync \n");
    return UPNP_E_SUCCESS;
}

#endif /* INCLUDE_CLIENT_APIS */
#endif /* EXCLUDE_SOAP */


/*******************************************************************************
 *
 *                             Client API
 *
 ******************************************************************************/

int UpnpOpenHttpPost(
	const char *url,
	void **handle,
	const char *contentType,
	int contentLength,
	int timeout)
{
	return http_OpenHttpPost(
		url, handle, contentType, contentLength, timeout);
}


int UpnpWriteHttpPost(
	void *handle,
	char *buf,
	size_t *size,
	int timeout)
{
	return http_WriteHttpPost(handle, buf, size, timeout);
}


int UpnpCloseHttpPost(
	void *handle,
	int *httpStatus,
	int timeout)
{
	return http_CloseHttpPost(handle, httpStatus, timeout);
}


int UpnpOpenHttpGet(
	const char *url_str,
	void **Handle,
	char **contentType,
	int *contentLength,
	int *httpStatus,
	int timeout)
{
	return http_OpenHttpGet(
		url_str, Handle, contentType, contentLength, httpStatus, timeout);
}


int UpnpOpenHttpGetProxy(
	const char *url_str,
	const char *proxy_str,
	void **Handle,
	char **contentType,
	int *contentLength,
	int *httpStatus,
	int timeout)
{
	return http_OpenHttpGetProxy(
		url_str, proxy_str, Handle, contentType, contentLength,
		httpStatus, timeout);
}


int UpnpOpenHttpGetEx(
	const char *url_str,
	void **Handle,
	char **contentType,
	int *contentLength,
	int *httpStatus,
	int lowRange,
	int highRange,
	int timeout)
{
	return http_OpenHttpGetEx(
		url_str, Handle, contentType, contentLength, httpStatus,
		lowRange, highRange, timeout);
}


int UpnpCancelHttpGet(void *Handle)
{
	return http_CancelHttpGet(Handle);
}


int UpnpCloseHttpGet(void *Handle)
{
	return http_CloseHttpGet(Handle);
}


int UpnpReadHttpGet(void *Handle, char *buf, size_t *size, int timeout)
{
	return http_ReadHttpGet(Handle, buf, size, timeout);
}


int UpnpHttpGetProgress(void *Handle, size_t *length, size_t *total)
{
	return http_HttpGetProgress(Handle, length, total);
}


int UpnpDownloadUrlItem(const char *url, char **outBuf, char *contentType)
{
	int ret_code;
	size_t dummy;

	if (url == NULL || outBuf == NULL || contentType == NULL)
		return UPNP_E_INVALID_PARAM;
	ret_code = http_Download(url, HTTP_DEFAULT_TIMEOUT, outBuf, &dummy,
				 contentType);
	if (ret_code > 0)
		/* error reply was received */
		ret_code = UPNP_E_INVALID_URL;

	return ret_code;
}

int UpnpGetIfInfo(const char *IfName)
{
	char szBuffer[MAX_INTERFACES * sizeof(struct ifreq)];
	struct ifconf ifConf;
	struct ifreq ifReq;
	FILE *inet6_procfd;
	size_t i;
	int LocalSock;
	struct in6_addr v6_addr;
	unsigned if_idx;
	char addr6[8][5];
	char buf[INET6_ADDRSTRLEN];
	int ifname_found = 0;
	int valid_addr_found = 0;

	/* Copy interface name, if it was provided. */
	if (IfName != NULL) {
		if (strlen(IfName) > sizeof(gIF_NAME))
			return UPNP_E_INVALID_INTERFACE;

		memset(gIF_NAME, 0, sizeof(gIF_NAME));
		strncpy(gIF_NAME, IfName, sizeof(gIF_NAME) - 1);
		ifname_found = 1;
	}
	/* Create an unbound datagram socket to do the SIOCGIFADDR ioctl on.  */
	if ((LocalSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
		CDBG_INFO(
			   "Can't create addrlist socket\n");
		return UPNP_E_INIT;
	}
	/* Get the interface configuration information...  */
	ifConf.ifc_len = (int)sizeof szBuffer;
	ifConf.ifc_ifcu.ifcu_buf = (caddr_t) szBuffer;

	if (ioctl(LocalSock, SIOCGIFCONF, &ifConf) < 0) {
		CDBG_INFO(
			   "DiscoverInterfaces: SIOCGIFCONF returned error\n");
		close(LocalSock);
		return UPNP_E_INIT;
	}
	/* Cycle through the list of interfaces looking for IP addresses.  */
	for (i = (size_t)0; i < (size_t)ifConf.ifc_len;) {
		struct ifreq *pifReq =
		    (struct ifreq *)((caddr_t) ifConf.ifc_req + i);
		i += sizeof *pifReq;
		/* See if this is the sort of interface we want to deal with. */
		memset(ifReq.ifr_name, 0, sizeof(ifReq.ifr_name));
		strncpy(ifReq.ifr_name, pifReq->ifr_name,
			sizeof(ifReq.ifr_name) - 1);
		if (ioctl(LocalSock, SIOCGIFFLAGS, &ifReq) < 0) {
			CDBG_INFO(
				   "Can't get interface flags for %s:\n",
				   ifReq.ifr_name);
		}
		/* Skip LOOPBACK interfaces, DOWN interfaces and interfaces that  */
		/* don't support MULTICAST. */
		if ((ifReq.ifr_flags & IFF_LOOPBACK)
		    || (!(ifReq.ifr_flags & IFF_UP))
		    || (!(ifReq.ifr_flags & IFF_MULTICAST))) {
			continue;
		}
		if (ifname_found == 0) {
			/* We have found a valid interface name. Keep it. */
			memset(gIF_NAME, 0, sizeof(gIF_NAME));
			strncpy(gIF_NAME, pifReq->ifr_name, sizeof(gIF_NAME) - 1);
			ifname_found = 1;
		} else {
			if (strncmp
			    (gIF_NAME, pifReq->ifr_name,
			     sizeof(gIF_NAME)) != 0) {
				/* This is not the interface we're looking for. */
				continue;
			}
		}
		/* Check address family. */
		if (pifReq->ifr_addr.sa_family == AF_INET) {
			/* Copy interface name, IPv4 address and interface index. */
			memset(gIF_NAME, 0, sizeof(gIF_NAME));
			strncpy(gIF_NAME, pifReq->ifr_name, sizeof(gIF_NAME) - 1);
			inet_ntop(AF_INET,
				  &((struct sockaddr_in *)&pifReq->ifr_addr)->
				  sin_addr, gIF_IPV4, sizeof(gIF_IPV4));
			gIF_INDEX = if_nametoindex(gIF_NAME);
			valid_addr_found = 1;
			break;
		} else {
			/* Address is not IPv4 */
			ifname_found = 0;
		}
	}
	close(LocalSock);
	/* Failed to find a valid interface, or valid address. */
	if (ifname_found == 0 || valid_addr_found == 0) {
		CDBG_INFO(
			   "Failed to find an adapter with valid IP addresses for use.\n");

		return UPNP_E_INVALID_INTERFACE;
	}
	/* Try to get the IPv6 address for the same interface  */
	/* from "/proc/net/if_inet6", if possible. */
	inet6_procfd = fopen("/proc/net/if_inet6", "r");
	if (inet6_procfd) {
		while (fscanf(inet6_procfd,
			      "%4s%4s%4s%4s%4s%4s%4s%4s %02x %*02x %*02x %*02x %*20s\n",
			      addr6[0], addr6[1], addr6[2], addr6[3],
			      addr6[4], addr6[5], addr6[6], addr6[7],
			      &if_idx) != EOF) {
			/* Get same interface as IPv4 address retrieved. */
			if (gIF_INDEX == if_idx) {
				snprintf(buf, sizeof(buf),
					 "%s:%s:%s:%s:%s:%s:%s:%s", addr6[0],
					 addr6[1], addr6[2], addr6[3], addr6[4],
					 addr6[5], addr6[6], addr6[7]);
				/* Validate formed address and check for link-local. */
				if (inet_pton(AF_INET6, buf, &v6_addr) > 0) {
					if (IN6_IS_ADDR_ULA(&v6_addr)) {
						/* Got valid IPv6 ula. */
						memset(gIF_IPV6_ULA_GUA, 0,
							sizeof(gIF_IPV6_ULA_GUA));
						strncpy(gIF_IPV6_ULA_GUA, buf,
							sizeof
							(gIF_IPV6_ULA_GUA) - 1);
					} else if (IN6_IS_ADDR_GLOBAL(&v6_addr)
						   && strlen(gIF_IPV6_ULA_GUA)
						   == (size_t)0) {
						/* got a GUA, should store it while no ULA is found */
						memset(gIF_IPV6_ULA_GUA, 0,
                                                        sizeof(gIF_IPV6_ULA_GUA));
						strncpy(gIF_IPV6_ULA_GUA, buf,
							sizeof
							(gIF_IPV6_ULA_GUA) - 1);
					} else
					    if (IN6_IS_ADDR_LINKLOCAL(&v6_addr)
						&& strlen(gIF_IPV6) == (size_t)0) {
						/* got a Link local IPv6 address. */
						memset(gIF_IPV6, 0,
                                                        sizeof(gIF_IPV6));
						strncpy(gIF_IPV6, buf,
							sizeof(gIF_IPV6) - 1);
					}
				}
			}
		}
		fclose(inet6_procfd);
	}
	CDBG_INFO(
		   "Interface name=%s, index=%d, v4=%s, v6=%s, ULA or GUA v6=%s\n",
		   gIF_NAME, gIF_INDEX, gIF_IPV4, gIF_IPV6, gIF_IPV6_ULA_GUA);

	return UPNP_E_SUCCESS;
}


/*!
 * \brief Schedule async functions in threadpool.
 */
#ifdef INCLUDE_CLIENT_APIS
void UpnpThreadDistribution(struct UpnpNonblockParam *Param)
{
	/*int errCode = 0;*/

	CDBG_INFO(
		"Inside UpnpThreadDistribution \n");

	switch (Param->FunName) {
#if EXCLUDE_GENA == 0
	case SUBSCRIBE: {
		struct Upnp_Event_Subscribe Evt;
		UpnpString *Sid = UpnpString_new();
		UpnpString *Url = UpnpString_new();
		UpnpString_set_String(Url, Param->Url);
		memset(&Evt, 0, sizeof(Evt));
		Evt.ErrCode = genaSubscribe(
			Param->Handle,
			Url,
			(int *)&Param->TimeOut,
			Sid);
		strncpy(Evt.PublisherUrl, Param->Url,
			sizeof(Evt.PublisherUrl) - 1);
		Evt.TimeOut = Param->TimeOut;
		strncpy((char *)Evt.Sid, UpnpString_get_String(Sid),
			sizeof(Evt.Sid) - 1);
		Param->Fun(UPNP_EVENT_SUBSCRIBE_COMPLETE, &Evt, Param->Cookie);
		UpnpString_delete(Sid);
		UpnpString_delete(Url);
		free(Param);
		break;
	}
	case UNSUBSCRIBE: {
		struct Upnp_Event_Subscribe Evt;
		UpnpString *Sid = UpnpString_new();
		UpnpString_set_String(Sid, Param->SubsId);
		memset(&Evt, 0, sizeof(Evt));
		Evt.ErrCode = genaUnSubscribe(
			Param->Handle,
			Sid);
		strncpy((char *)Evt.Sid, UpnpString_get_String(Sid),
			sizeof(Evt.Sid) - 1);
		strncpy(Evt.PublisherUrl, "", sizeof(Evt.PublisherUrl) - 1);
		Evt.TimeOut = 0;
		Param->Fun(UPNP_EVENT_UNSUBSCRIBE_COMPLETE, &Evt, Param->Cookie);
		UpnpString_delete(Sid);
		free(Param);
		break;
	}
	case RENEW: {
		struct Upnp_Event_Subscribe Evt;
		UpnpString *Sid = UpnpString_new();
		UpnpString_set_String(Sid, Param->SubsId);
		memset(&Evt, 0, sizeof(Evt));
		Evt.ErrCode = genaRenewSubscription(
			Param->Handle,
			Sid,
			&Param->TimeOut);
		Evt.TimeOut = Param->TimeOut;
		strncpy((char *)Evt.Sid, UpnpString_get_String(Sid),
			sizeof(Evt.Sid) - 1);
		Param->Fun(UPNP_EVENT_RENEWAL_COMPLETE, &Evt, Param->Cookie);
		UpnpString_delete(Sid);
		free(Param);
		break;
	}
#endif /* EXCLUDE_GENA == 0 */
#if EXCLUDE_SOAP == 0
	case ACTION: {
		SoapSendAction(
			Param->Url,
			Param->ServiceType);
		Param->Fun(UPNP_CONTROL_ACTION_COMPLETE, NULL, Param->Cookie);
		free(Param);
		break;
	}
#endif /* EXCLUDE_SOAP == 0 */
	default:
		break;
	}

	CDBG_INFO(
		"Exiting UpnpThreadDistribution\n");
}
#endif /* INCLUDE_CLIENT_APIS */


/*!
 * \brief Get callback function ptr from a handle.
 *
 * \return Upnp_FunPtr
 */
Upnp_FunPtr GetCallBackFn(UpnpClient_Handle Hnd)
{
	return ((struct Handle_Info *)HandleTable[Hnd])->Callback;
}


/* Assumes at most one client */
Upnp_Handle_Type GetClientHandleInfo(
	UpnpClient_Handle *client_handle_out,
	struct Handle_Info **HndInfo)
{
	Upnp_Handle_Type ret = HND_CLIENT;
	UpnpClient_Handle client;

	switch (GetHandleInfo(1, HndInfo)) {
	case HND_CLIENT:
		client = 1;
		break;
	default:
		switch (GetHandleInfo(2, HndInfo)) {
		case HND_CLIENT:
			client = 2;
			break;
		default:
			client = -1;
			ret = HND_INVALID;
		}
	}

	*client_handle_out = client;
	return ret;
}


Upnp_Handle_Type GetDeviceHandleInfo(
	int AddressFamily,
	UpnpDevice_Handle *device_handle_out,
	struct Handle_Info **HndInfo)
{
	*device_handle_out = -1;
	return HND_INVALID;
}


Upnp_Handle_Type GetHandleInfo(
	UpnpClient_Handle Hnd,
	struct Handle_Info **HndInfo)
{
	Upnp_Handle_Type ret = HND_INVALID;

	CDBG_INFO("GetHandleInfo: entering, Handle is %d\n", Hnd);

	if (Hnd < 1 || Hnd >= NUM_HANDLE) {
		CDBG_INFO("GetHandleInfo: Handle out of range\n");
	} else if (HandleTable[Hnd] == NULL) {
		CDBG_INFO("GetHandleInfo: HandleTable[%d] is NULL\n",
			Hnd);
	} else if (HandleTable[Hnd] != NULL) {
		*HndInfo = (struct Handle_Info *)HandleTable[Hnd];
		ret = ((struct Handle_Info *)*HndInfo)->HType;
	}

	CDBG_INFO("GetHandleInfo: exiting\n");

	return ret;
}


int PrintHandleInfo(UpnpClient_Handle Hnd)
{
    struct Handle_Info * HndInfo;
    if (HandleTable[Hnd] != NULL) {
        HndInfo = HandleTable[Hnd];
            CDBG_INFO("Printing information for Handle_%d\n", Hnd);
            CDBG_INFO("HType_%d\n", HndInfo->HType);
    } else {
        return UPNP_E_INVALID_HANDLE;
    }

    return UPNP_E_SUCCESS;
}


int getlocalhostname(char *out, size_t out_len)
{
	int ret = UPNP_E_SUCCESS;
	char tempstr[INET_ADDRSTRLEN];
	const char *p = NULL;

#ifdef WIN32
	struct hostent *h = NULL;
	struct sockaddr_in LocalAddr;

	memset(&LocalAddr, 0, sizeof(LocalAddr));

	gethostname(out, out_len);
	h = gethostbyname(out);
	if (h != NULL) {
		memcpy(&LocalAddr.sin_addr, h->h_addr_list[0], 4);
		p = inet_ntop(AF_INET, &LocalAddr.sin_addr, tempstr, sizeof(tempstr));
		if (p) {
			strncpy(out, p, out_len);
		} else {
			CDBG_INFO(
				"getlocalhostname: inet_ntop returned error\n" );
			ret = UPNP_E_INIT;
		}
	} else {
		CDBG_INFO("getlocalhostname: gethostbyname returned error\n" );
		ret = UPNP_E_INIT;
	}

#elif (defined(BSD) && BSD >= 199306) || defined(__FreeBSD_kernel__)
	struct ifaddrs *ifap, *ifa;

	if (getifaddrs(&ifap) != 0) {
		CDBG_INFO("DiscoverInterfaces: getifaddrs() returned error\n");
		return UPNP_E_INIT;
	}

	/* cycle through available interfaces */
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		/* Skip loopback, point-to-point and down interfaces, 
		 * except don't skip down interfaces
		 * if we're trying to get a list of configurable interfaces. */
		if ((ifa->ifa_flags & IFF_LOOPBACK) ||
		    (!( ifa->ifa_flags & IFF_UP))) {
			continue;
		}
		if (ifa->ifa_addr->sa_family == AF_INET) {
			/* We don't want the loopback interface. */
			if (((struct sockaddr_in *)(ifa->ifa_addr))->sin_addr.s_addr ==
			    htonl(INADDR_LOOPBACK)) {
				continue;
			}
			p = inet_ntop(AF_INET,
				&((struct sockaddr_in *)(ifa->ifa_addr))->sin_addr,
				tempstr, sizeof(tempstr));
			if (p) {
				strncpy(out, p, out_len);
			} else {
				CDBG_INFO("getlocalhostname: inet_ntop returned error\n");
				ret = UPNP_E_INIT;
			}
			CDBG_INFO("Inside getlocalhostname: after strncpy %s\n", out);
			break;
		}
	}
	freeifaddrs(ifap);

	ret = ifa ? UPNP_E_SUCCESS : UPNP_E_INIT;
#else
	char szBuffer[MAX_INTERFACES * sizeof (struct ifreq)];
	struct ifconf ifConf;
	struct ifreq ifReq;
	int nResult;
	long unsigned int i;
	int LocalSock;
	struct sockaddr_in LocalAddr;
	int j = 0;

	/* purify */
	memset(&ifConf,  0, sizeof(ifConf));
	memset(&ifReq,   0, sizeof(ifReq));
	memset(szBuffer, 0, sizeof(szBuffer));
	memset(&LocalAddr, 0, sizeof(LocalAddr));

	/* Create an unbound datagram socket to do the SIOCGIFADDR ioctl on.  */
	LocalSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (LocalSock == INVALID_SOCKET) {
		CDBG_INFO(
			"Can't create addrlist socket\n");
		return UPNP_E_INIT;
	}
	/* Get the interface configuration information... */
	ifConf.ifc_len = (int)sizeof szBuffer;
	ifConf.ifc_ifcu.ifcu_buf = (caddr_t) szBuffer;
	nResult = ioctl(LocalSock, SIOCGIFCONF, &ifConf);
	if (nResult < 0) {
		CDBG_INFO(
			"DiscoverInterfaces: SIOCGIFCONF returned error\n");
		close(LocalSock);
		return UPNP_E_INIT;
	}

	/* Cycle through the list of interfaces looking for IP addresses. */
	for (i = 0lu; i < (long unsigned int)ifConf.ifc_len && j < DEFAULT_INTERFACE; ) {
		struct ifreq *pifReq =
			(struct ifreq *)((caddr_t)ifConf.ifc_req + i);
		i += sizeof *pifReq;
		/* See if this is the sort of interface we want to deal with. */
		memset(ifReq.ifr_name, 0, sizeof(ifReq.ifr_name));
		strncpy(ifReq.ifr_name, pifReq->ifr_name,
			sizeof(ifReq.ifr_name) - 1);
		if (ioctl(LocalSock, SIOCGIFFLAGS, &ifReq) < 0) {
			CDBG_INFO(
				"Can't get interface flags for %s:\n",
				ifReq.ifr_name);
		}
		/* Skip loopback, point-to-point and down interfaces,
		 * except don't skip down interfaces
		 * if we're trying to get a list of configurable interfaces. */
		if ((ifReq.ifr_flags & IFF_LOOPBACK) ||
		    (!(ifReq.ifr_flags & IFF_UP))) {
			continue;
		}
		if (pifReq->ifr_addr.sa_family == AF_INET) {
			/* Get a pointer to the address...*/
			memcpy(&LocalAddr, &pifReq->ifr_addr,
				sizeof pifReq->ifr_addr);
			/* We don't want the loopback interface. */
			if (LocalAddr.sin_addr.s_addr ==
			    htonl(INADDR_LOOPBACK)) {
				continue;
			}
		}
		/* increment j if we found an address which is not loopback
		 * and is up */
		j++;
	}
	close(LocalSock);

	p = inet_ntop(AF_INET, &LocalAddr.sin_addr, tempstr, sizeof(tempstr));
	if (p) {
		strncpy(out, p, out_len);
	} else {
		CDBG_INFO("getlocalhostname: inet_ntop returned error\n" );
		ret = UPNP_E_INIT;
	}
	CDBG_INFO("Inside getlocalhostname: after strncpy %s\n", out);
#endif
	return ret;
}

int UpnpAddVirtualDir(const char *newDirName)
{
    virtualDirList *pNewVirtualDir;
    virtualDirList *pLast;
    virtualDirList *pCurVirtualDir;
    char dirName[NAME_SIZE];

    memset( dirName, 0, sizeof( dirName ) );
    if( UpnpSdkInit != 1 ) {
        /* SDK is not initialized */
        return UPNP_E_FINISH;
    }

    if( ( newDirName == NULL ) || ( strlen( newDirName ) == ( size_t ) 0 ) ) {
        return UPNP_E_INVALID_PARAM;
    }

    if( *newDirName != '/' ) {
        if (strlen(newDirName) > sizeof(dirName) - 2)
            return UPNP_E_INVALID_PARAM;
        dirName[0] = '/';
        strncpy( dirName + 1, newDirName, sizeof( dirName ) - 2 );
    } else {
        if (strlen(newDirName) > sizeof(dirName) - 1)
            return UPNP_E_INVALID_PARAM;
        strncpy( dirName, newDirName, sizeof( dirName ) - 1 );
    }

    pCurVirtualDir = pVirtualDirList;
    while( pCurVirtualDir != NULL ) {
        /* already has this entry */
        if( strcmp( pCurVirtualDir->dirName, dirName ) == 0 ) {
            return UPNP_E_SUCCESS;
        }

        pCurVirtualDir = pCurVirtualDir->next;
    }

    pNewVirtualDir =
        ( virtualDirList * ) malloc( sizeof( virtualDirList ) );
    if( pNewVirtualDir == NULL ) {
        return UPNP_E_OUTOF_MEMORY;
    }
    pNewVirtualDir->next = NULL;
    memset( pNewVirtualDir->dirName, 0, sizeof( pNewVirtualDir->dirName ) );
    strncpy( pNewVirtualDir->dirName, dirName,
	sizeof( pNewVirtualDir->dirName ) - 1);
    *( pNewVirtualDir->dirName + strlen( dirName ) ) = 0;

    if( pVirtualDirList == NULL ) { /* first virtual dir */
        pVirtualDirList = pNewVirtualDir;
    } else {
        pLast = pVirtualDirList;
        while( pLast->next != NULL ) {
            pLast = pLast->next;
        }
        pLast->next = pNewVirtualDir;
    }

    return UPNP_E_SUCCESS;
}


int UpnpRemoveVirtualDir(const char *dirName)
{
    virtualDirList *pPrev;
    virtualDirList *pCur;
    int found = 0;

    if( UpnpSdkInit != 1 ) {
        return UPNP_E_FINISH;
    }

    if( dirName == NULL ) {
        return UPNP_E_INVALID_PARAM;
    }

    if( pVirtualDirList == NULL ) {
        return UPNP_E_INVALID_PARAM;
    }
    /* Handle the special case where the directory that we are */
    /* removing is the first in the list. */
    if (strcmp( pVirtualDirList->dirName, dirName ) == 0)
    {
        pPrev = pVirtualDirList;
        pVirtualDirList = pVirtualDirList->next;
        free( pPrev );
        return UPNP_E_SUCCESS;
    }

    pCur = pVirtualDirList->next;
    pPrev = pVirtualDirList;

    while( pCur != NULL ) {
        if( strcmp( pCur->dirName, dirName ) == 0 ) {
            pPrev->next = pCur->next;
            free( pCur );
            found = 1;
            break;
        } else {
            pPrev = pCur;
            pCur = pCur->next;
        }
    }

    if( found == 1 )
        return UPNP_E_SUCCESS;
    else
        return UPNP_E_INVALID_PARAM;
}


void UpnpRemoveAllVirtualDirs(void)
{
    virtualDirList *pCur;
    virtualDirList *pNext;

    if( UpnpSdkInit != 1 ) {
        return;
    }

    pCur = pVirtualDirList;

    while( pCur != NULL ) {
        pNext = pCur->next;
        free( pCur );

        pCur = pNext;
    }

    pVirtualDirList = NULL;
}


int UpnpEnableWebserver(int enable)
{
    int retVal = UPNP_E_SUCCESS;

    if( UpnpSdkInit != 1 ) {
        return UPNP_E_FINISH;
    }

    switch ( enable ) {
        default:
            retVal = UPNP_E_INVALID_PARAM;
    }

    return retVal;
}


/*!
 * \brief Checks if the webserver is enabled or disabled. 
 *
 * \return 1, if webserver is enabled or 0, if webserver is disabled.
 */
int UpnpIsWebserverEnabled(void)
{
	if (UpnpSdkInit != 1) {
		return 0;
	}

	return bWebServerState == (WebServerState)WEB_SERVER_ENABLED;
}

int UpnpSetVirtualDirCallbacks(struct UpnpVirtualDirCallbacks *callbacks)
{
	int ret = 0;

	if( UpnpSdkInit != 1 ) {
		/* SDK is not initialized */
		return UPNP_E_FINISH;
	}

	if( callbacks == NULL )
		return UPNP_E_INVALID_PARAM;

	ret = UpnpVirtualDir_set_GetInfoCallback(callbacks->get_info) == UPNP_E_SUCCESS
	   && UpnpVirtualDir_set_OpenCallback(callbacks->open) == UPNP_E_SUCCESS
	   && UpnpVirtualDir_set_ReadCallback(callbacks->read) == UPNP_E_SUCCESS
	   && UpnpVirtualDir_set_WriteCallback(callbacks->write) == UPNP_E_SUCCESS
	   && UpnpVirtualDir_set_SeekCallback(callbacks->seek) == UPNP_E_SUCCESS
	   && UpnpVirtualDir_set_CloseCallback(callbacks->close) == UPNP_E_SUCCESS;

	return ret ? UPNP_E_SUCCESS : UPNP_E_INVALID_PARAM;
}

int UpnpVirtualDir_set_GetInfoCallback(VDCallback_GetInfo callback)
{
	int ret = UPNP_E_SUCCESS;
	if (!callback) {
	        ret = UPNP_E_INVALID_PARAM;
	} else {
		virtualDirCallback.get_info = callback;
	}

	return ret;
}


int UpnpVirtualDir_set_OpenCallback(VDCallback_Open callback)
{
	int ret = UPNP_E_SUCCESS;
	if (!callback) {
	        ret = UPNP_E_INVALID_PARAM;
	} else {
		virtualDirCallback.open = callback;
	}

	return ret;
}


int UpnpVirtualDir_set_ReadCallback(VDCallback_Read callback)
{
	int ret = UPNP_E_SUCCESS;
	if (!callback) {
	        ret = UPNP_E_INVALID_PARAM;
	} else {
		virtualDirCallback.read = callback;
	}

	return ret;
}


int UpnpVirtualDir_set_WriteCallback(VDCallback_Write callback)
{
	int ret = UPNP_E_SUCCESS;
	if (!callback) {
	        ret = UPNP_E_INVALID_PARAM;
	} else {
		virtualDirCallback.write = callback;
	}

	return ret;
}


int UpnpVirtualDir_set_SeekCallback(VDCallback_Seek callback)
{
	int ret = UPNP_E_SUCCESS;
	if (!callback) {
	        ret = UPNP_E_INVALID_PARAM;
	} else {
		virtualDirCallback.seek = callback;
	}

	return ret;
}


int UpnpVirtualDir_set_CloseCallback(VDCallback_Close callback)
{
	int ret = UPNP_E_SUCCESS;
	if (!callback) {
	        ret = UPNP_E_INVALID_PARAM;
	} else {
		virtualDirCallback.close = callback;
	}

	return ret;
}

int UpnpSetContentLength(UpnpClient_Handle Hnd, size_t contentLength)
{
	int errCode = UPNP_E_SUCCESS;
	struct Handle_Info *HInfo = NULL;

	do {
		if (UpnpSdkInit != 1) {
			errCode = UPNP_E_FINISH;
			break;
		}

		HandleLock();

		switch (GetHandleInfo(Hnd, &HInfo)) {
		case HND_DEVICE:
			break;
		default:
			HandleUnlock();
		        return UPNP_E_INVALID_HANDLE;
		}
		if (contentLength > MAX_SOAP_CONTENT_LENGTH) {
			errCode = UPNP_E_OUTOF_BOUNDS;
			break;
		}
		g_maxContentLength = contentLength;
	} while (0);

	HandleUnlock();
	return errCode;
}

int UpnpSetMaxContentLength(size_t contentLength)
{
	int errCode = UPNP_E_SUCCESS;

	do {
		if (UpnpSdkInit != 1) {
			errCode = UPNP_E_FINISH;
			break;
		}
		g_maxContentLength = contentLength;
	} while(0);

	return errCode;
}

/* @} UPnPAPI */
