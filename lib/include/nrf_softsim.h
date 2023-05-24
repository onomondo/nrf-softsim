#ifndef _NRF_SOFTSIM_H_
#define _NRF_SOFTSIM_H_

/**
 * @brief Initialize the SoftSIM library and install handlers. Only use if
 * CONFIG_NRF_SOFTSIM_AUTO_INIT is not set.
 *
 *
 * @return 0 on success
 */
int nrf_softsim_init(void);

/**
 * @brief
 *
 * Provision a SoftSIM profile to protected storage.
 * len must be exactly 328 bytes long.
 *
 *
 * @param profile String representing a SoftSIM profile. This encodes IMSI,
 * ICCID and necessary keys.
 * @param len Length of profile passed
 * @return 0 on success
 */
int nrf_sofsim_provision(uint8_t *profile, size_t len);

/**
 * @brief Check if a SoftSIM profile is provisioned in protected storage.
 *
 *
 * @return 1 if provisioned, 0 if not
 */
int nrf_sofsim_check_provisioned(void);

#endif /* _NRF_SOFTSIM_H_ */
