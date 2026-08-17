/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Shared libFuzzer driver for the SoftSIM native_sim fuzz targets.
 *
 * This is the arch/posix fuzz sample (zephyr/samples/subsys/debug/fuzz) reduced
 * to a reusable driver. With CONFIG_ARCH_POSIX_LIBFUZZER=y the arch layer sets
 * NSI_NO_MAIN=1, so libFuzzer owns the process main() and calls
 * LLVMFuzzerTestOneInput() once per mutated input, "outside" the OS. We boot the
 * native simulator once and then hand each case into the running Zephyr instance
 * as a fake interrupt, so the code under test executes with the real scheduler
 * and heap around it -- coverage-guided, not corpus replay.
 *
 * Each target implements fuzz_one() (and optionally fuzz_setup()); nothing else
 * differs between targets.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/toolchain.h>
#include <irq_ctrl.h>

#include <onomondo/softsim/log.h>
#include "fuzz_entry.h"

#if !defined(CONFIG_BOARD_NATIVE_SIM)
#error "SoftSIM fuzz targets build only for native_sim (CONFIG_ARCH_POSIX_LIBFUZZER)."
#endif
#include <nsi_cpu_if.h>
#include <nsi_main_semipublic.h>

/* A free OS interrupt line, used only to carry fuzz cases from the libFuzzer
 * driver into the OS, and the simulated time we let the OS run per case. Both
 * are the upstream sample's defaults, hardcoded here to avoid a per-target
 * Kconfig -- nothing else in these apps registers an interrupt. */
#define FUZZ_IRQ   31
#define FUZZ_TICKS 2

/* The onomondo-uicc core logs through ss_logp(); its own log.c defines that
 * symbol only under the submodule's BUILD_TESTING, and firmware/tests supply
 * their own. A fuzzer wants silence -- this runs on every mutation -- so swallow
 * it. When a crash needs log context, rebuild a single reproducer against
 * ss_logp_zephyr.c, or replay it through the tests/apdu ztest. */
void ss_logp(uint32_t subsys, uint32_t level, const char *file, int line, const char *format, ...)
{
	ARG_UNUSED(subsys);
	ARG_UNUSED(level);
	ARG_UNUSED(file);
	ARG_UNUSED(line);
	ARG_UNUSED(format);
}

/* Weak no-op so targets that need no one-time setup can omit it. */
__attribute__((weak)) void fuzz_setup(void)
{
}

/* The fuzz case, handed from the libFuzzer driver to the OS thread. */
static const uint8_t *fuzz_buf;
static size_t fuzz_sz;

K_SEM_DEFINE(fuzz_sem, 0, K_SEM_MAX_LIMIT);

static void fuzz_isr(const void *arg)
{
	ARG_UNUSED(arg);
	k_sem_give(&fuzz_sem);
}

int main(void)
{
	fuzz_setup();

	IRQ_CONNECT(FUZZ_IRQ, 0, fuzz_isr, NULL, 0);
	irq_enable(FUZZ_IRQ);

	while (true) {
		k_sem_take(&fuzz_sem, K_FOREVER);
		fuzz_one(fuzz_buf, fuzz_sz);
	}

	return 0;
}

/* Entry point libFuzzer calls per case, in the host environment outside the OS:
 * boot the simulator once, then deliver each case as an interrupt and let the OS
 * run to quiescence before returning control to the fuzzer. */
NATIVE_SIMULATOR_IF int LLVMFuzzerTestOneInput(const uint8_t *data, size_t sz)
{
	static bool runner_initialized;

	if (!runner_initialized) {
		nsi_init(0, NULL);
		runner_initialized = true;
	}

	fuzz_buf = data;
	fuzz_sz = sz;

	hw_irq_ctrl_set_irq(FUZZ_IRQ);
	nsi_exec_for(k_ticks_to_us_ceil64(FUZZ_TICKS));

	return 0;
}
