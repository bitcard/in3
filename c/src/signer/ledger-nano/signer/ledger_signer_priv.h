/*******************************************************************************
 * This file is part of the Incubed project.
 * Sources: https://github.com/slockit/in3-c
 * 
 * Copyright (C) 2018-2020 slock.it GmbH, Blockchains LLC
 * 
 * 
 * COMMERCIAL LICENSE USAGE
 * 
 * Licensees holding a valid commercial license may use this file in accordance 
 * with the commercial license agreement provided with the Software or, alternatively, 
 * in accordance with the terms contained in a written agreement between you and 
 * slock.it GmbH/Blockchains LLC. For licensing terms and conditions or further 
 * information please contact slock.it at in3@slock.it.
 * 	
 * Alternatively, this file may be used under the AGPL license as follows:
 *    
 * AGPL LICENSE USAGE
 * 
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free Software 
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *  
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A 
 * PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
 * [Permissions of this strong copyleft license are conditioned on making available 
 * complete source code of licensed works and modifications, which include larger 
 * works using a licensed work, under the same license. Copyright and license notices 
 * must be preserved. Contributors provide an express grant of patent rights.]
 * You should have received a copy of the GNU Affero General Public License along 
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *******************************************************************************/
#ifndef in3_ledger_signer_priv_h__
#define in3_ledger_signer_priv_h__

#include "../../../core/client/client.h"
#include "../../../third-party/crypto/ecdsa.h"
#include "../../../third-party/crypto/secp256k1.h"
#include "types.h"

in3_ret_t is_ledger_device_connected();
in3_ret_t eth_ledger_sign(void* p_data, in3_plugin_act_t action, void* p_ctx);

void set_command_params();
void extract_signture(bytes_t i_raw_sig, uint8_t* o_sig);
int  get_recid_from_pub_key(const ecdsa_curve* curve, uint8_t* pub_key, const uint8_t* sig, const uint8_t* digest);

typedef struct ledger {
  uint8_t   path[5];
  address_t adr;
} in3_ledger_t;

#endif