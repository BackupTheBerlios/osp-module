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
#include "../../sr_module.h"
#include "../../mem/mem.h"
#include "../../usr_avp.h"
#include "destination.h"


/* A list of destination URIs */
str OSPDESTS_LABEL = {"osp_dests",9};






/**
 * Allocate new osp destination and pre-set initial values
 *
 * Returns: NULL on failure
 */
osp_dest* createDestination() {
	osp_dest* dest;

	DBG("osp: Creating osp destination\n");

	dest = (osp_dest*) pkg_malloc(sizeof(osp_dest));

	if (dest != NULL) {
		memset(dest,0,sizeof(osp_dest));
		dest->sizeofcallid   =  sizeof(dest->callid);
		dest->sizeoftoken    =  sizeof(dest->osptoken);
	} else {
		LOG(L_ERR, "osp: createDestination: Failed to allocate memory for an osp destination\n");
	}
	
	return dest;
}

void  deleteDestination(osp_dest* dest) {
	pkg_free(dest);
}


/** Save destination as an AVP
 *  name - OSPDESTS_LABEL
 *  value - osp_dest wrapped in a string
 *
 *  Returns: 0 - success, -1 failure
 */
int saveDestination(osp_dest* dest) {
	str* wrapper;
	int status = -1;

	DBG("osp: Saving destination to avp\n");

	wrapper = (str *)pkg_malloc(sizeof(str));

	wrapper->s   = (char *)dest;
	wrapper->len = sizeof(osp_dest);

	if (add_avp(AVP_NAME_STR|AVP_VAL_STR,(int_str)&OSPDESTS_LABEL,(int_str)wrapper) == 0) {
		status = 0;
	} else {
		LOG(L_ERR, "osp: Failed to add_avp destination\n");
	}

	return status;
}



/** Retrieved an unused destination from an AVP
 *  name - OSPDESTS_LABEL
 *  value - osp_dest wrapped in a string
 *  There can be 0, 1 or more destinations.  Find the 1st unused destination (used==0),
 *  return it, and mark it as used (used==1).
 *
 *  Returns: NULL on failure
 */
osp_dest* getDestination() {
	osp_dest*       retVal   = NULL;
	osp_dest*       dest     = NULL;
	struct usr_avp* dest_avp = NULL;
	int_str         dest_val;

	DBG("osp: Looking for the first unused destination\n");

	for (	dest_avp=search_first_avp(AVP_NAME_STR|AVP_VAL_STR,(int_str)&OSPDESTS_LABEL,NULL);
		dest_avp != NULL;
		dest_avp=search_next_avp(dest_avp,NULL)) {

		get_avp_val(dest_avp, &dest_val);

		/* osp dest is wrapped in a string */
		dest = (osp_dest *)dest_val.s->s;

		if (dest->used == 0) {
			DBG("osp: Found\n");
			break;
		} else {
			DBG("osp: This destination has already been used\n");
		}
	}

	if (dest != NULL && dest->used==0) {
		dest->used = 1;
		retVal = dest;
	} else {
		DBG("osp: There is no unused destinations\n");
	}

	return retVal;
}




