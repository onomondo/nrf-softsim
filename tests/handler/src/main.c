/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Unit tests for the modem request handler in lib/nrf_softsim.c.
 *
 * There is no arithmetic here to check -- the value is protocol conformance
 * with the modem: every request must be answered exactly once, every payload
 * must be freed exactly once, and the UICC context must never be handed to the
 * UICC core as NULL. Those are call-count and argument assertions, which is
 * what FFF is for.
 *
 * The UICC core, the filesystem port and the modem SoftSIM API are all faked,
 * so the real work queue and FIFO still run and the concurrency is genuine.
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <nrf_modem_softsim.h>
#include <onomondo/softsim/softsim.h>
#include <onomondo/utils/ss_profile.h>

#include "ss_crypto.h"
#include <nrf_softsim.h>

/* nrf_softsim.c does the LOG_MODULE_REGISTER for "softsim" itself. */

DEFINE_FFF_GLOBALS;

/* Modem SoftSIM API (nrfxlib) */
FAKE_VALUE_FUNC(int, nrf_modem_softsim_req_handler_set, nrf_modem_softsim_req_handler_t);
FAKE_VALUE_FUNC(int, nrf_modem_softsim_res, enum nrf_modem_softsim_cmd, uint16_t, const void *,
		uint16_t);
FAKE_VALUE_FUNC(int, nrf_modem_softsim_err, enum nrf_modem_softsim_cmd, uint16_t);
FAKE_VOID_FUNC(nrf_modem_softsim_data_free, void *);

/* UICC core (onomondo-uicc) */
FAKE_VALUE_FUNC(struct ss_context *, ss_new_ctx);
FAKE_VOID_FUNC(ss_free_ctx, struct ss_context *);
FAKE_VOID_FUNC(ss_reset, struct ss_context *);
FAKE_VALUE_FUNC(size_t, ss_atr, struct ss_context *, uint8_t *, size_t);
FAKE_VALUE_FUNC(uint8_t, ss_is_suspended, struct ss_context *);
FAKE_VALUE_FUNC(size_t, ss_application_apdu_transact, struct ss_context *, uint8_t *, size_t,
		uint8_t *, size_t *);

/* Filesystem port (lib/ss_fs.c) and crypto (lib/ss_crypto.c) */
FAKE_VALUE_FUNC(int, ss_init_fs);
FAKE_VALUE_FUNC(int, ss_deinit_fs);
FAKE_VALUE_FUNC(int, port_provision, struct ss_profile *);
FAKE_VALUE_FUNC(int, port_check_provisioned);
FAKE_VALUE_FUNC(int, ss_utils_check_key_existence, enum key_identifier_base);

/*
 * These two are declared with VLA-style parameters, which FFF cannot express --
 * a generated `const char *` prototype trips -Werror=vla-parameter against the
 * real header. The provisioning path is not exercised here, so plain stubs with
 * the exact declared signature are enough.
 */
uint8_t ss_profile_from_string(uint16_t len, const char input_string[len],
			       struct ss_profile *profile)
{
	ARG_UNUSED(len);
	ARG_UNUSED(input_string);
	ARG_UNUSED(profile);
	return 0;
}

int ss_utils_setup_key(size_t key_len, uint8_t key[static key_len], enum key_identifier_base key_id)
{
	ARG_UNUSED(key_len);
	ARG_UNUSED(key);
	ARG_UNUSED(key_id);
	return 0;
}

/*
 * The module still exports the init entry point as onomondo_init(); the header
 * documents it as nrf_softsim_init(), a mismatch tracked separately. Declare
 * what actually exists.
 */
int onomondo_init(void);

#define ALL_FAKES(F)                                                                               \
	F(nrf_modem_softsim_req_handler_set)                                                       \
	F(nrf_modem_softsim_res)                                                                   \
	F(nrf_modem_softsim_err)                                                                   \
	F(nrf_modem_softsim_data_free)                                                             \
	F(ss_new_ctx)                                                                              \
	F(ss_free_ctx)                                                                             \
	F(ss_reset)                                                                                \
	F(ss_atr)                                                                                  \
	F(ss_is_suspended)                                                                         \
	F(ss_application_apdu_transact)                                                            \
	F(ss_init_fs)                                                                              \
	F(ss_deinit_fs)                                                                            \
	F(port_provision)                                                                          \
	F(port_check_provisioned)                                                                  \
	F(ss_utils_check_key_existence)

/* nrf_softsim.c keeps the UICC handle in a non-static global, so the tests can
 * both observe it and put it back to a known state. */
extern struct ss_context *ctx;

/* The handler itself has internal linkage; capture the pointer the module hands
 * to nrf_modem_softsim_req_handler_set() during init. */
static nrf_modem_softsim_req_handler_t req_handler;

/* A non-NULL cookie standing in for a real UICC context. */
static struct ss_context *const FAKE_CTX = (struct ss_context *)0xC0FFEE;

static int capture_handler(nrf_modem_softsim_req_handler_t handler)
{
	req_handler = handler;
	return 0;
}

static struct ss_context *new_ctx_ok(void)
{
	return FAKE_CTX;
}

static size_t atr_ok(struct ss_context *c, uint8_t *buf, size_t len)
{
	ARG_UNUSED(c);
	memset(buf, 0x3b, MIN(len, 8));
	return MIN(len, 8);
}

static size_t apdu_ok(struct ss_context *c, uint8_t *rsp, size_t rsp_len, uint8_t *req,
		      size_t *req_len)
{
	ARG_UNUSED(c);
	ARG_UNUSED(req);
	ARG_UNUSED(req_len);
	memset(rsp, 0x90, MIN(rsp_len, 2));
	return MIN(rsp_len, 2);
}

/* The modem owns request payloads; mirror that by really releasing them so the
 * heap stays balanced across a long test run. */
static void data_free_real(void *data)
{
	k_free(data);
}

static void reset_all_fakes(void)
{
	ALL_FAKES(RESET_FAKE)
	FFF_RESET_HISTORY();

	nrf_modem_softsim_req_handler_set_fake.custom_fake = capture_handler;
	ss_new_ctx_fake.custom_fake = new_ctx_ok;
	ss_atr_fake.custom_fake = atr_ok;
	ss_application_apdu_transact_fake.custom_fake = apdu_ok;
	nrf_modem_softsim_data_free_fake.custom_fake = data_free_real;
}

static void *suite_setup(void)
{
	reset_all_fakes();

	/* Starts the SoftSIM work queue exactly once for the whole binary. */
	zassert_ok(onomondo_init(), "SoftSIM init failed");
	zassert_not_null(req_handler, "module did not register a request handler");

	return NULL;
}

static void test_before(void *fixture)
{
	ARG_UNUSED(fixture);

	reset_all_fakes();
	ctx = NULL;
}

ZTEST_SUITE(softsim_handler, NULL, suite_setup, test_before, NULL, NULL);

static int completions(void)
{
	return nrf_modem_softsim_res_fake.call_count + nrf_modem_softsim_err_fake.call_count;
}

/* The work queue is a real thread; wait for it rather than sleeping blindly. */
static void wait_for_completions(int expected)
{
	for (int i = 0; i < 2000 && completions() < expected; i++) {
		k_msleep(1);
	}
	zassert_equal(completions(), expected, "work queue did not answer %d request(s)", expected);
}

static void submit(enum nrf_modem_softsim_cmd cmd, uint16_t req_id, void *data, uint16_t len)
{
	req_handler(cmd, req_id, data, len);
}

/* --- response pairing ------------------------------------------------------ */

ZTEST(softsim_handler, test_each_request_is_answered_exactly_once)
{
	submit(NRF_MODEM_SOFTSIM_INIT, 1, NULL, 0);
	wait_for_completions(1);

	uint8_t *apdu = k_malloc(4);

	zassert_not_null(apdu);
	memcpy(apdu, "\x00\xa4\x00\x0c", 4);
	submit(NRF_MODEM_SOFTSIM_APDU, 2, apdu, 4);
	wait_for_completions(2);

	submit(NRF_MODEM_SOFTSIM_DEINIT, 3, NULL, 0);
	wait_for_completions(3);

	zassert_equal(nrf_modem_softsim_res_fake.call_count, 3);
	zassert_equal(nrf_modem_softsim_err_fake.call_count, 0);

	/* Each answer must carry back the req_id it was asked with. */
	zassert_equal(nrf_modem_softsim_res_fake.arg1_history[0], 1);
	zassert_equal(nrf_modem_softsim_res_fake.arg1_history[1], 2);
	zassert_equal(nrf_modem_softsim_res_fake.arg1_history[2], 3);
}

ZTEST(softsim_handler, test_payload_is_freed_exactly_once)
{
	submit(NRF_MODEM_SOFTSIM_INIT, 1, NULL, 0);
	wait_for_completions(1);

	uint8_t *apdu = k_malloc(4);

	zassert_not_null(apdu);
	submit(NRF_MODEM_SOFTSIM_APDU, 2, apdu, 4);
	wait_for_completions(2);

	zassert_equal(nrf_modem_softsim_data_free_fake.call_count, 1,
		      "the APDU payload must be released exactly once");
	zassert_equal_ptr(nrf_modem_softsim_data_free_fake.arg0_val, apdu,
			  "the released pointer must be the one handed in");
}

ZTEST(softsim_handler, test_requests_without_payload_are_not_freed)
{
	submit(NRF_MODEM_SOFTSIM_INIT, 1, NULL, 0);
	submit(NRF_MODEM_SOFTSIM_DEINIT, 2, NULL, 0);
	wait_for_completions(2);

	zassert_equal(nrf_modem_softsim_data_free_fake.call_count, 0,
		      "there was no payload to free");
}

/* --- context lifecycle ----------------------------------------------------- */

ZTEST(softsim_handler, test_repeated_init_reuses_the_context)
{
	submit(NRF_MODEM_SOFTSIM_INIT, 1, NULL, 0);
	wait_for_completions(1);
	submit(NRF_MODEM_SOFTSIM_INIT, 2, NULL, 0);
	wait_for_completions(2);
	submit(NRF_MODEM_SOFTSIM_INIT, 3, NULL, 0);
	wait_for_completions(3);

	zassert_equal(ss_new_ctx_fake.call_count, 1,
		      "a second INIT must not allocate another context");
	zassert_equal_ptr(ctx, FAKE_CTX);
}

ZTEST(softsim_handler, test_deinit_releases_the_context_and_commits)
{
	submit(NRF_MODEM_SOFTSIM_INIT, 1, NULL, 0);
	wait_for_completions(1);
	submit(NRF_MODEM_SOFTSIM_DEINIT, 2, NULL, 0);
	wait_for_completions(2);

	zassert_equal(ss_free_ctx_fake.call_count, 1);
	zassert_equal(ss_deinit_fs_fake.call_count, 1, "DEINIT must flush the filesystem");
	zassert_is_null(ctx, "the handle must be cleared so a later INIT re-creates it");
}

ZTEST(softsim_handler, test_deinit_while_suspended_keeps_the_context)
{
	submit(NRF_MODEM_SOFTSIM_INIT, 1, NULL, 0);
	wait_for_completions(1);

	ss_is_suspended_fake.return_val = 1;

	submit(NRF_MODEM_SOFTSIM_DEINIT, 2, NULL, 0);
	wait_for_completions(2);

	zassert_equal(ss_free_ctx_fake.call_count, 0, "a suspended card must keep its context");
	zassert_equal(ss_deinit_fs_fake.call_count, 0);
	zassert_not_null(ctx);
	zassert_equal(nrf_modem_softsim_res_fake.call_count, 2, "DEINIT is still answered");
}

/*
 * Direct regression for the heap corruption fixed in 00bcdab ("Heap corrupted
 * by de-initializing softsim many times"): every context created must be
 * released again, however many attach/detach cycles the modem drives.
 */
ZTEST(softsim_handler, test_context_accounting_is_balanced_over_many_cycles)
{
	const int cycles = 100;

	for (int i = 0; i < cycles; i++) {
		submit(NRF_MODEM_SOFTSIM_INIT, (uint16_t)(2 * i), NULL, 0);
		submit(NRF_MODEM_SOFTSIM_DEINIT, (uint16_t)(2 * i + 1), NULL, 0);
		wait_for_completions(2 * (i + 1));
	}

	zassert_equal(ss_new_ctx_fake.call_count, cycles);
	zassert_equal(ss_free_ctx_fake.call_count, cycles, "every context must be released");
	zassert_is_null(ctx);
}

/* --- the NULL-context orderings -------------------------------------------- */

/*
 * Known defect. DEINIT clears ctx and guards its own use of it, but the RESET
 * case calls ss_reset(ctx) unguarded -- and the modem sends RESET exactly when
 * a request has become unresponsive, which is when ordering is least
 * predictable. On target ss_reset() dereferences immediately.
 *
 * Asserted against the fake's recorded argument rather than by letting it
 * crash, so the suite reports the defect instead of taking the process down.
 * Expected to fail until RESET gets the same NULL guard DEINIT already has.
 */
ZTEST(softsim_handler, test_reset_never_receives_a_null_context)
{
	/* RESET arriving before anything else. */
	submit(NRF_MODEM_SOFTSIM_RESET, 1, NULL, 0);
	wait_for_completions(1);

	/* RESET arriving after the context was torn down. */
	submit(NRF_MODEM_SOFTSIM_INIT, 2, NULL, 0);
	wait_for_completions(2);
	submit(NRF_MODEM_SOFTSIM_DEINIT, 3, NULL, 0);
	wait_for_completions(3);
	submit(NRF_MODEM_SOFTSIM_RESET, 4, NULL, 0);
	wait_for_completions(4);

	/* Deliberately not asserting a call count: INIT resets the card too, and
	 * a fix may well drop the RESET-with-no-context call altogether. The
	 * invariant is only that no reset is ever handed a NULL context. */
	for (unsigned int i = 0; i < ss_reset_fake.call_count; i++) {
		zassert_not_null(ss_reset_fake.arg0_history[i],
				 "ss_reset() call %u received a NULL context", i);
	}
}
ZTEST_EXPECT_FAIL(softsim_handler, test_reset_never_receives_a_null_context);

/*
 * Same class, different entry point: ss_new_ctx() returns NULL when the heap is
 * exhausted, and ss_is_suspended(NULL) deliberately answers 0, so the guard in
 * the INIT case passes and ss_reset(NULL) runs. Expected to fail until INIT
 * checks the allocation.
 */
ZTEST(softsim_handler, test_init_handles_context_allocation_failure)
{
	ss_new_ctx_fake.custom_fake = NULL;
	ss_new_ctx_fake.return_val = NULL;

	submit(NRF_MODEM_SOFTSIM_INIT, 1, NULL, 0);
	wait_for_completions(1);

	zassert_equal(ss_reset_fake.call_count, 0,
		      "a failed context allocation must not reach ss_reset()");
}
ZTEST_EXPECT_FAIL(softsim_handler, test_init_handles_context_allocation_failure);
