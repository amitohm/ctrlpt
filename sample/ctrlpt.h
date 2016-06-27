#ifndef UPNP_TV_CTRLPT_H
#define UPNP_TV_CTRLPT_H

/**************************************************************************
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
 **************************************************************************/

/*!
 * \addtogroup UpnpSamples
 *
 * @{
 *
 * \name Contro Point Sample API
 *
 * @{
 *
 * \file
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "upnp.h"
#include "debug.h"
#include "UpnpString.h"

#define TV_SERVICE_SERVCOUNT	1
#define TV_SERVICE_CONTROL	0

#define TV_CONTROL_VARCOUNT	2
#define TV_CONTROL_POWER	0
#define TV_CONTROL_CHANNEL	1

#define TV_MAX_VAL_LEN		5

#define SUCCESS		0
#define ERROR		(-1)
#define WARNING		1

/* This should be the maximum VARCOUNT from above */
#define TV_MAXVARS		TV_CONTROL_VARCOUNT

extern struct DeviceNode *GlobalDeviceList;

typedef enum {
    CMDVAL_RESERVED = 0,
    CMDVAL_GETAPLIST,           //1
    CMDVAL_CONNECTTOAP,         //2
    CMDVAL_CLOSEAP,             //3
    CMDVAL_GETDEVINFO,          //4
    CMDVAL_SETNAME,             //5
    CMDVAL_RESET,               //6
    CMDVAL_REBOOT,              //7
    CMDVAL_SUBSCRIBE,           //8
    CMDVAL_UNSUBSCRIBE,         //9
    CMDVAL_SETSWITCHNAME,       //10
    CMDVAL_SETSWITCHTYPE,       //11
    CMDVAL_SETSWITCHVAL,        //12
    CMDVAL_MAX
} commandValue_t;

/* Used to identify default icon for a particular switch */
typedef enum {
    SWITCHTYPE_SWITCH,  //Default
    SWITCHTYPE_FAN,
    SWITCHTYPE_BULB,
    SWITCHTYPE_SOCKET,  //3 PIN connector for appliances
    SWITCHTYPE_MISC,    //Unknown devices
    SWITCHTYPE_MAX
} SwitchType;

struct OhmDevice {
    char UID[250];
    char DeviceType[250];
    char Location[250];
    int  AdvrTimeOut;
};

struct DeviceNode {
    struct OhmDevice device;
    struct DeviceNode *next;
};

extern ithread_mutex_t DeviceListMutex;

extern UpnpClient_Handle ctrlpt_handle;

void	CtrlPointPrintHelp(void);
int	CtrlPointDeleteNode(struct DeviceNode *);
int	CtrlPointRemoveDevice(const char *);
int	CtrlPointRemoveAll(void);
int	CtrlPointRefresh(void);

int	CtrlPointSendAction(int, int, const char *);
int	CtrlPointSendActionNumericArg(int devnum, int service, const char *actionName, const char *paramName, int paramValue);
int	CtrlPointSendPowerOn(int devnum);
int	CtrlPointSendPowerOff(int devnum);
int 	CtrlPointSendGetAPList(int devnum);

int	CtrlPointGetDevice(int, struct DeviceNode **);
int	CtrlPointPrintList(void);
int	CtrlPointPrintDevice(int);
void	CtrlPointAddDevice(struct Upnp_Discovery *);

int	CtrlPointCallbackEventHandler(Upnp_EventType, void *, void *);

/*!
 * \brief Checks the advertisement each device in the global device list.
 *
 * If an advertisement expires, the device is removed from the list.
 *
 * If an advertisement is about to expire, a search request is sent for that
 * device.
 */
void CtrlPointVerifyTimeouts(
	/*! [in] The increment to subtract from the timeouts each time the
	 * function is called. */
	int incr);

void	CtrlPointPrintCommands(void);
void*	CtrlPointCommandLoop(void *);
int	CtrlPointStart(void);
int	CtrlPointStop(void);
int	CtrlPointProcessCommand(char *cmdline);

/*!
 * \brief Print help info for this application.
 */
void CtrlPointPrintShortHelp(void);

/*!
 * \brief Print long help info for this application.
 */
void CtrlPointPrintLongHelp(void);

/*!
 * \briefPrint the list of valid command line commands to the user
 */
void CtrlPointPrintCommands(void);

/*!
 * \brief Function that receives commands from the user at the command prompt
 * during the lifetime of the device, and calls the appropriate
 * functions for those commands.
 */
void *CtrlPointCommandLoop(void *args);

/*!
 * \brief
 */
int CtrlPointProcessCommand(char *cmdline);

#ifdef __cplusplus
};
#endif


/*! @} Device Sample */

/*! @} UpnpSamples */

#endif /* UPNP_TV_CTRLPT_H */
