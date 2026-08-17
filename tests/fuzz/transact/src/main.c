/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Full-transact fuzz target: each case is one C-APDU fed to
 * ss_application_apdu_transact() over the real nRF filesystem port (ss_fs.c /
 * ss_cache.c on Zephyr NVS over the flash simulator), seeded with the same
 * template.bin image the firmware flashes. This drives what the stateless
 * parser target cannot: APDU dispatch, SELECT/READ/UPDATE, path resolution, the
 * DIR cache and (via ENVELOPE) the proactive and SMS-PP paths -- all with the
 * storage layer underneath, exactly as on a device.
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include <nrf_softsim.h>
#include <onomondo/softsim/softsim.h>

#include "fuzz_entry.h"

/* ss_fs.c does LOG_MODULE_DECLARE(softsim, ...); register it once here. */
LOG_MODULE_REGISTER(softsim, CONFIG_SOFTSIM_NRF_LOG_LEVEL);

static const uint8_t template_bin[] = {
#include <template_bin.inc>
};

/* Response: max Le (256) plus the status word. */
#define RSP_MAX 260
/* Input cap: header + extended (00 Lc1 Lc2) + a full 256-byte body + Le is
 * enough to reach the extended-length paths and the apdu.cmd[256] boundary. */
#define REQ_MAX 512

static struct ss_context *ctx;

void fuzz_setup(void)
{
	const struct device *flash_dev = PARTITION_DEVICE(nvs_storage);

	/* Seed the partition the way the flashed template.hex would: the raw NVS
	 * image at the partition base, erased flash after it. If this cannot be
	 * done the whole run is meaningless, so assert rather than limp on. */
	__ASSERT(device_is_ready(flash_dev), "flash simulator not ready");
	flash_erase(flash_dev, PARTITION_OFFSET(nvs_storage), PARTITION_SIZE(nvs_storage));
	flash_write(flash_dev, PARTITION_OFFSET(nvs_storage), template_bin, sizeof(template_bin));

	__ASSERT(ss_init_fs() == 0, "ss_init_fs() rejected the template image");

	ctx = ss_new_ctx();
	__ASSERT(ctx != NULL, "could not allocate a UICC context");
}

void fuzz_one(const uint8_t *data, size_t len)
{
	uint8_t req[REQ_MAX];
	uint8_t rsp[RSP_MAX];
	size_t req_len;

	if (len == 0 || len > sizeof(req))
		return;

	/* ss_reset re-selects the MF, giving every case the same selection start.
	 *
	 * ponytail: the flash simulator is process-global and the OS boots once, so
	 * writes (UPDATE BINARY, and any OTA/refresh reached via ENVELOPE) from
	 * earlier cases persist into later ones -- state accumulates. That helps
	 * reach deep states but means a crash found mid-run can need the whole
	 * corpus order to reproduce. Upgrade path if that bites: re-seed the
	 * partition (erase + write template_bin) here per case -- correct but
	 * ~1000x slower, so not the default. */
	ss_reset(ctx);

	/* transact may write into request_buf; hand it a private, sized copy. */
	memcpy(req, data, len);
	req_len = len;

	ss_application_apdu_transact(ctx, rsp, sizeof(rsp), req, &req_len);
}
