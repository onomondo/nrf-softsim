#ifndef _NRF_SOFTSIM_H
#define _NRF_SOFTSIM_H

#include <stddef.h>
#include <stdint.h>

struct ss_profile;

/**
 * @brief Initialize the SoftSIM library and install handlers
 *
 * Only use if CONFIG_NRF_SOFTSIM_AUTO_INIT is not set.
 *
 * This function initializes the SoftSIM module by calling the Onomondo
 * initialization function. It sets up the necessary context and prepares
 * the SoftSIM for use.
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
