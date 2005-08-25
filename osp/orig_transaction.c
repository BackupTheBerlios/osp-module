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
#include "destination.h"
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

const int FIRST_ROUTE = 1;
const int NEXT_ROUTE  = 0;


static int loadosproutes(     struct sip_msg* msg, int expectedDestCount);
static int prepareDestination(struct sip_msg* msg, int isFirst);







int requestosprouting(struct sip_msg* msg, char* ignore1, char* ignore2) {

	int res;     /* used for function results */
	int valid;   /* returncode for this function */

	/* parameters for API call */
	char osp_source_dev[200];
	char e164_source[1000];
	char e164_dest [1000];
	unsigned int number_callids = 1;
	OSPTCALLID* call_ids[number_callids];
	unsigned int log_size = 0;
	char* detail_log = NULL;
	const char** preferred = NULL;
	int dest_count;


	
	res = OSPPTransactionNew(_provider, &_transaction);
	getSourceAddress(msg,osp_source_dev);
	getFromUserpart(msg, e164_source);
	getToUserpart(msg, e164_dest);
	getCallId(msg, &(call_ids[0]));
	dest_count = _max_destinations;


	LOG(L_INFO,"osp: Requesting OSP authorization and routing for:\n"
		"osp_source = >%s< \n"
		"osp_source_port = >%s< \n"
		"osp_source_dev = >%s< \n"
		"e164_source = >%s< \n"
		"e164_dest = >%s< \n"
		"number_callids = >%i< \n"
		"callids[0] = >%.*s< \n"
		"dest_count = >%i< \n"
		"\n", 
		_device_ip,
		_device_port,
		osp_source_dev,
		e164_source,
		e164_dest,
		number_callids,
		call_ids[0]->ospmCallIdLen,
		call_ids[0]->ospmCallIdVal,
		dest_count
	);	


	if (strlen(_device_port) > 0) {
		OSPPTransactionSetNetworkIds(_transaction,_device_port,"");
	}

	/* try to request authorization */
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

	if (res == 0 && dest_count > 0) {
		LOG(L_INFO, "osp: there is %d osp routes.\n", dest_count);
		valid = loadosproutes(msg,dest_count);
	} else if (res == 0 && dest_count == 0) {
		LOG(L_INFO, "osp: there is 0 osp routes, the route is blocked\n");
		valid = MODULE_RETURNCODE_STOPROUTE;
	} else {
		LOG(L_ERR, "ERROR: osp: OSPPTransactionRequestAuthorisation returned %i\n", res);
		valid = MODULE_RETURNCODE_ERROR;
	}

	if (call_ids[0]!=NULL) {
		OSPPCallIdDelete(&(call_ids[0]));
	}
	
	return valid;
}


static int loadosproutes(struct sip_msg* msg, int expectedDestCount) {
	int result = MODULE_RETURNCODE_SUCCESS;
	int res;
	int count;

	osp_dest  dests[MAX_DESTS];
	osp_dest* dest;
	
	for (count = 0; count < expectedDestCount; count++) {

		dest = initDestination(&dests[count]);

		if (dest == NULL) {
			result = MODULE_RETURNCODE_ERROR;
			break;
		}

		if (count==0) {
			res = OSPPTransactionGetFirstDestination(
				_transaction,
				sizeof(dest->validafter),
				dest->validafter,
				dest->validuntil,
				&dest->timelimit,
				&dest->sizeofcallid,
				(void*)dest->callid,
				sizeof(dest->callednumber),
				dest->callednumber,
				sizeof(dest->callingnumber),
				dest->callingnumber,
				sizeof(dest->destination),
				dest->destination,
				sizeof(dest->destinationdevice),
				dest->destinationdevice,
				&dest->sizeoftoken,
				dest->osptoken);
		} else {
			res = OSPPTransactionGetNextDestination(
				_transaction,
				0,
				sizeof(dest->validafter),
				dest->validafter,
				dest->validuntil,
				&dest->timelimit,
				&dest->sizeofcallid,
				(void*)dest->callid,
				sizeof(dest->callednumber),
				dest->callednumber,
				sizeof(dest->callingnumber),
				dest->callingnumber,
				sizeof(dest->destination),
				dest->destination,
				sizeof(dest->destinationdevice),
				dest->destinationdevice,
				&dest->sizeoftoken,
				dest->osptoken);
		}
		
		if (res != 0) {
			LOG(L_ERR,"ERROR: osp: getDestination %d failed, expected number %d, current count %d\n",res,expectedDestCount,count);
			result = MODULE_RETURNCODE_ERROR;
			break;
		}

		OSPPTransactionGetDestNetworkId(_transaction,dest->network_id);

		LOG(L_INFO,"osp: getDestination %d returned the following information:\n"
		"  valid after: %s\n"
		"  valid until: %s\n"
		"   time limit: %i seconds\n"
		"       callid: %.*s\n"
		"calling number: %s\n"
		"called number: %s\n"
		"  destination: %s\n"
		"   network id: %s\n"
		"bn token size: %i\n",
		count, dest->validafter, dest->validuntil, dest->timelimit, dest->sizeofcallid, dest->callid, dest->callingnumber, dest->callednumber, 
		dest->destination, dest->network_id, dest->sizeoftoken);
	}

	/* save destination in reverse order,
	 * this way, when we start searching avps the destinations
	 * will be in order 
	 */
	if (result == MODULE_RETURNCODE_SUCCESS) {
		for(count = expectedDestCount -1; count >= 0; count--) {
			saveDestination(&dests[count]);
		}
	}

	OSPPTransactionDelete(_transaction);
	
	return result;
}





int preparefirstosproute(struct sip_msg* msg, char* ignore1, char* ignore2) {
	int result = MODULE_RETURNCODE_SUCCESS;

	LOG(L_INFO, "osp: Preparing 1st route\n");

	result = prepareDestination(msg,FIRST_ROUTE);

	return result;
}




int preparenextosproute(struct sip_msg* msg, char* ignore1, char* ignore2) {
	int result = MODULE_RETURNCODE_SUCCESS;

	LOG(L_INFO, "osp: Preparing next route\n");

	result = prepareDestination(msg,NEXT_ROUTE);


	return result;
}




int prepareallosproutes(struct sip_msg* msg, char* ignore1, char* ignore2) {
	int result = MODULE_RETURNCODE_SUCCESS;

	for( result = preparefirstosproute(msg,ignore1,ignore2);
	     result == MODULE_RETURNCODE_SUCCESS;
	     result = preparenextosproute(msg,ignore1,ignore2)) {
	}
	return result;
}




int prepareDestination(struct sip_msg* msg, int isFirst) {
	int result = MODULE_RETURNCODE_SUCCESS;
	str newuri = {NULL,0};

	osp_dest* dest = getDestination();

	if (dest != NULL) {

		rebuildDestionationUri(&newuri, dest->destination, dest->network_id, dest->callednumber);

		LOG(L_INFO, "osp: Preparing route to uri '%.*s'\n",newuri.len,newuri.s);

		if (isFirst == FIRST_ROUTE) {
			rewrite_uri(msg, &newuri);
			addOspHeader(msg,dest->osptoken,dest->sizeoftoken);
		} else {
			append_branch(msg, newuri.s, newuri.len, 0, 0, 0);
		}

	} else {
		LOG(L_INFO, "osp: There is no more routes\n");
		result = MODULE_RETURNCODE_STOPROUTE;
	}

	if (newuri.len > 0) {
		pkg_free(newuri.s);
	}
	
	return result;
}
