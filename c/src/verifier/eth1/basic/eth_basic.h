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

/** @file
 * Ethereum Nanon verification.
 * */

#ifndef in3_eth_basic_h__
#define in3_eth_basic_h__

#include "../../../core/client/plugin.h"

/**
 * verifies internal tx-values.
 */
in3_ret_t eth_verify_tx_values(in3_vctx_t* vc, d_token_t* tx, bytes_t* raw);

/**
 * verifies a transaction.
 */
in3_ret_t eth_verify_eth_getTransaction(in3_vctx_t* vc, bytes_t* tx_hash);

/**
 * verifies a transaction by block hash/number and id.
 */
in3_ret_t eth_verify_eth_getTransactionByBlock(in3_vctx_t* vc, d_token_t* blk, uint32_t tx_idx);

/**
 * verify account-proofs
 */
in3_ret_t eth_verify_account_proof(in3_vctx_t* vc);

/**
 * verifies a block
 */
in3_ret_t eth_verify_eth_getBlock(in3_vctx_t* vc, bytes_t* block_hash, uint64_t blockNumber);

/**
 * verifies block transaction count by number or hash
 */
in3_ret_t eth_verify_eth_getBlockTransactionCount(in3_vctx_t* vc, bytes_t* block_hash, uint64_t blockNumber);

/**
 * this function should only be called once and will register the eth-nano verifier.
 */
in3_ret_t in3_register_eth_basic(in3_t* c);

/**
 *  verify logs
 */
in3_ret_t eth_verify_eth_getLog(in3_vctx_t* vc, int l_logs);

/**
 * prepares a transaction and writes the data to the dst-bytes. In case of success, you MUST free only the data-pointer of the dst. 
 */
in3_ret_t eth_prepare_unsigned_tx(d_token_t* tx,  /**< a json-token desribing the transaction */
                                  in3_ctx_t* ctx, /**< the current context */
                                  bytes_t*   dst  /**< the bytes to write the result to. */
);

/**
 * signs a unsigned raw transaction and writes the raw data to the dst-bytes. In case of success, you MUST free only the data-pointer of the dst. 
 */
in3_ret_t eth_sign_raw_tx(bytes_t    raw_tx, /**< the unsigned raw transaction to sign */
                          in3_ctx_t* ctx,    /**< the current context */
                          address_t  from,   /**< the address of the account to sign with */
                          bytes_t*   dst     /**< the bytes to write the result to. */
);

/**
 * expects a req-object for a transaction and converts it into a sendRawTransaction after signing.
 */
in3_ret_t handle_eth_sendTransaction(in3_ctx_t* ctx, /**< the current context */
                                     d_token_t* req  /**< the request */
);

/**
 * minimum signer for the wallet, returns the signed message which needs to be freed
 */
RETURNS_NONULL NONULL char* eth_wallet_sign(const char* key, const char* data);

#endif // in3_eth_basic_h__