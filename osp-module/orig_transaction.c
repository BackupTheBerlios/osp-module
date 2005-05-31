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
#include "orig_transaction.h"
#include "sipheader.h"
#include "osp/osp.h"
#include "../../sr_module.h"
#include "../../locking.h"
#include "../../mem/mem.h"
#include "../../dset.h"

extern int   _max_destinations;
extern char* _device_ip;
extern char* _device_port;
extern OSPTPROVHANDLE _provider;

OSPTTRANHANDLE _transaction = -1;

#define lfr(x) if(x!=NULL) shm_free(x)


int requestosprouting(struct sip_msg* msg, char* ignore1, char* ignore2) {

	int res;     /* used for function results */
	int valid;   /* returncode for this function */

	/* parameters for API call */
	char osp_source_dev[200];
	char e164_source[1000];
	char e164_dest [1000];
	unsigned int number_callids = 1;
	OSPTCALLID* call_ids[number_callids];
	unsigned int dest_count = _max_destinations;
	unsigned int log_size = 0;
	char* detail_log = NULL;
	const char** preferred = NULL;


	
	res = OSPPTransactionNew(_provider, &_transaction);
	getSourceAddress(msg,osp_source_dev);
	getFromUserpart(msg, e164_source);
	getToUserpart(msg, e164_dest);
	getCallId(msg, &(call_ids[0]));


	DBG("dumping params for OSPPTransactionRequestAuthorisation:\n"
		"osp_source = >%s< \n"
		"osp_source_port = >%s< \n"
		"osp_source_dev = >%s< \n"
		"e164_source = >%s< \n"
		"e164_dest = >%s< \n"
		"number_callids = >%i< \n"
		"callids[0] = >%s< \n"
		"dest_count = >%i< \n"
		"\n", 
		_device_ip,
		_device_port,
		osp_source_dev,
		e164_source,
		e164_dest,
		number_callids,
		call_ids[0]->ospmCallIdVal,
		dest_count
	);	


	/* try to request authorization */
	OSPPTransactionSetNetworkIds(_transaction,_device_port,"");
	res = OSPPTransactionRequestAuthorisation(
	_transaction,       /* transaction handle */
	_device_ip,         /* from the configuration file */
	osp_source_dev,    /* source of call, protocol specific */
	e164_source,       /* calling number in nodotted e164 notation */
	OSPC_E164,
	e164_dest,         /* called number */
	OSPC_E164,
	"",                /* optional username string, used if no number */
	number_callids,    /* number of call ids, here always 1 */
	call_ids,          /* sized-1 array of call ids */
	preferred,         /* preferred destinations, here always NULL */
	&dest_count,       /* max destinations, after call dest_count */
	&log_size,          /* size allocated for detaillog (next param) 0=no log */
	detail_log);       /* memory location for detaillog to be stored */

	if (res == 0) {
		valid = MODULE_RETURNCODE_SUCCESS;
	} else {
		valid = MODULE_RETURNCODE_ERROR;
		LOG(L_ERR, "ERROR: osp: requestosprouting: OSPPTransactionRequestAuthorisation returned %i\n", res);
	}

	if (call_ids[0]!=NULL) {
		OSPPCallIdDelete(&(call_ids[0]));
	}
	
	return valid;
}


int tryallosproutes(struct sip_msg* msg, char* ignore1, char* ignore2) {
	int result = MODULE_RETURNCODE_SUCCESS;
	int res; /* result of api call */
	int newuriset = 0; /* check if we free newuri or not */
	int count;
	
	/* memory locations for result of get*Destination */
	char validafter[100];
	char validuntil[100];
	char callid[100];
	char callednumber[100];
	char callingnumber[100];
	char destination[100];
	char destinationdevice[100];
	char port[100];
	char osptoken[2000];

	unsigned int sizeofcallid = sizeof(callid);
	unsigned int timelimit = 0;
	unsigned int sizeoftoken = sizeof(osptoken);

	str newuri; /* new SIP uri after routing */
	newuri.s = NULL;

	for (count = 1;; count++) {
		if (count==1) {
			res = OSPPTransactionGetFirstDestination(
				_transaction,
				sizeof(validafter),
				validafter,
				validuntil,
				&timelimit,
				&sizeofcallid,
				(void*) callid,
				sizeof(callednumber),
				callednumber,
				sizeof(callingnumber),
				callingnumber,
				sizeof(destination),
				destination,
				sizeof(destinationdevice),
				destinationdevice,
				&sizeoftoken,
				osptoken);
		} else {
			res = OSPPTransactionGetNextDestination(
				_transaction,
				0,
				sizeof(validafter),
				validafter,
				validuntil,
				&timelimit,
				&sizeofcallid,
				(void*) callid,
				sizeof(callednumber),
				callednumber,
				sizeof(callingnumber),
				callingnumber,
				sizeof(destination),
				destination,
				sizeof(destinationdevice),
				destinationdevice,
				&sizeoftoken,
				osptoken);
		}
		
		if (res != 0) {
			LOG(L_INFO,"getDestination %d failed, no more destinations\n",count);
			break;
		}

		strcpy(port,"");

		OSPPTransactionGetDestNetworkId(_transaction,port);

		LOG(L_INFO,"getDestination %d returned the following information:\n"
		"  valid after: %s\n"
		"  valid until: %s\n"
		"   time limit: %i seconds\n"
		"       callid: %.*s\n"
		"called number: %s\n"
		"  destination: %s\n"
		"   network id: %s\n"
		"bn token size: %i\n",
		count, validafter, validuntil, timelimit, sizeofcallid, callid, callednumber, 
		destination, port, sizeoftoken);


		rebuildDestionationUri(&newuri, destination, port, callednumber);
		newuriset = 1;

		LOG(L_INFO,"new uri=%s\n",newuri.s);

		if (count==1) {
			LOG(L_INFO,"rewriting uri\n");
			rewrite_uri(msg, &newuri);
			addOspHeader(msg,osptoken,sizeoftoken);
		} else {
			LOG(L_INFO,"appending branch\n");
			append_branch(msg, newuri.s, newuri.len, 0, 0, 0);
		}
	}

	if (newuriset == 0)
		lfr(newuri.s);

	OSPPTransactionDelete(_transaction);
	
	LOG(L_INFO,"returning %d\n", result);

	return result;
}
