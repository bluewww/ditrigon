/* HexChat
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
*/

/*
 * For Diffie-Hellman key-exchange a 1080bit germain prime is used, the
 * generator g=2 renders a field Fp from 1 to p-1. Therefore breaking it
 * means to solve a discrete logarithm problem with no less than 1080bit.
 *
 * Base64 format is used to send the public keys over IRC.
 *
 * The calculated secret key is hashed with SHA-256, the result is converted
 * to base64 for final use with blowfish.
 */

#include "config.h"
#include "dh1080.h"

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/core_names.h>
#include <openssl/sha.h>

#include <string.h>
#include <glib.h>

#define DH1080_PRIME_BITS 1080
#define DH1080_PRIME_BYTES 135
#define SHA256_DIGEST_LENGTH 32
#define B64ABC "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
#define MEMZERO(x) memset(x, 0x00, sizeof(x))

/* All clients must use the same prime number to be able to keyx */
static const guchar prime1080[DH1080_PRIME_BYTES] =
{
	0xFB, 0xE1, 0x02, 0x2E, 0x23, 0xD2, 0x13, 0xE8, 0xAC, 0xFA, 0x9A, 0xE8, 0xB9, 0xDF, 0xAD, 0xA3, 0xEA,
	0x6B, 0x7A, 0xC7, 0xA7, 0xB7, 0xE9, 0x5A, 0xB5, 0xEB, 0x2D, 0xF8, 0x58, 0x92, 0x1F, 0xEA, 0xDE, 0x95,
	0xE6, 0xAC, 0x7B, 0xE7, 0xDE, 0x6A, 0xDB, 0xAB, 0x8A, 0x78, 0x3E, 0x7A, 0xF7, 0xA7, 0xFA, 0x6A, 0x2B,
	0x7B, 0xEB, 0x1E, 0x72, 0xEA, 0xE2, 0xB7, 0x2F, 0x9F, 0xA2, 0xBF, 0xB2, 0xA2, 0xEF, 0xBE, 0xFA, 0xC8,
	0x68, 0xBA, 0xDB, 0x3E, 0x82, 0x8F, 0xA8, 0xBA, 0xDF, 0xAD, 0xA3, 0xE4, 0xCC, 0x1B, 0xE7, 0xE8, 0xAF,
	0xE8, 0x5E, 0x96, 0x98, 0xA7, 0x83, 0xEB, 0x68, 0xFA, 0x07, 0xA7, 0x7A, 0xB6, 0xAD, 0x7B, 0xEB, 0x61,
	0x8A, 0xCF, 0x9C, 0xA2, 0x89, 0x7E, 0xB2, 0x8A, 0x61, 0x89, 0xEF, 0xA0, 0x7A, 0xB9, 0x9A, 0x8A, 0x7F,
	0xA9, 0xAE, 0x29, 0x9E, 0xFA, 0x7B, 0xA6, 0x6D, 0xEA, 0xFE, 0xFB, 0xEF, 0xBF, 0x0B, 0x7D, 0x8B
};

static EVP_PKEY *g_dh_params;

int
dh1080_init (void)
{
	OSSL_PARAM_BLD *bld = NULL;
	OSSL_PARAM *params = NULL;
	EVP_PKEY_CTX *pctx = NULL;
	BIGNUM *p = NULL, *g = NULL;
	int ret = 0;

	g_return_val_if_fail (g_dh_params == NULL, 0);

	p = BN_bin2bn (prime1080, DH1080_PRIME_BYTES, NULL);
	g = BN_new ();

	if (p == NULL || g == NULL)
		goto out;

	BN_set_word (g, 2);

	bld = OSSL_PARAM_BLD_new ();
	if (bld == NULL)
		goto out;

	if (!OSSL_PARAM_BLD_push_BN (bld, OSSL_PKEY_PARAM_FFC_P, p)
	    || !OSSL_PARAM_BLD_push_BN (bld, OSSL_PKEY_PARAM_FFC_G, g))
		goto out;

	params = OSSL_PARAM_BLD_to_param (bld);
	if (params == NULL)
		goto out;

	pctx = EVP_PKEY_CTX_new_from_name (NULL, "DH", NULL);
	if (pctx == NULL)
		goto out;

	if (EVP_PKEY_fromdata_init (pctx) <= 0
	    || EVP_PKEY_fromdata (pctx, &g_dh_params, EVP_PKEY_KEY_PARAMETERS, params) <= 0)
		goto out;

	ret = 1;

out:
	EVP_PKEY_CTX_free (pctx);
	OSSL_PARAM_free (params);
	OSSL_PARAM_BLD_free (bld);
	BN_free (p);
	BN_free (g);
	return ret;
}

void
dh1080_deinit (void)
{
	g_clear_pointer (&g_dh_params, EVP_PKEY_free);
}

static inline int
dh1080_verify_pub_key (const BIGNUM *pub_key)
{
	BIGNUM *p = NULL;
	BIGNUM *p_minus_1 = NULL;
	int ret = 0;

	if (!EVP_PKEY_get_bn_param (g_dh_params, OSSL_PKEY_PARAM_FFC_P, &p))
		return 0;

	p_minus_1 = BN_dup (p);
	if (p_minus_1 == NULL)
		goto out;

	BN_sub_word (p_minus_1, 1);

	/* Valid if: 1 < pub_key < p-1 */
	if (BN_cmp (pub_key, BN_value_one ()) > 0
	    && BN_cmp (pub_key, p_minus_1) < 0)
		ret = 1;

out:
	BN_free (p_minus_1);
	BN_free (p);
	return ret;
}

static guchar *
dh1080_decode_b64 (const char *data, gsize *out_len)
{
	GString *str = g_string_new (data);
	guchar *ret;

	if (str->len % 4 == 1 && str->str[str->len - 1] == 'A')
		g_string_truncate (str, str->len - 1);

	while (str->len % 4 != 0)
		g_string_append_c (str, '=');

	ret = (guchar *) g_string_free_and_steal (str);
	g_base64_decode_inplace ((char *) ret, out_len);
	return ret;
}

static char *
dh1080_encode_b64 (const guchar *data, gsize data_len)
{
	char *ret = g_base64_encode (data, data_len);
	char *p;

	if (!(p = strchr (ret, '=')))
	{
		char *new_ret = g_new(char, strlen(ret) + 2);
		strcpy (new_ret, ret);
		strcat (new_ret, "A");
		g_free (ret);
		ret = new_ret;
	}
	else
	{
		*p = '\0';
	}

  	return ret;
}

int
dh1080_generate_key (char **priv_key, char **pub_key)
{
	EVP_PKEY_CTX *kctx = NULL;
	EVP_PKEY *key = NULL;
	BIGNUM *bn_pub = NULL, *bn_priv = NULL;
	guchar buf[DH1080_PRIME_BYTES];
	int len;
	int ret = 0;

	g_assert (priv_key != NULL);
	g_assert (pub_key != NULL);

	kctx = EVP_PKEY_CTX_new_from_pkey (NULL, g_dh_params, NULL);
	if (kctx == NULL)
		goto out;

	if (EVP_PKEY_keygen_init (kctx) <= 0)
		goto out;

	if (EVP_PKEY_keygen (kctx, &key) <= 0)
		goto out;

	if (!EVP_PKEY_get_bn_param (key, OSSL_PKEY_PARAM_PUB_KEY, &bn_pub)
	    || !EVP_PKEY_get_bn_param (key, OSSL_PKEY_PARAM_PRIV_KEY, &bn_priv))
		goto out;

	MEMZERO (buf);
	len = BN_bn2bin (bn_priv, buf);
	*priv_key = dh1080_encode_b64 (buf, len);

	MEMZERO (buf);
	len = BN_bn2bin (bn_pub, buf);
	*pub_key = dh1080_encode_b64 (buf, len);

	ret = 1;

out:
	OPENSSL_cleanse (buf, sizeof (buf));
	BN_clear_free (bn_priv);
	BN_free (bn_pub);
	EVP_PKEY_free (key);
	EVP_PKEY_CTX_free (kctx);
	return ret;
}

int
dh1080_compute_key (const char *priv_key, const char *pub_key, char **secret_key)
{
	guchar *pub_key_data = NULL;
	gsize pub_key_len = 0;
	guchar *priv_key_data = NULL;
	gsize priv_key_len = 0;
	BIGNUM *bn_peer_pub = NULL;
	BIGNUM *bn_priv = NULL;
	BIGNUM *bn_our_pub = NULL;
	BIGNUM *p = NULL, *g = NULL;
	EVP_PKEY *our_key = NULL;
	EVP_PKEY *peer_key = NULL;
	EVP_PKEY_CTX *derive_ctx = NULL;
	OSSL_PARAM_BLD *bld = NULL;
	OSSL_PARAM *params = NULL;
	int ret = 0;

	g_assert (secret_key != NULL);

	/* Verify base64 strings */
	if (strspn (priv_key, B64ABC) != strlen (priv_key)
	    || strspn (pub_key, B64ABC) != strlen (pub_key))
		return 0;

	/* Decode peer public key */
	pub_key_data = dh1080_decode_b64 (pub_key, &pub_key_len);
	bn_peer_pub = BN_bin2bn (pub_key_data, pub_key_len, NULL);

	if (!dh1080_verify_pub_key (bn_peer_pub))
		goto out;

	/* Decode our private key */
	priv_key_data = dh1080_decode_b64 (priv_key, &priv_key_len);
	bn_priv = BN_bin2bn (priv_key_data, priv_key_len, NULL);

	/* Extract p and g from params */
	if (!EVP_PKEY_get_bn_param (g_dh_params, OSSL_PKEY_PARAM_FFC_P, &p)
	    || !EVP_PKEY_get_bn_param (g_dh_params, OSSL_PKEY_PARAM_FFC_G, &g))
		goto out;

	/* Recompute our public key: pub = g^priv mod p
	 * EVP_PKEY_fromdata with KEYPAIR requires both pub and priv */
	{
		BN_CTX *bn_ctx = BN_CTX_new ();
		bn_our_pub = BN_new ();
		if (bn_ctx == NULL || bn_our_pub == NULL
		    || !BN_mod_exp (bn_our_pub, g, bn_priv, p, bn_ctx))
		{
			BN_CTX_free (bn_ctx);
			goto out;
		}
		BN_CTX_free (bn_ctx);
	}

	/* Build our full keypair as EVP_PKEY */
	bld = OSSL_PARAM_BLD_new ();
	if (bld == NULL
	    || !OSSL_PARAM_BLD_push_BN (bld, OSSL_PKEY_PARAM_FFC_P, p)
	    || !OSSL_PARAM_BLD_push_BN (bld, OSSL_PKEY_PARAM_FFC_G, g)
	    || !OSSL_PARAM_BLD_push_BN (bld, OSSL_PKEY_PARAM_PUB_KEY, bn_our_pub)
	    || !OSSL_PARAM_BLD_push_BN (bld, OSSL_PKEY_PARAM_PRIV_KEY, bn_priv))
		goto out;

	params = OSSL_PARAM_BLD_to_param (bld);
	if (params == NULL)
		goto out;

	{
		EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_from_name (NULL, "DH", NULL);
		if (pctx == NULL
		    || EVP_PKEY_fromdata_init (pctx) <= 0
		    || EVP_PKEY_fromdata (pctx, &our_key, EVP_PKEY_KEYPAIR, params) <= 0)
		{
			EVP_PKEY_CTX_free (pctx);
			goto out;
		}
		EVP_PKEY_CTX_free (pctx);
	}

	OSSL_PARAM_BLD_free (bld);
	OSSL_PARAM_free (params);
	bld = NULL;
	params = NULL;

	/* Build peer's key (params + pub key only) */
	bld = OSSL_PARAM_BLD_new ();
	if (bld == NULL
	    || !OSSL_PARAM_BLD_push_BN (bld, OSSL_PKEY_PARAM_FFC_P, p)
	    || !OSSL_PARAM_BLD_push_BN (bld, OSSL_PKEY_PARAM_FFC_G, g)
	    || !OSSL_PARAM_BLD_push_BN (bld, OSSL_PKEY_PARAM_PUB_KEY, bn_peer_pub))
		goto out;

	params = OSSL_PARAM_BLD_to_param (bld);
	if (params == NULL)
		goto out;

	{
		EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_from_name (NULL, "DH", NULL);
		if (pctx == NULL
		    || EVP_PKEY_fromdata_init (pctx) <= 0
		    || EVP_PKEY_fromdata (pctx, &peer_key, EVP_PKEY_PUBLIC_KEY, params) <= 0)
		{
			EVP_PKEY_CTX_free (pctx);
			goto out;
		}
		EVP_PKEY_CTX_free (pctx);
	}

	/* Derive shared secret */
	derive_ctx = EVP_PKEY_CTX_new_from_pkey (NULL, our_key, NULL);
	if (derive_ctx == NULL
	    || EVP_PKEY_derive_init (derive_ctx) <= 0)
		goto out;

	/* Disable padding to match old DH_compute_key behavior
	 * (strip leading zero bytes from the shared secret) */
	{
		unsigned int pad = 0;
		OSSL_PARAM derive_params[2];
		derive_params[0] = OSSL_PARAM_construct_uint (OSSL_EXCHANGE_PARAM_PAD, &pad);
		derive_params[1] = OSSL_PARAM_construct_end ();
		if (EVP_PKEY_CTX_set_params (derive_ctx, derive_params) <= 0)
			goto out;
	}

	if (EVP_PKEY_derive_set_peer (derive_ctx, peer_key) <= 0)
		goto out;

	{
		guchar shared_key[DH1080_PRIME_BYTES] = { 0 };
		guchar sha256[SHA256_DIGEST_LENGTH] = { 0 };
		size_t shared_len = sizeof (shared_key);

		if (EVP_PKEY_derive (derive_ctx, shared_key, &shared_len) <= 0)
		{
			OPENSSL_cleanse (shared_key, sizeof (shared_key));
			goto out;
		}

		SHA256 (shared_key, shared_len, sha256);
		*secret_key = dh1080_encode_b64 (sha256, sizeof (sha256));

		OPENSSL_cleanse (shared_key, sizeof (shared_key));
		OPENSSL_cleanse (sha256, sizeof (sha256));
	}

	ret = 1;

out:
	EVP_PKEY_CTX_free (derive_ctx);
	EVP_PKEY_free (peer_key);
	EVP_PKEY_free (our_key);
	OSSL_PARAM_free (params);
	OSSL_PARAM_BLD_free (bld);
	BN_free (bn_our_pub);
	BN_clear_free (bn_priv);
	BN_free (bn_peer_pub);
	BN_free (p);
	BN_free (g);
	if (priv_key_data)
		OPENSSL_cleanse (priv_key_data, priv_key_len);
	g_free (priv_key_data);
	g_free (pub_key_data);
	return ret;
}
