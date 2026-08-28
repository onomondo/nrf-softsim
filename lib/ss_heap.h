/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _SS_HEAP_H_
#define _SS_HEAP_H_

#include <stdint.h>

#if defined(CONFIG_SOFTSIM_HEAP_STATS)
/* Logs the private heap when its peak grows. `ins` is the INS byte of the APDU
 * just handled, or 0 if the request was not an APDU. */
void ss_heap_log_stats(uint8_t ins);
#else
static inline void ss_heap_log_stats(uint8_t ins)
{
	(void)ins;
}
#endif

#endif /* _SS_HEAP_H_ */
