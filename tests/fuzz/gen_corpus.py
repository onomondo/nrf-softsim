#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
# SPDX-License-Identifier: GPL-3.0-only
"""Generate seed corpora for the SoftSIM fuzz targets from known-good APDUs.

Good seeds are what make the first minutes of coverage-guided fuzzing productive:
they hand libFuzzer valid framing to mutate instead of making it rediscover the
wire format byte by byte. The APDUs are extracted straight from the checked-in
golden transcripts (no transcription), so they stay in sync. Idempotent.

    python3 tests/fuzz/gen_corpus.py
"""
import os
import re

FUZZ = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(FUZZ))

# Pull every pure-hex, even-length string literal out of the golden transcripts.
HEX_SOURCES = [
    os.path.join(REPO, "lib/onomondo-uicc/tests/app_transact/app_transact_test.c"),
    os.path.join(REPO, "tests/apdu/src/main.c"),
]
_LIT = re.compile(r'"([0-9a-fA-F]{8,})"')


def extract_hex(path):
    out = []
    with open(path) as f:
        for m in _LIT.finditer(f.read()):
            s = m.group(1)
            if len(s) % 2 == 0:
                try:
                    out.append(bytes.fromhex(s))
                except ValueError:
                    pass
    return out


# Data fields that are themselves TLV structures -- good seeds for the decoders.
TLVISH = [
    "a0348001078120ffffffffffffffffffffffff8204ffffffff8304ffffffff8401ff",  # BER-TLV
    "810301020082020078",                                                    # short IEs
    "62178202412183022fe2a5039504008a01058b032f0603800200008800",            # FCP-like
]

# A minimal SMS-DELIVER TPDU: enough framing for ss_sms_hdr_decode to accept and
# the fuzzer to grow an OTA command from -- not itself a valid OTA command.
SMS_DELIVER = "0405812143000004112233440612345678"


def write_dir(path, blobs):
    os.makedirs(path, exist_ok=True)
    seen = set()
    i = 0
    for blob in blobs:
        if blob in seen:
            continue
        seen.add(blob)
        with open(os.path.join(path, f"seed_{i:03d}.bin"), "wb") as f:
            f.write(blob)
        i += 1
    return i


def main():
    apdu_bytes = []
    for src in HEX_SOURCES:
        apdu_bytes += extract_hex(src)
    tlv_bytes = [bytes.fromhex(t) for t in TLVISH]
    sms_bytes = bytes.fromhex(SMS_DELIVER)

    n = write_dir(os.path.join(FUZZ, "transact", "corpus"), apdu_bytes)
    print(f"transact/corpus:   {n} seeds")

    # parse_apdu: first byte selects the parser (enum in parse_apdu/src/main.c):
    # 0 apdu, 1 btlv, 2 ctlv, 3 tlv8, 4 sms_hdr, 5 profile.
    parse_seeds = [b"\x00" + a for a in apdu_bytes]
    parse_seeds += [bytes([s]) + t for s in (1, 2, 3) for t in tlv_bytes]
    parse_seeds += [b"\x04" + sms_bytes]
    parse_seeds += [b"\x05" + b"01120123456789abcdef0123456789ab"]
    n = write_dir(os.path.join(FUZZ, "parse_apdu", "corpus"), parse_seeds)
    print(f"parse_apdu/corpus: {n} seeds")

    n = write_dir(os.path.join(FUZZ, "sms_ota", "corpus"), [sms_bytes])
    print(f"sms_ota/corpus:    {n} seeds")


if __name__ == "__main__":
    main()
