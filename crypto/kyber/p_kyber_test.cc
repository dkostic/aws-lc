#include <gtest/gtest.h>
#include <openssl/base.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/mem.h>

#include "../crypto/evp_extra/internal.h"
#include "../internal.h"

TEST(Kyber512Test, EVP_PKEY_keygen) {
  EVP_PKEY_CTX *kyber_pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_KYBER512, nullptr);
  ASSERT_NE(kyber_pkey_ctx, nullptr);

  EVP_PKEY *kyber_pkey = EVP_PKEY_new();
  ASSERT_NE(kyber_pkey, nullptr);

  EXPECT_TRUE(EVP_PKEY_keygen_init(kyber_pkey_ctx));
  EXPECT_TRUE(EVP_PKEY_keygen(kyber_pkey_ctx, &kyber_pkey));
  ASSERT_NE(kyber_pkey->pkey.ptr, nullptr);

  const KYBER_512_KEY *kyber512Key = (KYBER_512_KEY *)(kyber_pkey->pkey.ptr);
  EXPECT_TRUE(kyber512Key->has_private);

  uint8_t *buf = nullptr;
  size_t buf_size;
  EXPECT_TRUE(EVP_PKEY_get_raw_public_key(kyber_pkey, buf, &buf_size));
  EXPECT_EQ((size_t)KYBER512_PUBLICKEY_BYTES, buf_size);

  buf = (uint8_t *)OPENSSL_malloc(buf_size);
  ASSERT_NE(buf, nullptr);
  EXPECT_TRUE(EVP_PKEY_get_raw_public_key(kyber_pkey, buf, &buf_size));

  buf_size = 0;
  EXPECT_FALSE(EVP_PKEY_get_raw_public_key(kyber_pkey, buf, &buf_size));

  uint32_t err = ERR_get_error();
  EXPECT_EQ(ERR_LIB_EVP, ERR_GET_LIB(err));
  EXPECT_EQ(EVP_R_BUFFER_TOO_SMALL, ERR_GET_REASON(err));
  OPENSSL_free(buf);
  buf = nullptr;

  EXPECT_TRUE(EVP_PKEY_get_raw_private_key(kyber_pkey, buf, &buf_size));
  EXPECT_EQ((size_t)KYBER512_SECRETKEY_BYTES, buf_size);

  buf = (uint8_t *)OPENSSL_malloc(buf_size);
  ASSERT_NE(buf, nullptr);
  EXPECT_TRUE(EVP_PKEY_get_raw_private_key(kyber_pkey, buf, &buf_size));

  buf_size = 0;
  EXPECT_FALSE(EVP_PKEY_get_raw_private_key(kyber_pkey, buf, &buf_size));
  err = ERR_get_error();
  EXPECT_EQ(ERR_LIB_EVP, ERR_GET_LIB(err));
  EXPECT_EQ(EVP_R_BUFFER_TOO_SMALL, ERR_GET_REASON(err));
  OPENSSL_free(buf);

  EVP_PKEY_free(kyber_pkey);
  EVP_PKEY_CTX_free(kyber_pkey_ctx);
}

TEST(Kyber512Test, EVP_PKEY_cmp) {
  EVP_PKEY_CTX *kyber_pkey_ctx1 = EVP_PKEY_CTX_new_id(EVP_PKEY_KYBER512, nullptr);
  ASSERT_NE(kyber_pkey_ctx1, nullptr);

  EVP_PKEY *kyber_pkey1 = EVP_PKEY_new();
  ASSERT_NE(kyber_pkey1, nullptr);

  EXPECT_TRUE(EVP_PKEY_keygen_init(kyber_pkey_ctx1));
  EXPECT_TRUE(EVP_PKEY_keygen(kyber_pkey_ctx1, &kyber_pkey1));
  ASSERT_NE(kyber_pkey1->pkey.ptr, nullptr);

  EVP_PKEY_CTX *kyber_pkey_ctx2 = EVP_PKEY_CTX_new_id(EVP_PKEY_KYBER512, nullptr);
  ASSERT_NE(kyber_pkey_ctx2, nullptr);

  EVP_PKEY *kyber_pkey2 = EVP_PKEY_new();
  ASSERT_NE(kyber_pkey2, nullptr);

  EXPECT_TRUE(EVP_PKEY_keygen_init(kyber_pkey_ctx2));
  EXPECT_TRUE(EVP_PKEY_keygen(kyber_pkey_ctx2, &kyber_pkey2));
  ASSERT_NE(kyber_pkey2->pkey.ptr, nullptr);

  EXPECT_EQ(0, EVP_PKEY_cmp(kyber_pkey1, kyber_pkey2));
  EXPECT_EQ(1, EVP_PKEY_cmp(kyber_pkey1, kyber_pkey1));
  EXPECT_EQ(1, EVP_PKEY_cmp(kyber_pkey2, kyber_pkey2));

  EVP_PKEY_free(kyber_pkey1);
  EVP_PKEY_free(kyber_pkey2);
  EVP_PKEY_CTX_free(kyber_pkey_ctx1);
  EVP_PKEY_CTX_free(kyber_pkey_ctx2);
}

TEST(Kyber512Test, EVP_PKEY_new_raw) {
  //Source key
  EVP_PKEY_CTX *kyber_pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_KYBER512, nullptr);
  ASSERT_NE(kyber_pkey_ctx, nullptr);

  EVP_PKEY *kyber_pkey = EVP_PKEY_new();
  ASSERT_NE(kyber_pkey, nullptr);

  EXPECT_TRUE(EVP_PKEY_keygen_init(kyber_pkey_ctx));
  EXPECT_TRUE(EVP_PKEY_keygen(kyber_pkey_ctx, &kyber_pkey));
  ASSERT_NE(kyber_pkey->pkey.ptr, nullptr);
  const KYBER_512_KEY *kyber512Key = (KYBER_512_KEY *)(kyber_pkey->pkey.ptr);

  //New raw public key
  EVP_PKEY *new_public = EVP_PKEY_new_raw_public_key(EVP_PKEY_KYBER512,
                                                     NULL,
                                                     kyber512Key->pub,
                                                     KYBER512_PUBLICKEY_BYTES);
  ASSERT_NE(new_public, nullptr);

  uint8_t *buf = nullptr;
  size_t buf_size;
  EXPECT_FALSE(EVP_PKEY_get_raw_private_key(new_public, buf, &buf_size));
  uint32_t err = ERR_get_error();
  EXPECT_EQ(ERR_LIB_EVP, ERR_GET_LIB(err));
  EXPECT_EQ(EVP_R_NOT_A_PRIVATE_KEY, ERR_GET_REASON(err));

  //EVP_PKEY_cmp just compares the public keys so this should return 1
  EXPECT_EQ(1, EVP_PKEY_cmp(kyber_pkey, new_public));

  //New raw private key
  EVP_PKEY *new_private = EVP_PKEY_new_raw_private_key(EVP_PKEY_KYBER512,
                                                       NULL,
                                                       kyber512Key->priv,
                                                       KYBER512_SECRETKEY_BYTES);
  ASSERT_NE(new_private, nullptr);
  const KYBER_512_KEY *newKyber512Key = (KYBER_512_KEY *)(new_private->pkey.ptr);
  EXPECT_EQ(0, OPENSSL_memcmp(kyber512Key->priv, newKyber512Key->priv, KYBER512_SECRETKEY_BYTES));

  EVP_PKEY_CTX_free(kyber_pkey_ctx);
  EVP_PKEY_free(kyber_pkey);
  EVP_PKEY_free(new_public);
  EVP_PKEY_free(new_private);
}

TEST(Kyber512Test, EVP_PKEY_size) {
  EVP_PKEY_CTX *kyber_pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_KYBER512, nullptr);
  ASSERT_NE(kyber_pkey_ctx, nullptr);

  EVP_PKEY *kyber_pkey = EVP_PKEY_new();
  ASSERT_NE(kyber_pkey, nullptr);

  EXPECT_TRUE(EVP_PKEY_keygen_init(kyber_pkey_ctx));
  EXPECT_TRUE(EVP_PKEY_keygen(kyber_pkey_ctx, &kyber_pkey));

  EXPECT_EQ(KYBER512_PUBLICKEY_BYTES + KYBER512_SECRETKEY_BYTES, EVP_PKEY_size(kyber_pkey));
  EXPECT_EQ(8*(KYBER512_PUBLICKEY_BYTES + KYBER512_SECRETKEY_BYTES), EVP_PKEY_bits(kyber_pkey));

  EVP_PKEY_free(kyber_pkey);
  EVP_PKEY_CTX_free(kyber_pkey_ctx);
}
