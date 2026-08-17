/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Stateless-parser fuzz target: the onomondo-uicc decoders that turn untrusted
 * bytes into structures, with no filesystem or context around them. One binary
 * multiplexes them -- the first input byte selects the decoder, the rest is its
 * input -- so the target (and binary) count stays low while coverage guidance
 * still keeps each decoder's corpus separable. Split one out if its corpus ever
 * needs isolation.
 *
 * Each case copies its input into an exactly-sized heap block so AddressSanitizer
 * red-zones it and catches reads one byte past the declared length -- the whole
 * point of fuzzing a length-driven parser. The decoders allocate through the
 * system heap here, so LeakSanitizer sees any list they leak on an error path.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <onomondo/utils/ss_profile.h>

#include "apdu.h"
#include "btlv.h"
#include "ctlv.h"
#include "tlv8.h"
#include "sms.h"
#include "fuzz_entry.h"

enum {
	FUZZ_APDU_PARSE,
	FUZZ_BTLV,
	FUZZ_CTLV,
	FUZZ_TLV8,
	FUZZ_SMS_HDR,
	FUZZ_PROFILE,
	FUZZ_TARGET_COUNT,
};

void fuzz_one(const uint8_t *data, size_t len)
{
	if (len < 1)
		return;

	uint8_t selector = data[0] % FUZZ_TARGET_COUNT;
	const uint8_t *in = data + 1;
	size_t in_len = len - 1;

	/* Exactly-sized copy so an over-read past in_len is a heap-buffer-overflow
	 * ASan can pin, not a silent read into slack. */
	uint8_t *buf = malloc(in_len ? in_len : 1);
	if (!buf)
		return;
	if (in_len)
		memcpy(buf, in, in_len);

	switch (selector) {
	case FUZZ_APDU_PARSE:
		/* ss_apdu_parse_exhaustive asserts len >= APDU_HEADER_SIZE; guard it
		 * so we fuzz the parse logic, not the assert. apdu.cmd[256] sits in
		 * this stack frame, so an over-long extended Lc overflows it -> ASan. */
		if (in_len >= APDU_HEADER_SIZE) {
			struct ss_apdu apdu;

			memset(&apdu, 0, sizeof(apdu));
			ss_apdu_parse_exhaustive(&apdu, buf, in_len);
		}
		break;
	case FUZZ_BTLV: {
		/* NULL descr: fuzz the raw decode rather than a specific IE table. */
		struct ss_list *l = ss_btlv_decode(buf, in_len, NULL);

		if (l)
			ss_btlv_free(l);
		break;
	}
	case FUZZ_CTLV: {
		struct ss_list *l = ss_ctlv_decode(buf, in_len);

		if (l)
			ss_ctlv_free(l);
		break;
	}
	case FUZZ_TLV8: {
		struct ss_list *l = ss_tlv8_decode(buf, in_len);

		if (l)
			ss_tlv8_free(l);
		break;
	}
	case FUZZ_SMS_HDR: {
		struct ss_sm_hdr hdr;

		memset(&hdr, 0, sizeof(hdr));
		ss_sms_hdr_decode(&hdr, buf, in_len);
		break;
	}
	case FUZZ_PROFILE:
		/* Reached in production only through nrf_softsim_provision(), which
		 * rejects len < 4; match that so a len<4 underflow is not charged
		 * against an unreachable path. buf is unterminated and exactly in_len
		 * long, so any read past len is caught. */
		if (in_len >= 4) {
			struct ss_profile profile;
			uint16_t n = in_len > UINT16_MAX ? UINT16_MAX : (uint16_t)in_len;

			memset(&profile, 0, sizeof(profile));
			ss_profile_from_string(n, (const char *)buf, &profile);
		}
		break;
	}

	free(buf);
}
