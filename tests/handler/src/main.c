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
#include <onomondo/softsim/mem.h>
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

/* The allocator port (lib/ss_heap.c) is not compiled here. Route to the kernel
 * heap so allocation stays real and the balance tests keep their meaning. */
void *port_malloc(size_t size)
{
	return k_malloc(size);
}

void port_free(void *ptr)
{
	k_free(ptr);
}

/*
 * ss_utils_setup_key is declared with a VLA-style parameter, which FFF cannot
 * express -- a generated plain-pointer prototype trips -Werror=vla-parameter
 * against the real header. Hand-written recording stub instead. (The profile
 * parser, ss_profile_from_string, has the same signature problem but is not
 * faked at all: the provisioning suite below compiles the real one.)
 */
static struct {
	int count;
	enum key_identifier_base ids[8];
	uint8_t keys[8][16];
} key_setup;

int ss_utils_setup_key(size_t key_len, uint8_t key[static key_len], enum key_identifier_base key_id)
{
	if (key_setup.count < (int)ARRAY_SIZE(key_setup.ids) &&
	    key_len <= sizeof(key_setup.keys[0])) {
		key_setup.ids[key_setup.count] = key_id;
		memcpy(key_setup.keys[key_setup.count], key, key_len);
	}
	key_setup.count++;
	return 0;
}

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

/* nrf_softsim_provision() hands port_provision a pointer to a stack-local
 * struct; keep a deep copy, the pointer is dangling by assert time. */
static struct ss_profile captured_profile;

static int capture_profile(struct ss_profile *profile)
{
	captured_profile = *profile;
	return 0;
}

static void reset_all_fakes(void)
{
	ALL_FAKES(RESET_FAKE)
	FFF_RESET_HISTORY();
	memset(&key_setup, 0, sizeof(key_setup));
	memset(&captured_profile, 0, sizeof(captured_profile));

	nrf_modem_softsim_req_handler_set_fake.custom_fake = capture_handler;
	ss_new_ctx_fake.custom_fake = new_ctx_ok;
	ss_atr_fake.custom_fake = atr_ok;
	ss_application_apdu_transact_fake.custom_fake = apdu_ok;
	nrf_modem_softsim_data_free_fake.custom_fake = data_free_real;
	port_provision_fake.custom_fake = capture_profile;
}

static void *suite_setup(void)
{
	reset_all_fakes();

	/* Starts the SoftSIM work queue exactly once for the whole binary. */
	zassert_ok(nrf_softsim_init(), "SoftSIM init failed");
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

/*
 * The handler answers out of one static buffer for both the ATR and every APDU
 * response, and narrows the core's size_t length into the modem's uint16_t. That
 * plumbing is what the modem actually consumes, so pin the bytes and the length,
 * not just the call count.
 */
ZTEST(softsim_handler, test_answers_carry_the_core_response_bytes)
{
	const uint8_t *out;

	submit(NRF_MODEM_SOFTSIM_INIT, 1, NULL, 0);
	wait_for_completions(1);

	/* atr_ok() fills 8 bytes of 0x3b and returns 8. */
	zassert_equal(nrf_modem_softsim_res_fake.arg3_val, 8, "ATR length was not passed through");
	out = nrf_modem_softsim_res_fake.arg2_val;
	zassert_not_null(out, "INIT must answer with the ATR buffer");
	for (int i = 0; i < 8; i++) {
		zassert_equal(out[i], 0x3b, "ATR byte %d differs from what the core produced", i);
	}

	uint8_t *apdu = k_malloc(4);

	zassert_not_null(apdu);
	memcpy(apdu, "\x00\xa4\x00\x0c", 4);
	submit(NRF_MODEM_SOFTSIM_APDU, 2, apdu, 4);
	wait_for_completions(2);

	/* apdu_ok() fills 2 bytes of 0x90 and returns 2. */
	zassert_equal(nrf_modem_softsim_res_fake.arg3_val, 2,
		      "APDU response length was not passed through");
	out = nrf_modem_softsim_res_fake.arg2_val;
	zassert_not_null(out, "APDU must answer with the response buffer");
	zassert_equal(out[0], 0x90, "APDU response byte 0 differs");
	zassert_equal(out[1], 0x90, "APDU response byte 1 differs");

	/* DEINIT has nothing to say and must not point the modem at the buffer. */
	submit(NRF_MODEM_SOFTSIM_DEINIT, 3, NULL, 0);
	wait_for_completions(3);
	zassert_is_null(nrf_modem_softsim_res_fake.arg2_val, "DEINIT answered with a payload");
	zassert_equal(nrf_modem_softsim_res_fake.arg3_val, 0, "DEINIT answered with a length");
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

/*
 * The modem hands requests to an ISR-side callback that queues them, and the work
 * queue drains the FIFO in a loop. A burst that arrives while the drain loop is
 * finishing is the classic lost-wakeup shape, and the cycle test above never
 * queues more than two at a time. Submit a deep burst without waiting in
 * between: every request must still be answered exactly once.
 */
ZTEST(softsim_handler, test_a_deep_burst_is_drained_completely)
{
	const int burst = 32;

	submit(NRF_MODEM_SOFTSIM_INIT, 0, NULL, 0);
	wait_for_completions(1);

	for (int i = 0; i < burst; i++) {
		uint8_t *apdu = k_malloc(4);

		zassert_not_null(apdu, "heap exhausted at request %d", i);
		memcpy(apdu, "\x00\xa4\x00\x0c", 4);
		submit(NRF_MODEM_SOFTSIM_APDU, (uint16_t)(i + 1), apdu, 4);
	}

	wait_for_completions(burst + 1);
	zassert_equal(nrf_modem_softsim_err_fake.call_count, 0, "a queued request errored");
	zassert_equal(nrf_modem_softsim_data_free_fake.call_count, burst,
		      "every payload in the burst must be released");
}

/* --- the NULL-context orderings -------------------------------------------- */

/*
 * Known defect. The command enum has a gap at value 2 (INIT=1, APDU=3,
 * DEINIT=4, RESET=5), and the handler's default case answers nothing at all --
 * no response, no error -- so the modem is left waiting on a req_id that will
 * never come back. Any command the modem gains in a future firmware lands here.
 * Expected to fail until the default case answers with nrf_modem_softsim_err().
 */
ZTEST(softsim_handler, test_an_unknown_command_is_still_answered)
{
	submit((enum nrf_modem_softsim_cmd)2, 1, NULL, 0);

	for (int i = 0; i < 200 && completions() == 0; i++) {
		k_msleep(1);
	}

	zassert_equal(completions(), 1, "an unknown command left the modem without an answer");
}
ZTEST_EXPECT_FAIL(softsim_handler, test_an_unknown_command_is_still_answered);

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

/* ===========================================================================
 * nrf_softsim_provision(): the validation layer over the profile parser.
 *
 * The parser itself is upstream code with its own tests (onomondo-uicc
 * tests/utils); these cases pin the glue on top of it -- length bounds ahead
 * of the uint16_t cast, required-field rejection before anything is written
 * to the KMU, and that a legitimately zero-valued key survives: the OPc of
 * the GSMA TS.48 test profile is 32 hex-ASCII '0' characters, which must not
 * be mistaken for an absent tag.
 */

/* The TS.48 test profile the sample ships in overlay-static.conf:
 * IMSI 080910101032547698, ICCID 98001032547698103214, OPc = 32 x '0',
 * KI = KIC = KID = 000102030405060708090A0B0C0D0E0F. 190 chars, TLV-clean. */
static const char ts48_profile[] =
	"011208091010103254769802149800103254769810321403200000000000000000000000"
	"00000000000420000102030405060708090A0B0C0D0E0F0520000102030405060708090A"
	"0B0C0D0E0F0620000102030405060708090A0B0C0D0E0F";

static const uint8_t ts48_key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
				     0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

static uint8_t hexval(char c)
{
	return (c <= '9') ? (uint8_t)(c - '0') : (uint8_t)((c & 0x5f) - 'A' + 10);
}

/* Copy the TLV records of src into dst, skipping the record with the given
 * tag. Records are TAG(2 hex) | LEN(2 hex, counting DATA hex chars) | DATA.
 * Returns the new length; asserts src was a clean concatenation. */
static size_t drop_tag(const char *src, size_t len, uint8_t tag, char *dst)
{
	size_t out = 0;
	size_t pos = 0;

	while (pos + 4 <= len) {
		uint8_t rec_tag = (uint8_t)((hexval(src[pos]) << 4) | hexval(src[pos + 1]));
		size_t rec_len = 4 + ((hexval(src[pos + 2]) << 4) | hexval(src[pos + 3]));

		if (rec_tag != tag) {
			memcpy(&dst[out], &src[pos], rec_len);
			out += rec_len;
		}
		pos += rec_len;
	}
	zassert_equal(pos, len, "vector is not a clean TLV concatenation");
	zassert_true(out < len, "tag %02x not found in the vector", tag);

	return out;
}

static void provision_before(void *fixture)
{
	ARG_UNUSED(fixture);

	reset_all_fakes();
}

ZTEST_SUITE(softsim_provision, NULL, NULL, provision_before, NULL, NULL);

ZTEST(softsim_provision, test_valid_profile_provisions)
{
	uint8_t buf[sizeof(ts48_profile)];

	memcpy(buf, ts48_profile, sizeof(ts48_profile));

	zassert_ok(nrf_softsim_provision(buf, sizeof(ts48_profile) - 1));

	zassert_equal(port_provision_fake.call_count, 1);
	zassert_mem_equal(captured_profile._3F00_7ff0_6f07, "080910101032547698", IMSI_LEN);
	zassert_mem_equal(captured_profile._3F00_2FE2, "98001032547698103214", ICCID_LEN);

	/* The all-'0' OPc must arrive as hex-ASCII zeros -- and be accepted. */
	for (size_t i = 0; i < KEY_SIZE; i++) {
		zassert_equal(captured_profile._3F00_A001[KEY_SIZE + i], '0',
			      "OPc hex char %zu was mangled", i);
	}

	/* KI, KIC, KID reach the KMU as decoded bytes, in that order. */
	zassert_equal(key_setup.count, 3);
	zassert_equal(key_setup.ids[0], KEY_ID_KI);
	zassert_equal(key_setup.ids[1], KEY_ID_KIC);
	zassert_equal(key_setup.ids[2], KEY_ID_KID);
	for (int i = 0; i < 3; i++) {
		zassert_mem_equal(key_setup.keys[i], ts48_key, sizeof(ts48_key));
	}
}

ZTEST(softsim_provision, test_missing_required_field_is_rejected)
{
	/* IMSI, ICCID, OPC, KI, KIC, KID -- dropping any one of them must fail
	 * the whole provisioning, and fail it before any key hits the KMU. */
	const uint8_t required_tags[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
	char reduced[sizeof(ts48_profile)];

	for (size_t i = 0; i < ARRAY_SIZE(required_tags); i++) {
		size_t len =
			drop_tag(ts48_profile, sizeof(ts48_profile) - 1, required_tags[i], reduced);

		reset_all_fakes();
		zassert_true(nrf_softsim_provision((uint8_t *)reduced, len) != 0,
			     "profile without tag %02x was accepted", required_tags[i]);
		zassert_equal(port_provision_fake.call_count, 0,
			      "rejected profile (no tag %02x) still reached the filesystem",
			      required_tags[i]);
		zassert_equal(key_setup.count, 0,
			      "rejected profile (no tag %02x) still wrote KMU keys",
			      required_tags[i]);
	}
}

ZTEST(softsim_provision, test_undersized_input_is_rejected)
{
	/* Below 4 chars there is not even one TLV header. The parser's loop
	 * bound underflows for 0 and 1, reads out of bounds for 3, and returns
	 * success without parsing anything for 2 -- so the bound must hold in
	 * nrf_softsim_provision(), before the parser runs. */
	uint8_t buf[4] = {'0', '1', '1', '2'};

	for (size_t len = 0; len < 4; len++) {
		reset_all_fakes();
		zassert_true(nrf_softsim_provision(buf, len) != 0, "len %zu was accepted", len);
		zassert_equal(port_provision_fake.call_count, 0);
		zassert_equal(key_setup.count, 0);
	}
}

ZTEST(softsim_provision, test_oversized_input_is_rejected)
{
	/* The parser takes a uint16_t; a longer buffer must be rejected up
	 * front, not silently truncated into a "valid" prefix. The size is
	 * chosen so the truncated length (64) parses cleanly as unknown-tag
	 * records -- without the bound, this provisions an empty profile. */
	static uint8_t big[UINT16_MAX + 65];

	memset(big, '0', sizeof(big));

	zassert_true(nrf_softsim_provision(big, sizeof(big)) != 0);
	zassert_equal(port_provision_fake.call_count, 0);
	zassert_equal(key_setup.count, 0);
}

ZTEST(softsim_provision, test_malformed_tlv_is_rejected)
{
	/* A declared length that runs past the end of the input: once with no data
	 * at all after the header, once with a header whose data is present but
	 * two chars short. Both are the same parser check, from either side. */
	uint8_t overrun[] = {'0', '1', '9', '9'};
	uint8_t truncated_imsi[] = "011008091010103254";

	zassert_true(nrf_softsim_provision(overrun, sizeof(overrun)) != 0);
	zassert_true(nrf_softsim_provision(truncated_imsi, sizeof(truncated_imsi) - 1) != 0);
	zassert_equal(port_provision_fake.call_count, 0);
	zassert_equal(key_setup.count, 0);
}

/*
 * The per-tag length checks are a separate parser branch from the overrun check
 * above: the record is well formed and fits, but the declared length is wrong
 * for that particular tag. Every required tag has one, and until now none of
 * them ran -- a profile carrying a 16-char IMSI would have been copied into an
 * 18-byte field.
 */
ZTEST(softsim_provision, test_known_tag_with_the_wrong_length_is_rejected)
{
	const size_t imsi_rec = 4 + IMSI_LEN; /* the IMSI record heads the vector */
	char mutated[sizeof(ts48_profile)];
	size_t out;

	/* Re-declare the IMSI as two hex chars shorter and drop two data chars,
	 * so the buffer stays a clean TLV concatenation. */
	out = (size_t)snprintk(mutated, sizeof(mutated), "%02x%02x", 0x01, IMSI_LEN - 2);
	memcpy(&mutated[out], &ts48_profile[4], IMSI_LEN - 2);
	out += IMSI_LEN - 2;
	memcpy(&mutated[out], &ts48_profile[imsi_rec], sizeof(ts48_profile) - 1 - imsi_rec);
	out += sizeof(ts48_profile) - 1 - imsi_rec;

	zassert_true(nrf_softsim_provision((uint8_t *)mutated, out) != 0,
		     "an IMSI declared %d chars long was accepted", IMSI_LEN - 2);
	zassert_equal(port_provision_fake.call_count, 0);
	zassert_equal(key_setup.count, 0);
}

/* Copy the TS.48 vector into dst and append a trailing CRC32 record carrying
 * the given value. Returns the new length. dst must hold at least
 * sizeof(ts48_profile) + 12 chars. */
static size_t with_crc_record(uint32_t crc, char *dst)
{
	size_t len = sizeof(ts48_profile) - 1;

	memcpy(dst, ts48_profile, len);
	return len + (size_t)snprintk(&dst[len], 13, "%02x%02x%08x", CRC32_TAG, CRC32_LEN, crc);
}

/*
 * The optional trailing CRC32 record comes with the parser; its algorithm and
 * edge cases are pinned upstream (onomondo-uicc tests/utils). These two pin
 * the glue: a profile whose record matches provisions as usual, and a mismatch
 * fails the provisioning before anything reaches the KMU or the filesystem.
 * The record is built from ss_profile_crc32() itself, so the pair stays valid
 * if the algorithm ever changes -- what it pins is match vs. mismatch.
 */
ZTEST(softsim_provision, test_profile_with_matching_crc_provisions)
{
	char buf[sizeof(ts48_profile) + 12];
	size_t len = with_crc_record(ss_profile_crc32(ts48_profile, sizeof(ts48_profile) - 1), buf);

	zassert_ok(nrf_softsim_provision((uint8_t *)buf, len));
	zassert_equal(port_provision_fake.call_count, 1);
	zassert_equal(key_setup.count, 3);
}

ZTEST(softsim_provision, test_profile_with_bad_crc_is_rejected)
{
	char buf[sizeof(ts48_profile) + 12];
	size_t len =
		with_crc_record(ss_profile_crc32(ts48_profile, sizeof(ts48_profile) - 1) ^ 1, buf);

	zassert_true(nrf_softsim_provision((uint8_t *)buf, len) != 0,
		     "a profile with a mismatching CRC32 record was accepted");
	zassert_equal(port_provision_fake.call_count, 0);
	zassert_equal(key_setup.count, 0);
}
