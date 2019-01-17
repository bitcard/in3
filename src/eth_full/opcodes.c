
#include "../eth_basic/eth_basic.h"
#include "../eth_nano/eth_nano.h"
#include "../eth_nano/merkle.h"
#include "../eth_nano/rlp.h"
#include "../eth_nano/serialize.h"
#include "big.h"
#include "eth_full.h"
#include "evm.h"
#include "gas.h"
#include <client/context.h>
#include <crypto/bignum.h>
#include <crypto/ecdsa.h>
#include <crypto/secp256k1.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/data.h>
#include <util/mem.h>
#include <util/utils.h>

int evm_ensure_memory(evm_t* evm, uint32_t max_pos) {

#ifdef EVM_GAS
  if (max_pos > evm->memory.b.len) {
    int old_wc = (evm->memory.bsize + 31) / 32;
    int new_wc = (max_pos + 31) / 32;
    if (new_wc > old_wc) {
      int old_cost = old_wc * G_MEMORY + (old_wc * old_wc) / 512;
      int new_cost = new_wc * G_MEMORY + (new_wc * new_wc) / 512;
      if (new_cost > old_cost)
        subgas(new_cost - old_cost);
    }

    new_wc            = bb_check_size(&evm->memory, max_pos - evm->memory.b.len);
    evm->memory.b.len = max_pos;
    return new_wc;
#else
  if (max_pos > evm->memory.bsize) {
    return bb_check_size(&evm->memory, max_pos - evm->memory.b.len);
#endif
  } else
    return 0;
}

#define MATH_ADD 1
#define MATH_SUB 2
#define MATH_MUL 3
#define MATH_DIV 4
#define MATH_SDIV 5
#define MATH_MOD 6
#define MATH_SMOD 7
#define MATH_EXP 8
#define MATH_SIGNEXP 9

static int op_math(evm_t* evm, uint8_t op, uint8_t mod) {
  uint8_t *a, *b, res[65], *r = res;
  int      la = evm_stack_pop_ref(evm, &a), lb = evm_stack_pop_ref(evm, &b), l;
  if (la < 0 || lb < 0) return EVM_ERROR_EMPTY_STACK;
  switch (op) {
    case MATH_ADD:
      l = big_add(a, la, b, lb, res, mod ? 64 : 32);
      break;
    case MATH_SUB:
      l = big_sub(a, la, b, lb, res);
      break;
    case MATH_MUL:
      l = big_mul(a, la, b, lb, res, mod ? 65 : 32);
      break;
    case MATH_DIV:
      l = big_div(a, la, b, lb, 0, res);
      break;
    case MATH_SDIV:
      l = big_div(a, la, b, lb, 1, res);
      break;
    case MATH_MOD:
      l = big_mod(a, la, b, lb, 0, res);
      break;
    case MATH_SMOD:
      l = big_mod(a, la, b, lb, 1, res);
      break;
    case MATH_EXP:
      l = big_exp(a, la, b, lb, res);
      subgas(G_EXPBYTE * big_log256(b, lb));
      break;
    default:
      return EVM_ERROR_INVALID_OPCODE;
  }

  if (l < 0) return EVM_ERROR_UNSUPPORTED_CALL_OPCODE;
  // optimize
  while (l > 1 && r[0] == 0) {
    l--;
    r++;
  }

  if (mod) {
    uint8_t *mod_data, tmp[65];
    int      modl = evm_stack_pop_ref(evm, &mod_data);
    if (modl < 0) return modl;
    memcpy(tmp, r, l);
    l = big_mod(tmp, l, mod_data, modl, 0, res);
    r = res;
    if (l < 0) return l;

    // optimize
    while (l > 1 && r[0] == 0) {
      l--;
      r++;
    }
  }

  return evm_stack_push(evm, r, l);
}
static int op_signextend(evm_t* evm) {
  uint8_t* val;
  int32_t  k = evm_stack_pop_int(evm), l;
  if (k < 0) return k;
  l = evm_stack_pop_ref(evm, &val);
  if (l < 0) return l;

  uint8_t extendOnes = false;
  if (k <= 31) {
    extendOnes = l > 31 - k && (val[31 - k] & 0x80);
    uint8_t res[32];
    memset(res, 0, 32);
    memcpy(res + 32 - l, val, l);

    // 31-k-1 since k-th byte shouldn't be modified
    for (int i = 30 - k; i >= 0; i--) res[i] = extendOnes ? 0xff : 0;
    l   = 32;
    val = res;
    while (l > 0 && *val == 0) {
      l--;
      val++;
    }
    return evm_stack_push(evm, val, l);
  }
  return evm_stack_push(evm, val, l);
}
static int op_is_zero(evm_t* evm) {
  uint8_t res = 1, *a;
  int     l   = evm_stack_pop_ref(evm, &a), i;
  if (l < 0) return l;
  for (i = 0; i < l; i++) {
    if (a[i]) {
      res = 0;
      break;
    }
  }
  // push result to stack
  return evm_stack_push(evm, &res, 1);
}

static int op_not(evm_t* evm) {
  uint8_t res[32], *a;
  int     l = evm_stack_pop_ref(evm, &a), i;
  if (l < 0) return l;
  if (l < 32) memset(res, 0, 32 - l);
  memcpy(res + 32 - l, a, l);

  for (i = 0; i < 32; i++)
    res[i] = res[i] ^ 0xFF;

  a = res;
  l = 32;
  while (l > 1 && a[0] == 0) {
    a++;
    l--;
  }

  // push result to stack
  return evm_stack_push(evm, a, l);
}

#define OP_AND 0
#define OP_OR 1
#define OP_XOR 2

static int op_bit(evm_t* evm, uint8_t op) {
  uint8_t result[32], *res = result, *a;
  int     l, l1            = evm_stack_pop_ref(evm, &a), i, j;
  if (l1 < 0) return l1;
  memcpy(res + 32 - l1, a, l1);
  if (l1 < 32) memset(res, 0, 32 - l1);
  if ((l = evm_stack_pop_ref(evm, &a)) < 0) return EVM_ERROR_EMPTY_STACK;
  l1 = l1 > l ? l1 : l;
  res += 32 - l1;

  switch (op) {
    case OP_AND:
      for (i = 0, j = l1 - l; i < l; i++) res[j++] &= a[i];
      if (l < l1) memset(res, 0, l1 - l);
      break;
    case OP_OR:
      for (i = 0, j = l1 - l; i < l; i++) res[j++] |= a[i];
      break;
    case OP_XOR:
      for (i = 0, j = l1 - l; i < l; i++) res[j++] ^= a[i];
      break;
    default:
      return -1;
  }

  // push result to stack
  return evm_stack_push(evm, res, l1);
}

static int op_byte(evm_t* evm) {

  uint8_t pos, *b, res = 0xFF;
  int     l = evm_stack_pop_byte(evm, &pos);
  if (l == EVM_ERROR_EMPTY_STACK) return l;
  if (l < 0 || (pos & 0xE0)) res = 0;
  if ((l = evm_stack_pop_ref(evm, &b)) < 0) return EVM_ERROR_EMPTY_STACK;
  if (res) res = pos >= (32 - l) ? b[pos + l - 32] : 0;
  return evm_stack_push(evm, &res, 1);
}

static int op_cmp(evm_t* evm, int8_t eq, uint8_t sig) {
  uint8_t *a, *b, res = 0, sig_a = 0, sig_b = 0;
  int      len_a, len_b;
  // try fetch the 2 values from the stack
  if ((len_a = evm_stack_pop_ref(evm, &a)) < 0 || (len_b = evm_stack_pop_ref(evm, &b)) < 0) return EVM_ERROR_EMPTY_STACK;

  // if it is signed,. we store the sign and convert it to unsigned.
  if (sig) {
    sig_a = big_signed(a, len_a, a);
    sig_b = big_signed(b, len_b, b);
  }

  // compare the value
  switch (eq) {
    case -1:
      res = big_cmp(a, len_a, b, len_b) < 0;
      break;
    case 0:
      res = big_cmp(a, len_a, b, len_b) == 0;
      break;
    case 1:
      res = big_cmp(a, len_a, b, len_b) > 0;
      break;
  }

  // if it is signed we check check the sign and change the result if the sign changes.
  if (sig && eq) {
    if (sig_a && sig_b)
      res ^= 1;
    else if (sig_a || sig_b)
      res = eq < 0 ? sig_a : sig_b;
  }

  // push result to stack
  return evm_stack_push(evm, &res, 1);
}

int op_shift(evm_t* evm, uint8_t left) {
  uint8_t pos, *b, res[32];
  int     l;
  if ((evm->properties & EVM_EIP_CONSTANTINOPL) == 0) return EVM_ERROR_INVALID_OPCODE;
  l = evm_stack_pop_byte(evm, &pos);
  if (l == EVM_ERROR_EMPTY_STACK) return l;
  if (l < 0) { // the number is out of range
    if ((l = evm_stack_pop_ref(evm, &b)) < 0) return EVM_ERROR_EMPTY_STACK;
    if (left == 2 && l == 32 && (*b & 128)) { //signed number return max NUMBER as fault
      memset(res, 0xFF, 32);
      return evm_stack_push(evm, res, 32);
    } else {
      res[0] = 0;
      return evm_stack_push(evm, res, 1);
    }
  }

  if ((l = evm_stack_pop_ref(evm, &b)) < 0) return EVM_ERROR_EMPTY_STACK;
  memmove(res + 32 - l, b, l);
  if (l < 32) memset(res, 0, 32 - l);
  if (left == 1)
    big_shift_left(res, 32, pos);
  else if (left == 0)
    big_shift_right(res, 32, pos);
  else if (left == 2) { // signed shift right
    big_shift_right(res, 32, pos);
    if (l == 32 && (*b & 128)) { // the original number was signed
      for (l = 0; l<pos>> 3; l++) res[l] = 0xFF;
      l = 8 - (pos % 8);
      res[pos >> 3] |= (0XFF >> l) << l;
      return evm_stack_push(evm, res, 32);
    }
  }
  // optimize length
  for (pos = 32; pos > 0; pos--) {
    if (res[32 - pos]) break;
  }
  return evm_stack_push(evm, res + 32 - pos, pos);
}

static int op_sha3(evm_t* evm) {

  int32_t offset = evm_stack_pop_int(evm), len = evm_stack_pop_int(evm);
  if (offset < 0 || len < 0) return EVM_ERROR_EMPTY_STACK;
  if ((uint32_t)(offset + len) > evm->memory.bsize) return EVM_ERROR_ILLEGAL_MEMORY_ACCESS;
  uint8_t         res[32];
  struct SHA3_CTX ctx;
  sha3_256_Init(&ctx);
  sha3_Update(&ctx, evm->memory.b.data + offset, len);
  keccak_Final(&ctx, res);
  return evm_stack_push(evm, res, 32);
}

static int op_account(evm_t* evm, uint8_t key) {
  uint8_t *address, *data;
  int      l = evm_stack_pop_ref(evm, &address);
  if (l < 0) return EVM_ERROR_EMPTY_STACK;

#ifdef EVM_GAS
  if (key != EVM_ENV_BLOCKHASH) {
    account_t* ac = evm_get_account(evm, address, 0);
    uint8_t    tmp[4];
    if (ac) {
      data = NULL;
      if (key == EVM_ENV_BALANCE) {
        data = ac->balance;
        l    = 32;
      } else if (key == EVM_ENV_CODE_SIZE && ac->code.len) {
        int_to_bytes(ac->code.len, tmp);
        data = tmp;
        l    = 4;
      } else if (key == EVM_ENV_CODE_COPY && ac->code.len) {
        data = ac->code.data;
        l    = ac->code.len;
      } else if (key == EVM_ENV_CODE_HASH && ac->code.len) {
        uint8_t hash[32];
        sha3_to(&ac->code, hash);
        data = hash;
        l    = 32;
      }
      if (data) {
        while (data[0] == 0) {
          l--;
          data++;
        }
        return evm_stack_push(evm, data, l);
      }
    }
  }
#endif

  l = evm->env(evm, key, address, l, &data, 0, 0);
  return l < 0 ? l : evm_stack_push(evm, data, l);
}
static int op_dataload(evm_t* evm) {
  int pos = evm_stack_pop_int(evm);
  if (pos < 0) return pos;
  if (evm->call_data.len < (uint32_t) pos) return evm_stack_push_int(evm, 0);
  if (evm->call_data.len > (uint32_t) pos + 32)
    return evm_stack_push(evm, evm->call_data.data + pos, 32);
  else {
    uint8_t buffer[32];
    memset(buffer, 0, 32);
    if (evm->call_data.len - pos) {
      memcpy(buffer, evm->call_data.data + pos, evm->call_data.len - pos);
      return evm_stack_push(evm, buffer, 32);
    } else
      return evm_stack_push_int(evm, 0);
  }
}

static int op_datacopy(evm_t* evm, bytes_t* src) {
  int mem_pos = evm_stack_pop_int(evm), data_pos = evm_stack_pop_int(evm), data_len = evm_stack_pop_int(evm);
  if (mem_pos < 0 || data_len < 0 || data_pos < 0) return EVM_ERROR_EMPTY_STACK;
  if (src->len < (uint32_t)(data_pos + data_len)) return 0;
  if (evm_ensure_memory(evm, mem_pos + data_len) < 0) return EVM_ERROR_ILLEGAL_MEMORY_ACCESS;
  memcpy(evm->memory.b.data + mem_pos, src->data + data_pos, data_len);
  subgas(((data_len + 31) / 32) * G_COPY);
  return 0;
}

static int op_extcodecopy(evm_t* evm) {
  uint8_t *address, *data;
  int      l = evm_stack_pop_ref(evm, &address), mem_pos = evm_stack_pop_int(evm), code_pos = evm_stack_pop_int(evm), data_len = evm_stack_pop_int(evm);
  if (l < 0 || mem_pos < 0 || data_len < 0 || code_pos < 0) return EVM_ERROR_EMPTY_STACK;
  if (evm_ensure_memory(evm, mem_pos + data_len) < 0) return EVM_ERROR_ILLEGAL_MEMORY_ACCESS;

#ifdef EVM_GAS
  account_t* ac = evm_get_account(evm, address, 0);
  if (ac && ac->code.len) {
    memcpy(evm->memory.b.data + mem_pos, ac->code.data + code_pos, data_len);
    subgas(((data_len + 31) / 32) * G_COPY);
    return 0;
  }
#endif

  // address, memOffset, codeOffset, length
  int res = evm->env(evm, EVM_ENV_CODE_COPY, address, 20, &data, code_pos, data_len);
  if (res < 0) return res;
  memcpy(evm->memory.b.data + mem_pos, data, data_len);
  subgas(((data_len + 31) / 32) * G_COPY);
  return 0;
}

static int op_header(evm_t* evm, uint8_t index) {
  bytes_t b;
  int     l;
  if ((l = evm->env(evm, EVM_ENV_BLOCKHEADER, NULL, 0, &b.data, 0, 0)) < 0) return l;
  b.len = l;

  if (rlp_decode_in_list(&b, index, &b) == 1)
    return evm_stack_push(evm, b.data, b.len);
  else
    return evm_stack_push_int(evm, 0);
}

static int op_mload(evm_t* evm) {
  int mem_pos = evm_stack_pop_int(evm);
  if (mem_pos < 0) return EVM_ERROR_EMPTY_STACK;
  if (evm->memory.bsize < (uint32_t) mem_pos + 32) {
    uint8_t data[32];
    memset(data, 0, 32);
    if (evm->memory.bsize > (uint32_t) mem_pos)
      memcpy(data + 32 - evm->memory.bsize + mem_pos, evm->memory.b.data + mem_pos, evm->memory.bsize - mem_pos);
    return evm_stack_push(evm, data, 32);
  }
  return evm_stack_push(evm, evm->memory.b.data + mem_pos, 32);
}

static int op_mstore(evm_t* evm, uint8_t len) {
  int mem_pos = evm_stack_pop_int(evm), l;
  if (mem_pos < 0) return EVM_ERROR_EMPTY_STACK;
  if (evm_ensure_memory(evm, mem_pos + len) < 0) return EVM_ERROR_ILLEGAL_MEMORY_ACCESS;
  uint8_t* data;
  if ((l = evm_stack_pop_ref(evm, &data)) < 0) return EVM_ERROR_EMPTY_STACK;
  if (len == 1)
    evm->memory.b.data[mem_pos] = l ? data[l - 1] : 0;
  else {
    if (l < 32) memset(evm->memory.b.data + mem_pos, 0, 32 - l);
    memcpy(evm->memory.b.data + mem_pos + 32 - l, data, l);
  }
  return 0;
}

static int op_sload(evm_t* evm) {
  uint8_t *key, *value;
  int      l;
  if ((l = evm_stack_pop_ref(evm, &key)) < 0) return l;
#ifdef EVM_GAS
  storage_t* s = evm_get_storage(evm, evm->account, key, l, 0);
  if (s) {
    value = s->value;
    l     = 32;
    while (value[0] == 0 && l > 1) {
      l--;
      value++;
    }
    return evm_stack_push(evm, value, l);
  }
#endif
  if ((l = evm->env(evm, EVM_ENV_STORAGE, key, l, &value, 0, 0)) < 0) return l;
  return evm_stack_push(evm, value, l);
}

static int op_sstore(evm_t* evm) {
  uint8_t *key, *value;
  int      l_key, l_val;
  if ((l_key = evm_stack_pop_ref(evm, &key)) < 0) return l_key;
  if ((l_val = evm_stack_pop_ref(evm, &value)) < 0) return l_val;

#ifdef EVM_GAS
  storage_t* s       = evm_get_storage(evm, evm->account, key, l_key, 0);
  uint8_t    created = s == NULL, el = l_val;
  uint8_t    l_current = 0;
  if (created)
    s = evm_get_storage(evm, evm->account, key, l_key, 1);
  else {
    created = true;
    for (int i = 0; i < 32; i++) {
      if (s->value[i] != 0) {
        l_current = 32 - i;
        created   = false;
        break;
      }
    }
  }

  while (el > 0 && value[l_val - el] == 0) el--;

  if (evm->properties & EVM_EIP_CONSTANTINOPL) {
    uint8_t* original   = NULL;
    uint8_t  changed    = big_cmp(value, l_val, s->value, 32);
    int      l_original = evm->env(evm, EVM_ENV_STORAGE, key, l_key, &original, 0, 0);
    if (l_original < 0) l_original = 0;

    if (!changed) {
      subgas(GAS_CC_NET_SSTORE_NOOP_GAS);
    } else if (big_cmp(original, l_original, s->value, 32) == 0) {
      if (l_original == 0) {
        subgas(GAS_CC_NET_SSTORE_INIT_GAS);
      }
      if (el == 0) {
        evm->gas += GAS_CC_NET_SSTORE_CLEAR_REFUND;
      }

      subgas(GAS_CC_NET_SSTORE_CLEAN_GAS);
    } else {
      if (l_original) {
        if (l_current == 0)
          evm->gas -= GAS_CC_NET_SSTORE_CLEAR_REFUND;
        else
          evm->gas += GAS_CC_NET_SSTORE_CLEAR_REFUND;
      }

      if (big_cmp(original, l_original, value, l_val) == 0) {
        if (l_original == 0)
          evm->gas += GAS_CC_NET_SSTORE_RESET_CLEAR_REFUND;
        else
          evm->gas += GAS_CC_NET_SSTORE_RESET_REFUND;
      }
      subgas(GAS_CC_NET_SSTORE_DIRTY_GAS);
    }
  } else {
    if (el == 0 && created) {
      subgas(G_SRESET);
    } else if (el == 0 && !created) {
      subgas(G_SRESET);
      evm->gas += R_SCLEAR;
    } else if (el && created) {
      subgas(G_SSET);
    } else if (el && !created) {
      subgas(G_SRESET);
    }
  }

  uint256_set(value, l_val, s->value);
  return 0;
#else
  return EVM_ERROR_UNSUPPORTED_CALL_OPCODE;
#endif
}

static int op_jump(evm_t* evm, uint8_t cond) {
  int pos = evm_stack_pop_int(evm);
  if (pos < 0) return pos;
  if ((uint32_t) pos > evm->code.len || evm->code.data[pos] != 0x5B) return EVM_ERROR_INVALID_JUMPDEST;
  if (cond) {
    uint8_t c;
    int     ret = evm_stack_pop_byte(evm, &c);
    if (ret == EVM_ERROR_EMPTY_STACK) return EVM_ERROR_EMPTY_STACK;
    if (!c && ret >= 0) return 0; // the condition was false
  }
  evm->pos = pos + 1;
  return 0;
}

static int op_push(evm_t* evm, uint8_t len) {
  if (evm->code.len < (uint32_t) evm->pos + len) return EVM_ERROR_INVALID_PUSH;
  if (evm_stack_push(evm, evm->code.data + evm->pos, len) < 0)
    return EVM_ERROR_BUFFER_TOO_SMALL;
  evm->pos += len;
  return 0;
}

static int op_dup(evm_t* evm, uint8_t pos) {
  uint8_t* data;
  int      l = evm_stack_get_ref(evm, pos, &data);
  if (l < 0) return l;
  return evm_stack_push(evm, data, l);
}

static int op_swap(evm_t* evm, uint8_t pos) {
  uint8_t data[33], *a, *b;
  int     l1 = evm_stack_get_ref(evm, 1, &a);
  if (l1 < 0) return l1;
  int l2 = evm_stack_get_ref(evm, pos, &b);
  if (l2 < 0) return l2;
  if (l1 == l2) {
    memcpy(data, a, l1);
    memcpy(a, b, l1);
    memcpy(b, data, l1);
  } else if (l2 > l1) {
    memcpy(data, b, l2 + 1); // keep old b + len
    memcpy(b, a, l1 + 1);
    if (pos > 2) memmove(b + l1 + 1, b + l2 + 1, a - b - l2 - 1);
    memcpy(a + l1 - l2, data, l2 + 1);
  } else {
    memcpy(data, a, l1 + 1); // keep old b + len
    memcpy(a + l1 - l2, b, l2 + 1);
    if (pos > 2) memmove(b + l1 + 1, b + l2 + 1, a - b - l2 - 1);
    memcpy(b, data, l1 + 1);
  }
  return 0;
}

static int op_log(evm_t* evm, uint8_t len) {
  int memoffset = evm_stack_pop_int(evm);
  if (memoffset < 0) return memoffset;
  int memlen = evm_stack_pop_int(evm);
  if (memlen < 0) return memlen;
  subgas(len * G_LOGTOPIC + memlen * G_LOGDATA);
  if ((uint32_t) memoffset + memlen > evm->memory.b.len) return EVM_ERROR_ILLEGAL_MEMORY_ACCESS;
  logs_t* log = _malloc(sizeof(logs_t));

  log->next       = evm->root->logs;
  evm->root->logs = log;
  log->data.data  = _malloc(memlen);
  log->data.len   = memlen;
  memcpy(log->data.data, evm->memory.b.data + memoffset, memlen);
  log->topics.data = _malloc(len * 32);
  log->topics.len  = len * 32;

  uint8_t* t;
  int      l;

  for (int i = 0; i < len; i++) {
    if ((l = evm_stack_pop_ref(evm, &t)) < 0) return l;
    if (l < 32) memset(log->topics.data + i * 32, 0, 32 - l);
    memcpy(log->topics.data + i * 32 + 32 - l, t, l);
  }

  return 0;
}

int op_return(evm_t* evm, uint8_t revert) {
  int offset, len;
  if ((offset = evm_stack_pop_int(evm)) < 0) return offset;
  if ((len = evm_stack_pop_int(evm)) < 0) return len;

  if (evm->memory.bsize < (uint32_t) offset + len) return EVM_ERROR_ILLEGAL_MEMORY_ACCESS;
  if (evm->return_data.data) _free(evm->return_data.data);

  evm->return_data.data = _malloc(len);
  if (!evm->return_data.data) return EVM_ERROR_BUFFER_TOO_SMALL;
  memcpy(evm->return_data.data, evm->memory.b.data + offset, len);
  evm->return_data.len = len;
  evm->state           = revert ? EVM_STATE_REVERTED : EVM_STATE_STOPPED;
  return 0;
}
#define CALL_CALL 0
#define CALL_CODE 1
#define CALL_DELEGATE 2
#define CALL_STATIC 3

int op_call(evm_t* evm, uint8_t mode) {
  //
  // gasLimit, toAddress, value, inOffset, inLength, outOffset, outLength
  uint8_t *gas_limit, *address, *value, zero = 0;
  int32_t  l_gas, l_address, l_value = 0, in_offset, in_len, out_offset, out_len;
  if ((l_gas = evm_stack_pop_ref(evm, &gas_limit)) < 0) return l_gas;
  if ((l_address = evm_stack_pop_ref(evm, &address)) < 0) return l_address;
  if ((mode == CALL_CALL || mode == CALL_CODE) && (l_value = evm_stack_pop_ref(evm, &value)) < 0) return l_value;
  if ((in_offset = evm_stack_pop_int(evm)) < 0) return in_offset;
  if ((in_len = evm_stack_pop_int(evm)) < 0) return in_len;
  if ((out_offset = evm_stack_pop_int(evm)) < 0) return out_offset;
  if ((out_len = evm_stack_pop_int(evm)) < 0) return out_len;
  uint64_t gas = bytes_to_long(gas_limit, l_gas);

  if ((uint32_t) in_offset + in_len > evm->memory.bsize) return EVM_ERROR_ILLEGAL_MEMORY_ACCESS;

#ifdef EVM_GAS
  if (evm->gas < gas) return EVM_ERROR_OUT_OF_GAS;
#endif

  switch (mode) {
    case CALL_CALL:
      return evm_sub_call(evm,
                          address, address,
                          value, l_value,
                          evm->memory.b.data + in_offset, in_len,
                          evm->address,
                          evm->origin, gas, 0, out_offset, out_len);
    case CALL_CODE:
      return evm_sub_call(evm,
                          evm->address, address,
                          value, l_value,
                          evm->memory.b.data + in_offset, in_len,
                          evm->address,
                          evm->origin, gas, 0, out_offset, out_len);
    case CALL_DELEGATE:
      return evm_sub_call(evm,
                          evm->address, address,
                          evm->call_value.data, evm->call_value.len,
                          evm->memory.b.data + in_offset, in_len,
                          evm->caller,
                          evm->origin, gas, EVM_CALL_MODE_DELEGATE, out_offset, out_len);
    case CALL_STATIC:
      return evm_sub_call(evm,
                          address, address,
                          &zero, 1,
                          evm->memory.b.data + in_offset, in_len,
                          evm->address,
                          evm->origin, gas, EVM_CALL_MODE_STATIC, out_offset, out_len);
  }
  return EVM_ERROR_INVALID_OPCODE;
}

int evm_execute(evm_t* evm) {

  uint8_t op = evm->code.data[evm->pos++];
  if (op >= 0x60 && op <= 0x7F) // PUSH
    op_exec(op_push(evm, op - 0x5F), G_VERY_LOW);
  if (op >= 0x80 && op <= 0x8F) // DUP
    op_exec(op_dup(evm, op - 0x7F), G_VERY_LOW);
  if (op >= 0x90 && op <= 0x9F) // SWAP
    op_exec(op_swap(evm, op - 0x8E), G_VERY_LOW);
  if (op >= 0xA0 && op <= 0xA4) // LOG
    op_exec(op_log(evm, op - 0x9F), G_LOG);

  switch (op) {
    case 0x00: // STOP
      evm->state = EVM_STATE_STOPPED;
      return 0;

    case 0x01: //  ADD
      op_exec(op_math(evm, MATH_ADD, 0), G_VERY_LOW);
    case 0x02: //  MUL
      op_exec(op_math(evm, MATH_MUL, 0), G_LOW);
    case 0x03: //  SUB
      op_exec(op_math(evm, MATH_SUB, 0), G_VERY_LOW);
    case 0x04: //  DIV
      op_exec(op_math(evm, MATH_DIV, 0), G_LOW);
    case 0x05: //  SDIV
      op_exec(op_math(evm, MATH_SDIV, 0), G_LOW);
    case 0x06: //  MOD
      op_exec(op_math(evm, MATH_MOD, 0), G_LOW);
    case 0x07: //  SMOD
      op_exec(op_math(evm, MATH_SMOD, 0), G_LOW);
    case 0x08: //  ADDMOD
      op_exec(op_math(evm, MATH_ADD, 1), G_MID);
    case 0x09: //  MULMOD
      op_exec(op_math(evm, MATH_MUL, 1), G_MID);
    case 0x0A: //  EXP
      op_exec(op_math(evm, MATH_EXP, 0), G_EXP);
    case 0x0B: //  SIGNEXTEND
      op_exec(op_signextend(evm), G_LOW);

    case 0x10: // LT
      op_exec(op_cmp(evm, -1, 0), G_VERY_LOW);
    case 0x11: // GT
      op_exec(op_cmp(evm, 1, 0), G_VERY_LOW);
    case 0x12: // SLT
      op_exec(op_cmp(evm, -1, 1), G_VERY_LOW);
    case 0x13: // SGT
      op_exec(op_cmp(evm, 1, 1), G_VERY_LOW);
    case 0x14: // EQ
      op_exec(op_cmp(evm, 0, 0), G_VERY_LOW);
    case 0x15: // IS_ZERO
      op_exec(op_is_zero(evm), G_VERY_LOW);
    case 0x16: // AND
      op_exec(op_bit(evm, OP_AND), G_VERY_LOW);
    case 0x17: // OR
      op_exec(op_bit(evm, OP_OR), G_VERY_LOW);
    case 0x18: // XOR
      op_exec(op_bit(evm, OP_XOR), G_VERY_LOW);
    case 0x19: // NOT
      op_exec(op_not(evm), G_VERY_LOW);
    case 0x1a: // BYTE
      op_exec(op_byte(evm), G_VERY_LOW);
    case 0x1b: // SHL
      op_exec(op_shift(evm, 1), G_VERY_LOW);
    case 0x1c: // SHR
      op_exec(op_shift(evm, 0), G_VERY_LOW);
    case 0x1d: // SAR
      op_exec(op_shift(evm, 2), G_VERY_LOW);
    case 0x20: // SHA3
      op_exec(op_sha3(evm), G_SHA3);
    case 0x30: // ADDRESS
      op_exec(evm_stack_push(evm, evm->address, 20), G_BASE);
    case 0x31: // BALANCE
      op_exec(op_account(evm, EVM_ENV_BALANCE), G_BALANCE);
    case 0x32: // ORIGIN
      op_exec(evm_stack_push(evm, evm->origin, 20), G_BASE);
    case 0x33: // CALLER
      op_exec(evm_stack_push(evm, evm->caller, 20), G_BASE);
    case 0x34: // CALLVALUE
      op_exec(evm_stack_push(evm, evm->call_value.data, evm->call_value.len), G_BASE);
    case 0x35: // CALLDATALOAD
      op_exec(op_dataload(evm), G_VERY_LOW);
    case 0x36: // CALLDATA_SIZE
      op_exec(evm_stack_push_int(evm, evm->call_data.len), G_BASE);
    case 0x37: // CALLDATACOPY
      op_exec(op_datacopy(evm, &evm->call_data), G_VERY_LOW);
    case 0x38: // CODESIZE
      op_exec(evm_stack_push_int(evm, evm->code.len), G_BASE);
    case 0x39: // CODECOPY
      op_exec(op_datacopy(evm, &evm->code), G_VERY_LOW);
    case 0x3a: // GASPRICE
      op_exec(evm_stack_push(evm, evm->gas_price.data, evm->gas_price.len), G_BASE);
    case 0x3b: // EXTCODESIZE
      op_exec(op_account(evm, EVM_ENV_CODE_SIZE), G_EXTCODE);
    case 0x3c: // EXTCODECOPY
      op_exec(op_extcodecopy(evm), G_EXTCODE);
    case 0x3d: // RETURNDATASIZE
      op_exec(evm_stack_push_int(evm, evm->last_returned.len), G_BASE);
    case 0x3e: // RETURNDATACOPY
      op_exec(op_datacopy(evm, &evm->last_returned), G_VERY_LOW);
    case 0x3f: // EXTCODEHASH
      op_exec(op_account(evm, EVM_ENV_CODE_HASH), G_BALANCE);
    case 0x40: // BLOCKHASH
      op_exec(op_account(evm, EVM_ENV_BLOCKHASH), G_BLOCKHASH);
    case 0x41: // COINBASE
      op_exec(op_header(evm, BLOCKHEADER_MINER), G_BASE);
    case 0x42: // TIMESTAMP
      op_exec(op_header(evm, BLOCKHEADER_TIMESTAMP), G_BASE);
    case 0x43: // NUMBER
      op_exec(op_header(evm, BLOCKHEADER_NUMBER), G_BASE);
    case 0x44: // DIFFICULTY
      op_exec(op_header(evm, BLOCKHEADER_DIFFICULTY), G_BASE);
    case 0x45: // GASLIMIT
      op_exec(op_header(evm, BLOCKHEADER_GAS_LIMIT), G_BASE);

    case 0x50: // POP
      op_exec(evm_stack_pop(evm, NULL, 0), G_BASE);
    case 0x51: // MLOAD
      op_exec(op_mload(evm), G_VERY_LOW);
    case 0x52: // MSTORE
      op_exec(op_mstore(evm, 32), G_VERY_LOW);
    case 0x53: // MSTORE8
      op_exec(op_mstore(evm, 1), G_VERY_LOW);
    case 0x54: // SLOAD
      op_exec(op_sload(evm), G_SLOAD);
    case 0x55: // SSTORE
      return op_sstore(evm);
    case 0x56: // JUMP
      op_exec(op_jump(evm, 0), G_MID);
    case 0x57: // JUMPI
      op_exec(op_jump(evm, 1), G_HIGH);
    case 0x58: // PC
      op_exec(evm_stack_push_int(evm, evm->pos - 1), G_BASE);
    case 0x59: // MSIZE
      op_exec(evm_stack_push_int(evm, evm->memory.bsize), G_BASE);
    case 0x5a: // GAS     --> here we always return enough gas to keep going, since eth call should not use it anyway
#ifdef EVM_GAS
      op_exec(evm_stack_push_long(evm, evm->gas), G_BASE);
#else
      return evm_stack_push_int(evm, 0xFFFFFFF);
#endif
    case 0x5b: // JUMPDEST
      op_exec(0, G_JUMPDEST);
    case 0xF0: // CREATE   -> we don't support it for call
      return EVM_ERROR_UNSUPPORTED_CALL_OPCODE;
    case 0xF1: // CALL
      op_exec(op_call(evm, CALL_CALL), G_CALL);
    case 0xF2: // CALLCODE
      op_exec(op_call(evm, CALL_CODE), G_CALL);
    case 0xF3: // RETURN
      return op_return(evm, 0);
    case 0xF4: // DELEGATE_CALL
      op_exec(op_call(evm, CALL_DELEGATE), G_CALL);
    case 0xFA: // STATIC_CALL
      op_exec(op_call(evm, CALL_STATIC), G_CALL);
    case 0xFD: // REVERT
      return op_return(evm, 1);
    case 0xFE: // INVALID OPCODE
      return EVM_ERROR_INVALID_OPCODE;
    case 0xFF: // SELFDESTRUCT
      return EVM_ERROR_INVALID_OPCODE;

    default:
      return EVM_ERROR_INVALID_OPCODE;
  }
}

int evm_run(evm_t* evm) {
  uint32_t timeout = 0xFFFFFFFF;
  int      res     = 0;
  evm->state       = EVM_STATE_RUNNING;
  while (res >= 0 && evm->state == EVM_STATE_RUNNING && evm->pos < evm->code.len) {
#ifdef TEST
    uint32_t last     = evm->pos;
    uint64_t last_gas = evm->gas;
    res               = evm_execute(evm);
    if (evm->properties & EVM_DEBUG) evm_print_stack(evm, last_gas, last);
#else
    res = evm_execute(evm);
#endif
    if ((timeout--) == 0) return EVM_ERROR_TIMEOUT;
  }
#ifdef TEST
  if (evm->properties & EVM_DEBUG) printf("\n Result-code (%i) : ", res);
#endif
  return res;
}