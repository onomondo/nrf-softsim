#ifndef _SOFTIM_PROFILE_H_
#define _SOFTIM_PROFILE_H_

#include <stdint.h>
#include <stddef.h>

#define IMSI_LEN  (18 / 2)
#define A001_LEN  (66 / 2)
#define A004_LEN  (228 / 2)
#define ICCID_LEN (20 / 2)

#define IMSI_TAG  (0x01)
#define ICCID_TAG (0x02)
#define OPC_TAG   (0x03)
#define KI_TAG    (0x04)
#define KIC_TAG   (0x05)
#define KID_TAG   (0x06)
#define SMSP_TAG  (0x07)  // SFI '6F42' -> TODO figure out formatting
#define END_TAG   (0xFF)

#define KEY_SIZE          (32 / 2)
#define KMU_KEY_SIZE      (16)
#define SMSP_RECORD_SIZE  (52 / 2)

struct ss_profile {
  uint8_t ICCID[ICCID_LEN];        // ICCID formatted to be written directly to FS
  uint8_t IMSI[IMSI_LEN];          // IMSI formatted to be written directly to FS
  uint8_t OPC[KEY_SIZE];           // OPC cannot go to the kmu unfortunately.
  uint8_t K[KMU_KEY_SIZE];         // Kc - write to KMU
  uint8_t KIC[KMU_KEY_SIZE];       // OTA key - write to KMU
  uint8_t KID[KMU_KEY_SIZE];       // OTA key - write to KMU
  uint8_t A001[A001_LEN];          // 32 bytes KI, 32 bytes OPC, 2 byte indication
                                   // encoded as HEX
  uint8_t A004[A004_LEN];          // TODO determine precise encoding scheme used.
                                   // Contains multiple recodes in theory.
  uint8_t SMSP[SMSP_RECORD_SIZE];  // SMS related parameters.
};

/**
 * @brief
 *
 * @param len Len of profile
 * @param data Raw profile data
 * @param profile Memory to hold profile. Caller must allocate memory.
 */
void decode_profile(size_t len, uint8_t data[static len], struct ss_profile *profile);

#endif  // _SOFTIM_PROFILE_H_
