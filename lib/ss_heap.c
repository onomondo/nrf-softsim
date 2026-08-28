/*
 * SPDX-FileCopyrightText: Copyright (c) 2023 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <zephyr/kernel.h>

#include <onomondo/softsim/mem.h>

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
