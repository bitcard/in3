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

#include "../../core/client/context_internal.h"
#include "../../core/client/keys.h"
#include "../../core/client/plugin.h"
#include "../../core/util/log.h"
#include "../../core/util/mem.h"
#include "../../third-party/crypto/ecdsa.h"
#include "../../third-party/crypto/rand.h"
#include "../../third-party/crypto/secp256k1.h"
#include "../../verifier/eth1/basic/eth_basic.h"
#include "../../verifier/eth1/nano/rlp.h"
#include "abi.h"
#include "ens.h"
#include "eth_api.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#ifdef ETH_FULL
#include "../../third-party/tommath/tommath.h"
#endif
#define ETH_SIGN_PREFIX "\x19" \
                        "Ethereum Signed Message:\n%u"

static in3_ret_t in3_abiEncode(in3_rpc_handle_ctx_t* ctx, d_token_t* params) {
  in3_ret_t  ret   = IN3_OK;
  bytes_t    data  = {0};
  char*      error = NULL;
  char*      sig   = d_get_string_at(params, 0);
  d_token_t* para  = d_get_at(params, 1);
  if (!sig) return ctx_set_error(ctx->ctx, "missing signature", IN3_EINVAL);
  if (!para) return ctx_set_error(ctx->ctx, "missing values", IN3_EINVAL);
  abi_sig_t* s = abi_sig_create(sig, &error);
  if (!error)
    data = abi_encode(s, para, &error);
  if (!error)
    ret = in3_rpc_handle_with_bytes(ctx, data);
  if (s) abi_sig_free(s);
  if (data.data) _free(data.data);
  return error ? ctx_set_error(ctx->ctx, error, IN3_EINVAL) : ret;
}

static in3_ret_t in3_abiDecode(in3_rpc_handle_ctx_t* ctx, d_token_t* params) {
  char*       error = NULL;
  json_ctx_t* res   = NULL;
  char*       sig   = d_get_string_at(params, 0);
  bytes_t     data  = d_to_bytes(d_get_at(params, 1));
  if (!sig) return ctx_set_error(ctx->ctx, "missing signature", IN3_EINVAL);
  if (!data.data) return ctx_set_error(ctx->ctx, "missing data", IN3_EINVAL);
  if (d_len(params) > 2) return ctx_set_error(ctx->ctx, "too many arguments (only 2 alllowed)", IN3_EINVAL);

  abi_sig_t* req = abi_sig_create(sig, &error);
  if (!error) res = abi_decode(req, data, &error);
  if (req) abi_sig_free(req);
  if (error) return ctx_set_error(ctx->ctx, error, IN3_EINVAL);
  char* result = d_create_json(res, res->result);
  in3_rpc_handle_with_string(ctx, result);
  _free(result);
  if (res) json_free(res);
  return IN3_OK;
}

static in3_ret_t in3_checkSumAddress(in3_rpc_handle_ctx_t* ctx, d_token_t* params) {
  if (d_len(params) > 2) return ctx_set_error(ctx->ctx, "must be max 2 arguments", IN3_EINVAL);
  char     result[45];
  bytes_t* adr = d_get_bytes_at(params, 0);
  if (!adr || adr->len != 20) return ctx_set_error(ctx->ctx, "the address must have 20 bytes", IN3_EINVAL);
  in3_ret_t res = to_checksum(adr->data, d_get_int_at(params, 1) ? ctx->ctx->client->chain_id : 0, result + 1);
  if (res) return ctx_set_error(ctx->ctx, "Could not create the checksum address", res);
  result[0]  = '\'';
  result[43] = '\'';
  result[44] = 0;

  return in3_rpc_handle_with_string(ctx, result);
}

static in3_ret_t in3_ens(in3_rpc_handle_ctx_t* ctx, d_token_t* params) {
  char*        name     = d_get_string_at(params, 0);
  char*        type     = d_get_string_at(params, 1);
  bytes_t      registry = d_to_bytes(d_get_at(params, 2));
  int          res_len  = 20;
  in3_ens_type ens_type = ENS_ADDR;
  bytes32_t    result;

  // verify input
  if (!type) type = "addr";
  if (!name || !strchr(name, '.')) return ctx_set_error(ctx->ctx, "the first param msut be a valid domain name", IN3_EINVAL);
  if (strcmp(type, "addr") == 0)
    ens_type = ENS_ADDR;
  else if (strcmp(type, "resolver") == 0)
    ens_type = ENS_RESOLVER;
  else if (strcmp(type, "owner") == 0)
    ens_type = ENS_OWNER;
  else if (strcmp(type, "hash") == 0)
    ens_type = ENS_HASH;
  else
    return ctx_set_error(ctx->ctx, "currently only 'hash','addr','owner' or 'resolver' are allowed as type", IN3_EINVAL);
  if (registry.data && registry.len != 20) return ctx_set_error(ctx->ctx, "the registry must be a 20 bytes address", IN3_EINVAL);

  TRY(ens_resolve(ctx->ctx, name, registry.data, ens_type, result, &res_len))

  return in3_rpc_handle_with_bytes(ctx, bytes(result, res_len));
}

static in3_ret_t in3_sha3(in3_rpc_handle_ctx_t* ctx, d_token_t* params) {
  if (!params || d_len(params) != 1) return ctx_set_error(ctx->ctx, "no data", IN3_EINVAL);
  bytes32_t hash;
  keccak(d_to_bytes(params + 1), hash);
  return in3_rpc_handle_with_bytes(ctx, bytes(hash, 32));
}
static const char* UNITS[] = {
    "wei", "",
    "kwei", "\x03",
    "Kwei", "\x03",
    "babbage", "\x03",
    "femtoether", "\x03",
    "mwei", "\x06",
    "Mwei", "\x06",
    "lovelace", "\x06",
    "picoether", "\x06",
    "gwei", "\x09",
    "Gwei", "\x09",
    "shannon", "\x09",
    "nanoether", "\x09",
    "nano", "\x09",
    "szabo", "\x0c",
    "microether", "\x0c",
    "micro", "\x0c",
    "finney", "\x0f",
    "milliether", "\x0f",
    "milli", "\x0f",
    "ether", "\x12",
    "eth", "\x12",
    "kether", "\x15",
    "grand", "\x15",
    "mether", "\x18",
    "gether", "\x1b",
    "tether", "\x1e",
    NULL};

int string_val_to_bytes(char* val, char* unit, bytes32_t target) {
  if (!val) return IN3_EINVAL;
  int l = strlen(val), nl = l, exp = 0, p = 0;

  if (l == 1 && val[0] == '0') {
    *target = 0;
    return 1;
  }
  if (val[0] == '0' && val[1] == 'x') return unit == NULL ? hex_to_bytes(val + 2, l - 2, target, l) : IN3_EINVAL;
  if (unit == NULL) {
    while (nl && val[nl - 1] > '9') nl--;
    if (nl < l) unit = val + nl;
  }
  if (unit) {
    for (int i = 0; UNITS[i]; i += 2) {
      if (strcmp(UNITS[i], unit) == 0) {
        exp = *UNITS[i + 1];
        break;
      }
      else if (!UNITS[i + 2])
        return IN3_EINVAL;
    }
  }
  if (!exp && l < 20) {
    bytes_t b = bytes(target, 8);
    long_to_bytes(atoll(nl < l ? strncpy(alloca(nl + 1), val, nl) : val), target);
    b_optimize_len(&b);
    if (b.len < 8) memmove(target, b.data, b.len);
    return (int) b.len;
  }
#ifdef ETH_FULL
  char* dst = alloca(l + exp + 10);
  char* dot = strchr(val, '.');
  if (!dot)
    memcpy(dst, val, (p = l) + 1);
  else if (dot - val != 1 || *val != '0')
    memcpy(dst, val, (p = dot - val) + 1);
  dst[p + exp] = 0;
  if (exp) {
    if (dot) {
      dot++;
      for (int i = 0; i < exp; i++) {
        if (*dot && dot - val < nl) {
          dst[p + i] = *dot;
          dot++;
        }
        else
          dst[p + i] = '0';
      }
    }
    else
      memset(dst + p, '0', exp);
  }
  // remove leading zeros
  while (*dst == '0' && dst[1]) dst++;
  size_t s;
  mp_int d;
  mp_init(&d);
  mp_read_radix(&d, dst, 10);
  mp_export(target, &s, 1, sizeof(uint8_t), 1, 0, &d);
  mp_clear(&d);
  return (int) s;
#else
  UNUSED_VAR(p);
  bytes_t b = bytes(target, 8);
  long_to_bytes(parse_float_val(nl < l ? strncpy(alloca(nl + 1), val, nl) : val, exp), target);
  b_optimize_len(&b);
  if (b.len < 8) memmove(target, b.data, b.len);
  return (int) b.len;
#endif
}

static in3_ret_t in3_toWei(in3_rpc_handle_ctx_t* ctx, d_token_t* params) {
  if (!params || d_len(params) != 2 || d_type(params + 2) != T_STRING) return ctx_set_error(ctx->ctx, "must have 2 params as strings", IN3_EINVAL);
  char* val = d_get_string_at(params, 0);
  if (!val) {
    if (d_type(params + 1) == T_INTEGER) {
      val = alloca(20);
      sprintf(val, "%i", d_int(params + 1));
    }
    else
      return ctx_set_error(ctx->ctx, "the value must be a string", IN3_EINVAL);
  }
  bytes32_t tmp;
  int       s = string_val_to_bytes(val, d_get_string_at(params, 1), tmp);
  return s < 0
             ? ctx_set_error(ctx->ctx, "invalid number string", IN3_EINVAL)
             : in3_rpc_handle_with_bytes(ctx, bytes(tmp, (uint32_t) s));
}

static in3_ret_t in3_config(in3_rpc_handle_ctx_t* ctx, d_token_t* params) {
  if (!params || d_len(params) != 1 || d_type(params + 1) != T_OBJECT) return ctx_set_error(ctx->ctx, "no valid config-object as argument", IN3_EINVAL);

  ctx->ctx->client->pending--; // we need to to temporarly decrees it in order to allow configuring
  str_range_t r   = d_to_json(params + 1);
  char        old = r.data[r.len];
  r.data[r.len]   = 0;
  char* ret       = in3_configure(ctx->ctx->client, r.data);
  r.data[r.len]   = old;
  ctx->ctx->client->pending++;

  if (ret) {
    ctx_set_error(ctx->ctx, ret, IN3_ECONFIG);
    free(ret);
    return IN3_ECONFIG;
  }

  return in3_rpc_handle_with_string(ctx, "true");
}

static in3_ret_t in3_getConfig(in3_rpc_handle_ctx_t* ctx) {
  char* ret = in3_get_config(ctx->ctx->client);
  in3_rpc_handle_with_string(ctx, ret);
  _free(ret);
  return IN3_OK;
}

static in3_ret_t in3_pk2address(in3_rpc_handle_ctx_t* ctx, d_token_t* params) {
  bytes_t* pk = d_get_bytes_at(params, 0);
  if (!pk || pk->len != 32 || d_len(params) != 1) return ctx_set_error(ctx->ctx, "Invalid private key! must be 32 bytes long", IN3_EINVAL);

  uint8_t public_key[65], sdata[32];
  ecdsa_get_public_key65(&secp256k1, pk->data, public_key);

  if (strcmp(d_get_stringk(ctx->ctx->requests[0], K_METHOD), "in3_pk2address") == 0) {
    keccak(bytes(public_key + 1, 64), sdata);
    return in3_rpc_handle_with_bytes(ctx, bytes(sdata + 12, 20));
  }
  else
    return in3_rpc_handle_with_bytes(ctx, bytes(public_key + 1, 64));
}

static in3_ret_t in3_ecrecover(in3_rpc_handle_ctx_t* ctx, d_token_t* params) {
  bytes_t  msg      = d_to_bytes(d_get_at(params, 0));
  bytes_t* sig      = d_get_bytes_at(params, 1);
  char*    sig_type = d_get_string_at(params, 2);
  if (!sig_type) sig_type = "raw";
  if (!sig || sig->len != 65) return ctx_set_error(ctx->ctx, "Invalid signature! must be 65 bytes long", IN3_EINVAL);
  if (!msg.data) return ctx_set_error(ctx->ctx, "Missing message", IN3_EINVAL);

  bytes32_t hash;
  uint8_t   pub[65];
  bytes_t   pubkey_bytes = {.len = 64, .data = ((uint8_t*) &pub) + 1};
  if (strcmp(sig_type, "eth_sign") == 0) {
    char*     tmp = alloca(msg.len + 30);
    const int l   = sprintf(tmp, ETH_SIGN_PREFIX, msg.len);
    memcpy(tmp + l, msg.data, msg.len);
    msg.data = (uint8_t*) tmp;
    msg.len += l;
  }
  if (strcmp(sig_type, "hash") == 0) {
    if (msg.len != 32) return ctx_set_error(ctx->ctx, "The message hash must be 32 byte", IN3_EINVAL);
    memcpy(hash, msg.data, 32);
  }
  else
    keccak(msg, hash);

  if (ecdsa_recover_pub_from_sig(&secp256k1, pub, sig->data, hash, sig->data[64] >= 27 ? sig->data[64] - 27 : sig->data[64]))
    return ctx_set_error(ctx->ctx, "Invalid Signature", IN3_EINVAL);

  sb_t* sb = in3_rpc_handle_start(ctx);

  sb_add_char(sb, '{');
  keccak(pubkey_bytes, hash);
  sb_add_bytes(sb, "\"publicKey\":", &pubkey_bytes, 1, false);
  sb_add_char(sb, ',');
  pubkey_bytes.data = hash + 12;
  pubkey_bytes.len  = 20;
  sb_add_bytes(sb, "\"address\":", &pubkey_bytes, 1, false);
  sb_add_char(sb, '}');
  return in3_rpc_handle_finish(ctx);
}

static in3_ret_t in3_sign_data(in3_rpc_handle_ctx_t* ctx, d_token_t* params) {
  bytes_t        data     = d_to_bytes(d_get_at(params, 0));
  const bytes_t* pk       = d_get_bytes_at(params, 1);
  char*          sig_type = d_get_string_at(params, 2);
  if (!sig_type) sig_type = "raw";

  //  if (!pk) return ctx_set_error(ctx, "Invalid sprivate key! must be 32 bytes long", IN3_EINVAL);
  if (!data.data) return ctx_set_error(ctx->ctx, "Missing message", IN3_EINVAL);

  if (strcmp(sig_type, "eth_sign") == 0) {
    char*     tmp = alloca(data.len + 30);
    const int l   = sprintf(tmp, ETH_SIGN_PREFIX, data.len);
    memcpy(tmp + l, data.data, data.len);
    data.data = (uint8_t*) tmp;
    data.len += l;
    sig_type = "raw";
  }

  in3_sign_ctx_t sc = {0};
  sc.ctx            = ctx->ctx;
  sc.message        = data;
  sc.account        = pk ? *pk : bytes(NULL, 0);
  sc.type           = strcmp(sig_type, "hash") == 0 ? SIGN_EC_RAW : SIGN_EC_HASH;

  if ((sc.account.len == 20 || sc.account.len == 0) && in3_plugin_is_registered(ctx->ctx->client, PLGN_ACT_SIGN)) {
    TRY(in3_plugin_execute_first(ctx->ctx, PLGN_ACT_SIGN, &sc));
  }
  else if (sc.account.len == 32) {
    sc.signature = bytes(_malloc(65), 65);
    if (sc.type == SIGN_EC_RAW)
      ecdsa_sign_digest(&secp256k1, pk->data, data.data, sc.signature.data, sc.signature.data + 64, NULL);
    else if (strcmp(sig_type, "raw") == 0)
      ecdsa_sign(&secp256k1, HASHER_SHA3K, pk->data, data.data, data.len, sc.signature.data, sc.signature.data + 64, NULL);
    else {
      _free(sc.signature.data);
      return ctx_set_error(ctx->ctx, "unsupported sigType", IN3_EINVAL);
    }
  }
  else
    return ctx_set_error(ctx->ctx, "Invalid private key! Must be either an address(20 byte) or an raw private key (32 byte)", IN3_EINVAL);

  bytes_t sig_bytes = sc.signature;
  if (sc.signature.len == 65)
    sc.signature.data[64] += 27;

  sb_t* sb = in3_rpc_handle_start(ctx);
  sb_add_char(sb, '{');
  sb_add_bytes(sb, "\"message\":", &data, 1, false);
  sb_add_char(sb, ',');
  if (strcmp(sig_type, "raw") == 0) {
    bytes32_t hash_val;
    bytes_t   hash_bytes = bytes(hash_val, 32);
    keccak(data, hash_val);
    sb_add_bytes(sb, "\"messageHash\":", &hash_bytes, 1, false);
  }
  else
    sb_add_bytes(sb, "\"messageHash\":", &data, 1, false);
  sb_add_char(sb, ',');
  sb_add_bytes(sb, "\"signature\":", &sig_bytes, 1, false);
  sig_bytes = bytes(sc.signature.data, 32);
  sb_add_char(sb, ',');
  sb_add_bytes(sb, "\"r\":", &sig_bytes, 1, false);
  sig_bytes = bytes(sc.signature.data + 32, 32);
  sb_add_char(sb, ',');
  sb_add_bytes(sb, "\"s\":", &sig_bytes, 1, false);
  char v[15];
  sprintf(v, ",\"v\":%d}", (unsigned int) sc.signature.data[64]);
  sb_add_chars(sb, v);
  _free(sc.signature.data);
  return in3_rpc_handle_finish(ctx);
}

static in3_ret_t in3_cacheClear(in3_rpc_handle_ctx_t* ctx) {
  TRY(in3_plugin_execute_first(ctx->ctx, PLGN_ACT_CACHE_CLEAR, NULL));
  return in3_rpc_handle_with_string(ctx, "true");
}

static in3_ret_t in3_decryptKey(in3_rpc_handle_ctx_t* ctx, d_token_t* params) {
  d_token_t* keyfile        = d_get_at(params, 0);
  bytes_t    password_bytes = d_to_bytes(d_get_at(params, 1));
  bytes32_t  dst;

  if (!password_bytes.data) return ctx_set_error(ctx->ctx, "you need to specify a passphrase", IN3_EINVAL);
  if (!keyfile || d_type(keyfile) != T_OBJECT) return ctx_set_error(ctx->ctx, "no valid key given", IN3_EINVAL);
  char* passphrase = alloca(password_bytes.len + 1);
  memcpy(passphrase, password_bytes.data, password_bytes.len);
  passphrase[password_bytes.len] = 0;
  in3_ret_t res                  = decrypt_key(keyfile, passphrase, dst);
  if (res) return ctx_set_error(ctx->ctx, "Invalid key", res);
  return in3_rpc_handle_with_bytes(ctx, bytes(dst, 32));
}

static in3_ret_t in3_prepareTx(in3_rpc_handle_ctx_t* ctx, d_token_t* params) {
  d_token_t* tx  = d_get_at(params, 0);
  bytes_t    dst = {0};
#if defined(ETH_BASIC) || defined(ETH_FULL)
  TRY(eth_prepare_unsigned_tx(tx, ctx->ctx, &dst))
#else
  if (params || tx || ctx) return ctx_set_error(ctx->ctx, "eth_basic is needed in order to use eth_prepareTx", IN3_EINVAL);
#endif
  in3_rpc_handle_with_bytes(ctx, dst);
  _free(dst.data);
  return IN3_OK;
}

static in3_ret_t in3_signTx(in3_rpc_handle_ctx_t* ctx, d_token_t* params) {
  bytes_t*  data   = d_get_bytes_at(params, 0);
  bytes_t*  from_b = d_get_bytes_at(params, 1);
  address_t from;
  memset(from, 0, 20);
  if (from_b && from_b->data && from_b->len == 20) memcpy(from, from_b->data, 20);
  bytes_t dst = {0};
#if defined(ETH_BASIC) || defined(ETH_FULL)
  TRY(eth_sign_raw_tx(*data, ctx->ctx, from, &dst))
#else
  if (data || ctx || from[0] || params) return ctx_set_error(ctx->ctx, "eth_basic is needed in order to use eth_prepareTx", IN3_EINVAL);
#endif
  in3_rpc_handle_with_bytes(ctx, dst);
  _free(dst.data);
  return IN3_OK;
}

static in3_ret_t handle_intern(void* pdata, in3_plugin_act_t action, void* plugin_ctx) {
  UNUSED_VAR(pdata);
  UNUSED_VAR(action);

  in3_rpc_handle_ctx_t* rpc_ctx = plugin_ctx;
  char*                 method  = d_get_stringk(rpc_ctx->request, K_METHOD);
  d_token_t*            params  = d_get(rpc_ctx->request, K_PARAMS);

  if (strcmp(method, "in3_abiEncode") == 0) return in3_abiEncode(rpc_ctx, params);
  if (strcmp(method, "in3_abiDecode") == 0) return in3_abiDecode(rpc_ctx, params);
  if (strcmp(method, "in3_checksumAddress") == 0) return in3_checkSumAddress(rpc_ctx, params);
  if (strcmp(method, "in3_ens") == 0) return in3_ens(rpc_ctx, params);
  if (strcmp(method, "web3_sha3") == 0) return in3_sha3(rpc_ctx, params);
  if (strcmp(method, "in3_toWei") == 0) return in3_toWei(rpc_ctx, params);
  if (strcmp(method, "in3_config") == 0) return in3_config(rpc_ctx, params);
  if (strcmp(method, "in3_getConfig") == 0) return in3_getConfig(rpc_ctx);
  if (strcmp(method, "in3_pk2address") == 0) return in3_pk2address(rpc_ctx, params);
  if (strcmp(method, "in3_pk2public") == 0) return in3_pk2address(rpc_ctx, params);
  if (strcmp(method, "in3_ecrecover") == 0) return in3_ecrecover(rpc_ctx, params);
  if (strcmp(method, "in3_signData") == 0) return in3_sign_data(rpc_ctx, params);
  if (strcmp(method, "in3_cacheClear") == 0) return in3_cacheClear(rpc_ctx);
  if (strcmp(method, "in3_decryptKey") == 0) return in3_decryptKey(rpc_ctx, params);
  if (strcmp(method, "in3_prepareTx") == 0) return in3_prepareTx(rpc_ctx, params);
  if (strcmp(method, "in3_signTx") == 0) return in3_signTx(rpc_ctx, params);

  return IN3_EIGNORE;
}

in3_ret_t in3_register_eth_api(in3_t* c) {
  return plugin_register(c, PLGN_ACT_RPC_HANDLE, handle_intern, NULL, false);
}
