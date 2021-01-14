/* Copyright (c) 2020, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#if !defined(OPENSSL_NO_ASM) && defined(OPENSSL_X86_64) && \
    !defined(OPENSSL_SMALL)

#include <openssl/ec.h>

#include "p256_p384_common-x86_64.h"

// P-384 field operations.
//
// An element mod P in P-384 is represented as a little-endian array of
// |P384_LIMBS| |BN_ULONG|s, spanning the full range of values.
//
// The following functions take fully-reduced inputs mod P and give
// fully-reduced outputs. They may be used in-place.
#define P384_LIMBS (384 / BN_BITS2)

// A P384_POINT_AFFINE represents a P-384 point in affine coordinates. Infinity
// is encoded as (0, 0).
typedef struct {
  BN_ULONG X[P384_LIMBS];
  BN_ULONG Y[P384_LIMBS];
} P384_POINT_AFFINE;

#define PRECOMP384_ROWS_NUM 64
typedef P384_POINT_AFFINE PRECOMP384_ROW[PRECOMP384_ROWS_NUM];

#include "p384-x86_64-table.h"

// One converted into the Montgomery domain
static const BN_ULONG ONE_384[P384_LIMBS] = {
    TOBN(0xffffffff, 0x00000001), TOBN(0x00000000, 0xffffffff),
    TOBN(0x00000000, 0x00000001), TOBN(0x00000000, 0x00000000),
    TOBN(0x00000000, 0x00000000), TOBN(0x00000000, 0x00000000)};

// ecp_nistp384_select_w7 sets |*val| to |in[index-1]| if 1 <= |index| <= 64
// and all zeros (the point at infinity) if |index| is 0. This is done in
// constant time.
static inline void ecp_nistp384_select_w7(EC_RAW_POINT *val,
                                          const P384_POINT_AFFINE in[64],
                                          int index) {
  OPENSSL_memset(val, 0, sizeof(*val));

  for (int i = 0; i < PRECOMP384_ROWS_NUM; i++) {
    BN_ULONG mask = constant_time_eq_w(i, index - 1);

    for (int j = 0; j < P384_LIMBS; j++) {
      val->X.words[j] |= (mask & in[i].X[j]);
      val->Y.words[j] |= (mask & in[i].Y[j]);
    }
  }

  // We set Z to zero if |val| is infinity and |ONE_384| otherwise. |val| was
  // computed from the table, so it is infinity iff |wvalue >> 1| is zero.
  copy_conditional(val->Z.words, ONE_384, is_not_zero(index), P384_LIMBS);
}

static void ecp_nistp384_point_mul_base(const EC_GROUP *group, EC_RAW_POINT *r,
                                        const EC_SCALAR *scalar) {
  assert(group->field.width == P384_LIMBS);

  alignas(32) EC_RAW_POINT t;
  EC_FELEM tmp;
  size_t index = 0;
  uint8_t p_str[49] = {0};

  // Store scalar->bytes in a byte extended temporary location p_str
  OPENSSL_memcpy(p_str, scalar->bytes, 48);

  // First window
  unsigned wvalue = calc_first_wvalue(&index, p_str);

  ecp_nistp384_select_w7(r, ecp_nistp384_precomputed[0], wvalue >> 1);
  ec_felem_neg(group, &tmp, &r->Y);
  copy_conditional(r->Y.words, tmp.words, wvalue & 1, P384_LIMBS);

  for (int i = 1; i < 55; i++) {
    wvalue = calc_wvalue(&index, p_str);

    ecp_nistp384_select_w7(&t, ecp_nistp384_precomputed[i], wvalue >> 1);

    ec_felem_neg(group, &tmp, &t.Y);
    copy_conditional(t.Y.words, tmp.words, wvalue & 1, P384_LIMBS);

    ec_GFp_mont_add(group, r, r, &t);
  }
}

DEFINE_METHOD_FUNCTION(EC_METHOD, EC_GFp_nistp384_method) {
  out->group_init = ec_GFp_mont_group_init;
  out->group_finish = ec_GFp_mont_group_finish;
  out->group_set_curve = ec_GFp_mont_group_set_curve;
  out->point_get_affine_coordinates = ec_GFp_mont_point_get_affine_coordinates;
  out->add = ec_GFp_mont_add;
  out->dbl = ec_GFp_mont_dbl;
  out->mul = ec_GFp_mont_mul;
  out->mul_base = ecp_nistp384_point_mul_base;
  out->mul_public = NULL;
  out->mul_public_batch = ec_GFp_mont_mul_public_batch;
  out->felem_mul = ec_GFp_mont_felem_mul;
  out->felem_sqr = ec_GFp_mont_felem_sqr;
  out->felem_to_bytes = ec_GFp_mont_felem_to_bytes;
  out->felem_from_bytes = ec_GFp_mont_felem_from_bytes;
  out->felem_reduce = ec_GFp_mont_felem_reduce;
  out->felem_exp = ec_GFp_mont_felem_exp;
  out->scalar_inv0_montgomery = ec_simple_scalar_inv0_montgomery;
  out->scalar_to_montgomery_inv_vartime =
      ec_simple_scalar_to_montgomery_inv_vartime;
  out->cmp_x_coordinate = ec_GFp_mont_cmp_x_coordinate;
}

#endif  // !OPENSSL_NO_ASM && OPENSSL_X86_64 && !OPENSSL_SMALL
