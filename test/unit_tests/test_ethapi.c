/*******************************************************************************
 * This file is part of the Incubed project.
 * Sources: https://github.com/slockit/in3-c
 * 
 * Copyright (C) 2018-2019 slock.it GmbH, Blockchains LLC
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

#ifndef TEST
#define TEST
#endif
#ifndef TEST
#define DEBUG
#endif

#include "../../src/core/client/cache.h"
#include "../../src/core/client/context.h"
#include "../../src/core/client/nodelist.h"
#include "../../src/core/util/data.h"
#include "../../src/core/util/log.h"
#include "../../src/core/client/keys.h"
#include "../../src/core/util/scache.h"
#include "../../src/verifier/eth1/nano/eth_nano.h"
#include "../../src/verifier/eth1/full/eth_full.h"
#include "../../src/api/eth1/eth_api.h"
#include "../../src/transport/curl/in3_curl.h"
#include "../test_utils.h"
#include "./mock.h"
#include <stdio.h>
#include <unistd.h>
/*
uint256_t         eth_getStorageAt(in3_t* in3, address_t account, bytes32_t key, eth_blknum_t block);      
bytes_t           eth_getCode(in3_t* in3, address_t account, eth_blknum_t block);                          
uint256_t         eth_getBalance(in3_t* in3, address_t account, eth_blknum_t block);                       
uint64_t          eth_blockNumber(in3_t* in3);                                                             
uint64_t          eth_gasPrice(in3_t* in3);                                                                
eth_block_t*      eth_getBlockByNumber(in3_t* in3, eth_blknum_t number, bool include_tx);                  
eth_block_t*      eth_getBlockByHash(in3_t* in3, bytes32_t hash, bool include_tx);                         
eth_log_t*        eth_getLogs(in3_t* in3, char* fopt);                                                     
in3_ret_t         eth_newFilter(in3_t* in3, json_ctx_t* options);                                          
in3_ret_t         eth_newBlockFilter(in3_t* in3);                                                          
in3_ret_t         eth_newPendingTransactionFilter(in3_t* in3);                                             
bool              eth_uninstallFilter(in3_t* in3, size_t id);                                              
in3_ret_t         eth_getFilterChanges(in3_t* in3, size_t id, bytes32_t** block_hashes, eth_log_t** logs); 
in3_ret_t         eth_getFilterLogs(in3_t* in3, size_t id, eth_log_t** logs);                              
uint64_t          eth_chainId(in3_t* in3);                                                                 
uint64_t          eth_getBlockTransactionCountByHash(in3_t* in3, bytes32_t hash);                          
uint64_t          eth_getBlockTransactionCountByNumber(in3_t* in3, eth_blknum_t block);                    
json_ctx_t*       eth_call_fn(in3_t* in3, address_t contract, eth_blknum_t block, char* fn_sig, ...);      
uint64_t          eth_estimate_fn(in3_t* in3, address_t contract, eth_blknum_t block, char* fn_sig, ...);  
eth_tx_t*         eth_getTransactionByHash(in3_t* in3, bytes32_t tx_hash);                                 
eth_tx_t*         eth_getTransactionByBlockHashAndIndex(in3_t* in3, bytes32_t block_hash, size_t index);   
eth_tx_t*         eth_getTransactionByBlockNumberAndIndex(in3_t* in3, eth_blknum_t block, size_t index);   
uint64_t          eth_getTransactionCount(in3_t* in3, address_t address, eth_blknum_t block);              
eth_block_t*      eth_getUncleByBlockNumberAndIndex(in3_t* in3, bytes32_t hash, size_t index);             
uint64_t          eth_getUncleCountByBlockHash(in3_t* in3, bytes32_t hash);                                
uint64_t          eth_getUncleCountByBlockNumber(in3_t* in3, eth_blknum_t block);                          
bytes_t*          eth_sendTransaction(in3_t* in3, address_t from, address_t to, OPTIONAL_T(uint64_t) gas,  
                                      OPTIONAL_T(uint64_t) gas_price, OPTIONAL_T(uint256_t) value,         
                                      OPTIONAL_T(bytes_t) data, OPTIONAL_T(uint64_t) nonce);               
bytes_t*          eth_sendRawTransaction(in3_t* in3, bytes_t data); 
*/

static in3_t* in3 = NULL;
static void init_in3( in3_transport_send custom_transport, uint64_t chain) {
  int err;
  in3_register_eth_full();
  in3 = in3_new();
  in3->transport    = custom_transport; // use curl to handle the requests
  in3->requestCount = 1;              // number of requests to sendp
  in3->includeCode  = 1;
  in3->chainId      = chain;
  in3->max_attempts = 1;
  in3->requestCount = 1; // number of requests to sendp
  in3_log_set_level(LOG_TRACE);
  in3_log_set_quiet(0);
}
static void free_in3(){
  _free(in3);
  in3=NULL;
}

static in3_ret_t mock_transport(in3_request_t* req) {
  in3_log_debug("Req : \n");
  for (int i = 0; i < req->urls_len; i++) {
    if (strstr(req->payload, "nodeList") != NULL) {
      in3_log_debug("Returning Node List ...\n");
      sb_add_range(&req->results[i].result, node_res, 0, node_len);
    } else if (strstr(req->payload, "eth_call") != NULL) {
      in3_log_debug("Returning Call Response ...\n");
      sb_add_range(&req->results[i].result, ethcall_res, 0, ethcall_len);
    } else if (strstr(req->payload, "eth_getCode") != NULL) {
      in3_log_debug("Returning getCode Response ...\n");
      sb_add_range(&req->results[i].result, code_res, 0, code_len);
    } else if (strstr(req->payload, "eth_getBlockByHash") != NULL) {
      in3_log_debug("Returning block by hash  Response ...\n");
      sb_add_range(&req->results[i].result, blk_hash_res, 0, blk_hash_len);
    } else if (strstr(req->payload, "eth_getBlockByNumber") != NULL) {
      in3_log_debug("Returning block by number Response ...\n");
      sb_add_range(&req->results[i].result, blk_num_res, 0, blk_num_len);
    } else if (strstr(req->payload, "eth_getBalance") != NULL) {
      in3_log_debug("Returning eth_getBalance Response ...\n");
      sb_add_range(&req->results[i].result, balance_res, 0, balance_len);
    } else if (strstr(req->payload, "eth_getTransactionByHash") != NULL) {
      in3_log_debug("Returning eth_getTransactionByHash Response ...\n");
      sb_add_range(&req->results[i].result, tx_hash_res, 0, tx_hash_len);
    } else if (strstr(req->payload, "eth_getBlockTransactionCountByHash") != NULL) {
      in3_log_debug("Returning eth_getBlockTransactionCountByHash Response ...\n");
      sb_add_range(&req->results[i].result, count_tx_hash_res, 0, count_tx_hash_len);
    } else if (strstr(req->payload, "eth_getBlockTransactionCountByNumber") != NULL) {
      in3_log_debug("Returning eth_getBlockTransactionCountByNumber Response ...\n");
      sb_add_range(&req->results[i].result, count_tx_number_res, 0, count_tx_number_len);
    } else if (strstr(req->payload, "eth_getLogs") != NULL) {
      in3_log_debug("Returning eth_getLogs Response ...\n");
      sb_add_range(&req->results[i].result, logs_res, 0, logs_len);
    } else if (strstr(req->payload, "eth_chainId") != NULL) {
      in3_log_debug("Returning eth_getLogs Response ...\n");
      sb_add_range(&req->results[i].result, chainid_res, 0, chainid_len);
    } else if (strstr(req->payload, "eth_getTransactionReceipt") != NULL) {
      in3_log_debug("Returning eth_getLogs Response ...\n");
      sb_add_range(&req->results[i].result, tx_receipt_res, 0, tx_receipt_len);
    }


//
//
  }
  return 0;
}
static in3_ret_t curl_transport(in3_request_t* req) {
  //in3_ret_t r = send_curl(req);
  return send_curl_blocking((const char**) req->urls, req->urls_len, req->payload, req->results);
  //return r;
}

static void test_get_balance() {
  init_in3(mock_transport, 0x5);
  // the address of account whose balance we want to get
  address_t account;
  hex2byte_arr("0xF99dbd3CFc292b11F74DeEa9fa730825Ee0b56f2", -1, account, 20);
  // get balance of account
  long double balance = as_double(eth_getBalance(in3, account, BLKNUM(1692767)));
  // if the result is null there was an error an we can get the latest error message from eth_lat_error()
  //balance ? printf("Balance: %Lf\n", balance) : printf("error getting the balance : %s\n", eth_last_error());

  TEST_ASSERT_TRUE(balance > 0.0);
}

static void test_get_logs() {
  init_in3(mock_transport, 0x5);
  // Create filter options
  char b[30];
  sprintf(b, "{\"fromBlock\":\"0x%" PRIx64 "\"}", eth_blockNumber(in3) - 2);
  json_ctx_t* jopt = parse_json(b);

  // Create new filter with options
  size_t fid = eth_newFilter(in3, jopt);

  // Get logs
  eth_log_t* logs = NULL;
  in3_ret_t  ret  = eth_getFilterLogs(in3, fid, &logs);
  if (ret != IN3_OK) {
    printf("eth_getFilterLogs() failed [%d]\n", ret);
    return;
  }
  while (logs) {
    eth_log_t* l = logs;
    printf("--------------------------------------------------------------------------------\n");
    printf("\tlogId: %lu\n", l->log_index);
    printf("\tTxId: %lu\n", l->transaction_index);
    printf("\thash: ");
    ba_print(l->block_hash, 32);
    printf("\n\tnum: %" PRIu64 "\n", l->block_number);
    printf("\taddress: ");
    ba_print(l->address, 20);
    printf("\n\tdata: ");
    b_print(&l->data);
    printf("\ttopics[%lu]: ", l->topic_count);
    for (size_t i = 0; i < l->topic_count; i++) {
      printf("\n\t");
      ba_print(l->topics[i], 32);
    }
    printf("\n");
    logs = logs->next;
    free(l->data.data);
    free(l->topics);
    free(l);
  }
  eth_uninstallFilter(in3, fid);
  free_json(jopt);

  TEST_ASSERT_TRUE(ret == IN3_OK);
}

static void test_get_tx(void) {
  // the hash of transaction that we want to get
  init_in3(mock_transport, 0x5);
  bytes32_t tx_hash;
  hex2byte_arr("0x9241334b0b568ef6cd44d80e37a0ce14de05557a3cfa98b5fd1d006204caf164", -1, tx_hash, 32);

  // get the tx by hash
  eth_tx_t* tx = eth_getTransactionByHash(in3, tx_hash);

  // if the result is null there was an error an we can get the latest error message from eth_last_error()
  if (!tx)
    printf("error getting the tx : %s\n", eth_last_error());
  else {
    printf("Transaction #%d of block #%llx", tx->transaction_index, tx->block_number);
    free(tx);
  }

  TEST_ASSERT_TRUE( tx!= NULL);
}

static void test_get_tx_receipt(void) {
  // the hash of transaction whose receipt we want to get

  init_in3(mock_transport, 0x5);
  bytes32_t tx_hash;
  hex2byte_arr("0x8e7fb87e95c69a780490fce3ea14b44c78366fc45baa6cb86a582166c10c6d9d", -1, tx_hash, 32);

  // get the tx receipt by hash
  eth_tx_receipt_t* txr = eth_getTransactionReceipt(in3, tx_hash);

  // if the result is null there was an error an we can get the latest error message from eth_last_error()
  if (!txr)
    printf("error getting the tx : %s\n", eth_last_error());
  else {
    printf("Transaction #%d of block #%llx, gas used = %" PRIu64 ", status = %s\n", txr->transaction_index, txr->block_number, txr->gas_used, txr->status ? "success" : "failed");
    free_tx_receipt(txr);
  }
  TEST_ASSERT_TRUE(txr);
}

static void test_send_tx(void) {
  init_in3(curl_transport, 0x5);
  // prepare parameters
  address_t to, from;
  hex2byte_arr("0x63FaC9201494f0bd17B9892B9fae4d52fe3BD377", -1, from, 20);
  hex2byte_arr("0xd46e8dd67c5d32be8058bb8eb970870f07244567", -1, to, 20);

  bytes_t* data = hex2byte_new_bytes("d46e8dd67c5d32be8d46e8dd67c5d32be8058bb8eb970870f072445675058bb8eb970870f072445675", 82);

  // send the tx
  bytes_t* tx_hash = eth_sendTransaction(in3, from, to, OPTIONAL_T_VALUE(uint64_t, 0x96c0), OPTIONAL_T_VALUE(uint64_t, 0x9184e72a000), OPTIONAL_T_VALUE(uint256_t, to_uint256(0x9184e72a)), OPTIONAL_T_VALUE(bytes_t, *data), OPTIONAL_T_UNDEFINED(uint64_t));

  // if the result is null there was an error and we can get the latest error message from eth_last_error()
  if (!tx_hash)
    printf("error sending the tx : %s\n", eth_last_error());
  else {
    printf("Transaction hash: ");
    b_print(tx_hash);
    b_free(tx_hash);
  }
  b_free(data);
  TEST_ASSERT_TRUE(tx_hash);
  free_in3();
}

static void test_eth_chain_id(void) {
  init_in3(curl_transport, 0x5);
  uint64_t chain_id = eth_chainId(in3);
  TEST_ASSERT_TRUE(chain_id > 0);
  free_in3();
}

static void test_eth_gas_price(void) {
  init_in3(curl_transport, 0x1);
  uint64_t price = eth_gasPrice(in3);
  TEST_ASSERT_TRUE(price > 1);
  free_in3();
}

static void test_eth_getblock_number(void) {
  init_in3(mock_transport, 0x5);

  eth_block_t* block = eth_getBlockByNumber(in3, BLKNUM(1692767), false);

  // if the result is null there was an error an we can get the latest error message from eth_lat_error()
  if (!block)
    printf("error getting the block : %s\n", eth_last_error());
  else {
    printf("Number of transactions in Block #%llu: %d\n", block->number, block->tx_count);
    free(block);
  }
  TEST_ASSERT_TRUE(block->number == 1692767llu);
  free_in3();
}

static void test_eth_getblock_txcount_number(void) {
  init_in3(curl_transport, 0x5);
  
  uint64_t tx_count  = eth_getBlockTransactionCountByNumber(in3, BLKNUM(1692767));

  TEST_ASSERT_TRUE(tx_count > 0);
  free_in3();
}

static void test_eth_getblock_txcount_hash(void) {
  init_in3(curl_transport, 0x5);
  bytes32_t blk_hash;
  hex2byte_arr("0x1c9d592c4ad3fba02f7aa063e8048b3ff12551fd377e78061ab6ad146cc8df4d", -1, blk_hash, 32);

  uint64_t tx_count = eth_getBlockTransactionCountByHash(in3, blk_hash);

  TEST_ASSERT_TRUE(tx_count > 0);
  free_in3();
}

static void test_eth_getblock_hash(void) {
  init_in3(curl_transport, 0x5);
  bytes32_t blk_hash;
  // 0x9cd22d209f24344147494d05d13f335b6e63af930abdc60f3db63627589e1438
  hex2byte_arr("0x1c9d592c4ad3fba02f7aa063e8048b3ff12551fd377e78061ab6ad146cc8df4d", -1, blk_hash, 32);

  //eth_block_t* block = eth_getBlockByNumber(in3, BLKNUM_EARLIEST(), false);
  eth_block_t* block = eth_getBlockByHash(in3, blk_hash, false);

  // if the result is null there was an error an we can get the latest error message from eth_lat_error()
  if (!block)
    printf("error getting the block : %s\n", eth_last_error());
  else {
    printf("Number of transactions in Block #%llu: %d\n", block->number, block->tx_count);
    free(block);
  }
  TEST_ASSERT_TRUE(block->number == 1692767llu);
  free_in3();
}

static void test_eth_call_fn(void) {
  init_in3(mock_transport, 0x5); 
  address_t contract;
  //setup lock access contract address to be excuted with eth_call
  hex2byte_arr("0x36643F8D17FE745a69A2Fd22188921Fade60a98B", -1, contract, 20);
  //ask for the access to the lock
  json_ctx_t* response = eth_call_fn(in3, contract, BLKNUM_LATEST(), "hasAccess():bool");
  if (!response) {
    in3_log_debug("Could not get the response: %s", eth_last_error());
    return;
  }
  //convert the response to a uint32_t,
  uint8_t access = d_int(response->result);
  in3_log_debug("Access granted? : %d \n", access);

  //    clean up resources
  free_json(response);
  TEST_ASSERT_TRUE(access == 1);
  free_in3();
}

/*
 * Main
 */
int main() {
  TEST_ASSERT_EQUAL(0, mem_stack_size());
  memstack();
  in3_log_set_udata_(NULL);
  in3_log_set_lock_(NULL);
  in3_log_set_fp_(NULL);
  in3_log_set_quiet_(false);
  in3_log_set_level(LOG_TRACE);

  // now run tests
  TESTS_BEGIN();
  //RUN_TEST(test_eth_call_fn);
  RUN_TEST(test_eth_getblock_number);
  RUN_TEST(test_eth_getblock_hash);
  //RUN_TEST(test_eth_getblock_txcount_hash);
  //RUN_TEST(test_eth_getblock_txcount_number);
  //RUN_TEST(test_get_tx_receipt);
  RUN_TEST(test_send_tx);
 // RUN_TEST(test_get_balance);
  //RUN_TEST(test_eth_chain_id);

  //RUN_TEST(test_get_logs);
  //RUN_TEST(test_get_tx);
  return TESTS_END();
}
