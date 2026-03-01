#include "config.h"

#ifdef USE_OPENSSL

#include <glib.h>
#include <openssl/hmac.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <string.h>

#include "scram.h"
#include "ssl.h"

void
safe_strcpy (char *dest, const char *src, int bytes_left)
{
	if (bytes_left <= 0)
	{
		return;
	}

	g_strlcpy (dest, src, (gsize)bytes_left);
}

static X509 *
build_cert_with_cn_and_san (const char *common_name, const char *san_value)
{
	X509 *cert;
	X509_NAME *subject;
	X509V3_CTX ctx;
	X509_EXTENSION *san_ext;

	cert = X509_new ();
	if (cert == NULL)
	{
		return NULL;
	}

	X509_set_version (cert, 2);
	ASN1_INTEGER_set (X509_get_serialNumber (cert), 1);

	subject = X509_get_subject_name (cert);
	if (subject == NULL)
	{
		X509_free (cert);
		return NULL;
	}

	if (X509_NAME_add_entry_by_txt (subject, "CN", MBSTRING_ASC,
										 (const unsigned char *)common_name, -1, -1, 0) != 1)
	{
		X509_free (cert);
		return NULL;
	}

	if (X509_set_subject_name (cert, subject) != 1 ||
		 X509_set_issuer_name (cert, subject) != 1)
	{
		X509_free (cert);
		return NULL;
	}

	X509V3_set_ctx_nodb (&ctx);
	X509V3_set_ctx (&ctx, cert, cert, NULL, NULL, 0);

	san_ext = X509V3_EXT_conf_nid (NULL, &ctx, NID_subject_alt_name,
										 (char *)san_value);
	if (san_ext == NULL)
	{
		X509_free (cert);
		return NULL;
	}

	X509_add_ext (cert, san_ext, -1);
	X509_EXTENSION_free (san_ext);

	return cert;
}

static void
test_ssl_san_cn_fallback_rejects_mismatch (void)
{
	X509 *cert = build_cert_with_cn_and_san ("victim.example", "DNS:attacker.example");
	int rv;

	g_assert_nonnull (cert);

	rv = _SSL_check_hostname (cert, "victim.example");
	g_assert_cmpint (rv, !=, 0);

	X509_free (cert);
}

static scram_session *
start_scram_session (char **client_first)
{
	scram_session *session;
	scram_status status;
	size_t output_len = 0;

	*client_first = NULL;
	session = scram_session_create ("sha256", "user", "pass");
	g_assert_nonnull (session);

	status = scram_process (session, "", client_first, &output_len);
	g_assert_cmpint (status, ==, SCRAM_IN_PROGRESS);
	g_assert_nonnull (*client_first);
	g_assert_cmpuint (output_len, >, 0);

	return session;
}

static void
test_scram_iteration_suffix_is_rejected (void)
{
	scram_session *session;
	char *client_first;
	char *server_first;
	char *nonce_pos;
	char *output = NULL;
	size_t output_len = 0;
	scram_status status;

	session = start_scram_session (&client_first);

	nonce_pos = strstr (client_first, ",r=");
	g_assert_nonnull (nonce_pos);
	nonce_pos += 3;

	server_first = g_strdup_printf ("r=%sserver,s=c2FsdA==,i=1x", nonce_pos);
	status = scram_process (session, server_first, &output, &output_len);

	g_assert_cmpint (status, ==, SCRAM_ERROR);
	g_assert_nonnull (session->error);
	g_assert_null (output);

	g_free (client_first);
	g_free (server_first);
	g_free (output);
	scram_session_free (session);
}

static void
test_scram_server_final_rejects_invalid_prefix (void)
{
	scram_session *session;
	char *client_first;
	char *server_first;
	char *nonce_pos;
	char *output = NULL;
	size_t output_len = 0;
	scram_status status;
	unsigned char server_key[EVP_MAX_MD_SIZE];
	unsigned char server_signature[EVP_MAX_MD_SIZE];
	unsigned int server_key_len = 0;
	unsigned int server_signature_len = 0;
	char *server_signature_b64;
	char *server_final;

	session = start_scram_session (&client_first);

	nonce_pos = strstr (client_first, ",r=");
	g_assert_nonnull (nonce_pos);
	nonce_pos += 3;

	server_first = g_strdup_printf ("r=%sserver,s=c2FsdA==,i=1", nonce_pos);
	status = scram_process (session, server_first, &output, &output_len);
	g_assert_cmpint (status, ==, SCRAM_IN_PROGRESS);
	g_assert_nonnull (output);

	HMAC (session->digest, session->salted_password, session->digest_size,
			(const unsigned char *)"Server Key", strlen ("Server Key"),
			server_key, &server_key_len);
	HMAC (session->digest, server_key, session->digest_size,
			(const unsigned char *)session->auth_message, strlen (session->auth_message),
			server_signature, &server_signature_len);

	server_signature_b64 = g_base64_encode (server_signature, server_signature_len);
	server_final = g_strdup_printf ("x=%s", server_signature_b64);

	g_free (output);
	output = NULL;
	output_len = 0;

	status = scram_process (session, server_final, &output, &output_len);
	g_assert_cmpint (status, ==, SCRAM_ERROR);
	g_assert_null (output);

	g_free (client_first);
	g_free (server_first);
	g_free (server_signature_b64);
	g_free (server_final);
	g_free (output);
	scram_session_free (session);
}

static void
test_scram_server_first_rejects_invalid_salt_base64 (void)
{
	scram_session *session;
	char *client_first;
	char *server_first;
	char *nonce_pos;
	char *output = NULL;
	size_t output_len = 0;
	scram_status status;

	session = start_scram_session (&client_first);

	nonce_pos = strstr (client_first, ",r=");
	g_assert_nonnull (nonce_pos);
	nonce_pos += 3;

	server_first = g_strdup_printf ("r=%sserver,s=%%%%,i=1", nonce_pos);
	status = scram_process (session, server_first, &output, &output_len);

	g_assert_cmpint (status, ==, SCRAM_ERROR);
	g_assert_nonnull (session->error);
	g_assert_null (output);

	g_free (client_first);
	g_free (server_first);
	g_free (output);
	scram_session_free (session);
}

static void
test_scram_server_final_accepts_trailing_junk (void)
{
	scram_session *session;
	char *client_first;
	char *server_first;
	char *nonce_pos;
	char *output = NULL;
	size_t output_len = 0;
	scram_status status;
	unsigned char server_key[EVP_MAX_MD_SIZE];
	unsigned char server_signature[EVP_MAX_MD_SIZE];
	unsigned int server_key_len = 0;
	unsigned int server_signature_len = 0;
	char *server_signature_b64;
	char *server_final;

	session = start_scram_session (&client_first);

	nonce_pos = strstr (client_first, ",r=");
	g_assert_nonnull (nonce_pos);
	nonce_pos += 3;

	server_first = g_strdup_printf ("r=%sserver,s=c2FsdA==,i=1", nonce_pos);
	status = scram_process (session, server_first, &output, &output_len);
	g_assert_cmpint (status, ==, SCRAM_IN_PROGRESS);
	g_assert_nonnull (output);

	HMAC (session->digest, session->salted_password, session->digest_size,
			(const unsigned char *)"Server Key", strlen ("Server Key"),
			server_key, &server_key_len);
	HMAC (session->digest, server_key, session->digest_size,
			(const unsigned char *)session->auth_message, strlen (session->auth_message),
			server_signature, &server_signature_len);

	server_signature_b64 = g_base64_encode (server_signature, server_signature_len);
	server_final = g_strdup_printf ("v=%s!!!!", server_signature_b64);

	g_free (output);
	output = NULL;
	output_len = 0;

	status = scram_process (session, server_final, &output, &output_len);
	g_assert_cmpint (status, ==, SCRAM_SUCCESS);

	g_free (client_first);
	g_free (server_first);
	g_free (server_signature_b64);
	g_free (server_final);
	g_free (output);
	scram_session_free (session);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/security/ssl/san-cn-fallback-rejects-mismatch",
						  test_ssl_san_cn_fallback_rejects_mismatch);
	g_test_add_func ("/security/scram/iteration-suffix-is-rejected",
						  test_scram_iteration_suffix_is_rejected);
	g_test_add_func ("/security/scram/server-final-rejects-invalid-prefix",
						  test_scram_server_final_rejects_invalid_prefix);
	g_test_add_func ("/security/scram/server-first-rejects-invalid-salt-base64",
						  test_scram_server_first_rejects_invalid_salt_base64);
	g_test_add_func ("/security/scram/server-final-accepts-trailing-junk",
						  test_scram_server_final_accepts_trailing_junk);

	return g_test_run ();
}

#else

int
main (void)
{
	return 0;
}

#endif
