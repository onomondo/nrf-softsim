/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _NRF_SOFTSIM_H
#define _NRF_SOFTSIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ss_profile;

/**
 * @brief Initialize the SoftSIM library and install handlers
 *
 * Brings up the SoftSIM filesystem, installs the modem request handler and
 * starts the SoftSIM work queue.
 *
 * With CONFIG_SOFTSIM_AUTO_INIT=y (the default) this runs from SYS_INIT at
 * APPLICATION level, before main(), and the modem is told to use the software
 * SIM from an NRF_MODEM_LIB_ON_INIT hook. Call this function only when that
 * option is disabled, and then the application owns both halves:
 *
 *   1. Call it before any other SoftSIM API -- nrf_softsim_provision() and
 *      nrf_softsim_check_provisioned() need the filesystem it initializes.
 *   2. Select the software SIM itself with AT%CSUS=2 after nrf_modem_lib_init(),
 *      because the hook that normally does so is compiled out.
 *
 * @return 0 on success
 */
int nrf_softsim_init(void);

/**
 * @brief Provision a SoftSIM profile to protected storage
 *
 * @param profile a SoftSIM profile string. This encodes IMSI, ICCID, and necessary keys
 * @param len Length of the profile passed
 *
 * @return 0 on success
 */
int nrf_softsim_provision(uint8_t *profile, size_t len);

/**
 * @brief Check if a SoftSIM profile is provisioned in protected storage.
 *
 * @return 1 if provisioned, 0 if not
 */
int nrf_softsim_check_provisioned(void);

#ifdef CONFIG_SOFTSIM_FACTORY_RESET_ON_PROVISION
/**
 * @brief Whether a profile was provisioned during this boot
 *
 * Returns nonzero only on the boot where nrf_softsim_provision() succeeded —
 * the static profile (via SYS_INIT) or a serial profile from the sample. Lets
 * the application run a one-shot modem factory reset right after provisioning.
 *
 * Compiled only when CONFIG_SOFTSIM_FACTORY_RESET_ON_PROVISION=y.
 *
 * @return true if provisioned this boot, false otherwise
 */
bool nrf_softsim_just_provisioned(void);

/**
 * @brief Factory-reset the modem (wipe its NVM)
 *
 * Issues AT+CFUN=4 and AT%XFACTORYRESET=0 so the modem boots clean with a new
 * SIM identity. Call after a fresh nrf_softsim_provision() and once the modem
 * library is initialised; reboot afterwards for the reset to take effect.
 *
 * Compiled only when CONFIG_SOFTSIM_FACTORY_RESET_ON_PROVISION=y.
 */
void nrf_softsim_modem_factory_reset(void);
#endif /* CONFIG_SOFTSIM_FACTORY_RESET_ON_PROVISION */

/**
 * @brief Initialize the SoftSIM filesystem
 *
 * Sets up the in-memory cache that backs the SoftSIM filesystem and primes it
 * from NVS-backed persistent storage.
 *
 * @return 0 on success, negative error code on failure
 */
int ss_init_fs(void);

/**
 * @brief Deinitialize the SoftSIM filesystem and commit pending writes
 *
 * Flushes any dirty cache entries to NVS, releases the cache memory, and
 * marks the filesystem as uninitialized.
 *
 * @return 0 on success
 */
int ss_deinit_fs(void);

/**
 * @brief Provision a parsed SoftSIM profile into NVS
 *
 * Internal helper used by nrf_softsim_provision after the encoded profile
 * string has been parsed. Writes the IMSI, ICCID, A001 and A004 records
 * from @p profile into persistent storage.
 *
 * External callers should use nrf_softsim_provision instead, which also
 * handles parsing and key installation in the KMU.
 *
 * @param profile The parsed profile containing the data to provision
 *
 * @return 0 on success, -1 on failure
 */
int port_provision(struct ss_profile *profile);

/**
 * @brief Check whether the persisted IMSI differs from the default sentinel
 *
 * Internal helper used by nrf_softsim_check_provisioned. Reads the IMSI
 * record from NVS and compares it against the default-uninitialized value.
 *
 * External callers should use nrf_softsim_check_provisioned instead, which
 * additionally verifies that the SoftSIM AES key is present in the KMU.
 *
 * @return 1 if a non-default IMSI is present, 0 otherwise
 */
int port_check_provisioned(void);

#endif /* _NRF_SOFTSIM_H */
