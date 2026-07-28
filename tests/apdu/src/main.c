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

static void *suite_setup(void)
{
	/* Seed the partition exactly the way the flashed template.hex would:
	 * the raw NVS image at the partition base, erased flash after it. */
	const struct device *flash_dev = PARTITION_DEVICE(nvs_storage);

	zassert_true(device_is_ready(flash_dev));
	zassert_ok(
		flash_erase(flash_dev, PARTITION_OFFSET(nvs_storage), PARTITION_SIZE(nvs_storage)));
	zassert_ok(flash_write(flash_dev, PARTITION_OFFSET(nvs_storage), template_bin,
			       sizeof(template_bin)));

	zassert_ok(ss_init_fs(), "ss_init_fs() rejected the template image");

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

	/* Fresh reset: the MF is selected, no EF is. */
	size_t len = transact(read10, sizeof(read10), rsp);

	expect_sw(rsp, len, 0x6986, "READ BINARY with no EF selected");
}
