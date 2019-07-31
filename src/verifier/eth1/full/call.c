#include "../../../core/client/verifier.h"
#include "../../../core/util/mem.h"
#include "big.h"
#include "evm.h"
#include "gas.h"
#include "mem.h"
#include <string.h>
#ifdef EVM_GAS
#include "accounts.h"
#endif
// free a evm-instance
void evm_free(evm_t* evm) {
  if (evm->last_returned.data) _free(evm->last_returned.data);
  if (evm->return_data.data) _free(evm->return_data.data);
  if (evm->stack.b.data) _free(evm->stack.b.data);
  if (evm->memory.b.data) _free(evm->memory.b.data);
  if (evm->invalid_jumpdest) _free(evm->invalid_jumpdest);

#ifdef EVM_GAS
  logs_t* l = NULL;
  while (evm->logs) {
    l = evm->logs;
    _free(l->data.data);
    _free(l->topics.data);
    evm->logs = l->next;
    _free(l);
  }

  account_t* ac = NULL;
  storage_t* s  = NULL;

  while (evm->accounts) {
    ac = evm->accounts;
    s  = NULL;
    while (ac->storage) {
      s           = ac->storage;
      ac->storage = s->next;
      _free(s);
    }
    evm->accounts = ac->next;
    _free(ac);
  }
#endif
}

/**
 * sets the default for the evm.
 */
int evm_prepare_evm(evm_t*      evm,
                    address_t   address,
                    address_t   account,
                    address_t   origin,
                    address_t   caller,
                    evm_get_env env,
                    void*       env_ptr,
                    wlen_t      mode) {
  evm->stack.b.data = _malloc(64);
  evm->stack.b.len  = 0;
  evm->stack.bsize  = 64;

  evm->memory.b.data = _calloc(32, 1);
  evm->memory.b.len  = 0;
  evm->memory.bsize  = 32;
  memset(evm->memory.b.data, 0, 32);

  evm->stack_size       = 0;
  evm->invalid_jumpdest = NULL;

  evm->pos   = 0;
  evm->state = EVM_STATE_INIT;

  evm->last_returned.data = NULL;
  evm->last_returned.len  = 0;

  evm->properties = EVM_PROP_CONSTANTINOPL;

  evm->env     = env;
  evm->env_ptr = env_ptr;

  evm->gas_price.data = NULL;
  evm->gas_price.len  = 0;

  evm->call_data.data = NULL;
  evm->call_data.len  = 0;

  evm->call_value.data = NULL;
  evm->call_value.len  = 0;

  evm->return_data.data = NULL;
  evm->return_data.len  = 0;

  evm->caller = caller;
  evm->origin = origin;
  if (mode == EVM_CALL_MODE_CALLCODE)
    evm->account = address;
  else
    evm->account = account;
  evm->address = address;

#ifdef EVM_GAS
  evm->accounts = NULL;
  evm->gas      = 0;
  evm->logs     = NULL;
  evm->parent   = NULL;
  evm->refund   = 0;
  evm->init_gas = 0;
#endif

  // if the address is NULL this is a CREATE-CALL, so don't try to fetch the code here.
  if (address) {
    // get the code
    uint8_t* tmp = NULL;
    int      l   = env(evm, EVM_ENV_CODE_SIZE, account, 20, &tmp, 0, 32);
    // error?
    if (l < 0) return l;
    evm->code.len = bytes_to_int(tmp, l);

    // copy the code or return error
    l = env(evm, EVM_ENV_CODE_COPY, account, 20, &evm->code.data, 0, 0);
    return l < 0 ? l : 0;
  } else
    return 0;
}

/**
 * handle internal calls.
 */
int evm_sub_call(evm_t*    parent,
                 address_t address,
                 address_t code_address,
                 uint8_t* value, wlen_t l_value,
                 uint8_t* data, uint32_t l_data,
                 address_t caller,
                 address_t origin,
                 uint64_t  gas,
                 wlen_t    mode,
                 uint32_t out_offset, uint32_t out_len

) {
  // create a new evm
  evm_t evm;
  int   res = evm_prepare_evm(&evm, address, code_address, origin, caller, parent->env, parent->env_ptr, mode), success = 0;

  evm.properties      = parent->properties;
  evm.call_data.data  = data;
  evm.call_data.len   = l_data;
  evm.call_value.data = value;
  evm.call_value.len  = l_value;

  // if this is a static call, we set the static flag which can be checked before any state-chage occur.
  if (mode == EVM_CALL_MODE_STATIC) evm.properties |= EVM_PROP_STATIC;

#ifdef EVM_GAS
  // inherit root-evm
  evm.parent = parent;

  uint64_t   max_gas_provided = parent->gas - (parent->gas >> 6);
  account_t* new_account      = NULL;

  if (!address) {
    new_account = evm_get_account(&evm, code_address, 1);
    // this is a create-call
    evm.code               = bytes(data, l_data);
    evm.call_data.len      = 0;
    evm.address            = code_address;
    new_account->nonce[31] = 1;

    // increment the nonce of the sender
    account_t* sender_account = evm_get_account(&evm, caller, 1);
    bytes32_t  new_nonce;
    uint8_t    one = 1;
    uint256_set(new_nonce, big_add(sender_account->nonce, 32, &one, 1, new_nonce, 32), sender_account->nonce);

    // handle gas
    gas = max_gas_provided;
  } else
    gas = min(gas, max_gas_provided);

  // give the call the amount of gas
  evm.gas            = gas;
  evm.gas_price.data = parent->gas_price.data;
  evm.gas_price.len  = parent->gas_price.len;

  // and try to transfer the value
  if (res == 0 && !big_is_zero(value, l_value)) {
    // if we have a value and this should be static we throw
    if (mode == EVM_CALL_MODE_STATIC)
      res = EVM_ERROR_UNSUPPORTED_CALL_OPCODE;
    else {
      // only for CALL or CALLCODE we add the CALLSTIPEND
      uint32_t gas_call_value = 0;
      if (mode == EVM_CALL_MODE_CALL || mode == EVM_CALL_MODE_CALLCODE) {
        evm.gas += G_CALLSTIPEND;
        gas_call_value = G_CALLVALUE;
      }
      res = transfer_value(&evm, parent->address, evm.address, value, l_value, gas_call_value);
    }
  }
  if (res == 0) {
    // if we don't even have enough gas
    if (parent->gas < gas)
      res = EVM_ERROR_OUT_OF_GAS;
    else
      parent->gas -= gas;
  }

#else
  UNUSED_VAR(value);
  UNUSED_VAR(gas);
  UNUSED_VAR(l_value);
#endif

  // execute the internal call
  if (res == 0) success = evm_run(&evm);

  // put the success in the stack ( in case of a create we add the new address)
  if (!address && success == 0)
    res = evm_stack_push(parent, evm.account, 20);
  else
    res = evm_stack_push_int(parent, success == 0 ? 1 : 0);

  // if we have returndata we write them into memory
  if (success == 0 && evm.return_data.data) {
    // if we have a target to write the result to we do.
    if (out_len) res = evm_mem_write(parent, out_offset, evm.return_data, out_len);

#ifdef EVM_GAS
    // if we created a new account, we can now copy the return_data as code
    if (new_account)
      new_account->code = evm.return_data;
#endif

    // move the return_data to parent.
    if (res == 0) {
      if (parent->last_returned.data) _free(parent->last_returned.data);
      parent->last_returned = evm.return_data;
      evm.return_data.data  = NULL;
      evm.return_data.len   = 0;
    }
  }

#ifdef EVM_GAS
  // if it was successfull we copy the new state to the parent
  if (success == 0 && evm.state != EVM_STATE_REVERTED)
    copy_state(parent, &evm);

  // if we have gas left and it was successfull we returen it to the parent process.
  if (success == 0) parent->gas += evm.gas;
#endif

  // clean up
  evm_free(&evm);

  // we always return 0 since a failure simply means we write a 0 on the stack.
  return res;
}

/**
 * run a evm-call
 */
int evm_call(void* vc,
             address_t   address,
             uint8_t* value, wlen_t l_value,
             uint8_t* data, uint32_t l_data,
             address_t caller,
             uint64_t  gas,
             bytes_t** result) {

  evm_t evm;
  int   res = evm_prepare_evm(&evm, address, address, caller, caller, in3_get_env, vc, 0);

  // check if the caller is empty
  uint8_t* ccaller = caller;
  int      l       = 20;
  optimize_len(ccaller, l);

#ifdef EVM_GAS
  // we only transfer initial value if the we have caller
  if (res == 0 && l > 1) res = transfer_value(&evm, caller, address, value, l_value, 0);
#else
  UNUSED_VAR(gas);
  UNUSED_VAR(value);
  UNUSED_VAR(l_value);
#endif

//  evm.properties     = EVM_PROP_DEBUG;
#ifdef EVM_GAS
  evm.gas = gas;
#endif
  evm.call_data.data = data;
  evm.call_data.len  = l_data;
  if (res == 0) res = evm_run(&evm);
  if (res == 0 && evm.return_data.data)
    *result = b_dup(&evm.return_data);
  evm_free(&evm);

  return res;
}
