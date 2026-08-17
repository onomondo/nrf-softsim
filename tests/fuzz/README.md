<!--
SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
SPDX-License-Identifier: GPL-3.0-only
-->

# SoftSIM fuzz targets

Coverage-guided [libFuzzer](https://llvm.org/docs/LibFuzzer.html) harnesses for
the untrusted-input surface of the SoftSIM stack: the bytes a SoftSIM cannot
choose — raw APDUs from the modem, BER/COMPREHENSION/TLV structures, and SMS-PP
OTA command packets.

They use Zephyr's built-in libfuzzer integration
(`CONFIG_ARCH_POSIX_LIBFUZZER`), which runs **only on `native_sim/native/64`
with a 64-bit clang** — not in QEMU, and not through Ztest. libFuzzer owns the
process `main()`; each mutated input is delivered into the running Zephyr
instance as a fake interrupt (the `common/fuzz_entry.c` scaffold, adapted from
`zephyr/samples/subsys/debug/fuzz`), so the code under test runs with the real
scheduler, heap, and — for two of the three targets — the real filesystem port
underneath it.

## Targets

| Target | Entry point | Filesystem | Heap | Sanitizers |
|---|---|---|---|---|
| `parse_apdu` | `ss_apdu_parse_exhaustive`, `ss_btlv/ctlv/tlv8_decode`, `ss_sms_hdr_decode`, `ss_profile_from_string` (first input byte selects which) | none | system (malloc) → real LSan | ASan, LSan, UBSan |
| `transact` | `ss_application_apdu_transact` | real nRF port (ss_fs.c on NVS + flash simulator), seeded from `lib/profile/template.bin` | port (k_malloc) | ASan, LSan |
| `sms_ota` | `ss_uicc_sms_rx` (SMS-PP → OTA remote command) | same as `transact` | port (k_malloc) | ASan, LSan |

`parse_apdu` is stateless and fast; `transact`/`sms_ota` boot the OS once and
exercise the storage layer, so their state accumulates across cases within a run
(see the `ponytail:` notes in their `src/main.c`).

## Run one locally

Needs the nRF Connect SDK workspace and a 64-bit clang. native_sim does **not**
run on macOS — use the `ghcr.io/nrfconnect/sdk-nrf-toolchain` image or Linux.

```sh
export ZEPHYR_TOOLCHAIN_VARIANT=llvm
export LLVM_TOOLCHAIN_PATH=/usr        # wherever bin/clang lives

west build -b native_sim/native/64 -d build \
    modules/lib/onomondo-softsim/tests/fuzz/parse_apdu \
    -- -DCONFIG_ASAN=y

# Fuzz for two minutes, seeded from the checked-in corpus. Point libFuzzer at a
# writable copy so the seeds are not mutated in place.
cp -a modules/lib/onomondo-softsim/tests/fuzz/parse_apdu/corpus work_corpus
./build/zephyr/zephyr.exe -max_total_time=120 work_corpus
```

Rising `cov:`/`ft:` and `NEW` lines mean it is exploring, not replaying. A crash
drops a `crash-<hash>` reproducer; replay it with
`./build/zephyr/zephyr.exe crash-<hash>`.

The logs are silenced for throughput (`ss_logp` is a no-op in `fuzz_entry.c`).
For log context on a crash, replay the single reproducer through the
`tests/apdu` ztest, or rebuild the harness linking `lib/ss_logp_zephyr.c`.

## CI

`.github/workflows/fuzz.yml`:

- **build** — compiles every harness under the llvm toolchain; a blocking gate
  on PRs touching `tests/fuzz/` or `lib/`.
- **fuzz** — timed runs on a nightly schedule (and `workflow_dispatch`), one per
  target, uploading any reproducer as an artifact. Deliberately **not** a PR
  gate: these harnesses are expected to surface real findings, which should not
  block unrelated work.

## Corpus

Seeds are generated from the checked-in golden APDU transcripts; regenerate with
`tests/fuzz/gen_corpus.py` (idempotent). New units libFuzzer discovers are the
minimisation product of a run — commit genuinely new coverage back into
`corpus/` to keep it useful, but not raw run output.
