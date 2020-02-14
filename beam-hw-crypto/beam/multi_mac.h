// Copyright 2019 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.#include "misc.h"

#ifndef _MULTI_MAC_H_
#define _MULTI_MAC_H_

#include "definitions.h"

// #define MULTI_MAC_CASUAL_MAX_ODD 31 // (1 << 5) - 1
#define MULTI_MAC_CASUAL_MAX_ODD 1
#define MULTI_MAC_CASUAL_COUNT \
  2  // (MULTI_MAC_CASUAL_MAX_ODD >> 1) + 2 // we need a single even: x2
// #define MULTI_MAC_PREPARED_MAX_ODD  0xff // 255
#define MULTI_MAC_PREPARED_MAX_ODD 1
#define MULTI_MAC_PREPARED_COUNT 1  // (MULTI_MAC_PREPARED_MAX_ODD >> 1) + 1

typedef struct {
  uint32_t next_item;
  uint32_t odd;
} _multi_mac_fast_aux_t;

// 292 bytes of data
typedef struct {
  // 124 bytes * 2 = 248 bytes
  secp256k1_gej pt[MULTI_MAC_CASUAL_COUNT];
  // 32 bytes
  secp256k1_scalar k;
  uint32_t prepared;
  // 8 bytes
  _multi_mac_fast_aux_t aux;
} multi_mac_casual_t;

typedef struct {
  // 124 bytes * 1 = 124 bytes
  secp256k1_gej pt[MULTI_MAC_PREPARED_COUNT];
} multi_mac_prepared_t;

#define MAX_CASUAL 1
#define MAX_PREPARED 64U * 2 + 1
// 21456 bytes
typedef struct {
  //multi_mac_casual_t *casual;
  // 292 bytes * 1 = 292 bytes bytes
  multi_mac_casual_t casual[MAX_CASUAL];
  uint32_t n_casual;

  // 124 bytes * 129 = 15996 bytes
  multi_mac_prepared_t prepared[MAX_PREPARED];
  // 32 bytes * 129 = 4128 bytes
  secp256k1_scalar k_prepared[MAX_PREPARED];
  // 8 bytes * 129 = 1032 bytes
  _multi_mac_fast_aux_t aux_prepared[MAX_PREPARED];
  //_multi_mac_fast_aux_t *aux_prepared;
  //multi_mac_prepared_t **prepared;
  uint32_t n_prepared;
} multi_mac_t;

void multi_mac_with_bufs_alloc(multi_mac_t *mm, int max_casual,
                               int max_prepared);

void multi_mac_with_bufs_free(multi_mac_t *mm);

void multi_mac_reset(multi_mac_t *mm);

void multi_mac_casual_init_new(multi_mac_casual_t *casual,
                               const secp256k1_gej *p);

void multi_mac_casual_init(multi_mac_casual_t *casual, const secp256k1_gej *p,
                           const secp256k1_scalar *k);

void multi_mac_fast_aux_schedule(_multi_mac_fast_aux_t *aux, const secp256k1_scalar *k,
                                 unsigned int iBitsRemaining,
                                 unsigned int nMaxOdd, unsigned int *pTbl,
                                 unsigned int iThisEntry);

void multi_mac_calculate(multi_mac_t *mm, secp256k1_gej *res);

#endif  // _MULTI_MAC_H_