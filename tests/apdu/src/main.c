/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Golden-transcript test: the real onomondo-uicc core over the real nRF
 * filesystem port (ss_fs.c/ss_cache.c on Zephyr NVS over the flash
 * simulator), seeded with the same template.bin image the firmware flashes
 * at the nvs_storage partition. Nothing is mocked.
 *
 * This is the net under everything the unit suites cannot see: the DIR
 * cache, sector rotation, path resolution and APDU dispatch working
 * together. A change that keeps every unit suite green but breaks how the
 * pieces compose -- a storage-macro slip, a submodule bump moving a file
 * definition, a cache eviction bug -- surfaces here as a changed status
 * word or a changed byte.
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

#include <nrf_softsim.h>
#include <onomondo/softsim/softsim.h>

/* ss_fs.c does LOG_MODULE_DECLARE(softsim, ...); register it once here.
 * (The UICC core logs through its own softsim_uicc module.) */
LOG_MODULE_REGISTER(softsim, CONFIG_SOFTSIM_NRF_LOG_LEVEL);

static const uint8_t template_bin[] = {
#include <template_bin.inc>
};

/* Response buffer: max Le (256) plus the status word, as SIM_HAL_MAX_LE. */
#define RSP_MAX 260

static struct ss_context *ctx;

static size_t transact(const uint8_t *apdu, size_t apdu_len, uint8_t *rsp)
{
	uint8_t req[RSP_MAX];
	size_t req_len = apdu_len;

	memcpy(req, apdu, apdu_len);

	return ss_application_apdu_transact(ctx, rsp, RSP_MAX, req, &req_len);
}

static void expect_sw(const uint8_t *rsp, size_t len, uint16_t sw, const char *what)
{
	zassert_true(len >= 2, "%s: response too short (%zu)", what, len);
	zassert_equal(((uint16_t)rsp[len - 2] << 8) | rsp[len - 1], sw,
		      "%s: SW %02x%02x, expected %04x", what, rsp[len - 2], rsp[len - 1], sw);
}

/* zassume, not zassert: a failing assert does not stop a suite setup, so an
 * unready flash device would otherwise be erased and written anyway. */
static void *suite_setup(void)
{
	/* Seed the partition exactly the way the flashed template.hex would:
	 * the raw NVS image at the partition base, erased flash after it. */
	const struct device *flash_dev = PARTITION_DEVICE(nvs_storage);

	zassume_true(device_is_ready(flash_dev), "flash simulator not ready");
	zassume_ok(
		flash_erase(flash_dev, PARTITION_OFFSET(nvs_storage), PARTITION_SIZE(nvs_storage)));
	zassume_ok(flash_write(flash_dev, PARTITION_OFFSET(nvs_storage), template_bin,
			       sizeof(template_bin)));

	zassume_ok(ss_init_fs(), "ss_init_fs() rejected the template image");

	return NULL;
}

static void test_before(void *fixture)
{
	ARG_UNUSED(fixture);

	ctx = ss_new_ctx();
	zassert_not_null(ctx, "could not allocate a UICC context");
	/* Not optional: reset is what selects the MF -- and it must run after
	 * ss_init_fs(), because it reads the MF definition from storage. */
	ss_reset(ctx);
}

static void test_after(void *fixture)
{
	ARG_UNUSED(fixture);

	ss_free_ctx(ctx);
	ctx = NULL;
}

ZTEST_SUITE(softsim_apdu, NULL, suite_setup, test_before, test_after, NULL);

/*
 * The ATR is a compile-time constant of the core (softsim.c); golden-pin it
 * whole. The modem parses it before anything else works, so an accidental
 * change is an attach failure in the field.
 */
ZTEST(softsim_apdu, test_atr_is_the_known_22_bytes)
{
	static const uint8_t golden[] = {0x3b, 0x9f, 0x01, 0x80, 0x1f, 0x87, 0x80, 0x31,
					 0xe0, 0x73, 0xfe, 0x21, 0x00, 0x67, 0x4a, 0x4c,
					 0x75, 0x30, 0x34, 0x05, 0x4b, 0x25};
	uint8_t atr[RSP_MAX] = {0};

	size_t len = ss_atr(ctx, atr, sizeof(atr));

	zassert_equal(len, sizeof(golden), "ATR length changed");
	zassert_mem_equal(atr, golden, sizeof(golden), "ATR bytes changed");
}

/*
 * SELECT down to EF.ICCID and read it back. The content golden is the
 * template's unprovisioned placeholder ICCID -- 00 11 22 .. 99 -- which is
 * exactly what a fresh device presents before provisioning.
 */
ZTEST(softsim_apdu, test_select_and_read_iccid)
{
	static const uint8_t select_mf[] = {0x00, 0xa4, 0x00, 0x0c, 0x02, 0x3f, 0x00};
	static const uint8_t select_iccid[] = {0x00, 0xa4, 0x00, 0x04, 0x02, 0x2f, 0xe2};
	static const uint8_t read_iccid[] = {0x00, 0xb0, 0x00, 0x00, 0x0a};
	static const uint8_t golden[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
					 0x66, 0x77, 0x88, 0x99, 0x90, 0x00};
	uint8_t rsp[RSP_MAX];
	size_t len;

	len = transact(select_mf, sizeof(select_mf), rsp);
	expect_sw(rsp, len, 0x9000, "SELECT MF");

	/* P2=04: return the FCP. It is re-encoded from the stored definition,
	 * so pin its shape (an FCP template naming the selected file), not its
	 * bytes. */
	len = transact(select_iccid, sizeof(select_iccid), rsp);
	expect_sw(rsp, len, 0x9000, "SELECT EF.ICCID");
	zassert_true(len > 2, "SELECT with P2=04 must return FCP data");
	zassert_equal(rsp[0], 0x62, "response is not an FCP template");

	static const uint8_t fid_tlv[] = {0x83, 0x02, 0x2f, 0xe2};
	bool fid_found = false;

	for (size_t i = 0; i + sizeof(fid_tlv) <= len; i++) {
		if (memcmp(&rsp[i], fid_tlv, sizeof(fid_tlv)) == 0) {
			fid_found = true;
			break;
		}
	}
	zassert_true(fid_found, "FCP does not name EF.ICCID (tag 83)");

	len = transact(read_iccid, sizeof(read_iccid), rsp);
	zassert_equal(len, sizeof(golden), "READ BINARY returned %zu bytes", len);
	zassert_mem_equal(rsp, golden, sizeof(golden), "ICCID content changed");
}

ZTEST(softsim_apdu, test_select_unknown_file_fails_cleanly)
{
	static const uint8_t select_bogus[] = {0x00, 0xa4, 0x00, 0x0c, 0x02, 0x5f, 0x99};
	uint8_t rsp[RSP_MAX];

	size_t len = transact(select_bogus, sizeof(select_bogus), rsp);

	expect_sw(rsp, len, 0x6a82, "SELECT of an unknown FID");
}

ZTEST(softsim_apdu, test_read_binary_without_an_ef_fails_cleanly)
{
	static const uint8_t read10[] = {0x00, 0xb0, 0x00, 0x00, 0x0a};
	uint8_t rsp[RSP_MAX];

	/* Fresh reset: the MF is selected, no EF is. The core answers 6981
	 * (command incompatible with file structure -- the current file is a
	 * DF), where TS 102 221 would also allow 6986; either is a clean
	 * rejection, this pins the one the core actually gives. */
	size_t len = transact(read10, sizeof(read10), rsp);

	expect_sw(rsp, len, 0x6981, "READ BINARY with no EF selected");
}

/*
 * The write path, end to end: UPDATE BINARY into EF.LOCI, then tear the
 * filesystem down and bring it back up so the bytes have to come off flash
 * rather than out of the cache buffer they were written into.
 *
 * EF.LOCI is the file the modem rewrites on every location update, so this is
 * also the most-travelled write on a real device. Nothing else in the suite
 * writes, which left the cache dirty-tracking and the ss_deinit_fs() flush
 * unexercised at this level.
 */
ZTEST(softsim_apdu, test_update_binary_survives_a_remount)
{
	static const uint8_t select_adf[] = {0x00, 0xa4, 0x00, 0x04, 0x02, 0x7f, 0xf0};
	static const uint8_t select_loci[] = {0x00, 0xa4, 0x00, 0x04, 0x02, 0x6f, 0x7e};
	static const uint8_t read_loci[] = {0x00, 0xb0, 0x00, 0x00, 0x0b};
	static const uint8_t update_loci[] = {0x00, 0xd6, 0x00, 0x00, 0x0b, 0x11, 0x22, 0x33,
					      0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};
	static const uint8_t golden[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
					 0x88, 0x99, 0xaa, 0xbb, 0x90, 0x00};
	uint8_t rsp[RSP_MAX];
	size_t len;

	len = transact(select_adf, sizeof(select_adf), rsp);
	expect_sw(rsp, len, 0x9000, "SELECT ADF.USIM");
	len = transact(select_loci, sizeof(select_loci), rsp);
	expect_sw(rsp, len, 0x9000, "SELECT EF.LOCI");

	len = transact(update_loci, sizeof(update_loci), rsp);
	expect_sw(rsp, len, 0x9000, "UPDATE BINARY EF.LOCI");

	len = transact(read_loci, sizeof(read_loci), rsp);
	zassert_equal(len, sizeof(golden), "READ BINARY returned %zu bytes", len);
	zassert_mem_equal(rsp, golden, sizeof(golden), "the update was not visible");

	/* Drop every cache buffer and re-read the directory table from flash. */
	zassert_ok(ss_deinit_fs(), "could not shut the filesystem down");
	zassert_ok(ss_init_fs(), "the filesystem did not come back up");

	/* A new context, since the old one's selection state referred to the
	 * filesystem that was just torn down. */
	ss_free_ctx(ctx);
	ctx = ss_new_ctx();
	zassert_not_null(ctx);
	ss_reset(ctx);

	len = transact(select_adf, sizeof(select_adf), rsp);
	expect_sw(rsp, len, 0x9000, "SELECT ADF.USIM after remount");
	len = transact(select_loci, sizeof(select_loci), rsp);
	expect_sw(rsp, len, 0x9000, "SELECT EF.LOCI after remount");
	len = transact(read_loci, sizeof(read_loci), rsp);
	zassert_equal(len, sizeof(golden), "READ BINARY returned %zu bytes after remount", len);
	zassert_mem_equal(rsp, golden, sizeof(golden), "the update did not reach flash");
}

/*
 * The nRF9151 modem issues STATUS with an extended Le field
 * (80 F2 00 00 00 01 68, Le = 360) during USIM initialisation, even though the
 * card's ATR declares no support for extended lengths. Rejecting it was tried
 * upstream and proved on hardware to stop the device attaching at all -- the
 * card must answer normally instead, and an extended Le only bounds the
 * response size, so answering short is correct.
 *
 * This is the cheapest possible guard against that rejection coming back with
 * a future core bump.
 */
ZTEST(softsim_apdu, test_status_with_an_extended_le_is_answered)
{
	static const uint8_t status_ext_le[] = {0x80, 0xf2, 0x00, 0x00, 0x00, 0x01, 0x68};
	uint8_t rsp[RSP_MAX];

	size_t len = transact(status_ext_le, sizeof(status_ext_le), rsp);

	/* 6700 is the wrong-length rejection this must never become. */
	expect_sw(rsp, len, 0x9000, "STATUS with an extended Le");
	zassert_true(len > 2, "the extended Le suppressed the response data");
	zassert_equal(rsp[0], 0x62, "STATUS did not answer with an FCP template");
}

/*
 * AUTHENTICATE is the one command every attach depends on, and it is the only
 * thing in the suite that runs Milenage and AES -- both are linked in but were
 * otherwise never called, so a broken cipher would have gone unnoticed here.
 *
 * The AUTN below is not a valid one for the template's key material, so the
 * card answers with a resynchronisation token (TS 31.102 tag 0xDC + AUTS)
 * instead of a success response. That answer is still fully computed: AUTS
 * carries the card's own sequence number encrypted under the key it read from
 * EF.A001, so pinning it covers the key-reading path, the resynchronisation
 * functions and the AES primitive underneath, without needing a valid vector.
 */
ZTEST(softsim_apdu, test_authenticate_answers_with_a_resync_token)
{
	static const uint8_t select_adf[] = {0x00, 0xa4, 0x00, 0x04, 0x02, 0x7f, 0xf0};
	static const uint8_t authenticate[] = {
		0x00, 0x88, 0x00, 0x81, 0x22, /* CLA INS P1 P2 Lc */
		0x10, 1,    2,	  3,	4,    5,    6,	  7,	8,    9,   10,
		11,   12,   13,	  14,	15,   16, /* L_RAND, RAND */
		0x10, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
		0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31 /* L_AUTN, AUTN */
	};
	/* Tag 0xDC, 14-byte AUTS, then SW. */
	static const uint8_t golden[] = {0xdc, 0x0e, 0xd3, 0x8b, 0xcb, 0xfa, 0xad, 0xdc, 0xdf,
					 0xa1, 0x25, 0x74, 0x23, 0x2e, 0xdf, 0xca, 0x90, 0x00};
	uint8_t rsp[RSP_MAX];
	size_t len;

	len = transact(select_adf, sizeof(select_adf), rsp);
	expect_sw(rsp, len, 0x9000, "SELECT ADF.USIM");

	len = transact(authenticate, sizeof(authenticate), rsp);
	expect_sw(rsp, len, 0x9000, "AUTHENTICATE");
	zassert_equal(rsp[0], 0xdc, "AUTHENTICATE did not answer with a resync token");
	zassert_equal(rsp[1], 0x0e, "AUTS length changed");
	zassert_equal(len, sizeof(golden), "AUTHENTICATE returned %zu bytes", len);
	zassert_mem_equal(rsp, golden, sizeof(golden), "the computed AUTS changed");
}
