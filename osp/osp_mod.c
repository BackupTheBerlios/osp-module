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
#include "orig_transaction.h"
#include "term_transaction.h"
#include "../../sr_module.h"
#include "../../data_lump_rpl.h"
#include "../../mem/mem.h"
#include "../../timer.h"
#include "../../locking.h"
#include <stdio.h>

MODULE_VERSION

extern int   _spWeights[2];
extern char* _spURIs[2];
extern char* _private_key;
extern char* _local_certificate;
extern char* _ca_certificate;
extern char* _device_ip;
extern char* _device_port;
extern int   _ssl_lifetime;
extern int   _persistence;
extern int   _retry_delay;
extern int   _retry_limit;
extern int   _timeout;
extern int   _max_destinations;
extern int   _token_format;
extern int   _crypto_hw_support;
extern OSPTPROVHANDLE _provider;
extern char _PRIVATE_KEY[255];
extern char _LOCAL_CERTIFICATE[255];
extern char _CA_CERTIFICATE[255];

/* exported function prototypes */
static int mod_init(void);
static int child_init(int);
static void mod_destroy();

static int  verify_parameter();
static void dump_parameter();


static cmd_export_t cmds[]={
	{"checkospheader",       checkospheader,       0, 0, REQUEST_ROUTE|FAILURE_ROUTE}, 
	{"validateospheader",    validateospheader,    0, 0, REQUEST_ROUTE|FAILURE_ROUTE}, 
	{"requestosprouting",    requestosprouting,    0, 0, REQUEST_ROUTE|FAILURE_ROUTE}, 
	{"preparefirstosproute", preparefirstosproute, 0, 0, REQUEST_ROUTE|FAILURE_ROUTE}, 
	{"preparenextosproute",  preparenextosproute,  0, 0, REQUEST_ROUTE|FAILURE_ROUTE}, 
	{"prepareallosproutes",  prepareallosproutes,  0, 0, REQUEST_ROUTE|FAILURE_ROUTE}, 
	{0, 0, 0, 0, 0}
};

static param_export_t params[]={ 
	{"sp1_uri",           STR_PARAM, &_spURIs[0]},
	{"sp1_weight",        INT_PARAM, &(_spWeights[0])},
	{"sp2_uri",           STR_PARAM, &_spURIs[1]},
	{"sp2_weight",        INT_PARAM, &(_spWeights[1])},

	{"device_ip",         STR_PARAM, &_device_ip},
	{"device_port",       STR_PARAM, &_device_port},

	{"private_key",       STR_PARAM, &_private_key},
	{"local_certificate", STR_PARAM, &_local_certificate},
	{"ca_certificates",   STR_PARAM, &_ca_certificate},
	{"enable_crypto_hardware_support", 
                              INT_PARAM, &_crypto_hw_support},
	{"token_format",      INT_PARAM, &_token_format},
	{"ssl_lifetime",      INT_PARAM, &_ssl_lifetime},
	{"persistence",       INT_PARAM, &_persistence},
	{"retry_delay",       INT_PARAM, &_retry_delay},
	{"retry_limit",       INT_PARAM, &_retry_limit},
	{"timeout",           INT_PARAM, &_timeout},
	{"max_destinations",  INT_PARAM, &_max_destinations},
	{0,0,0} 
};

struct module_exports exports = {
	"osp", 
	cmds,
	params,
	
	mod_init,   /* module initialization function */
	0,          /* response function*/
	mod_destroy,/* destroy function */
	0,          /* oncancel function */
	child_init, /* per-child init function */
};


static int mod_init(void)
{
	DBG("---------------------Initializing OSP module\n");

	if (verify_parameter() != 0) 
		return 1;   /* at least one parameter incorrect -> error */

	append_hf = find_export("append_hf", 1, 0);
        if (append_hf == NULL) {
                LOG(L_ERR, "ERROR: osp: mod_init: could not find append_hf, make shure textops is loaded\n");
                return 1;
        }   

	/* everything is fine, initialization done */
	return 0;	
}






static int child_init(int rank) {
	int code = -1;

	DBG("---------------------Initializing OSP module for the child process\n");

	DBG("Initializing the toolkit and creating a new provider\n");

	code = setup_provider();

	DBG("Result: (%i) Provider (%i)\n", code, _provider);

	return 0;
}

static void mod_destroy() {
	DBG("---------------------Destroying OSP module for the child process\n");
}	


int verify_parameter() {
	/* Assume success. If any validation fails the values will be set to -1 */
	int errorcode = 0;

	LOG(L_INFO,"osp: Initialzing OSP module\n");

	/* Default location for the cert files is in the compile time variable CFG_DIR */
	if (_private_key == NULL) {
		sprintf(_PRIVATE_KEY,"%spkey.pem",CFG_DIR);
		_private_key = _PRIVATE_KEY;
	} 

	if (_local_certificate == NULL) {
		sprintf(_LOCAL_CERTIFICATE,"%slocalcert.pem",CFG_DIR);
		_local_certificate = _LOCAL_CERTIFICATE;
	}

	if (_ca_certificate == NULL) {
		sprintf(_CA_CERTIFICATE,"%scacert_0.pem",CFG_DIR);
		_ca_certificate = _CA_CERTIFICATE;
	}

	if (_device_ip == NULL) {
		_device_ip = "";
	}

	if (_device_port == NULL) {
		_device_port = "";
	}

	if (_max_destinations > MAX_DESTS || _max_destinations < 1) {
		_max_destinations = 5;	
		LOG(L_WARN,"WARN: osp: Maximum destinations 'max_destinations' is out of range, re-setting to 5\n");
	}

	if (_token_format < 0 || _token_format > 2) {
		_token_format = 0;
		LOG(L_WARN,"WARN: osp: Token format 'token_format' is out of range, re-setting to 0\n");
	}

	if (_spURIs[1] == NULL) {
		_spURIs[1] = _spURIs[0];
	}

	if (_spURIs[0] == NULL) {
		LOG(L_ERR,"ERROR: osp: Service Point 1 'sp1_uri' must be configured\n");
		errorcode = -1;
	}

	dump_parameter();

	return errorcode;
}


void dump_parameter() {
	LOG(L_INFO, "osp: module parameter settings\n"
	" sp1_uri: '%s'"
	" sp1_weight: '%d'"
	" sp2_uri: '%s'"
	" sp2_weight: '%d'"
	" device_ip: '%s'"
	" device_port: '%s'"
	" private_key: '%s'"
	" local_certificate: '%s'"
	" ca_certificates: '%s'"
	" enable_crypto_hardware_support: '%d'"
	" token_format: '%d'"
	" ssl_lifetime: '%d'"
	" persistence: '%d'"
	" retry_delay: '%d'"
	" retry_limit: '%d'"
	" timeout: '%d'"
	" max_destinations: '%d'",
	_spURIs[0],
	_spWeights[0],
	_spURIs[1],
	_spWeights[1],
	_device_ip,
	_device_port,
	_private_key,
	_local_certificate,
	_ca_certificate,
	_crypto_hw_support,
	_token_format,
	_ssl_lifetime,
	_persistence,
	_retry_delay,
	_retry_limit,
	_timeout,
	_max_destinations);
}

