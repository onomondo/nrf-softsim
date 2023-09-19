#include <stdint.h>
#include <stddef.h>
#include <onomondo/softsim/log.h>
#include "crypto_port.h"
#include <psa/crypto.h>
#include <zephyr/logging/log.h>
#include "profile.h"
#include <onomondo/softsim/mem.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>
#include <string.h>
LOG_MODULE_REGISTER(softsim_crypto);

enum key_identifier_base key_id_to_kmu_slot(uint8_t key_id) {
  switch (key_id) {
    case KI_TAG:
      return KEY_ID_KI;
    case KID_TAG:
      return KEY_ID_KID;
    case KIC_TAG:
      return KEY_ID_KIC;
    default:
      return KEY_ID_UNKNOWN;
  }
}

#define SHARED_BUFFER_SIZE (256)
uint8_t shared_buffer[SHARED_BUFFER_SIZE];

#define ASSERT_STATUS(actual, expected)                                      \
  do {                                                                       \
    if ((actual) != (expected)) {                                            \
      LOG_ERR(                                                               \
          "\tassertion failed at %s:%d - "                                   \
          "actual:%d expected:%d",                                           \
          __FILE__, __LINE__, (psa_status_t)actual, (psa_status_t)expected); \
      goto exit;                                                             \
    }                                                                        \
  } while (0)

static psa_status_t cipher_operation(psa_cipher_operation_t *operation, const uint8_t *input, size_t input_size,
                                     size_t part_size, uint8_t *output, size_t output_size, size_t *output_len) {
  psa_status_t status;
  size_t bytes_to_write = 0, bytes_written = 0, len = 0;

  *output_len = 0;
  while (bytes_written != input_size) {
    bytes_to_write = (input_size - bytes_written > part_size ? part_size : input_size - bytes_written);

    status = psa_cipher_update(operation, input + bytes_written, bytes_to_write, output + *output_len,
                               output_size - *output_len, &len);
    ASSERT_STATUS(status, PSA_SUCCESS);

    bytes_written += bytes_to_write;
    *output_len += len;
  }

  status = psa_cipher_finish(operation, output + *output_len, output_size - *output_len, &len);
  ASSERT_STATUS(status, PSA_SUCCESS);
  *output_len += len;

exit:
  return (status);
}

int calc_cc(uint8_t *cc, size_t cc_len, uint8_t *key, size_t key_len, enum enc_algorithm alg, uint8_t *data1,
            size_t data1_len, uint8_t *data2, size_t data2_len) {
  psa_mac_operation_t operation = PSA_MAC_OPERATION_INIT;
  enum key_identifier_base slot_id = key_id_to_kmu_slot(key[0]);

  psa_key_handle_t key_handle;
  psa_status_t status;

  // ONLY SUPPORTS AES_CMAC on NRF9160 as KMU doesn't support 3DES CMAC
  if (alg != AES_CMAC) {
    return -EINVAL;
  }

  LOG_DBG("CMAC key resolved to: %d", slot_id);
  // has to be KID
  if (slot_id == KEY_ID_UNKNOWN || slot_id != KEY_ID_KID) {
    LOG_ERR("Unknown key id: %d", key[0]);
    return -EINVAL;
  }

  status = psa_open_key((psa_key_id_t)slot_id, &key_handle);

  if (status != PSA_SUCCESS) {
    LOG_ERR("psa_open_key failed! (Error: %d)", status);
    return -EINVAL;
  }

  assert(data1_len % AES_BLOCKSIZE == 0);

  psa_mac_operation_t mac_op;
  mac_op = psa_mac_operation_init();
  status = psa_mac_sign_setup(&mac_op, key_handle, PSA_ALG_CMAC);

  uint8_t mac_buf[16];
  size_t mac_len, stream_block_size = 16, bytes_processed = 0;

  // guarenteed to be multiple of 16
  while ((data1_len - bytes_processed) > stream_block_size) {
    status = psa_mac_update(&mac_op, data1 + bytes_processed, stream_block_size);
    bytes_processed += stream_block_size;

    ASSERT_STATUS(status, PSA_SUCCESS);
  }

  bytes_processed = 0;  // Streaming block 2
  while ((data2_len - bytes_processed) > stream_block_size) {
    status = psa_mac_update(&mac_op, data2 + bytes_processed, stream_block_size);
    bytes_processed += stream_block_size;

    ASSERT_STATUS(status, PSA_SUCCESS);
  }
  status = psa_mac_update(&mac_op, data2 + bytes_processed, data2_len - bytes_processed);
  ASSERT_STATUS(status, PSA_SUCCESS);

  status = psa_mac_sign_finish(&mac_op, mac_buf, sizeof(mac_buf), &mac_len);
  ASSERT_STATUS(status, PSA_SUCCESS);

  memcpy(cc, mac_buf, cc_len);
exit:
  psa_mac_abort(&operation);
  return status == PSA_SUCCESS ? 0 : -EINVAL;
}

// we have dropped 3DES support. AES is better in all aspects execpt support by other operators
// might be limited.
void ss_utils_3des_decrypt(uint8_t *buffer, size_t buffer_len, const uint8_t *key) { return; }
void ss_utils_3des_encrypt(uint8_t *buffer, size_t buffer_len, const uint8_t *key) { return; }

/*! Perform an in-place AES decryption with the common settings of OTA
 *  (CBC mode, zero IV).
 *  \param[inout] buffer user provided memory with plaintext to decrypt.
 *  \param[in] buffer_len length of the plaintext data to decrypt (multiple of
 * 16). \param[in] key AES key. \param[in] key_len length of the AES key. */
void ss_utils_aes_decrypt(uint8_t *buffer, size_t buffer_len, const uint8_t *key, size_t key_len) {
  enum key_identifier_base slot_id = key_id_to_kmu_slot(key[0]);
  psa_key_handle_t key_handle;
  psa_status_t status;

  LOG_DBG("AES decrypt: resolved to key id: %d", slot_id);

  if (slot_id == KEY_ID_UNKNOWN) {
    LOG_ERR("Unknown key id: %d", key[0]);
    return;
  }

  status = psa_open_key((psa_key_id_t)slot_id, &key_handle);

  if (status != PSA_SUCCESS) {
    LOG_ERR("ss_utils_aes_decrypt: psa_open_key failed! (Error: %d)", status);
    return;
  }

  psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
  uint8_t iv[AES_BLOCKSIZE] = {0};  // per standards in telco..
  // uint8_t *decrypted_buffer = SS_ALLOC_N(buffer_len);
  uint8_t *decrypted_buffer = shared_buffer;

  uint32_t out_len = 0;

  status = psa_cipher_decrypt_setup(&operation, slot_id, PSA_ALG_CBC_NO_PADDING);
  ASSERT_STATUS(status, PSA_SUCCESS);

  psa_cipher_set_iv(&operation, iv, AES_BLOCKSIZE);

  status =
      cipher_operation(&operation, buffer, buffer_len, AES_BLOCKSIZE, decrypted_buffer, SHARED_BUFFER_SIZE, &out_len);
  ASSERT_STATUS(status, PSA_SUCCESS);

  memcpy(buffer, decrypted_buffer, out_len);

exit:
  psa_cipher_abort(&operation);
  return;
}

/*! Perform an in-place AES encryption with the common settings of OTA
 *  (CBC mode, zero IV).
 *  \param[inout] buffer user provided memory with plaintext to encrypt.
 *  \param[in] buffer_len length of the plaintext data to encrypt (multiple of
 * 16). \param[in] key 16 byte AES key. \param[in] key_len length of the AES
 * key. */
void ss_utils_aes_encrypt(uint8_t *buffer, size_t buffer_len, const uint8_t *key, size_t key_len) {
  // Key derived from the fist byte of the KIC/KID key
  enum key_identifier_base slot_id = key_id_to_kmu_slot(key[0]);
  psa_key_handle_t key_handle;
  psa_status_t status;

  LOG_DBG("AES encrypt to key id: %d", slot_id);

  if (slot_id == KEY_ID_UNKNOWN) {
    printk("Unknown key id: %d", key[0]);
    return;
  }

  status = psa_open_key((psa_key_id_t)slot_id, &key_handle);

  if (status != PSA_SUCCESS) {
    LOG_ERR("ss_utils_aes_decrypt: psa_open_key failed! (Error: %d)", status);
    return;
  }

  psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
  uint8_t iv[AES_BLOCKSIZE] = {0};  // per standards in telco..
  // uint8_t *encrypted_buffer = SS_ALLOC_N(buffer_len);
  uint8_t *encrypted_buffer = shared_buffer;
  uint32_t out_len = 0;

  status = psa_cipher_encrypt_setup(&operation, key_handle, PSA_ALG_CBC_NO_PADDING);
  ASSERT_STATUS(status, PSA_SUCCESS);

  psa_cipher_set_iv(&operation, iv, AES_BLOCKSIZE);

  status =
      cipher_operation(&operation, buffer, buffer_len, AES_BLOCKSIZE, encrypted_buffer, SHARED_BUFFER_SIZE, &out_len);
  ASSERT_STATUS(status, PSA_SUCCESS);

  memcpy(buffer, encrypted_buffer, out_len);

exit:
  psa_cipher_abort(&operation);
  return;
}

int aes_128_encrypt_block(const uint8_t *key, const uint8_t *in, uint8_t *out) {
  uint8_t buffer_cpy[16];
  memcpy(buffer_cpy, in, 16);
  ss_utils_aes_encrypt(buffer_cpy, 16, key, 16);
  memcpy(out, buffer_cpy, 16);
  return 0;
}

int ss_utils_setup_key_helper(size_t key_len, uint8_t key[static key_len], int key_id, psa_key_usage_t usage_flags,
                              psa_algorithm_t alg, psa_key_type_t key_type) {
  psa_status_t status;
  psa_key_attributes_t key_attributes = PSA_KEY_ATTRIBUTES_INIT;
  psa_key_handle_t key_handle;

  psa_set_key_usage_flags(&key_attributes, usage_flags);
  psa_set_key_algorithm(&key_attributes, alg);
  psa_set_key_type(&key_attributes, key_type);
  psa_set_key_bits(&key_attributes, 128);
  psa_set_key_lifetime(&key_attributes, PSA_KEY_LIFETIME_PERSISTENT);
  psa_set_key_id(&key_attributes, (psa_key_id_t)key_id);
  status = psa_import_key(&key_attributes, key, key_len, &key_handle);

  if (status != PSA_SUCCESS) {
    LOG_ERR("Failed to import key, ERR: %d", status);
    psa_reset_key_attributes(&key_attributes);
    return -1;
  }

  psa_reset_key_attributes(&key_attributes);

  return 0;
}

int ss_utils_setup_key(size_t key_len, uint8_t key[static key_len], enum key_identifier_base key_id) {
  psa_status_t status;

  assert(key_len == 16);
  assert(key_id >= KEY_ID_KI && key_id <= KEY_ID_KID);
  LOG_DBG("Provisioning key %d...", key_id);

  status = psa_crypto_init();
  if (status != PSA_SUCCESS) {
    LOG_ERR("Failed to initialize PSA crypto library. %d", status);
    return -1;
  }

  switch (key_id) {
    case KEY_ID_KI:
      // KI only used for encryption
      ss_utils_setup_key_helper(key_len, key, key_id, PSA_KEY_USAGE_ENCRYPT, PSA_ALG_CBC_NO_PADDING, PSA_KEY_TYPE_AES);
      break;
    case KEY_ID_KIC:
      // KIC only used for encryption/decrypt AES operations
      ss_utils_setup_key_helper(key_len, key, key_id, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT,
                                PSA_ALG_CBC_NO_PADDING, PSA_KEY_TYPE_AES);

      break;
    case KEY_ID_KID:
      // KID only used for CMAC operations
      ss_utils_setup_key_helper(key_len, key, key_id, PSA_KEY_USAGE_SIGN_MESSAGE, PSA_ALG_CMAC, PSA_KEY_TYPE_AES);

      break;
    default:
      LOG_ERR("Unknown key id: %d", key_id);
      return -1;
  }
  return 0;
}

int ss_utils_check_key_existence(enum key_identifier_base key_id) {
  psa_status_t status;
  status = psa_open_key((psa_key_id_t)key_id, &(psa_key_handle_t){0});
  LOG_DBG("Check key existence - open key returned: %d\n", status);
  return status == PSA_SUCCESS;
}
