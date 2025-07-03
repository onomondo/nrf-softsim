#ifndef _NRF_SOFTSIM_H
#define _NRF_SOFTSIM_H

#include <stddef.h>
#include <stdint.h>

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
 * @param profile String representing a SoftSIM profile. This encodes IMSI, ICCID, and necessary keys
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

#endif /* _NRF_SOFTSIM_H */
