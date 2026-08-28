/*
 * SPDX-FileCopyrightText: Copyright (c) 2023 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/mem_stats.h>

#include <onomondo/softsim/mem.h>

#include "ss_heap.h"

/* SoftSIM's own heap: the application cannot allocate from it, and SoftSIM does
 * not allocate from the application's. Wrapped like the kernel wraps the system
 * heap, so the Kconfig value means usable bytes. */
K_HEAP_DEFINE(softsim_port_heap, Z_HEAP_MIN_SIZE_FOR(CONFIG_SOFTSIM_HEAP_SIZE));

/**
 * @brief Custom allocator
 *
 * @param size Size of memory to allocate
 *
 * @return Pointer to allocated memory
 */
void *port_malloc(size_t size)
{
	/* K_NO_WAIT: reachable from the modem request handler. size ? size : 1
	 * keeps k_malloc's contract, which never passed a zero size on. */
	return k_heap_alloc(&softsim_port_heap, size ? size : 1, K_NO_WAIT);
}

/**
 * @brief Custom free
 *
 * @param ptr Pointer to memory to free
 */
void port_free(void *ptr)
{
	k_heap_free(&softsim_port_heap, ptr);
}

#if defined(CONFIG_SOFTSIM_HEAP_STATS)
/* Registered by nrf_softsim.c, which is always in the image when this is on. */
LOG_MODULE_DECLARE(softsim, CONFIG_SOFTSIM_NRF_LOG_LEVEL);

void ss_heap_log_stats(uint8_t ins)
{
	/* Only the SoftSIM work queue calls this, so the static needs no lock. */
	static size_t logged_peak;
	struct sys_memory_stats stats;

	sys_heap_runtime_stats_get(&softsim_port_heap.heap, &stats);

	if (stats.max_allocated_bytes <= logged_peak) {
		return;
	}
	logged_peak = stats.max_allocated_bytes;

	/* A high-water mark, not a largest-free-block: it does not show
	 * fragmentation. INS 0x00 means the peak grew outside an APDU. */
	LOG_INF("SoftSIM heap: new peak %zu of %d configured, INS 0x%02x (%zu used, %zu free)",
		stats.max_allocated_bytes, CONFIG_SOFTSIM_HEAP_SIZE, ins, stats.allocated_bytes,
		stats.free_bytes);
}
#endif
