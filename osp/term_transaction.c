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
#include "term_transaction.h"
#include "sipheader.h"
#include "osp/osp.h"
#include "osp/ospb64.h"
#include "../../sr_module.h"
#include "../../locking.h"
#include "../../mem/mem.h"

extern OSPTPROVHANDLE _provider;
extern int _token_format;


int checkospheader(struct sip_msg* msg, char* ignore1, char* ignore2) {
	char temp[3000];
	int  sizeoftemp = sizeof(temp);


	if (getOspHeader(msg, temp, &sizeoftemp) != 0) {
		return MODULE_RETURNCODE_ERROR;
	} else {
		return MODULE_RETURNCODE_SUCCESS;
	}
}




/** validate OSP header */
int validateospheader (struct sip_msg* msg, char* ignore1, char* ignore2) {
	int res; 
	int valid;

	OSPTTRANHANDLE transaction = -1;
	char e164_source[1000];
	char e164_dest [1000];
	unsigned int authorized = 0;
	unsigned int time_limit = 0;
	unsigned int log_size = 0;
	void *detail_log = NULL;
	OSPTCALLID* call_id = NULL;

	char token[3000];
	unsigned int sizeoftoken = sizeof(token);


	valid = MODULE_RETURNCODE_ERROR;

	if (0!= (res=OSPPTransactionNew(_provider, &transaction))) {
		LOG(L_ERR, "ERROR: osp: Failed to create a new OSP transaction id %d\n",res);
	} else if (0 != getFromUserpart(msg, e164_source)) {
		LOG(L_ERR, "ERROR: osp: Failed to extract calling number\n");
	} else if (0 != getToUserpart(msg, e164_dest)) {
		LOG(L_ERR, "ERROR: osp: Failed to extract called number\n");
	} else if (0 != getCallId(msg, &call_id)) {
		LOG(L_ERR, "ERROR: osp: Failed to extract call id\n");
	} else if (0 != getOspHeader(msg, token, &sizeoftoken)) {
		LOG(L_ERR, "ERROR: osp: Failed to extract OSP authorization token\n");
	} else {
		LOG(L_INFO, "About to validate OSP token for:\n"
			"transaction = >%i< \n"
			"e164_source = >%s< \n"
			"e164_dest = >%s< \n"
			"callid = >%.*s< \n",
			transaction,
			e164_source,
			e164_dest,
			call_id->ospmCallIdLen,
			call_id->ospmCallIdVal);

		res = OSPPTransactionValidateAuthorisation(
			transaction,
			"",
			"",
			"",
			"",
			e164_source,
			OSPC_E164,
			e164_dest,
			OSPC_E164,
			call_id->ospmCallIdLen,
			call_id->ospmCallIdVal,
			sizeoftoken,
			token,
			&authorized,
			&time_limit,
			&log_size,
			detail_log,
			_token_format);
	
		if (res == 0 && authorized == 1) {
			LOG(L_INFO, "osp: Call is authorized for %d seconds",time_limit);
			valid = MODULE_RETURNCODE_SUCCESS;
		} else {
			LOG(L_ERR,"ERROR: osp: Token is not valid, code %i\n", res);
		}
	}

	if (transaction != -1) {
		OSPPTransactionDelete(transaction);
	}

	if (call_id!=NULL) {
		OSPPCallIdDelete(&call_id);
	}
	
	return valid;
}

