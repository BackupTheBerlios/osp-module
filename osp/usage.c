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



#include "usage.h"
#include "sipheader.h"
#include "destination.h"
#include "osptoolkit.h"
#include "../../sr_module.h"
#include "../../usr_avp.h"
#include <string.h>


extern OSPTPROVHANDLE _provider;
extern char* _device_ip;
extern str ORIG_OSPDESTS_LABEL;

int buildUsageFromDestination(OSPTTRANHANDLE transaction, osp_dest* dest, int last_code_for_previous_destination);
int reportUsageFromDestination(OSPTTRANHANDLE transaction, osp_dest* dest);
int reportUsageFromCooky(char* cooky, OSPTCALLID* call_id, int isOrig, struct sip_msg* msg);

#define OSP_ORIG_COOKY "osp-o"
#define OSP_TERM_COOKY "osp-t"

#define RELEASE_SOURCE_ORIG 0
#define RELEASE_SOURCE_TERM 1

/*
 *
 *
 */
void record_transaction_info(struct sip_msg* msg, OSPTTRANHANDLE transaction, char* uac, char* from, char* to, time_t time_auth, int isOrig)
{

	char buf[1000];
	str  toAdd;

	sprintf(buf,
		";%s=t%llu_s%s_T%d",
		(isOrig==1?OSP_ORIG_COOKY:OSP_TERM_COOKY),
		get_transaction_id(transaction),
		uac,
		(unsigned int)time_auth);

	toAdd.s = buf;
	toAdd.len = strlen(buf);

	DBG("osp:record_transaction_info: adding rr param '%s'\n",buf);

	if (add_rr_param) {
		add_rr_param(msg,(char *)&toAdd,NULL);
	} else {
		LOG(L_WARN,"osp:record_transaction_info: add_rr_param function is not found, can not record information about the OSP transaction\n");
	}
}




void record_orig_transaction(struct sip_msg* msg, OSPTTRANHANDLE transaction, char* uac, char* from, char* to, time_t time_auth)
{
	int isOrig = 1;

	record_transaction_info(msg,transaction,uac,from,to,time_auth,isOrig);
}


void record_term_transaction(struct sip_msg* msg, OSPTTRANHANDLE transaction, char* uac, char* from, char* to, time_t time_auth)
{
	int isOrig = 0;

	record_transaction_info(msg,transaction,uac,from,to,time_auth,isOrig);
}






int reportospusage(struct sip_msg* msg, char* ignore1, char* ignore2)
{

	int result = MODULE_RETURNCODE_FALSE;

	char route_params[2000];
	int  isOrig;
	char *tmp;
	char *token;

	OSPTCALLID* call_id = NULL;

	getCallId(msg, &call_id);

	if (getRouteParams(msg,route_params)==0) {
		for (token = strtok_r(route_params,";",&tmp);
		     token;
		     token = strtok_r(NULL,";",&tmp)) {

			if (strncmp(token,OSP_ORIG_COOKY,strlen(OSP_ORIG_COOKY))==0) {
				LOG(L_INFO,"osp: Reporting originating call duration usage for call-id '%.*s'\n",call_id->ospmCallIdLen,call_id->ospmCallIdVal);
				isOrig = 1;
				reportUsageFromCooky(token+strlen(OSP_ORIG_COOKY)+1, call_id, isOrig, msg);
				result = MODULE_RETURNCODE_TRUE;
			} else if (strncmp(token,OSP_TERM_COOKY,strlen(OSP_TERM_COOKY))==0) {
				LOG(L_INFO,"osp: Reporting terminating call duration usage for call-id '%.*s'\n",call_id->ospmCallIdLen,call_id->ospmCallIdVal);
				isOrig = 0;
				reportUsageFromCooky(token+strlen(OSP_TERM_COOKY)+1, call_id, isOrig, msg);
				result = MODULE_RETURNCODE_TRUE;
			} else {
				DBG("osp:reportusage: ignoring param '%s'\n",token);
			}
		}
	}

	if (call_id!=NULL) {
		OSPPCallIdDelete(&call_id);
	}

	if (result==MODULE_RETURNCODE_FALSE) {
		DBG("There is no OSP originating or terminating usage information\n");
	}

	return result;
}




int reportUsageFromCooky(char* cooky, OSPTCALLID* call_id, int isOrig, struct sip_msg* msg)
{

	int errorCode = 0;

	char *tmp;
	char *token;
	char name;
	char *value;

	unsigned long long transaction_id = 0;
	char *user_agent_client = "";
	time_t auth_time = -1;
	time_t end_time = time(NULL);

	int release_source;
	char first_via[200];
	char from[200];
	char to[200];
	char next_hop[200];
	char *calling;
	char *called;
	char *terminator;

	OSPTTRANHANDLE transaction_handle = -1;

	DBG("osp:reportUsageFromCooky: '%s' isOrig '%d'\n",cooky,isOrig);


	for (token = strtok_r(cooky,"_",&tmp);
	     token;
	     token = strtok_r(NULL,"_",&tmp)) {

		name = *token;
		value= token + 1;

		switch (name) {
			case 't':
				transaction_id = atoll(value);
				break;
			case 'T':
				auth_time = atoi(value);
				break;
			case 's':
				user_agent_client = value;
				break;
			default:
				LOG(L_ERR,"osp:reportUsageFromCooky: unexpected tag '%c' / value '%s'\n",name,value);
				break;
		}
	}

	getSourceAddress(msg,first_via);
	getFromUserpart( msg, from);
	getToUserpart(   msg, to);
	getNextHop(      msg, next_hop);

	if (strcmp(first_via,user_agent_client)==0) {
		LOG(L_INFO,"osp: Originator '%s' released the call\n",first_via);
		release_source = RELEASE_SOURCE_ORIG;
		calling = from;
		called = to;
		terminator = next_hop;
	} else {
		LOG(L_INFO,"osp: Terminator '%s' released the call\n",first_via);
		release_source = RELEASE_SOURCE_TERM;
		calling = to;
		called = from;
		terminator = first_via;
	}


	errorCode = OSPPTransactionNew(_provider, &transaction_handle);

	DBG("osp:reportUsageFromCooky: created new transaction handle %d / code %d\n",transaction_handle,errorCode);

	errorCode = OSPPTransactionBuildUsageFromScratch(
		transaction_handle,
		transaction_id,
		isOrig==1?OSPC_SOURCE:OSPC_DESTINATION,
		isOrig==1?_device_ip:user_agent_client,
		isOrig==1?terminator:_device_ip,
		isOrig==1?user_agent_client:"",
		"",
		calling,
		OSPC_E164,
		called,
		OSPC_E164,
		call_id->ospmCallIdLen,
		call_id->ospmCallIdVal,
		(enum OSPEFAILREASON)0,
		NULL,
		NULL);

	DBG("osp:reportUsageFromCooky: started building usage handle %d / code %d\n",transaction_handle,errorCode);

	OSPPTransactionRecordFailure(
		transaction_handle,
		(enum OSPEFAILREASON)1016);

	errorCode = OSPPTransactionReportUsage(
		transaction_handle,
		end_time - auth_time,
		auth_time,
		end_time,
		0,0,
		0,0,
		release_source,
		NULL,
		0,0,0,0,
		NULL,NULL);

	DBG("osp:reportUsageFromCooky: reported usage handle %d / code %d\n",transaction_handle,errorCode);

	errorCode = OSPPTransactionDelete(transaction_handle);

	DBG("osp:reportUsageFromCooky: delete transaction handle %d / code %d\n",transaction_handle,errorCode);

	return errorCode;
}




void reportOrigCallSetUpUsage()
{
	osp_dest*       dest          = NULL;
	osp_dest*       last_used_dest= NULL;
	struct usr_avp* dest_avp      = NULL;
	int_str         dest_val;
	int             errorCode     = 0;
	OSPTTRANHANDLE  transaction   = -1;
	int             last_code_for_previous_destination = 0;

	DBG("osp: reportOrigCallSetUpUsage: IN\n");

	errorCode = OSPPTransactionNew(_provider, &transaction);

	for (	dest_avp=search_first_avp(AVP_NAME_STR|AVP_VAL_STR,(int_str)&ORIG_OSPDESTS_LABEL,NULL);
		dest_avp != NULL;
		dest_avp=search_next_avp(dest_avp,NULL)) {

		get_avp_val(dest_avp, &dest_val);

		/* osp dest is wrapped in a string */
		dest = (osp_dest *)dest_val.s->s;

		if (dest->used == 1) {
			if (dest->reported == 1) {
				DBG("osp: reportOrigCallSetUpUsage: source usage has already been reported\n");
				break;
			}

			DBG("osp: reportOrigCallSetUpUsage: iterating through a used destination\n");

			dumbDestDebugInfo(dest);

			last_used_dest = dest;

			errorCode = buildUsageFromDestination(transaction,dest,last_code_for_previous_destination);

			last_code_for_previous_destination = dest->last_code;
		} else {
			DBG("osp: reportOrigCallSetUpUsage: this destination has not been used, breaking out\n");
			break;
		}
	}

	if (last_used_dest) {
		LOG(L_INFO,"osp: Reporting originating call set-up usage for call-id '%.*s'\n",last_used_dest->sizeofcallid,last_used_dest->callid);
		errorCode = reportUsageFromDestination(transaction, last_used_dest);
	}

	DBG("osp: reportOrigCallSetUpUsage: OUT\n");
}



void reportTermCallSetUpUsage()
{

	osp_dest*       dest          = NULL;
	int             errorCode     = 0;
	OSPTTRANHANDLE  transaction   = -1;

	DBG("osp: reportTermCallSetUpUsage: IN\n");

	if ((dest=getTermDestination())) {

		if (dest->reported == 0) {
			LOG(L_INFO,"osp: Reporting terminating call set-up usage for call-id '%.*s'\n",dest->sizeofcallid,dest->callid);
			errorCode = OSPPTransactionNew(_provider, &transaction);
			errorCode = buildUsageFromDestination(transaction,dest,0);
			errorCode = reportUsageFromDestination(transaction,dest);
		} else {
			DBG("osp: reporTermtCallSetUpUsage: destination usage has already been reported\n");
		}

	} else {
		DBG("osp: reportTermCallSetUpUsage: There is no term dest to report\n");
	}

	DBG("osp: reportTermCallSetUpUsage: OUT\n");
}







int buildUsageFromDestination(OSPTTRANHANDLE transaction, osp_dest* dest, int last_code_for_previous_destination)
{

	int errorCode;

	dest->reported = 1;

	errorCode = OSPPTransactionBuildUsageFromScratch(
		transaction,
		dest->tid,
		dest->type,
		dest->source,
		dest->destination,
		dest->sourcedevice,
		dest->destinationdevice,
		dest->callingnumber,
		OSPC_E164,
		dest->callednumber,
		OSPC_E164,
		dest->sizeofcallid,
		dest->callid,
		(enum OSPEFAILREASON)last_code_for_previous_destination,
		NULL,
		NULL);

	return errorCode;
}



int reportUsageFromDestination(OSPTTRANHANDLE transaction, osp_dest* dest)
{

	int errorCode;

	OSPPTransactionRecordFailure(
		transaction,
		(enum OSPEFAILREASON)dest->last_code);

	errorCode = OSPPTransactionReportUsage(
		transaction,              /* In - Transaction handle */
		0,                        /* In - Length of call */
		dest->time_auth,          /* In - Call start time */
		0,                        /* In - Call end time */
		dest->time_180,           /* In - Call alert time */
		dest->time_200,           /* In - Call connect time */
		dest->time_180 ? 1:0,     /* In - Is PDD Info present */
                                          /* In - Post Dial Delay */
		dest->time_180 ? dest->time_180 - dest->time_auth : 0,
		0,                        /* In - EP that released the call */
		"",                       /* In - conference Id. Max 100 char long */
		0,                        /* In - Packets not received by peer */
		0,                        /* In - Fraction of packets not received by peer */
		0,                        /* In - Packets not received that were expected */
		0,                        /* In - Fraction of packets expected but not received */
		NULL,NULL);

	DBG("osp:reportCallSetUpUsage reported usage, code %d\n", errorCode);

	OSPPTransactionDelete(transaction);

	return errorCode;
}




