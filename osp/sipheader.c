/*
 * ser osp module. 
 *
 * This module enables ser to communicate with an Open Settlement 
 * Protocol (OSP) server.  The Open Settlement Protocol is an ETSI 
 * defined standard for Inter-Domain VoIP pricing, authorization
 * and usage exchange.  The technical specifications for OSP 
 * (ETSI TS 101 321 V4.1.1) are available at www.etsi.org.
 *
 * Uli Abend was the original contributor to this module.
 * 
 * Copyright (C) 2001-2005 Fhg Fokus
 *
 * This file is part of ser, a free SIP server.
 *
 * ser is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * For a license to use the ser software under conditions
 * other than those described here, or to purchase support for this
 * software, please contact iptel.org by e-mail at the following addresses:
 *    info@iptel.org
 *
 * ser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "osp_mod.h"
#include "provider.h"
#include "sipheader.h"
#include "../../sr_module.h"
#include <stdio.h>
#include "osp/osp.h"
#include "osp/ospb64.h"
#include "../../mem/mem.h"
#include "../../timer.h"
#include "../../locking.h"
#include "../../forward.h"
#include "../../parser/parse_uri.h"
#include "../../parser/parse_from.h"
#include "../../parser/parse_rr.h"





int getFromUserpart(struct sip_msg* msg, char *fromuser) {
	struct to_body* from;
	struct sip_uri uri;
	int retVal = 1;

	strcpy(fromuser, "");

	if (msg->from != NULL) {
		if (parse_from_header(msg) == 0) {
			from = ((struct to_body *)msg->from->parsed);
			if (parse_uri(from->uri.s, from->uri.len, &uri) == 0) {
				strncpy(fromuser, uri.user.s, uri.user.len);
				fromuser[uri.user.len] = '\0';
				skipPlus(fromuser);
				retVal = 0;
			} else {
				LOG(L_ERR, "ERROR: osp: getFromUserpart: could not parse from uri\n");
			}
		} else {
			LOG(L_ERR, "ERROR: osp: getFromUserpart: could not parse from header\n");
		}
	} else {
		LOG(L_ERR, "ERROR: osp: getFromUserpart: could not find from header\n");
	}

	return 0;
}

int getToUserpart(struct sip_msg* msg, char *touser) {
	struct to_body* to;
	struct sip_uri uri;
	int retVal = 1;

	strcpy(touser, "");

	if (msg->to != NULL) {
		to = ((struct to_body *)msg->to->parsed);

		if (parse_uri(to->uri.s, to->uri.len, &uri) == 0) {
			strncpy(touser, uri.user.s, uri.user.len);
			touser[uri.user.len] = '\0';
			skipPlus(touser);
			retVal = 0;
		} else {
			LOG(L_ERR, "ERROR: osp: getToUserpart: could not parse to uri\n");
		}
	} else {
		LOG(L_ERR, "ERROR: osp: getToUserpart: could not find to header\n");
	}

	return retVal;
}


void skipPlus(char* e164) {
	if (*e164 == '+') {
		strncpy(e164,e164+1,strlen(e164)-1);
		e164[strlen(e164)-1] = 0;
	}
}




int addOspHeader(struct sip_msg* msg, char* token, int sizeoftoken) {

	char headerBuffer[3500];
	char encodedToken[3000];
	int  sizeofencodedToken = sizeof(encodedToken);
	str  headerVal;
	int  retVal = 1;

	if (OSPPBase64Encode(token, sizeoftoken, encodedToken, &sizeofencodedToken) == 0) {
		snprintf(headerBuffer,
			 sizeof(headerBuffer),
			 "%s%.*s\r\n", 
			 OSP_HEADER,
			 sizeofencodedToken,
			 encodedToken);

		headerVal.s = headerBuffer;
		headerVal.len = strlen(headerBuffer);

		DBG("osp: Setting osp token header field - (%s)\n", headerBuffer);

		if (append_hf(msg,(char *)&headerVal,NULL) > 0) {
			retVal = 0;
		} else {
			LOG(L_ERR, "ERROR: osp: addOspHeader: failed to append osp header to the message\n");
		}
	} else {
		LOG(L_ERR, "ERROR: osp: addOspHeader: base64 encoding failed\n");
	}

	return retVal;
}




int getOspHeader(struct sip_msg* msg, char* token, int* sizeoftoken) {
	struct hdr_field *hf;

	int code;
	int retVal = 1;

	parse_headers(msg, HDR_EOH, 0);

	for (hf=msg->headers; hf; hf=hf->next) {
		if ( (hf->type == HDR_OTHER) && (hf->name.len == OSP_HEADER_LEN-2)) {
			// possible hit
			if (strncasecmp(hf->name.s, OSP_HEADER, OSP_HEADER_LEN) == 0) {
				if ( (code=OSPPBase64Decode(hf->body.s, hf->body.len, token, sizeoftoken)) == 0) {
					retVal = 0;
				} else {
					LOG(L_ERR, "ERROR: osp: getOspHeader: failed to base64 decode OSP token, reason - %d\n",code);
					LOG(L_ERR, "ERROR: osp: header '%.*s' length %d\n",hf->body.len,hf->body.s,hf->body.len);
				}
				break;
			}		
		} 
	}

	return retVal;
}




int getCallId(struct sip_msg* msg, OSPTCALLID** callid) {

	struct hdr_field *cid;
	int retVal = 1;

	cid = (struct hdr_field*) msg->callid;

	if (cid != NULL) {
		*callid = OSPPCallIdNew(cid->body.len,cid->body.s);
		retVal = 0;
	} else {
		LOG(L_ERR, "ERROR: osp: getCallId: could not find Call-Id-Header\n");
	}	

	return retVal;
}


/* get first VIA header and uses the IP and port found there
 * output format is [0.0.0.0]:5060 
 */
int getSourceAddress(struct sip_msg* msg, char* source_address) {
	struct hdr_field* hf;
	struct via_body* via_body;
	int retVal = 1;

	/* no need to call parse_headers, called already and VIA is parsed
	 * anyway by default 
	 */
	for (hf=msg->headers; hf; hf=hf->next) {
		if (hf->type == HDR_VIA) {
			// found first VIA
			via_body = (struct via_body*)hf->parsed;	
			/* check if hostname or IP address */
			if ( (*(via_body->host.s) < '0') || (*(via_body->host.s) > '9') ) {
				/* hostname does not start with number */
				strncpy(source_address, via_body->host.s, via_body->host.len);
			} else {
				/* IP addresses have to be enclosed by [ ] */
				*source_address = '[';
				strncpy(source_address+1, via_body->host.s, via_body->host.len);
				*(source_address+via_body->host.len+1) = ']';
				*(source_address+via_body->host.len+2) = '\0';
			}	
			strncat(source_address, ":", 1);
			strncat(source_address, via_body->port_str.s, via_body->port_str.len);

			DBG("osp: getSourceAddress: result is %s\n", source_address);

			retVal = 0;
			break;
		} 
	}

	return retVal;
}


/* Get route parameters from the 1st Route or Request Line
*/
int getRouteParams(struct sip_msg* msg, char* route_params) {
	int retVal = 1;

        struct hdr_field* hdr;
        struct sip_uri puri;
        rr_t* rt;

	 DBG("osp: getRouteParams: parsed uri: host '%.*s' port '%d' vars '%.*s'\n",
             msg->parsed_uri.host.len,msg->parsed_uri.host.s,
             msg->parsed_uri.port_no,
             msg->parsed_uri.params.len,msg->parsed_uri.params.s);

        if (!(hdr=msg->route)) {
                DBG("osp: getRouteParams: there is no route headers\n");
        } else if (!(rt=(rr_t*)hdr->parsed)) {
                LOG(L_ERR, "ERROR: osp: getRouteParams: the route headers are not parsed\n");
        } else if (parse_uri(rt->nameaddr.uri.s,rt->nameaddr.uri.len,&puri) != 0) {
                LOG(L_ERR, "ERROR: osp: getRouteParams: failed to parse the route URI '%.*s'\n",rt->nameaddr.uri.len,rt->nameaddr.uri.s);
        } else if (check_self(&puri.host, puri.port_no ? puri.port_no : SIP_PORT, PROTO_NONE) != 1) {
                DBG("osp: getRouteParams: the route uri is NOT mine\n");
                DBG("osp: getRouteParams: host '%.*s' port '%d'\n",puri.host.len,puri.host.s,puri.port_no);
                DBG("osp: getRouteParams: params '%.*s'\n",puri.params.len,puri.params.s);
        } else {
                DBG("osp: getRouteParams: the Route IS mine - '%.*s'\n",puri.params.len,puri.params.s);
                DBG("osp: getRouteParams: host '%.*s' port '%d'\n",puri.host.len,puri.host.s,puri.port_no);
                strncpy(route_params,puri.params.s,puri.params.len);
                route_params[puri.params.len] = 0;
                retVal = 0;
        }

        if (retVal == 1 && msg->parsed_uri.params.len > 0) {
                DBG("osp: getRouteParams: using route params fromt he request uri\n");
                strncpy(route_params,msg->parsed_uri.params.s,msg->parsed_uri.params.len);
                route_params[msg->parsed_uri.params.len] = 0;
                retVal = 0;
        }

	return retVal;
}



int rebuildDestionationUri(str *newuri, char *destination, char *port, char *callednumber) {
	#define TRANS ";transport=tcp" 
	#define TRANS_LEN 14
	char* buf;
	int sizeofcallednumber;
	int sizeofdestination;
	int sizeofport;

	sizeofcallednumber = strlen(callednumber);
	sizeofdestination = strlen(destination);
	sizeofport = strlen(port);

	DBG("osp: rebuilddestionationstr >%s<(%i) >%s<(%i) >%s<(%i)\n",
		callednumber, sizeofcallednumber, destination, sizeofdestination, port, sizeofport);


		
	/* "sip:+" + callednumber + "@" + destination + : + port + " SIP/2.0" */
	newuri->s = (char*) pkg_malloc(sizeofdestination + sizeofcallednumber + sizeofport + 1 + 16 + TRANS_LEN);
	if (newuri == NULL) {
		LOG(L_ERR, "ERROR: osp: rebuilddestionationstr: no memory\n");
		return 1;
	}	
	buf = newuri->s;

	*buf++ = 's';
	*buf++ = 'i';
	*buf++ = 'p';
	*buf++ = ':';

	/* Don't prepend +
	if (*callednumber == '+') {
		// already starts with
	} else {
		*buf++ = '+';
	}
	*/

	memcpy(buf, callednumber, sizeofcallednumber);
	buf += sizeofcallednumber;
	*buf++ = '@';
	
	if (*destination == '[') {
		/* leave out annoying [] */
		memcpy(buf, destination+1, sizeofdestination-2);
		buf += sizeofdestination-2;
	} else {
		memcpy(buf, destination, sizeofdestination);
		buf += sizeofdestination;
	}
	
	if (sizeofport > 0) {
		*buf++ = ':';
		memcpy(buf, port, sizeofport);
		buf += sizeofport;
	}

/*	*buf++ = ' ';
	*buf++ = 'S';
	*buf++ = 'I';
	*buf++ = 'P';
	*buf++ = '/';
	*buf++ = '2';
	*buf++ = '.';
	*buf++ = '0';
*/
	/* zero terminate for convenience */
	//memcpy(buf, TRANS, TRANS_LEN);
	//buf+=TRANS_LEN;
	//*buf = '\0';
	newuri->len = buf - newuri->s;

	DBG("newuri is >%.*s<\n", newuri->len, newuri->s);
	return 0; /* success */
}

