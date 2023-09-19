#include "profile.h"
#include <string.h>

uint8_t hex_to_uint8(const char *hex);

/**
 *  ASSERTions might be a bit agressive but reasoning behind is that this abosoletely
 * cannot go wrong in prod.
 */
void decode_profile(size_t len, uint8_t data[static len], struct ss_profile *profile) {
  *profile = (struct ss_profile){0};

  // It is stored as a string of hex values so NULL terminate it
  memset(profile->A001, '0', A001_LEN);

  // for future compatibility we use TLV encoding for the profile
  // I.e. TAG | LEN | DATA[LEN]

  size_t pos = 0;
  size_t data_end = 0;
  size_t data_start = 0;
  while (pos < len - 2) {
    uint8_t tag = hex_to_uint8((char *)&data[pos]);
    uint8_t data_len = hex_to_uint8((char *)&data[pos + 2]);
    data_start = pos + 4;

    data_end = data_start + data_len;
    pos = data_end;

    // bad encoding
    assert(data_end <= len);

    switch (tag) {
      case ICCID_TAG:
        assert(data_len == ICCID_LEN);
        memcpy(profile->ICCID, &data[data_start], data_len);
        break;
      case IMSI_TAG:
        assert(data_len == IMSI_LEN);
        memcpy(profile->IMSI, &data[data_start], data_len);
        break;
      case OPC_TAG:
        assert(data_len == KEY_SIZE);
        memcpy(profile->OPC, &data[data_start], data_len);
        break;
        // special care here as we need to load into KMU
      case KI_TAG:
        assert(data_len == KEY_SIZE);
        for (int i = 0; i < KMU_KEY_SIZE; i++) {
          profile->K[i] = hex_to_uint8((char *)&data[data_start + i * 2]);
        }
        break;
      case KIC_TAG:
        assert(data_len == KEY_SIZE);
        for (int i = 0; i < KMU_KEY_SIZE; i++) {
          profile->KIC[i] = hex_to_uint8((char *)&data[data_start + i * 2]);
        }
        break;
      case KID_TAG:
        assert(data_len == KEY_SIZE);
        for (int i = 0; i < KMU_KEY_SIZE; i++) {
          profile->KID[i] = hex_to_uint8((char *)&data[data_start + i * 2]);
        }
        break;
      case SMSP_TAG:
        assert(data_len == SMSP_RECORD_SIZE);
        // set SMSP file content
        memcpy(profile->SMSP, &data[data_start], data_len);
        break;
      case END_TAG:
        // end of profile
        pos = len;
        break;
      default:
        // unknown tag, skip
        break;
    }
  }
  // for now OPC must live here
  memcpy(&profile->A001[KEY_SIZE], profile->OPC, KEY_SIZE);
  // when KMU is invoked we pick key based on the tag passed.
  // As far as SoftSIM is concerned 0x010000...00  is the KI key. AES
  // implementation uses this pseudokey to invoke the KMU with correct key
  // slot.
  // KI_TAG -> 0x04
  profile->A001[0] = '0';
  profile->A001[1] = '4';

  // stucture (a004) [TAR[3] | MSL | KIC_IND | KID_IND | KIC[32] | KID[32] |
  // "ffff..."]
  char *a004_header = "b00011060303";
  const size_t header_size = 12;
  const size_t record_size = header_size + KEY_SIZE + KEY_SIZE;
  memcpy(profile->A004, a004_header, header_size);

  // set rest to ffff...
  memset(&profile->A004[record_size * 1], 'f', sizeof(profile->A004) - 1 * record_size);

  // set KIC KID tags
  profile->A004[header_size] = '0';
  profile->A004[header_size] = '5';

  // SET KID TAG
  profile->A004[header_size + KEY_SIZE] = '0';
  profile->A004[header_size + KEY_SIZE + 1] = '6';
}

uint8_t hex_to_uint8(const char *hex) {
  char hex_str[3] = {0};
  hex_str[0] = hex[0];
  hex_str[1] = hex[1];
  return (hex_str[0] % 32 + 9) % 25 * 16 + (hex_str[1] % 32 + 9) % 25;
}
