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
	int rc, service, var;

	if (NULL == node) {
		SampleUtil_Print
		    ("ERROR: CtrlPointDeleteNode: Node is empty\n");
		return ERROR;
	}

	for (service = 0; service < TV_SERVICE_SERVCOUNT; service++) {
		/*
		   If we have a valid control SID, then unsubscribe 
		 */
		if (strcmp(node->device.TvService[service].SID, "") != 0) {
			rc = UpnpUnSubscribe(ctrlpt_handle,
					     node->device.TvService[service].
					     SID);
			if (UPNP_E_SUCCESS == rc) {
				SampleUtil_Print
				    ("Unsubscribed from Tv %s EventURL with SID=%s\n",
				     TvServiceName[service],
				     node->device.TvService[service].SID);
			} else {
				SampleUtil_Print
				    ("Error unsubscribing to Tv %s EventURL -- %d\n",
				     TvServiceName[service], rc);
			}
		}

		for (var = 0; var < TvVarCount[service]; var++) {
			if (node->device.TvService[service].VariableStrVal[var]) {
				free(node->device.
				     TvService[service].VariableStrVal[var]);
			}
		}
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
	const char *actionname,
	const char **param_name,
	char **param_val,
	int param_count)
{
	struct TvDeviceNode *devnode;
	IXML_Document *actionNode = NULL;
	int rc = SUCCESS;

	ithread_mutex_lock(&DeviceListMutex);

	rc = CtrlPointGetDevice(devnum, &devnode);
	if (SUCCESS == rc) {
	    actionNode =
		UpnpMakeAction("PowerOn" /*actionname*/, TvServiceType[service],
			0, NULL);

	    printf("%s: ControlURL: %s\n",__func__,devnode->device.TvService[service].ControlURL);

	    rc = UpnpSendActionAsync(ctrlpt_handle,
		    devnode->device.TvService[service].ControlURL,
		    actionname,NULL,
		    actionNode,
		    CtrlPointCallbackEventHandler, NULL);

	    if (rc != UPNP_E_SUCCESS) {
		SampleUtil_Print("Error in UpnpSendActionAsync -- %d\n",
			rc);
		rc = ERROR;
	    }
	}

	ithread_mutex_unlock(&DeviceListMutex);

	if (actionNode)
		ixmlDocument_free(actionNode);

	return rc;
}

int CtrlPointSendPowerOn(int devnum)
{
	return CtrlPointSendAction(
		TV_SERVICE_CONTROL, devnum, "PowerOn", NULL, NULL, 0);
}

int CtrlPointSendPowerOff(int devnum)
{
	return CtrlPointSendAction(
		TV_SERVICE_CONTROL, devnum, "PowerOff", NULL, NULL, 0);
}

int CtrlPointSendGetAPList(int devnum)
{
    const char actionname[] =
	"{\r\n\"command\":\r\n{\"commandName\":\"getAPList\",\"commandValue\":1,\"commandType\":\"proprietary\"},\r\n\"parameters\":\r\n{}\r\n}";
	return CtrlPointSendAction(
		TV_SERVICE_CONTROL, devnum, actionname, NULL, NULL, 0);
}

int CtrlPointSendConnectToAP(int devnum)
{
    const char actionname[] =
	"{\r\n\"command\":\r\n{\"commandName\":\"connectToAP\",\"commandValue\":2,\"commandType\":\"proprietary\"},\r\n\"parameters\":\r\n{\"password\":\"8cmf-bl68-9tni\",\"SSID\":\"Tabahi\",\"security\":1,\"securityType\":\"WPA\"}\r\n}";
	return CtrlPointSendAction(
		TV_SERVICE_CONTROL, devnum, actionname, NULL, NULL, 0);
}

int CtrlPointSendCloseAP(int devnum)
{
    const char actionname[] =
	"{\r\n\"command\":\r\n{\"commandName\":\"closeAP\",\"commandValue\":3,\"commandType\":\"proprietary\"},\r\n\"parameters\":\r\n{}\r\n}";
	return CtrlPointSendAction(
		TV_SERVICE_CONTROL, devnum, actionname, NULL, NULL, 0);
}

int CtrlPointSendGetDevInfo(int devnum)
{
    const char actionname[] =
	"{\r\n\"command\":\r\n{\"commandName\":\"getProductInfo\",\"commandValue\":4,\"commandType\":\"proprietary\"},\r\n\"parameters\":\r\n{}\r\n}";
	return CtrlPointSendAction(
		TV_SERVICE_CONTROL, devnum, actionname, NULL, NULL, 0);
}

int CtrlPointSendSetName(int devnum)
{
    const char actionname[] =
	"{\r\n\"command\":\r\n{\"commandName\":\"setName\",\"commandValue\":5,\"commandType\":\"proprietary\"},\r\n\"parameters\":\r\n{\"name\":\"Bed Room\"}\r\n}";//,\"list\":[{\"name\":\"Amit\",\"age\":30},{\"name\":\"star\",\"age\":25},{\"name\":\"athu\",\"age\":19}]}\r\n}";
	return CtrlPointSendAction(
		TV_SERVICE_CONTROL, devnum, actionname, NULL, NULL, 0);
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
 * CtrlPointPrintDevice
 *
 * Description: 
 *       Print the identifiers and state table for a device from
 *       the global device list.
 *
 * Parameters:
 *   devnum -- The number of the device (order in the list,
 *             starting with 1)
 *
 ********************************************************************************/
int CtrlPointPrintDevice(int devnum)
{
	struct TvDeviceNode *tmpdevnode;
	int i = 0, service, var;
	char spacer[15];

	if (devnum <= 0) {
		SampleUtil_Print(
			"Error in CtrlPointPrintDevice: "
			"invalid devnum = %d\n",
			devnum);
		return ERROR;
	}

	ithread_mutex_lock(&DeviceListMutex);

	SampleUtil_Print("CtrlPointPrintDevice:\n");
	tmpdevnode = GlobalDeviceList;
	while (tmpdevnode) {
		i++;
		if (i == devnum)
			break;
		tmpdevnode = tmpdevnode->next;
	}
	if (!tmpdevnode) {
		SampleUtil_Print(
			"Error in CtrlPointPrintDevice: "
			"invalid devnum = %d  --  actual device count = %d\n",
			devnum, i);
	} else {
		SampleUtil_Print(
			"  TvDevice -- %d\n"
			"    |                  \n"
			"    +- UDN        = %s\n"
			"    +- DescDocURL     = %s\n"
			"    +- FriendlyName   = %s\n"
			"    +- PresURL        = %s\n"
			"    +- Adver. TimeOut = %d\n",
			devnum,
			tmpdevnode->device.UDN,
			tmpdevnode->device.DescDocURL,
			tmpdevnode->device.FriendlyName,
			tmpdevnode->device.PresURL,
			tmpdevnode->device.AdvrTimeOut);
		for (service = 0; service < TV_SERVICE_SERVCOUNT; service++) {
			if (service < TV_SERVICE_SERVCOUNT - 1)
				sprintf(spacer, "    |    ");
			else
				sprintf(spacer, "         ");
			SampleUtil_Print(
				"    |                  \n"
				"    +- Tv %s Service\n"
				"%s+- ServiceId       = %s\n"
				"%s+- ServiceType     = %s\n"
				"%s+- EventURL        = %s\n"
				"%s+- ControlURL      = %s\n"
				"%s+- SID             = %s\n"
				"%s+- ServiceStateTable\n",
				TvServiceName[service],
				spacer,
				tmpdevnode->device.TvService[service].ServiceId,
				spacer,
				tmpdevnode->device.TvService[service].ServiceType,
				spacer,
				tmpdevnode->device.TvService[service].EventURL,
				spacer,
				tmpdevnode->device.TvService[service].ControlURL,
				spacer,
				tmpdevnode->device.TvService[service].SID,
				spacer);
			for (var = 0; var < TvVarCount[service]; var++) {
				SampleUtil_Print(
					"%s     +- %-10s = %s\n",
					spacer,
					TvVarName[service][var],
					tmpdevnode->device.TvService[service].VariableStrVal[var]);
			}
		}
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
 *   DescDoc -- The description document for the device
 *   location -- The location of the description document URL
 *   expires -- The expiration time for this advertisement
 *
 ********************************************************************************/
void CtrlPointAddDevice(
	IXML_Document *DescDoc,
	const char *location,
	int expires)
{
	char *deviceType = NULL;
	char *friendlyName = NULL;
	char *baseURL = NULL;
	char *UDN = NULL;
	char *serviceId[TV_SERVICE_SERVCOUNT] = { NULL };
	char *eventURL[TV_SERVICE_SERVCOUNT] = { NULL };
	char *controlURL[TV_SERVICE_SERVCOUNT] = { NULL };
	Upnp_SID eventSID[TV_SERVICE_SERVCOUNT];
	int TimeOut[TV_SERVICE_SERVCOUNT] = {
		default_timeout
	};
	struct TvDeviceNode *deviceNode;
	struct TvDeviceNode *tmpdevnode;
	int ret = 1;
	int found = 0;
	int service;
	int var;

	ithread_mutex_lock(&DeviceListMutex);

	/* Read key elements from description document */
	UDN = SampleUtil_GetFirstDocumentItem(DescDoc, "UDN");
	printf("UDN: %s\n",UDN);
	deviceType = SampleUtil_GetFirstDocumentItem(DescDoc, "deviceType");
	printf("deviceType: %s\n",deviceType);
	friendlyName = SampleUtil_GetFirstDocumentItem(DescDoc, "friendlyName");
	printf("friendlyName: %s\n",friendlyName);
	baseURL = SampleUtil_GetFirstDocumentItem(DescDoc, "URLBase");
	printf("baseURL: %s\n",baseURL);

	if (strcmp(deviceType, TvDeviceType) == 0) {
		//SampleUtil_Print("Found Tv device\n");

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
			for (service = 0; service < TV_SERVICE_SERVCOUNT;
			     service++) {
				if (SampleUtil_FindAndParseService
				    (DescDoc, location, TvServiceType[service],
				     &serviceId[service], &eventURL[service],
				     &controlURL[service])) {
					SampleUtil_Print
					    ("Subscribing to EventURL %s...\n",
					     eventURL[service]);
					ret =
					    UpnpSubscribe(ctrlpt_handle,
							  eventURL[service],
							  &TimeOut[service],
							  eventSID[service]);
					if (ret == UPNP_E_SUCCESS) {
						SampleUtil_Print
						    ("Subscribed to EventURL with SID=%s\n",
						     eventSID[service]);
					} else {
						SampleUtil_Print
						    ("Error Subscribing to EventURL -- %d\n",
						     ret);
						strcpy(eventSID[service], "");
					}
				} else {
					SampleUtil_Print
					    ("Error: Could not find Service: %s\n",
					     TvServiceType[service]);
				}
			}
			/* Create a new device node */
			deviceNode =
			    (struct TvDeviceNode *)
			    malloc(sizeof(struct TvDeviceNode));
			strcpy(deviceNode->device.UDN, UDN);
			strcpy(deviceNode->device.DescDocURL, location);
			strcpy(deviceNode->device.FriendlyName, friendlyName);
			deviceNode->device.AdvrTimeOut = expires;
			for (service = 0; service < TV_SERVICE_SERVCOUNT;
			     service++) {
				if (serviceId[service] == NULL) {
					/* not found */
					continue;
				}
				strcpy(deviceNode->device.TvService[service].
				       ServiceId, serviceId[service]);
				strcpy(deviceNode->device.TvService[service].
				       ServiceType, TvServiceType[service]);
				strcpy(deviceNode->device.TvService[service].
				       ControlURL, controlURL[service]);
				strcpy(deviceNode->device.TvService[service].
				       EventURL, eventURL[service]);
				strcpy(deviceNode->device.TvService[service].
				       SID, eventSID[service]);
				for (var = 0; var < TvVarCount[service]; var++) {
					deviceNode->device.
					    TvService[service].VariableStrVal
					    [var] =
					    (char *)malloc(TV_MAX_VAL_LEN);
					strcpy(deviceNode->device.
					       TvService[service].VariableStrVal
					       [var], "");
				}
			}
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
	if (friendlyName)
		free(friendlyName);
	if (UDN)
		free(UDN);
	if (baseURL)
		free(baseURL);
	for (service = 0; service < TV_SERVICE_SERVCOUNT; service++) {
		if (serviceId[service])
			free(serviceId[service]);
		if (controlURL[service])
			free(controlURL[service]);
		if (eventURL[service])
			free(eventURL[service]);
	}
}

void StateUpdate(char *UDN, int Service, IXML_Document *ChangedVariables,
		   char **State)
{
	IXML_NodeList *properties;
	IXML_NodeList *variables;
	IXML_Element *property;
	IXML_Element *variable;
	long unsigned int length;
	long unsigned int length1;
	long unsigned int i;
	int j;
	char *tmpstate = NULL;

	SampleUtil_Print("Tv State Update (service %d):\n", Service);
	/* Find all of the e:property tags in the document */
	properties = ixmlDocument_getElementsByTagName(ChangedVariables,
		"e:property");
	if (properties) {
		length = ixmlNodeList_length(properties);
		for (i = 0; i < length; i++) {
			/* Loop through each property change found */
			property = (IXML_Element *)ixmlNodeList_item(
				properties, i);
			/* For each variable name in the state table,
			 * check if this is a corresponding property change */
			for (j = 0; j < TvVarCount[Service]; j++) {
				variables = ixmlElement_getElementsByTagName(
					property, TvVarName[Service][j]);
				/* If a match is found, extract 
				 * the value, and update the state table */
				if (variables) {
					length1 = ixmlNodeList_length(variables);
					if (length1) {
						variable = (IXML_Element *)
							ixmlNodeList_item(variables, 0);
						tmpstate =
						    SampleUtil_GetElementValue(variable);
						if (tmpstate) {
							strcpy(State[j], tmpstate);
							SampleUtil_Print(
								" Variable Name: %s New Value:'%s'\n",
								TvVarName[Service][j], State[j]);
						}
						if (tmpstate)
							free(tmpstate);
						tmpstate = NULL;
					}
					ixmlNodeList_free(variables);
					variables = NULL;
				}
			}
		}
		ixmlNodeList_free(properties);
	}
	return;
	UDN = UDN;
}

/********************************************************************************
 * CtrlPointHandleEvent
 *
 * Description: 
 *       Handle a UPnP event that was received.  Process the event and update
 *       the appropriate service state table.
 *
 * Parameters:
 *   sid -- The subscription id for the event
 *   eventkey -- The eventkey number for the event
 *   changes -- The DOM document representing the changes
 *
 ********************************************************************************/
void CtrlPointHandleEvent(
	const char *sid,
	int evntkey,
	IXML_Document *changes)
{
	struct TvDeviceNode *tmpdevnode;
	int service;

	ithread_mutex_lock(&DeviceListMutex);

	tmpdevnode = GlobalDeviceList;
	while (tmpdevnode) {
		for (service = 0; service < TV_SERVICE_SERVCOUNT; ++service) {
			if (strcmp(tmpdevnode->device.TvService[service].SID, sid) ==  0) {
				SampleUtil_Print("Received Tv %s Event: %d for SID %s\n",
					TvServiceName[service],
					evntkey,
					sid);
				StateUpdate(
					tmpdevnode->device.UDN,
					service,
					changes,
					(char **)&tmpdevnode->device.TvService[service].VariableStrVal);
				break;
			}
		}
		tmpdevnode = tmpdevnode->next;
	}

	ithread_mutex_unlock(&DeviceListMutex);
}

/********************************************************************************
 * CtrlPointHandleSubscribeUpdate
 *
 * Description: 
 *       Handle a UPnP subscription update that was received.  Find the 
 *       service the update belongs to, and update its subscription
 *       timeout.
 *
 * Parameters:
 *   eventURL -- The event URL for the subscription
 *   sid -- The subscription id for the subscription
 *   timeout  -- The new timeout for the subscription
 *
 ********************************************************************************/
void CtrlPointHandleSubscribeUpdate(
	const char *eventURL,
	const Upnp_SID sid,
	int timeout)
{
	struct TvDeviceNode *tmpdevnode;
	int service;

	ithread_mutex_lock(&DeviceListMutex);

	tmpdevnode = GlobalDeviceList;
	while (tmpdevnode) {
		for (service = 0; service < TV_SERVICE_SERVCOUNT; service++) {
			if (strcmp
			    (tmpdevnode->device.TvService[service].EventURL,
			     eventURL) == 0) {
				SampleUtil_Print
				    ("Received Tv %s Event Renewal for eventURL %s\n",
				     TvServiceName[service], eventURL);
				strcpy(tmpdevnode->device.TvService[service].
				       SID, sid);
				break;
			}
		}

		tmpdevnode = tmpdevnode->next;
	}

	ithread_mutex_unlock(&DeviceListMutex);

	return;
	timeout = timeout;
}

void CtrlPointHandleGetVar(
	const char *controlURL,
	const char *varName,
	const DOMString varValue)
{

	struct TvDeviceNode *tmpdevnode;
	int service;

	ithread_mutex_lock(&DeviceListMutex);

	tmpdevnode = GlobalDeviceList;
	while (tmpdevnode) {
		for (service = 0; service < TV_SERVICE_SERVCOUNT; service++) {
			if (strcmp
			    (tmpdevnode->device.TvService[service].ControlURL,
			     controlURL) == 0) {
				SampleUtil_StateUpdate(varName, varValue,
						       tmpdevnode->device.UDN,
						       GET_VAR_COMPLETE);
				break;
			}
		}
		tmpdevnode = tmpdevnode->next;
	}

	ithread_mutex_unlock(&DeviceListMutex);
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

	//SampleUtil_PrintEvent(EventType, Event);
	switch ( EventType ) {
	/* SSDP Stuff */
	case UPNP_DISCOVERY_ADVERTISEMENT_ALIVE:
	case UPNP_DISCOVERY_SEARCH_RESULT: {
		struct Upnp_Discovery *d_event = (struct Upnp_Discovery *)Event;
		IXML_Document *DescDoc = NULL;
		int ret;

		if (d_event->ErrCode != UPNP_E_SUCCESS) {
			SampleUtil_Print("Error in Discovery Callback -- %d\n",
				d_event->ErrCode);
		}
		ret = UpnpDownloadXmlDoc(d_event->Location, &DescDoc);
		if (ret != UPNP_E_SUCCESS) {
			SampleUtil_Print("Error obtaining device description from %s -- error = %d\n",
				d_event->Location, ret);
		} else {
			CtrlPointAddDevice(
				DescDoc, d_event->Location, d_event->Expires);
		}
		if (DescDoc) {
			ixmlDocument_free(DescDoc);
		}
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
		struct Upnp_Action_Complete *a_event = (struct Upnp_Action_Complete *)Event;

		if (a_event->ErrCode != UPNP_E_SUCCESS) {
			SampleUtil_Print("Error in  Action Complete Callback -- %d\n",
					a_event->ErrCode);
		}
		/* No need for any processing here, just print out results.
		 * Service state table updates are handled by events. */
		break;
	}
	case UPNP_CONTROL_GET_VAR_COMPLETE: {
		struct Upnp_State_Var_Complete *sv_event = (struct Upnp_State_Var_Complete *)Event;

		if (sv_event->ErrCode != UPNP_E_SUCCESS) {
			SampleUtil_Print("Error in Get Var Complete Callback -- %d\n",
					sv_event->ErrCode);
		} else {
			CtrlPointHandleGetVar(
				sv_event->CtrlUrl,
				sv_event->StateVarName,
				sv_event->CurrentVal);
		}
		break;
	}
	/* GENA Stuff */
	case UPNP_EVENT_RECEIVED: {
		struct Upnp_Event *e_event = (struct Upnp_Event *)Event;

		CtrlPointHandleEvent(
			e_event->Sid,
			e_event->EventKey,
			e_event->ChangedVariables);
		break;
	}
	case UPNP_EVENT_SUBSCRIBE_COMPLETE:
	case UPNP_EVENT_UNSUBSCRIBE_COMPLETE:
	case UPNP_EVENT_RENEWAL_COMPLETE: {
		struct Upnp_Event_Subscribe *es_event = (struct Upnp_Event_Subscribe *)Event;

		if (es_event->ErrCode != UPNP_E_SUCCESS) {
			SampleUtil_Print("Error in Event Subscribe Callback -- %d\n",
					es_event->ErrCode);
		} else {
			CtrlPointHandleSubscribeUpdate(
				es_event->PublisherUrl,
				es_event->Sid,
				es_event->TimeOut);
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
			CtrlPointHandleSubscribeUpdate(
				es_event->PublisherUrl,
				newSID,
				TimeOut);
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
	PRTDEV,
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
	{"PrintDev",      PRTDEV,      2, "<devnum>"},
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
	int rc;
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
	case PRTDEV:
		CtrlPointPrintDevice(arg1);
		break;
	case LSTDEV:
		CtrlPointPrintList();
		break;
	case REFRESH:
		CtrlPointRefresh();
		break;
	case EXITCMD:
		rc = CtrlPointStop();
		exit(rc);
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
