/*
 * Copyright 2014-2016 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright (c) 2014, Intel Corporation. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 *
 * Originally written by Shay Gueron (1, 2), and Vlad Krasnov (1)
 * (1) Intel Corporation, Israel Development Center, Haifa, Israel
 * (2) University of Haifa, Israel
 *
 * Reference:
 * S.Gueron and V.Krasnov, "Fast Prime Field Elliptic Curve Cryptography with
 *                          256 Bit Primes"
 */

#ifndef OPENSSL_HEADER_EC_P256__P384_X86_64_H
#define OPENSSL_HEADER_EC_P256__P384_X86_64_H

#if !defined(OPENSSL_NO_ASM) && defined(OPENSSL_X86_64) && \
    !defined(OPENSSL_SMALL)

#if defined(__cplusplus)
extern "C" {
#endif


// Recode window to a signed digit, see |ec_GFp_nistp_recode_scalar_bits| in
// util.c for details
static unsigned booth_recode_w5(unsigned in) {
  unsigned s, d;

  s = ~((in >> 5) - 1);
  d = (1 << 6) - in - 1;
  d = (d & s) | (in & ~s);
  d = (d >> 1) + (d & 1);

  return (d << 1) + (s & 1);
}

static unsigned booth_recode_w7(unsigned in) {
  unsigned s, d;

  s = ~((in >> 7) - 1);
  d = (1 << 8) - in - 1;
  d = (d & s) | (in & ~s);
  d = (d >> 1) + (d & 1);

  return (d << 1) + (s & 1);
}

// copy_conditional copies |src| to |dst| if |move| is one and leaves it as-is
// if |move| is zero.
//
// WARNING: this breaks the usual convention of constant-time functions
// returning masks.
static void copy_conditional(BN_ULONG *dst,
                             const BN_ULONG *src,
                             BN_ULONG move,
                             size_t num_of_limbs) {
  BN_ULONG mask1 = ((BN_ULONG)0) - move;
  bn_select_words(dst, mask1, src, dst, num_of_limbs);
}

// is_not_zero returns one iff in != 0 and zero otherwise.
//
// WARNING: this breaks the usual convention of constant-time functions
// returning masks.
//
// (define-fun is_not_zero ((in (_ BitVec 64))) (_ BitVec 64)
//   (bvlshr (bvor in (bvsub #x0000000000000000 in)) #x000000000000003f)
// )
//
// (declare-fun x () (_ BitVec 64))
//
// (assert (and (= x #x0000000000000000) (= (is_not_zero x) #x0000000000000001)))
// (check-sat)
//
// (assert (and (not (= x #x0000000000000000)) (= (is_not_zero x) #x0000000000000000)))
// (check-sat)
//
static BN_ULONG is_not_zero(BN_ULONG in) {
  in |= (0 - in);
  in >>= BN_BITS2 - 1;
  return in;
}

// p_str size is 33 bytes for p-256 and 49 bytes for p-384
static unsigned calc_first_wvalue(size_t *index, const uint8_t *p_str) {
  static const unsigned kWindowSize = 7;
  static const unsigned kMask = (1 << (7 /* kWindowSize */ + 1)) - 1;
  *index = kWindowSize;

  unsigned wvalue = (p_str[0] << 1) & kMask;
  return booth_recode_w7(wvalue);
}

// p_str size is 33 bytes for p-256 and 49 bytes for p-384
static unsigned calc_wvalue(size_t *index, const uint8_t *p_str) {
  static const unsigned kWindowSize = 7;
  static const unsigned kMask = (1 << (7 /* kWindowSize */ + 1)) - 1;

  const unsigned off = (*index - 1) / 8;
  unsigned wvalue = p_str[off] | p_str[off + 1] << 8;
  wvalue = (wvalue >> ((*index - 1) % 8)) & kMask;
  *index += kWindowSize;

  return booth_recode_w7(wvalue);
}

#if defined(__cplusplus)
}  // extern C++
#endif

#endif // !OPENSSL_NO_ASM && OPENSSL_X86_64 && !OPENSSL_SMALL

#endif // OPENSSL_HEADER_EC_P256__P384_X86_64_H
