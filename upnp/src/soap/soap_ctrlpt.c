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

#include "config.h"
#ifdef INCLUDE_CLIENT_APIS
#if EXCLUDE_SOAP == 0

#include <assert.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>

#include "miniserver.h"
#include "membuffer.h"
#include "httpparser.h"
#include "httpreadwrite.h"
#include "statcodes.h"
#include "parsetools.h"
#include "upnpapi.h"
#include "soaplib.h"
#include "uri.h"
#include "upnp.h"

#include "unixutil.h"

#define SOAP_ACTION_RESP	1
#define SOAP_VAR_RESP		2
/*#define SOAP_ERROR_RESP       3*/
#define SOAP_ACTION_RESP_ERROR  3
#define SOAP_VAR_RESP_ERROR	4

#undef DBG_TAG
#define DBG_TAG "SOAP"

/*!
 * \brief Adds "MAN" field in the HTTP header.
 *
 * \return 0 on success, UPNP_E_OUTOFMEMORY on error.
 */
static UPNP_INLINE int add_man_header(
	/* [in,out] HTTP header. */
	membuffer *headers)
{
	size_t n;
	char *soap_action_hdr;
	const char *man_hdr =
		"MAN: \"http://schemas.xmlsoap.org/soap/envelope/\"; ns=01\r\n01-";

	/* change POST to M-POST */
	if (membuffer_insert(headers, "M-", 2, 0) != 0)
		return UPNP_E_OUTOF_MEMORY;
	soap_action_hdr = strstr(headers->buf, "SOAPACTION:");
	/* can't fail */
	assert(soap_action_hdr != NULL);
	/* insert MAN header */
	n = (size_t)(soap_action_hdr - headers->buf);
	if (membuffer_insert(headers, man_hdr, strlen(man_hdr), n))
		return UPNP_E_OUTOF_MEMORY;

	return 0;
}

/****************************************************************************
*	Function :	soap_request_and_response
*
*	Parameters :
*		IN membuffer* request :	request that will be sent to the device
*		IN uri_type* destination_url :	destination address string
*		OUT http_parser_t *response :	response from the device
*
*	Description :	This function sends the control point's request to the 
*		device and receives a response from it.
*
*	Return : int
*
*	Note :
****************************************************************************/
static int
soap_request_and_response( IN membuffer * request,
                           IN uri_type * destination_url,
                           OUT http_parser_t * response )
{
    int ret_code;

    ret_code = http_RequestAndResponse( destination_url, request->buf,
                                        request->length,
                                        SOAPMETHOD_POST,
                                        UPNP_TIMEOUT, response );
    if( ret_code != 0 ) {
        httpmsg_destroy( &response->msg );
        return ret_code;
    }
    /* method-not-allowed error */
    if( response->msg.status_code == HTTP_METHOD_NOT_ALLOWED ) {
        ret_code = add_man_header( request );   /* change to M-POST msg */
        if( ret_code != 0 ) {
            return ret_code;
        }

        httpmsg_destroy( &response->msg );  /* about to reuse response */

        /* try again */
        ret_code = http_RequestAndResponse( destination_url, request->buf,
                                            request->length,
                                            HTTPMETHOD_MPOST,
                                            UPNP_TIMEOUT,
                                            response );
        if( ret_code != 0 ) {
            httpmsg_destroy( &response->msg );
        }

    }

    return ret_code;
}

/****************************************************************************
*	Function :	SoapSendAction
*
*	Parameters :
*		IN char* action_url :	device contrl URL 
*		IN char *service_type :	device service type
*		IN IXML_Document *action_node : SOAP action node	
*		OUT IXML_Document **response_node :	SOAP response node
*
*	Description :	This function is called by UPnP API to send the SOAP 
*		action request and waits till it gets the response from the device
*		pass the response to the API layer
*
*	Return :	int
*		returns UPNP_E_SUCCESS if successful else returns appropriate error
*	Note :
****************************************************************************/
int
SoapSendAction( IN char *action_url,
                IN char *service_type)
{
    membuffer request;
    int err_code;
    int ret_code;
    http_parser_t response;
    uri_type url;
    int got_response = FALSE;

    off_t content_length;
    char *request_body;
    size_t request_body_len;

    /*const char *request_body=
	"{\r\n\"command\":\r\n{\"commandName\":\"setName\",\"commandValue\":5,\"commandType\":\"proprietary\"},\r\n\"parameters\":\r\n{\"name\":\"Bed Room\"}\r\n}";*/
    request_body = service_type;
    request_body_len = strlen(request_body);

    err_code = UPNP_E_OUTOF_MEMORY; /* default error */

    CDBG_INFO(
        "Inside SoapSendAction():" );
    /* init */
    membuffer_init( &request );

    /* parse url */
    if( http_FixStrUrl( action_url, strlen( action_url ), &url ) != 0 ) {
        err_code = UPNP_E_INVALID_URL;
        goto error_handler;
    }

    CDBG_INFO("path=%.*s, hostport=%.*s\n",
        (int)url.pathquery.size,
        url.pathquery.buff,
        (int)url.hostport.text.size,
        url.hostport.text.buff );

    /* make request msg */
    request.size_inc = 50;
    content_length = (off_t)(request_body_len);
    if (http_MakeMessage(
	&request, 1, 1,
	"q" "N" "s" "sc" "s" "sc" "Uc" "b",
	SOAPMETHOD_POST, &url,
        content_length,
        ContentTypeHeader,
	"SOAPACTION:",
	"UID: ", "yyccbb12345",
        request_body, request_body_len ) != 0 ) {
        goto error_handler;
    }

    ret_code = soap_request_and_response( &request, &url, &response );
    got_response = TRUE;
    if( ret_code != UPNP_E_SUCCESS ) {
        err_code = ret_code;
        goto error_handler;
    }
    CDBG_INFO("Response:---------------\n%s\n",response.msg.entity.buf);

    err_code = UPNP_E_SUCCESS;
error_handler:
    membuffer_destroy( &request );
    if( got_response ) {
        httpmsg_destroy( &response.msg );
    }

    return err_code;
}

#endif /* EXCLUDE_SOAP */
#endif /* INCLUDE_CLIENT_APIS */
