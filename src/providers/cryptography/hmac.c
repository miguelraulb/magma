/**
 * @file    /magma/providers/cryptography/hmac.c
 *
 * @brief   Functions used to create hmac's using specified digest functions.
 *
 * @Author  Ivan
 * $Date$
 * $Revision$
 * 7/2/2015: Zach refactored Kent's code to remove the recursive call in the
 *          case of the 'output' param being null with 2 static calls to wrap
 *          the 'output' param states. I liked the recursive call because that
 *          approach works well if you have n params that are optionally null.
 *          Regardless, until the day that there are more than one param that c
 *          an be optionally null, this approach that removes the recursion is
 *          probably easier for most to follow.  Kent.
 * 7/1/2015: Refactored hmac_digest to have a single routine that does
 *          most of the work: hmac_multi_digest().  If called with a null
 *          output param, the output param is allocated and a recursive call
 *          back to hmac_multi_digest() is made.  The simpler routine:
 *          hmac_digest() is replaced with a call to hmac_multi_digest() with
 *          a rounds count of 1. Kent.
 */

#include "magma.h"

static
size_t
buffer_size_get(stringer_t * buffer) {
	uint32_t opts;

	opts = *((uint32_t *)buffer);
	if (st_valid_avail(opts)) {
		return st_avail_get(buffer);
	} else {
		return st_length_get(buffer);
	}
}

static
stringer_t *
hmac_multi_digest_nonnull_output (
	uint_t rounds,
	digest_t *digest,
	stringer_t *s,
	stringer_t *key,
	stringer_t *output)
{
	HMAC_CTX ctx;
	uint32_t opts;
	size_t buffer_size;
	int_t digest_output_size;
	uint_t hmac_output_size;

	if (rounds < 1) {
		log_pedantic("rounds must be > 0");
		goto error;
	}

	if (digest == NULL) {
		log_pedantic("Digest algorithm is NULL");
		goto error;
	}

	digest_output_size = EVP_MD_size_d((const EVP_MD *)digest);
	if (digest_output_size < 1) {
		log_pedantic("EVP_MD_size_d() returned invalid size for digest algorithm");
		goto error;
	}

	if (st_empty(s)) {
		log_pedantic("Input stringer 's' is NULL or empty");
		goto error;
	}

	if (st_empty(key)) {
		log_pedantic("Key stringer 'key' is NULL or empty");
		goto error;
	}

	if (output == NULL) {
		log_pedantic("Input stringer 'output' is NULL");
		goto error;
	}

	opts = *((uint32_t *)output);
	if (!st_valid_destination(opts)) {
		// if it's not valid, you can't write the stringer?
		log_error("Cannot write to stringer");
		goto error;
	}

	buffer_size = buffer_size_get(output);
	if (buffer_size < digest_output_size) {
		log_error(
			"'output' stringer is too small. {avail = %zu / required = %i}",
			buffer_size,
			digest_output_size
		);
		goto error;
	}

	HMAC_CTX_init_d(&ctx);

	if (HMAC_Init_ex_d(
			&ctx,
			st_data_get(key),
			st_length_get(key),
			(const EVP_MD *)digest,
			NULL) != 1) {
		log_error("Failed HMAC_Init_ex_d(). {%s}", ERR_error_string_d(ERR_get_error_d(), NULL));
		goto cleanup_ctx;
	}

	for (uint_t i = 0; i < rounds; i++) {
		if (HMAC_Update_d(
				&ctx,
				st_data_get(s),
				st_length_get(s)) != 1) {
			log_error("Failed HMAC_Update_d(). {%s}", ERR_error_string_d(ERR_get_error_d(), NULL));
			goto cleanup_ctx;
		}
	}

	if (HMAC_Final_d(
			&ctx,
			st_data_get(output),
			&hmac_output_size) != 1) {
		log_error("Failed HMAC_Final_d(). {%s}", ERR_error_string_d(ERR_get_error_d(), NULL));
		goto cleanup_ctx;
	}

	if (digest_output_size != hmac_output_size) {
		log_error("HMAC output size does not match digest output size");
		goto cleanup_ctx;
	}

	// Set the stringer length if applicable
	if (st_valid_tracked(opts)) {
		st_length_set(output, digest_output_size);
	}

	HMAC_CTX_cleanup_d(&ctx);
	return output;

cleanup_ctx:
	HMAC_CTX_cleanup_d(&ctx);
error:
	return NULL;
}

static
stringer_t *
hmac_multi_digest_null_output(
	uint_t rounds,
	digest_t *digest,
	stringer_t *s,
	stringer_t *key)
{
	int_t digest_output_size;
	stringer_t * output;
	stringer_t * alloced_output;

	digest_output_size = EVP_MD_size_d((const EVP_MD *)digest);
	alloced_output = st_alloc(digest_output_size);
	if (alloced_output == NULL) {
		log_error("st_alloc() failed for 'output'. {requested = %i}", digest_output_size);
		goto error;
	}

	output = hmac_multi_digest_nonnull_output(
		rounds,
		digest,
		s,
		key,
		alloced_output
	);

	if (output == NULL) {
		goto cleanup_alloced_output;
	}

	return output;

cleanup_alloced_output:
	st_free(alloced_output);
error:
	return NULL;
}

/**
 * @brief   hmac_multi_digest: perform an HMAC on a multi-concatenated input
 *          using the specified digest and key.
 * @param   rounds  The amount of times that input s should be self-concatenated to
 *                  serve as the input of the digest.
 * @param   digest  Digest to be used with the HMAC.
 * @param   s       Input data.
 * @param   key     Key used in HMAC.
 * @param   output  Stringer containing buffer for output.
 * @return  Pointer to stringer with buffer containing HMAC. NULL on failure.
 */
stringer_t *
hmac_multi_digest(
	uint_t rounds,
	digest_t *digest,
	stringer_t *s,
	stringer_t *key,
	stringer_t *output)
{
	if (output == NULL) {
		output = hmac_multi_digest_null_output(
			rounds,
			digest,
			s,
			key
		);
	} else {
		output = hmac_multi_digest_nonnull_output(
			rounds,
			digest,
			s,
			key,
			output
		);
	}

	return output;
}

/**
 * @brief  hmac_digest: perform an HMAC using the specified digest and key.
 * @param  digest   Digest to be used with the HMAC.
 * @param  s        Input data.
 * @param  key      Key used in HMAC.
 * @param  output   Stringer containing buffer for output.
 * @return Pointer to stringer with buffer containing HMAC. NULL on failure.
 */
stringer_t *
hmac_digest (digest_t *digest, stringer_t *s, stringer_t *key, stringer_t *output) {
	return hmac_multi_digest(1, digest, s, key, output);
}

/**
 * @brief   Helper functions
 * @param   s       Input data.
 * @param   key     HMAC key.
 * @param   output  Stringer with HMAC
 * @return  Pointer to stringer with HMAC buffer. NULL on failure.
 */
stringer_t * hmac_md4(stringer_t *s, stringer_t *key, stringer_t *output) {
	return hmac_digest((digest_t *)EVP_md4_d(), s, key, output);
}

stringer_t * hmac_md5(stringer_t *s, stringer_t *key, stringer_t *output) {
	return hmac_digest((digest_t *)EVP_md5_d(), s, key, output);
}

stringer_t * hmac_sha(stringer_t *s, stringer_t *key, stringer_t *output) {
	return hmac_digest((digest_t *)EVP_sha_d(), s, key, output);
}

stringer_t * hmac_sha1(stringer_t *s, stringer_t *key, stringer_t *output) {
	return hmac_digest((digest_t *)EVP_sha1_d(), s, key, output);
}

stringer_t * hmac_sha224(stringer_t *s, stringer_t *key, stringer_t *output) {
	return hmac_digest((digest_t *)EVP_sha224_d(), s, key, output);
}

stringer_t * hmac_sha256(stringer_t *s, stringer_t *key, stringer_t *output) {
	return hmac_digest((digest_t *)EVP_sha256_d(), s, key, output);
}

stringer_t * hmac_sha384(stringer_t *s, stringer_t *key, stringer_t *output) {
	return hmac_digest((digest_t *)EVP_sha384_d(), s, key, output);
}

stringer_t * hmac_sha512(stringer_t *s, stringer_t *key, stringer_t *output) {
	return hmac_digest((digest_t *)EVP_sha512_d(), s, key, output);
}

stringer_t * hmac_ripemd160(stringer_t *s, stringer_t *key, stringer_t *output) {
	return hmac_digest((digest_t *)EVP_ripemd160_d(), s, key, output);
}

/**
 * @brief   Multi-concatenated HMAC-SHA512
 * @param   rounds  Number of times input is self-concantenated.
 * @param   s       Input.
 * @param   key     HMAC key.
 * @param   output  Stringer with HMAC.
 * @return  Pointer to stringer with buffer containing HMAC, NULL on failure.
*/
stringer_t * hmac_multi_sha512(uint_t rounds, stringer_t *s, stringer_t *key, stringer_t *output) {
	return hmac_multi_digest(rounds, (digest_t *)EVP_sha512_d(), s, key, output);
}


