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
 * \addtogroup UpnpSamples
 *
 * @{
 *
 * \name Control Point Sample Module
 *
 * @{
 *
 * \file
 */

#include "ctrlpt.h"

#include "upnp.h"

/*!
 * Mutex for protecting the global device list in a multi-threaded,
 * asynchronous environment. All functions should lock this mutex before
 * reading or writing the device list. 
 */
ithread_mutex_t DeviceListMutex;

UpnpClient_Handle ctrlpt_handle = -1;

/*! Device type for tv device. */
const char TvDeviceType[] = "urn:upnp:device:ohm:1";
const char TvUDN[] = "uuid:Upnp-OhmSwitch-1_0-1234";

/*! Service names.*/
const char *TvServiceName[] = { "Control" };

/*!
   Global arrays for storing variable names and counts for 
   TvControl and TvPicture services 
 */
const char *TvVarName[TV_SERVICE_SERVCOUNT][TV_MAXVARS] = {
    {"Power", "Channel"},
};
char TvVarCount[TV_SERVICE_SERVCOUNT] =
    { TV_CONTROL_VARCOUNT };

/*!
   Timeout to request during subscriptions 
 */
int default_timeout = 1801;

/*!
   The first node in the global device list, or NULL if empty 
 */
struct TvDeviceNode *GlobalDeviceList = NULL;

/********************************************************************************
 * CtrlPointDeleteNode
 *
 * Description: 
 *       Delete a device node from the global device list.  Note that this
 *       function is NOT thread safe, and should be called from another
 *       function that has already locked the global device list.
 *
 * Parameters:
 *   node -- The device node
 *
 ********************************************************************************/
int
CtrlPointDeleteNode( struct TvDeviceNode *node )
{
	if (NULL == node) {
		SampleUtil_Print
		    ("ERROR: CtrlPointDeleteNode: Node is empty\n");
		return ERROR;
	}

	/*Notify New Device Added */
	SampleUtil_StateUpdate(NULL, NULL, node->device.UDN, DEVICE_REMOVED);
	free(node);
	node = NULL;

	return SUCCESS;
}

/********************************************************************************
 * CtrlPointRemoveDevice
 *
 * Description: 
 *       Remove a device from the global device list.
 *
 * Parameters:
 *   UDN -- The Unique Device Name for the device to remove
 *
 ********************************************************************************/
int CtrlPointRemoveDevice(const char *UDN)
{
	struct TvDeviceNode *curdevnode;
	struct TvDeviceNode *prevdevnode;

	ithread_mutex_lock(&DeviceListMutex);

	curdevnode = GlobalDeviceList;
	if (!curdevnode) {
		SampleUtil_Print(
			"WARNING: CtrlPointRemoveDevice: Device list empty\n");
	} else {
		if (0 == strcmp(curdevnode->device.UDN, UDN)) {
			GlobalDeviceList = curdevnode->next;
			CtrlPointDeleteNode(curdevnode);
		} else {
			prevdevnode = curdevnode;
			curdevnode = curdevnode->next;
			while (curdevnode) {
				if (strcmp(curdevnode->device.UDN, UDN) == 0) {
					prevdevnode->next = curdevnode->next;
					CtrlPointDeleteNode(curdevnode);
					break;
				}
				prevdevnode = curdevnode;
				curdevnode = curdevnode->next;
			}
		}
	}

	ithread_mutex_unlock(&DeviceListMutex);

	return SUCCESS;
}

/********************************************************************************
 * CtrlPointRemoveAll
 *
 * Description: 
 *       Remove all devices from the global device list.
 *
 * Parameters:
 *   None
 *
 ********************************************************************************/
int CtrlPointRemoveAll(void)
{
	struct TvDeviceNode *curdevnode, *next;

	ithread_mutex_lock(&DeviceListMutex);

	curdevnode = GlobalDeviceList;
	GlobalDeviceList = NULL;

	while (curdevnode) {
		next = curdevnode->next;
		CtrlPointDeleteNode(curdevnode);
		curdevnode = next;
	}

	ithread_mutex_unlock(&DeviceListMutex);

	return SUCCESS;
}

/********************************************************************************
 * CtrlPointRefresh
 *
 * Description: 
 *       Clear the current global device list and issue new search
 *	 requests to build it up again from scratch.
 *
 * Parameters:
 *   None
 *
 ********************************************************************************/
int CtrlPointRefresh(void)
{
	int rc;

	CtrlPointRemoveAll();
	/* Search for all devices of type tvdevice version 1,
	 * waiting for up to 5 seconds for the response */
	rc = UpnpSearchAsync(ctrlpt_handle, 5, TvDeviceType, NULL);
	if (UPNP_E_SUCCESS != rc) {
		SampleUtil_Print("Error sending search request%d\n", rc);

		return ERROR;
	}

	return SUCCESS;
}

/********************************************************************************
 * CtrlPointSendAction
 *
 * Description: 
 *       Send an Action request to the specified service of a device.
 *
 * Parameters:
 *   service -- The service
 *   devnum -- The number of the device (order in the list,
 *             starting with 1)
 *   actionname -- The name of the action.
 *   param_name -- An array of parameter names
 *   param_val -- The corresponding parameter values
 *   param_count -- The number of parameters
 *
 ********************************************************************************/
int CtrlPointSendAction(
	int service,
	int devnum,
	const char *actionname)
{
	struct TvDeviceNode *devnode;
	int rc = SUCCESS;

	ithread_mutex_lock(&DeviceListMutex);

	rc = CtrlPointGetDevice(devnum, &devnode);
	if (SUCCESS == rc) {
	    printf("%s: DescDocURL: %s\n",__func__,devnode->device.DescDocURL);

	    rc = UpnpSendActionAsync(ctrlpt_handle,
		    devnode->device.DescDocURL,
		    actionname,NULL,
		    CtrlPointCallbackEventHandler, NULL);
	    if (rc != UPNP_E_SUCCESS) {
		SampleUtil_Print("Error in UpnpSendActionAsync -- %d\n",
			rc);
		rc = ERROR;
	    }
	}

	ithread_mutex_unlock(&DeviceListMutex);

	return rc;
}

int CtrlPointSendPowerOn(int devnum)
{
	return CtrlPointSendAction(
		TV_SERVICE_CONTROL, devnum, "PowerOn");
}

int CtrlPointSendPowerOff(int devnum)
{
	return CtrlPointSendAction(
		TV_SERVICE_CONTROL, devnum, "PowerOff");
}

int CtrlPointSendGetAPList(int devnum)
{
    const char actionname[] =
	"{\r\n\"command\":\r\n{\"commandName\":\"getAPList\",\"commandValue\":1,\"commandType\":\"proprietary\"},\r\n\"parameters\":\r\n{}\r\n}";
	return CtrlPointSendAction(
		TV_SERVICE_CONTROL, devnum, actionname);
}

int CtrlPointSendConnectToAP(int devnum)
{
    const char actionname[] =
	"{\r\n\"command\":\r\n{\"commandName\":\"connectToAP\",\"commandValue\":2,\"commandType\":\"proprietary\"},\r\n\"parameters\":\r\n{\"password\":\"8cmf-bl68-9tni\",\"SSID\":\"Tabahi\",\"security\":1,\"securityType\":\"WPA\"}\r\n}";
	return CtrlPointSendAction(
		TV_SERVICE_CONTROL, devnum, actionname);
}

int CtrlPointSendCloseAP(int devnum)
{
    const char actionname[] =
	"{\r\n\"command\":\r\n{\"commandName\":\"closeAP\",\"commandValue\":3,\"commandType\":\"proprietary\"},\r\n\"parameters\":\r\n{}\r\n}";
	return CtrlPointSendAction(
		TV_SERVICE_CONTROL, devnum, actionname);
}

int CtrlPointSendGetDevInfo(int devnum)
{
    const char actionname[] =
	"{\r\n\"command\":\r\n{\"commandName\":\"getProductInfo\",\"commandValue\":4,\"commandType\":\"proprietary\"},\r\n\"parameters\":\r\n{}\r\n}";
	return CtrlPointSendAction(
		TV_SERVICE_CONTROL, devnum, actionname);
}

int CtrlPointSendSetName(int devnum)
{
    const char actionname[] =
	"{\r\n\"command\":\r\n{\"commandName\":\"setName\",\"commandValue\":5,\"commandType\":\"proprietary\"},\r\n\"parameters\":\r\n{\"name\":\"Bed Room\"}\r\n}";//,\"list\":[{\"name\":\"Amit\",\"age\":30},{\"name\":\"star\",\"age\":25},{\"name\":\"athu\",\"age\":19}]}\r\n}";
	return CtrlPointSendAction(
		TV_SERVICE_CONTROL, devnum, actionname);
}

/********************************************************************************
 * CtrlPointGetDevice
 *
 * Description: 
 *       Given a list number, returns the pointer to the device
 *       node at that position in the global device list.  Note
 *       that this function is not thread safe.  It must be called 
 *       from a function that has locked the global device list.
 *
 * Parameters:
 *   devnum -- The number of the device (order in the list,
 *             starting with 1)
 *   devnode -- The output device node pointer
 *
 ********************************************************************************/
int CtrlPointGetDevice(int devnum, struct TvDeviceNode **devnode)
{
	int count = devnum;
	struct TvDeviceNode *tmpdevnode = NULL;

	if (count)
		tmpdevnode = GlobalDeviceList;
	while (--count && tmpdevnode) {
		tmpdevnode = tmpdevnode->next;
	}
	if (!tmpdevnode) {
		SampleUtil_Print("Error finding TvDevice number -- %d\n",
				 devnum);
		return ERROR;
	}
	*devnode = tmpdevnode;

	return SUCCESS;
}

/********************************************************************************
 * CtrlPointPrintList
 *
 * Description: 
 *       Print the universal device names for each device in the global device list
 *
 * Parameters:
 *   None
 *
 ********************************************************************************/
int CtrlPointPrintList()
{
	struct TvDeviceNode *tmpdevnode;
	int i = 0;

	ithread_mutex_lock(&DeviceListMutex);

	SampleUtil_Print("TvCtrlPointPrintList:\n");
	tmpdevnode = GlobalDeviceList;
	while (tmpdevnode) {
		SampleUtil_Print(" %3d -- %s\n", ++i, tmpdevnode->device.UDN);
		tmpdevnode = tmpdevnode->next;
	}
	SampleUtil_Print("\n");
	ithread_mutex_unlock(&DeviceListMutex);

	return SUCCESS;
}

/********************************************************************************
 * CtrlPointAddDevice
 *
 * Description: 
 *       If the device is not already included in the global device list,
 *       add it.  Otherwise, update its advertisement expiration timeout.
 *
 * Parameters:
 *   location -- The location of the description document URL
 *   expires -- The expiration time for this advertisement
 *
 ********************************************************************************/
void CtrlPointAddDevice(
	const char *location,
	int expires)
{
	char *deviceType = NULL;
	char *UDN = NULL;
	struct TvDeviceNode *deviceNode;
	struct TvDeviceNode *tmpdevnode;
	int found = 0;

	ithread_mutex_lock(&DeviceListMutex);

	/* Read key elements from description document */
	UDN = (char *)malloc(sizeof(char)*(strlen(TvUDN)+1));
	if (NULL==UDN)
	    return;

	strncpy(UDN, TvUDN, strlen(TvUDN));
	UDN[strlen(TvUDN)] = '\0';

	deviceType = (char *)malloc(sizeof(char)*(strlen(TvDeviceType)+1));
	if (NULL==deviceType)
	    return;

	strncpy(deviceType, TvDeviceType, strlen(TvDeviceType));
	deviceType[strlen(TvDeviceType)] = '\0';

	if (strcmp(deviceType, TvDeviceType) == 0) {
		SampleUtil_Print("Found Ohm device\n");

		/* Check if this device is already in the list */
		tmpdevnode = GlobalDeviceList;
		while (tmpdevnode) {
			if (strcmp(tmpdevnode->device.UDN, UDN) == 0) {
				found = 1;
				break;
			}
			tmpdevnode = tmpdevnode->next;
		}

		if (found) {
			/* The device is already there, so just update  */
			/* the advertisement timeout field */
			tmpdevnode->device.AdvrTimeOut = expires;
		} else {
			/* Create a new device node */
			deviceNode =
			    (struct TvDeviceNode *)
			    malloc(sizeof(struct TvDeviceNode));
			strcpy(deviceNode->device.UDN, UDN);
			strcpy(deviceNode->device.DescDocURL, location);
			deviceNode->device.AdvrTimeOut = expires;
			deviceNode->next = NULL;
			/* Insert the new device node in the list */
			if ((tmpdevnode = GlobalDeviceList)) {
				while (tmpdevnode) {
					if (tmpdevnode->next) {
						tmpdevnode = tmpdevnode->next;
					} else {
						tmpdevnode->next = deviceNode;
						break;
					}
				}
			} else {
				GlobalDeviceList = deviceNode;
			}
			/*Notify New Device Added */
			SampleUtil_StateUpdate(NULL, NULL,
					       deviceNode->device.UDN,
					       DEVICE_ADDED);
		}
	}

	ithread_mutex_unlock(&DeviceListMutex);

	if (deviceType)
		free(deviceType);
	if (UDN)
		free(UDN);
}

/********************************************************************************
 * CtrlPointCallbackEventHandler
 *
 * Description: 
 *       The callback handler registered with the SDK while registering
 *       the control point.  Detects the type of callback, and passes the 
 *       request on to the appropriate function.
 *
 * Parameters:
 *   EventType -- The type of callback event
 *   Event -- Data structure containing event data
 *   Cookie -- Optional data specified during callback registration
 *
 ********************************************************************************/
int CtrlPointCallbackEventHandler(Upnp_EventType EventType, void *Event, void *Cookie)
{
	/*int errCode = 0;*/

	switch ( EventType ) {
	/* SSDP Stuff */
	case UPNP_DISCOVERY_ADVERTISEMENT_ALIVE:
	case UPNP_DISCOVERY_SEARCH_RESULT: {
		struct Upnp_Discovery *d_event = (struct Upnp_Discovery *)Event;

		if (d_event->ErrCode != UPNP_E_SUCCESS) {
			SampleUtil_Print("Error in Discovery Callback -- %d\n",
				d_event->ErrCode);
		}
		CtrlPointAddDevice(d_event->Location, d_event->Expires);
		//CtrlPointPrintList();
		break;
	}
	case UPNP_DISCOVERY_SEARCH_TIMEOUT:
		/* Nothing to do here... */
		break;
	case UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE: {
		struct Upnp_Discovery *d_event = (struct Upnp_Discovery *)Event;

		if (d_event->ErrCode != UPNP_E_SUCCESS) {
			SampleUtil_Print("Error in Discovery ByeBye Callback -- %d\n",
					d_event->ErrCode);
		}
		SampleUtil_Print("Received ByeBye for Device: %s\n", d_event->DeviceId);
		CtrlPointRemoveDevice(d_event->DeviceId);
		SampleUtil_Print("After byebye:\n");
		CtrlPointPrintList();
		break;
	}
	/* SOAP Stuff */
	case UPNP_CONTROL_ACTION_COMPLETE: {
		SampleUtil_Print("UPNP_CONTROL_ACTION_COMPLETE\n");
		/* No need for any processing here, just print out results.
		 * Service state table updates are handled by events. */
		break;
	}
	case UPNP_CONTROL_GET_VAR_COMPLETE: {
		struct Upnp_State_Var_Complete *sv_event = (struct Upnp_State_Var_Complete *)Event;

		if (sv_event->ErrCode != UPNP_E_SUCCESS) {
			SampleUtil_Print("Error in Get Var Complete Callback -- %d\n",
					sv_event->ErrCode);
		}
		break;
	}
	/* GENA Stuff */
	case UPNP_EVENT_RECEIVED: {
		struct Upnp_Event *e_event = (struct Upnp_Event *)Event;
		SampleUtil_Print("Event -- %d\n",
					e_event->EventKey);

		break;
	}
	case UPNP_EVENT_SUBSCRIBE_COMPLETE:
	case UPNP_EVENT_UNSUBSCRIBE_COMPLETE:
	case UPNP_EVENT_RENEWAL_COMPLETE: {
		struct Upnp_Event_Subscribe *es_event = (struct Upnp_Event_Subscribe *)Event;

		if (es_event->ErrCode != UPNP_E_SUCCESS) {
			SampleUtil_Print("Error in Event Subscribe Callback -- %d\n",
					es_event->ErrCode);
		}
		break;
	}
	case UPNP_EVENT_AUTORENEWAL_FAILED:
	case UPNP_EVENT_SUBSCRIPTION_EXPIRED: {
		struct Upnp_Event_Subscribe *es_event = (struct Upnp_Event_Subscribe *)Event;
		int TimeOut = default_timeout;
		Upnp_SID newSID;
		int ret;

		ret = UpnpSubscribe(
			ctrlpt_handle,
			es_event->PublisherUrl,
			&TimeOut,
			newSID);
		if (ret == UPNP_E_SUCCESS) {
			SampleUtil_Print("Subscribed to EventURL with SID=%s\n", newSID);
		} else {
			SampleUtil_Print("Error Subscribing to EventURL -- %d\n", ret);
		}
		break;
	}
	/* ignore these cases, since this is not a device */
	case UPNP_EVENT_SUBSCRIPTION_REQUEST:
	case UPNP_CONTROL_GET_VAR_REQUEST:
	case UPNP_CONTROL_ACTION_REQUEST:
		break;
	}

	return 0;
	Cookie = Cookie;
}

void CtrlPointVerifyTimeouts(int incr)
{
	struct TvDeviceNode *prevdevnode;
	struct TvDeviceNode *curdevnode;
	int ret;

	ithread_mutex_lock(&DeviceListMutex);

	prevdevnode = NULL;
	curdevnode = GlobalDeviceList;
	while (curdevnode) {
		curdevnode->device.AdvrTimeOut -= incr;
		/*SampleUtil_Print("Advertisement Timeout: %d\n", curdevnode->device.AdvrTimeOut); */
		if (curdevnode->device.AdvrTimeOut <= 0) {
			/* This advertisement has expired, so we should remove the device
			 * from the list */
			if (GlobalDeviceList == curdevnode)
				GlobalDeviceList = curdevnode->next;
			else
				prevdevnode->next = curdevnode->next;
			CtrlPointDeleteNode(curdevnode);
			if (prevdevnode)
				curdevnode = prevdevnode->next;
			else
				curdevnode = GlobalDeviceList;
		} else {
			if (curdevnode->device.AdvrTimeOut < 2 * incr) {
				/* This advertisement is about to expire, so
				 * send out a search request for this device
				 * UDN to try to renew */
				ret = UpnpSearchAsync(ctrlpt_handle, incr,
						      curdevnode->device.UDN,
						      NULL);
				if (ret != UPNP_E_SUCCESS)
					SampleUtil_Print
					    ("Error sending search request for Device UDN: %s -- err = %d\n",
					     curdevnode->device.UDN, ret);
			}
			prevdevnode = curdevnode;
			curdevnode = curdevnode->next;
		}
	}

	ithread_mutex_unlock(&DeviceListMutex);
}

/*!
 * \brief Function that runs in its own thread and monitors advertisement
 * and subscription timeouts for devices in the global device list.
 */
static int TvCtrlPointTimerLoopRun = 1;
void *CtrlPointTimerLoop(void *args)
{
	/* how often to verify the timeouts, in seconds */
	int incr = 30;

	while (TvCtrlPointTimerLoopRun) {
		isleep((unsigned int)incr);
		CtrlPointVerifyTimeouts(incr);
	}

	return NULL;
	args = args;
}

/*!
 * \brief Call this function to initialize the UPnP library and start the TV
 * Control Point.  This function creates a timer thread and provides a
 * callback handler to process any UPnP events that are received.
 *
 * \return SUCCESS if everything went well, else ERROR.
 */
int CtrlPointStart(print_string printFunctionPtr, state_update updateFunctionPtr, int combo)
{
	ithread_t timer_thread;
	int rc;
	unsigned short port = 0;
	char *ip_address = NULL;

	SampleUtil_Initialize(printFunctionPtr);
	SampleUtil_RegisterUpdateFunction(updateFunctionPtr);

	ithread_mutex_init(&DeviceListMutex, 0);

	SampleUtil_Print("Initializing UPnP Sdk with\n"
			 "\tipaddress = %s port = %u\n",
			 ip_address ? ip_address : "{NULL}", port);

	rc = UpnpInit(ip_address, port);
	if (rc != UPNP_E_SUCCESS) {
		SampleUtil_Print("WinCEStart: UpnpInit() Error: %d\n", rc);
		if (!combo) {
			UpnpFinish();

			return ERROR;
		}
	}
	if (!ip_address) {
		ip_address = UpnpGetServerIpAddress();
	}
	if (!port) {
		port = UpnpGetServerPort();
	}

	SampleUtil_Print("UPnP Initialized\n"
			 "\tipaddress = %s port = %u\n",
			 ip_address ? ip_address : "{NULL}", port);
	SampleUtil_Print("Registering Control Point\n");
	rc = UpnpRegisterClient(CtrlPointCallbackEventHandler,
				&ctrlpt_handle, &ctrlpt_handle);
	if (rc != UPNP_E_SUCCESS) {
		SampleUtil_Print("Error registering CP: %d\n", rc);
		UpnpFinish();

		return ERROR;
	}

	SampleUtil_Print("Control Point Registered\n");

	CtrlPointRefresh();

	/* start a timer thread */
	ithread_create(&timer_thread, NULL, CtrlPointTimerLoop, NULL);
	ithread_detach(timer_thread);

	return SUCCESS;
}

int CtrlPointStop(void)
{
	TvCtrlPointTimerLoopRun = 0;
	CtrlPointRemoveAll();
	UpnpUnRegisterClient( ctrlpt_handle );
	UpnpFinish();
	SampleUtil_Finish();

	return SUCCESS;
}

void CtrlPointPrintShortHelp(void)
{
	SampleUtil_Print(
		"Commands:\n"
		"  Help\n"
		"  HelpFull\n"
		"  ListDev\n"
		"  Refresh\n"
		"  PrintDev	<devnum>\n"
		"  PowerOn	<devnum>\n"
		"  PowerOff	<devnum>\n"
		"  GetAPList	<devnum>\n"
		"  ConnectToAp	<devnum>\n"
		"  CloseAP	<devnum>\n"
		"  GetDevInfo	<devnum>\n"
		"  SetName	<devnum>\n"
		"  Exit\n");
}

void CtrlPointPrintLongHelp(void)
{
	SampleUtil_Print(
		"\n"
		"******************************\n"
		"* TV Control Point Help Info *\n"
		"******************************\n"
		"\n"
		"This sample control point application automatically searches\n"
		"for and subscribes to the services of television device emulator\n"
		"devices, described in the tvdevicedesc.xml description document.\n"
		"It also registers itself as a tv device.\n"
		"\n"
		"Commands:\n"
		"  Help\n"
		"       Print this help info.\n"
		"  ListDev\n"
		"       Print the current list of TV Device Emulators that this\n"
		"         control point is aware of.  Each device is preceded by a\n"
		"         device number which corresponds to the devnum argument of\n"
		"         commands listed below.\n"
		"  Refresh\n"
		"       Delete all of the devices from the device list and issue new\n"
		"         search request to rebuild the list from scratch.\n"
		"  PrintDev       <devnum>\n"
		"       Print the state table for the device <devnum>.\n"
		"         e.g., 'PrintDev 1' prints the state table for the first\n"
		"         device in the device list.\n"
		"  PowerOn        <devnum>\n"
		"       Sends the PowerOn action to the Control Service of\n"
		"         device <devnum>.\n"
		"  PowerOff       <devnum>\n"
		"       Sends the PowerOff action to the Control Service of\n"
		"         device <devnum>.\n"
		"  Exit\n"
		"       Exits the control point application.\n");
}

/*! Tags for valid commands issued at the command prompt. */
enum cmdloop_tvcmds {
	PRTHELP = 0,
	PRTFULLHELP,
	POWON,
	POWOFF,
	GETAPLIST,
	CONNECTTOAP,
	CLOSEAP,
	GETDEVINFO,
	SETNAME,
	LSTDEV,
	REFRESH,
	EXITCMD
};

/*! Data structure for parsing commands from the command line. */
struct cmdloop_commands {
	/* the string  */
	const char *str;
	/* the command */
	int cmdnum;
	/* the number of arguments */
	int numargs;
	/* the args */
	const char *args;
} cmdloop_commands;

/*! Mappings between command text names, command tag,
 * and required command arguments for command line
 * commands */
static struct cmdloop_commands cmdloop_cmdlist[] = {
	{"Help",          PRTHELP,     1, ""},
	{"HelpFull",      PRTFULLHELP, 1, ""},
	{"ListDev",       LSTDEV,      1, ""},
	{"Refresh",       REFRESH,     1, ""},
	{"GetAPList",     GETAPLIST,   2, "<devnum>"},
	{"ConnectToAP",   CONNECTTOAP, 2, "<devnum>"},
	{"CloseAP",   	  CLOSEAP,     2, "<devnum>"},
	{"GetDevInfo",	  GETDEVINFO,  2, "<devnum>"},
	{"SetName",   	  SETNAME,     2, "<devnum>"},
	{"PowerOn",       POWON,       2, "<devnum>"},
	{"PowerOff",      POWOFF,      2, "<devnum>"},
	{"Exit",          EXITCMD,     1, ""}
};

void CtrlPointPrintCommands(void)
{
	int i;
	int numofcmds = (sizeof cmdloop_cmdlist) / sizeof (cmdloop_commands);

	SampleUtil_Print("Valid Commands:\n");
	for (i = 0; i < numofcmds; ++i) {
		SampleUtil_Print("  %-14s %s\n",
			cmdloop_cmdlist[i].str, cmdloop_cmdlist[i].args);
	}
	SampleUtil_Print("\n");
}

void *CtrlPointCommandLoop(void *args)
{
	char cmdline[100];

	while (1) {
		SampleUtil_Print("\n>> ");
		fgets(cmdline, 100, stdin);
		CtrlPointProcessCommand(cmdline);
	}

	return NULL;
	args = args;
}

int CtrlPointProcessCommand(char *cmdline)
{
	char cmd[100];
	int arg_val_err = -99999;
	int arg1 = arg_val_err;
	int arg2 = arg_val_err;
	int cmdnum = -1;
	int numofcmds = (sizeof cmdloop_cmdlist) / sizeof (cmdloop_commands);
	int cmdfound = 0;
	int i;
	int invalidargs = 0;
	int validargs;

	validargs = sscanf(cmdline, "%s %d %d", cmd, &arg1, &arg2);
	for (i = 0; i < numofcmds; ++i) {
		if (strcasecmp(cmd, cmdloop_cmdlist[i].str ) == 0) {
			cmdnum = cmdloop_cmdlist[i].cmdnum;
			cmdfound++;
			if (validargs != cmdloop_cmdlist[i].numargs)
				invalidargs++;
			break;
		}
	}
	if (!cmdfound) {
		SampleUtil_Print("Command not found; try 'Help'\n");
		return SUCCESS;
	}
	if (invalidargs) {
		SampleUtil_Print("Invalid arguments; try 'Help'\n");
		return SUCCESS;
	}
	switch (cmdnum) {
	case PRTHELP:
		CtrlPointPrintShortHelp();
		break;
	case PRTFULLHELP:
		CtrlPointPrintLongHelp();
		break;
	case POWON:
		CtrlPointSendPowerOn(arg1);
		break;
	case POWOFF:
		CtrlPointSendPowerOff(arg1);
		break;
	case GETAPLIST:
		CtrlPointSendGetAPList(arg1);
		break;
	case CONNECTTOAP:
		CtrlPointSendConnectToAP(arg1);
		break;
	case CLOSEAP:
		CtrlPointSendCloseAP(arg1);
		break;
	case GETDEVINFO:
		CtrlPointSendGetDevInfo(arg1);
		break;
	case SETNAME:
		CtrlPointSendSetName(arg1);
		break;
	case LSTDEV:
		CtrlPointPrintList();
		break;
	case REFRESH:
		CtrlPointRefresh();
		break;
	case EXITCMD: {
		int rc = CtrlPointStop();
		exit(rc);
		}
		break;
	default:
		SampleUtil_Print("Command not implemented; see 'Help'\n");
		break;
	}
	if(invalidargs)
		SampleUtil_Print("Invalid args in command; see 'Help'\n");

	return SUCCESS;
}

/*! @} Control Point Sample Module */

/*! @} UpnpSamples */
