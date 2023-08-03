#include <stdint.h>
#include <stddef.h>
#include <onomondo/softsim/log.h>

// argument to calc_cc
enum enc_algorithm {
  NONE,
  TRIPLE_DES_CBC2,
  AES_CBC,
  AES_CMAC,
};

// SLOTS TO KEEP KEYS IN :)
// TODO make configurable
enum key_identifier_base { KEY_ID_KI = 10, KEY_ID_KIC = 11, KEY_ID_KID = 12, KEY_ID_UNKNOWN = 13 };

#define AES_BLOCKSIZE 16
#define EINVAL 22
int calc_cc(uint8_t *cc, size_t cc_len, uint8_t *key, size_t key_len, enum enc_algorithm alg, uint8_t *data1,
            size_t data1_len, uint8_t *data2, size_t data2_len);

void ss_utils_3des_decrypt(uint8_t *buffer, size_t buffer_len, const uint8_t *key);

void ss_utils_3des_encrypt(uint8_t *buffer, size_t buffer_len, const uint8_t *key);

/*! Perform an in-place AES decryption with the common settings of OTA
 *  (CBC mode, zero IV).
 *  \param[inout] buffer user provided memory with plaintext to decrypt.
 *  \param[in] buffer_len length of the plaintext data to decrypt (multiple of
 * 16). \param[in] key AES key. \param[in] key_len length of the AES key. */
void ss_utils_aes_decrypt(uint8_t *buffer, size_t buffer_len, const uint8_t *key, size_t key_len);

/*! Perform an in-place AES encryption with the common settings of OTA
 *  (CBC mode, zero IV).
 *  \param[inout] buffer user provided memory with plaintext to encrypt.
 *  \param[in] buffer_len length of the plaintext data to encrypt (multiple of
 * 16). \param[in] key 16 byte AES key. \param[in] key_len length of the AES
 * key. */
void ss_utils_aes_encrypt(uint8_t *buffer, size_t buffer_len, const uint8_t *key, size_t key_len);

/**
 * @brief Setup key in KMU to be used for encryption/decryption/CC calculation
 *
 * @param key pointer to key data
 * @param key_len length of key data
 * @param key_id key identifier. Resolves into a slot in the KMU eventually.
 * This key id is derived during runtime by the content of the now unused
 * EF_A001 and EF_a004 file
 */

int ss_utils_setup_key(size_t key_len, uint8_t key[static key_len], enum key_identifier_base key_id);

/**
 * @brief check if a key exists in KMU or not. Use this to check for softsim
 * provisioning status.
 *
 *
 * @param key_id key identifier. Resolves into a slot in the KMU eventually.
 * @return int 1 if key exists, 0 if not
 */
int ss_utils_check_key_existence(enum key_identifier_base key_id);

int aes_128_encrypt_block(const uint8_t *key, const uint8_t *in, uint8_t *out);
