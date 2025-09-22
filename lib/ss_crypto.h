#ifndef SS_CRYPTO_H
#define SS_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#include <onomondo/softsim/log.h>

#define EINVAL        22
#define AES_BLOCKSIZE 16

/**
 * @brief Encryption algorithms supported by calc_cc
 */
enum enc_algorithm {
	NONE,
	TRIPLE_DES_CBC2,
	AES_CBC,
	AES_CMAC,
};

/**
 * @brief Key identifiers for KMU slots
 *
 * TODO: Make configurable
 */
enum key_identifier_base {
	KEY_ID_KI = 10,
	KEY_ID_KIC = 11,
	KEY_ID_KID = 12,
	KEY_ID_UNKNOWN = 13
};

/**
 * @brief Calculate cryptographic checksum (CC)
 *
 * @param cc Pointer to output buffer for CC
 * @param cc_len Length of the CC buffer
 * @param key Pointer to key data
 * @param key_len Length of the key data
 * @param alg Encryption algorithm to use
 * @param data1 Pointer to first data block
 * @param data1_len Length of the first data block
 * @param data2 Pointer to second data block
 * @param data2_len Length of the second data block
 *
 * @return int 0 on success, negative error code otherwise
 */
int ss_utils_ota_calc_cc(uint8_t *cc, size_t cc_len, uint8_t *key, size_t key_len, enum enc_algorithm alg,
			 uint8_t *data1, size_t data1_len, uint8_t *data2, size_t data2_len);

/**
 * @brief Perform in-place 3DES decryption
 *
 * @param buffer Pointer to buffer containing ciphertext
 * @param buffer_len Length of the buffer
 * @param key Pointer to 3DES key
 */
void ss_utils_3des_decrypt(uint8_t *buffer, size_t buffer_len, const uint8_t *key);

/**
 * @brief Perform in-place 3DES encryption
 *
 * @param buffer Pointer to buffer containing plaintext
 * @param buffer_len Length of the buffer
 * @param key Pointer to 3DES key
 */
void ss_utils_3des_encrypt(uint8_t *buffer, size_t buffer_len, const uint8_t *key);

/**
 * @brief Perform in-place AES decryption
 *
 * Using common UICC OTA settings (CBC mode, zero IV)
 *
 * @param buffer Pointer to buffer containing ciphertext
 * @param buffer_len Length of the buffer (must be a multiple of 16)
 * @param key Pointer to AES key
 * @param key_len Length of the AES key
 */
void ss_utils_aes_decrypt(uint8_t *buffer, size_t buffer_len, const uint8_t *key, size_t key_len);

/**
 * @brief Perform in-place AES encryption
 *
 * Using common UICC OTA settings (CBC mode, zero IV)
 *
 * @param buffer Pointer to buffer containing plaintext
 * @param buffer_len Length of the buffer (must be a multiple of 16)
 * @param key Pointer to AES key
 * @param key_len Length of the AES key
 */
void ss_utils_aes_encrypt(uint8_t *buffer, size_t buffer_len, const uint8_t *key, size_t key_len);

/**
 * @brief Setup key in KMU for encryption/decryption/CC calculation
 *
 * @param key Pointer to key data
 * @param key_len Length of key data
 * @param key_id Key identifier (resolves into a KMU slot)
 *
 * @return int 0 on success, negative error code otherwise
 */
int ss_utils_setup_key(size_t key_len, uint8_t key[static key_len], enum key_identifier_base key_id);

/**
 * @brief Check if a key exists in KMU
 *
 * Can be used to check SoftSIM provisioning status
 *
 * @param key_id Key identifier (resolves into a KMU slot)
 *
 * @return int 1 if key exists, 0 otherwise
 */
int ss_utils_check_key_existence(enum key_identifier_base key_id);

/**
 * @brief Perform AES-128 block encryption
 *
 * @param key Pointer to AES key
 * @param in Pointer to input plaintext block
 * @param out Pointer to output ciphertext block
 * 
 * @return int 0 on success, negative error code otherwise
 */
int aes_128_encrypt_block(const uint8_t *key, const uint8_t *in, uint8_t *out);

#endif /* SS_CRYPTO_H */
