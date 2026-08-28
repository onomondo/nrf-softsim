/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _SS_HEAP_H_
#define _SS_HEAP_H_

#if defined(CONFIG_SOFTSIM_HEAP_STATS)
/* Logs how much of the private heap SoftSIM has used. */
void ss_heap_log_stats(void);
#else
static inline void ss_heap_log_stats(void)
{
}
#endif

#endif /* _SS_HEAP_H_ */
