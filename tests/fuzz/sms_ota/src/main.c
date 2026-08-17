/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * SMS-PP / OTA fuzz target: each case is an SMS TPDU handed to ss_uicc_sms_rx()
 * -- the boundary an SMS-PP DOWNLOAD arrives at inside an ENVELOPE. It decodes
 * the TP header, reassembles concatenated messages, and (on a CPI command)
 * dispatches into ss_uicc_remote_cmd_receive(), so this reaches the SMS parser,
 * the concatenation state machine and the OTA command framing in one target,
 * without the fuzzer having to rediscover the ENVELOPE wrapping each time.
 *
 * It runs over the same provisioned filesystem as the transact target, because
 * ss_uicc_sms_rx uses the real context (ctx->fs_chg_filelist) and the OTA path
 * reads keys/counters from storage.
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include <nrf_softsim.h>
#include <onomondo/softsim/softsim.h>
#include <onomondo/softsim/utils.h>

#include "uicc_sms_rx.h"
#include "fuzz_entry.h"

LOG_MODULE_REGISTER(softsim, CONFIG_SOFTSIM_NRF_LOG_LEVEL);

static const uint8_t template_bin[] = {
#include <template_bin.inc>
};

#define RSP_MAX 260
/* An SMS-PP TPDU arrives inside an ENVELOPE, whose command body is capped at
 * apdu.cmd[256]; nothing longer can reach this path on a device. */
#define TPDU_MAX 256

static struct ss_context *ctx;

void fuzz_setup(void)
{
	const struct device *flash_dev = PARTITION_DEVICE(nvs_storage);

	__ASSERT(device_is_ready(flash_dev), "flash simulator not ready");
	flash_erase(flash_dev, PARTITION_OFFSET(nvs_storage), PARTITION_SIZE(nvs_storage));
	flash_write(flash_dev, PARTITION_OFFSET(nvs_storage), template_bin, sizeof(template_bin));

	__ASSERT(ss_init_fs() == 0, "ss_init_fs() rejected the template image");

	ctx = ss_new_ctx();
	__ASSERT(ctx != NULL, "could not allocate a UICC context");
}

void fuzz_one(const uint8_t *data, size_t len)
{
	uint8_t rsp[RSP_MAX];
	size_t rsp_len = sizeof(rsp);
	struct ss_buf *tpdu;

	if (len == 0 || len > TPDU_MAX)
		return;

	/* ponytail: ss_reset gives each case the same selection start, but the SMS
	 * concatenation state (ctx->proactive.sms_rx_state) and any flash writes
	 * persist across cases within the one booted OS -- reachability grows over
	 * a run, reproduction of a mid-run crash may need the corpus order. Same
	 * ceiling and upgrade path as the transact target.
	 *
	 * ponytail: the template image provisions no TAR key/counter files
	 * (0xA004/0xA005), so ss_uicc_remote_cmd_receive returns at its first
	 * ss_fs_select(TAR_KEY_FID) -- the SMS decode, reassembly and command
	 * framing are fuzzed, the OTA crypto/counter path past that select is not.
	 * Upgrade path: write those two files into NVS in fuzz_setup() (see the TAR
	 * handling in uicc_remote_cmd.c) to unlock the deeper OTA logic. */
	ss_reset(ctx);

	/* ss_uicc_sms_rx only reads the buffer; the caller owns it. */
	tpdu = ss_buf_alloc_and_cpy(data, len);
	ss_uicc_sms_rx(ctx, tpdu, &rsp_len, rsp);
	ss_buf_free(tpdu);
}
