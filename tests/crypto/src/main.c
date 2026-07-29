/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Tests for lib/ss_crypto.c -- the PSA glue that holds KI, KIC and KID and does
 * every AES and CMAC operation the card performs.
 *
 * On target the keys live in the nRF91 KMU behind TF-M; here the same file runs
 * against Mbed TLS's PSA implementation, because the "KMU slots" are ordinary
 * persistent PSA key ids and the file touches no nrf-specific API. The keys are
 * therefore real keys, the ciphertext is real ciphertext, and the vectors below
 * were computed independently (Python cryptography, AES-128) rather than by
 * recording what this code produces.
 *
 * Several cases document defects instead of passing: the error paths in here
 * swallow PSA failures and return success, which on a device means a card that
 * looks provisioned and cannot authenticate.
 */

#include <stdint.h>
#include <string.h>

#include <psa/crypto.h>
#include <zephyr/ztest.h>

#include "ss_crypto.h"
#include <onomondo/utils/ss_profile.h>

/* KI = KIC = KID = 000102..0f, as the GSMA TS.48 test profile carries them. */
static const uint8_t test_key[KMU_KEY_SIZE] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
					       0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

/* Two AES blocks of plaintext and its AES-128-CBC encryption under the key
 * above with the all-zero IV the telecom specs mandate. */
static const uint8_t plaintext[32] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
				      0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
				      0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
				      0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f};
static const uint8_t ciphertext[32] = {0x07, 0xfe, 0xef, 0x74, 0xe1, 0xd5, 0x03, 0x6e,
				       0x90, 0x0e, 0xee, 0x11, 0x8e, 0x94, 0x92, 0x93,
				       0x13, 0x8b, 0xb0, 0xfe, 0x94, 0x25, 0x20, 0xa0,
				       0x01, 0x3f, 0x42, 0x53, 0x1d, 0xd5, 0x02, 0x2c};

/* The OTA checksum runs CMAC over data1 followed by data2. */
static const uint8_t cmac_data1[16] = {0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
				       0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf};
static const uint8_t cmac_data2[16] = {0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
				       0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf};
static const uint8_t cmac_golden[16] = {0xcd, 0xb6, 0x8c, 0xa8, 0x77, 0x2c, 0x22, 0x12,
					0x47, 0xd6, 0xec, 0xa1, 0x96, 0xe2, 0x9d, 0x03};

/* The cipher helpers take the key by its profile tag, not by slot id: the first
 * byte of the "key" they are handed is the tag stored in EF.A001/EF.A004, which
 * key_id_to_kmu_slot() maps to a slot. */
static const uint8_t kic_ref[1] = {KIC_TAG};
static const uint8_t kid_ref[1] = {KID_TAG};

static void *suite_setup(void)
{
	zassume_equal(psa_crypto_init(), PSA_SUCCESS, "PSA crypto did not initialise");

	return NULL;
}

static void import_all_keys(void)
{
	uint8_t key[KMU_KEY_SIZE];

	/* The signature takes a non-const key. */
	memcpy(key, test_key, sizeof(key));
	zassert_ok(ss_utils_setup_key(sizeof(key), key, KEY_ID_KI), "KI import failed");
	zassert_ok(ss_utils_setup_key(sizeof(key), key, KEY_ID_KIC), "KIC import failed");
	zassert_ok(ss_utils_setup_key(sizeof(key), key, KEY_ID_KID), "KID import failed");
}

static void test_before(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Every slot starts empty, so a test can tell "imported" from "left over
	 * from the previous test". */
	psa_destroy_key((psa_key_id_t)KEY_ID_KI);
	psa_destroy_key((psa_key_id_t)KEY_ID_KIC);
	psa_destroy_key((psa_key_id_t)KEY_ID_KID);
}

ZTEST_SUITE(softsim_crypto, NULL, suite_setup, test_before, NULL, NULL);

/* --- key lifecycle --------------------------------------------------------- */

ZTEST(softsim_crypto, test_key_existence_follows_the_import)
{
	uint8_t key[KMU_KEY_SIZE];

	memcpy(key, test_key, sizeof(key));

	zassert_equal(ss_utils_check_key_existence(KEY_ID_KI), 0, "KI existed before import");
	zassert_ok(ss_utils_setup_key(sizeof(key), key, KEY_ID_KI));
	zassert_equal(ss_utils_check_key_existence(KEY_ID_KI), 1, "KI was not found after import");

	/* Provisioning writes one key at a time, so the others must stay absent. */
	zassert_equal(ss_utils_check_key_existence(KEY_ID_KIC), 0, "KIC appeared on its own");
	zassert_equal(ss_utils_check_key_existence(KEY_ID_KID), 0, "KID appeared on its own");
}

/*
 * Re-provisioning a device imports over keys that already exist, which the
 * helper handles by destroying the old key first. PSA rejects an import onto an
 * occupied id, so without that destroy the second provisioning would silently
 * leave the old key in place.
 */
ZTEST(softsim_crypto, test_importing_over_an_existing_key_replaces_it)
{
	uint8_t key[KMU_KEY_SIZE];
	uint8_t buf[sizeof(plaintext)];

	memcpy(key, test_key, sizeof(key));
	zassert_ok(ss_utils_setup_key(sizeof(key), key, KEY_ID_KIC));

	/* A different key the second time round. */
	for (size_t i = 0; i < sizeof(key); i++) {
		key[i] = (uint8_t)(0xf0 + i);
	}
	zassert_ok(ss_utils_setup_key(sizeof(key), key, KEY_ID_KIC), "re-import failed");
	zassert_equal(ss_utils_check_key_existence(KEY_ID_KIC), 1);

	/* The stored key must be the new one, so the old key's ciphertext is no
	 * longer what comes out. */
	memcpy(buf, plaintext, sizeof(buf));
	ss_utils_aes_encrypt(buf, sizeof(buf), kic_ref, sizeof(kic_ref));
	zassert_true(memcmp(buf, ciphertext, sizeof(buf)) != 0,
		     "encryption still used the replaced key");
}

/* --- the cipher helpers ---------------------------------------------------- */

ZTEST(softsim_crypto, test_aes_encrypt_matches_the_known_vector)
{
	uint8_t buf[sizeof(plaintext)];

	import_all_keys();
	memcpy(buf, plaintext, sizeof(buf));

	ss_utils_aes_encrypt(buf, sizeof(buf), kic_ref, sizeof(kic_ref));

	zassert_mem_equal(buf, ciphertext, sizeof(ciphertext),
			  "AES-CBC encryption does not match the reference vector");
}

ZTEST(softsim_crypto, test_aes_decrypt_matches_the_known_vector)
{
	uint8_t buf[sizeof(ciphertext)];

	import_all_keys();
	memcpy(buf, ciphertext, sizeof(buf));

	ss_utils_aes_decrypt(buf, sizeof(buf), kic_ref, sizeof(kic_ref));

	zassert_mem_equal(buf, plaintext, sizeof(plaintext),
			  "AES-CBC decryption does not match the reference vector");
}

ZTEST(softsim_crypto, test_ota_checksum_matches_the_known_vector)
{
	uint8_t cc[8] = {0};
	uint8_t data1[sizeof(cmac_data1)];
	uint8_t data2[sizeof(cmac_data2)];

	import_all_keys();
	memcpy(data1, cmac_data1, sizeof(data1));
	memcpy(data2, cmac_data2, sizeof(data2));

	/* OTA security uses the leading 8 bytes of the CMAC as the checksum. */
	zassert_ok(ss_utils_ota_calc_cc(cc, sizeof(cc), (uint8_t *)kid_ref, sizeof(kid_ref),
					AES_CMAC, data1, sizeof(data1), data2, sizeof(data2)),
		   "CMAC calculation failed");

	zassert_mem_equal(cc, cmac_golden, sizeof(cc),
			  "the OTA checksum does not match the reference CMAC");
}

/*
 * The checksum is only defined for CMAC here, because the KMU cannot do 3DES.
 * Any other algorithm has to be refused rather than quietly producing something
 * an operator would reject.
 */
ZTEST(softsim_crypto, test_ota_checksum_refuses_other_algorithms)
{
	uint8_t cc[8] = {0};
	uint8_t data1[sizeof(cmac_data1)];
	uint8_t data2[sizeof(cmac_data2)];

	import_all_keys();
	memcpy(data1, cmac_data1, sizeof(data1));
	memcpy(data2, cmac_data2, sizeof(data2));

	zassert_true(ss_utils_ota_calc_cc(cc, sizeof(cc), (uint8_t *)kid_ref, sizeof(kid_ref),
					  TRIPLE_DES_CBC2, data1, sizeof(data1), data2,
					  sizeof(data2)) != 0,
		     "3DES CMAC was accepted");

	/* The checksum key must be KID: KIC has no signing usage flag. */
	zassert_true(ss_utils_ota_calc_cc(cc, sizeof(cc), (uint8_t *)kic_ref, sizeof(kic_ref),
					  AES_CMAC, data1, sizeof(data1), data2,
					  sizeof(data2)) != 0,
		     "a non-KID key was accepted for the OTA checksum");
}

ZTEST(softsim_crypto, test_an_unknown_key_tag_leaves_the_buffer_alone)
{
	uint8_t buf[sizeof(plaintext)];
	const uint8_t bogus_ref[1] = {0x7f};

	import_all_keys();
	memcpy(buf, plaintext, sizeof(buf));

	ss_utils_aes_encrypt(buf, sizeof(buf), bogus_ref, sizeof(bogus_ref));

	zassert_mem_equal(buf, plaintext, sizeof(plaintext),
			  "an unresolvable key tag still modified the buffer");
}

/* --- documented defects ---------------------------------------------------- */

/*
 * Known defect. ss_utils_setup_key() ignores what the import helper returns and
 * always reports success, and nrf_softsim_provision() then logs that the keys
 * were written and carries on to write the IMSI. A device whose key import
 * failed therefore looks provisioned and can never authenticate -- the one
 * failure mode that cannot be recovered from the field.
 *
 * The import is made to fail by filling the key store first, which is a real
 * failure mode (the KMU has a fixed number of slots) and keeps the key itself
 * valid, so the length assertions inside the function are not what trips.
 *
 * Expected to fail until the return value is propagated.
 */
ZTEST(softsim_crypto, test_a_failed_key_import_is_reported)
{
	uint8_t key[KMU_KEY_SIZE];
	psa_key_id_t filler_base = 100;
	unsigned int filled = 0;

	memcpy(key, test_key, sizeof(key));

	/* Occupy the store until it refuses another persistent key. */
	for (unsigned int i = 0; i < 32; i++) {
		psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
		psa_key_id_t out;

		psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT);
		psa_set_key_algorithm(&attr, PSA_ALG_CBC_NO_PADDING);
		psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
		psa_set_key_bits(&attr, 128);
		psa_set_key_lifetime(&attr, PSA_KEY_LIFETIME_PERSISTENT);
		psa_set_key_id(&attr, filler_base + i);

		if (psa_import_key(&attr, key, sizeof(key), &out) != PSA_SUCCESS) {
			psa_reset_key_attributes(&attr);
			break;
		}
		psa_reset_key_attributes(&attr);
		filled++;
	}
	zassume_true(filled > 0 && filled < 32, "could not fill the key store (%u)", filled);

	const int rc = ss_utils_setup_key(sizeof(key), key, KEY_ID_KI);
	const int exists = ss_utils_check_key_existence(KEY_ID_KI);

	for (unsigned int i = 0; i < filled; i++) {
		psa_destroy_key(filler_base + i);
	}

	zassert_equal(exists, 0, "the import was expected to fail but the key is present");
	zassert_true(rc != 0, "ss_utils_setup_key() reported a failed import as success");
}
ZTEST_EXPECT_FAIL(softsim_crypto, test_a_failed_key_import_is_reported);

/*
 * Known defect. The cipher helpers copy through a fixed 256-byte scratch buffer
 * and never check the caller's length against it. PSA runs out of output space,
 * the error path jumps past the copy-back, and the function returns void -- so
 * the caller gets its plaintext back unchanged with no indication that nothing
 * was encrypted. For an OTA payload that means sending in the clear.
 *
 * Expected to fail until the helpers bound the length (and report failure).
 */
ZTEST(softsim_crypto, test_an_oversized_buffer_is_not_silently_left_in_the_clear)
{
	static uint8_t buf[512];
	static uint8_t original[512];

	import_all_keys();
	for (size_t i = 0; i < sizeof(buf); i++) {
		buf[i] = (uint8_t)i;
	}
	memcpy(original, buf, sizeof(original));

	ss_utils_aes_encrypt(buf, sizeof(buf), kic_ref, sizeof(kic_ref));

	zassert_true(memcmp(buf, original, sizeof(buf)) != 0,
		     "a buffer larger than the scratch space came back unencrypted");
}
ZTEST_EXPECT_FAIL(softsim_crypto, test_an_oversized_buffer_is_not_silently_left_in_the_clear);

/*
 * Known defect. 3DES support was dropped by making both helpers return without
 * touching the buffer, so a profile that expects 3DES OTA security gets
 * plaintext where ciphertext is required, silently. Returning an error, or not
 * offering the symbols at all, would at least surface it.
 *
 * Expected to fail until they report the lack of support.
 */
ZTEST(softsim_crypto, test_3des_does_not_pretend_to_encrypt)
{
	uint8_t buf[sizeof(plaintext)];

	memcpy(buf, plaintext, sizeof(buf));

	ss_utils_3des_encrypt(buf, sizeof(buf), test_key);

	zassert_true(memcmp(buf, plaintext, sizeof(buf)) != 0,
		     "ss_utils_3des_encrypt() left the plaintext untouched");
}
ZTEST_EXPECT_FAIL(softsim_crypto, test_3des_does_not_pretend_to_encrypt);
