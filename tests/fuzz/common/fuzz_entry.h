/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/* Implemented by each fuzz target: run one fuzz case. Called once per libFuzzer
 * input, on the OS main thread, with the raw mutated bytes. It must not abort on
 * inputs that only violate a documented precondition of the code under test --
 * guard those, so the fuzzer explores reachable states instead of the guard. */
void fuzz_one(const uint8_t *data, size_t len);

/* Optional one-time setup, run once on the OS main thread before the first case
 * (e.g. provision the filesystem). Weak no-op default in fuzz_entry.c. */
void fuzz_setup(void);
