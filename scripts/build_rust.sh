#!/bin/sh
cd scripts
cat <<EOF >../c/include/in3.rs.h
// AUTO-GENERATED FILE
// See scripts/build_rust.sh
#include "../src/core/client/context_internal.h"
#include "in3/bytes.h"
#include "in3/client.h"
#include "in3/context.h"
#include "in3/error.h"
#include "in3/eth_api.h"
#include "in3/in3_init.h"
#include "in3/in3_curl.h"
#include "../src/third-party/crypto/ecdsa.h"
EOF
cd ..
