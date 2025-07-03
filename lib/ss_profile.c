#include <string.h>

#include <zephyr/kernel.h>

#include "ss_profile.h"

/* Forward declarations */
static uint8_t ss_hex_to_uint8(const char *hex);
static void ss_hex_string_to_bytes(const uint8_t *hex, size_t hex_len, uint8_t bytes[static hex_len / 2]);

/* See in ss_profile.h */
void decode_profile(size_t len, uint8_t data[static len], struct ss_profile *profile)
{
	*profile = (struct ss_profile){0};

	size_t pos = 0;
	size_t data_end = 0;
	size_t data_start = 0;

	/* For future compatibility we use TLV encoding for the profile
	 * I.e. TAG | LEN | DATA[LEN] || TAG | LEN | DATA[LEN] || TAG | LEN | DATA[LEN] || ...
	 *
	 * ASSERTions might be a bit aggressive but reasoning behind is that this absolutely
	 * cannot go wrong in production.
	 */

	while (pos < len - 2) {
		uint8_t tag = ss_hex_to_uint8((char *)&data[pos]);
		uint8_t data_len = ss_hex_to_uint8((char *)&data[pos + 2]);
		data_start = pos + 4;

		data_end = data_start + data_len;

		/* Advance to next tag */
		pos = data_end;

		/* Bad encoding */
		__ASSERT_NO_MSG(data_end <= len);

		switch (tag) {
		case ICCID_TAG:
			__ASSERT_NO_MSG(data_len == ICCID_LEN * 2);
			ss_hex_string_to_bytes(&data[data_start], data_len, profile->ICCID);
			break;
		case IMSI_TAG:
			__ASSERT_NO_MSG(data_len == IMSI_LEN * 2);
			ss_hex_string_to_bytes(&data[data_start], data_len, profile->IMSI);
			break;
		case OPC_TAG:
			__ASSERT_NO_MSG(data_len == KEY_SIZE * 2);
			ss_hex_string_to_bytes(&data[data_start], data_len, profile->OPC);
			break;
		case KI_TAG:
			__ASSERT_NO_MSG(data_len == KEY_SIZE * 2);
			ss_hex_string_to_bytes(&data[data_start], data_len, profile->K);
			break;
		case KIC_TAG:
			__ASSERT_NO_MSG(data_len == KEY_SIZE * 2);
			ss_hex_string_to_bytes(&data[data_start], data_len, profile->KIC);
			break;
		case KID_TAG:
			__ASSERT_NO_MSG(data_len == KEY_SIZE * 2);
			ss_hex_string_to_bytes(&data[data_start], data_len, profile->KID);
			break;
		case SMSP_TAG:
			__ASSERT_NO_MSG(data_len == SMSP_RECORD_SIZE * 2);
			ss_hex_string_to_bytes(&data[data_start], data_len, profile->SMSP);
			break;
		case END_TAG:
			/* End of profile */
			pos = len;
			break;
		default:
			/* Unknown tag, skip */
			break;
		}
	}

	/* For now OPC must live here */
	memcpy(&profile->A001[KEY_SIZE], profile->OPC, KEY_SIZE);

	/* When KMU is invoked we pick key based on the tag passed.
	 * As far as SoftSIM is concerned 0x010000...00 is the KI key.
	 * AES implementation uses this pseudokey to invoke the KMU with correct key
	 * slot.
	 * KI_TAG -> 0x04
	 */
	profile->A001[0] = 0x4;

	/* Structure (a004) [TAR[3] | MSL | KIC_IND | KID_IND | KIC[32] | KID[32] |
	 * "ffff..."]
	 */
	char *a004_header = "b00011060101";
	const size_t header_size = 12 / 2;
	const size_t record_size = header_size + KEY_SIZE + KEY_SIZE;
	ss_hex_string_to_bytes(a004_header, strlen(a004_header), profile->A004);

	/* Set rest to ffff... */
	memset(&profile->A004[record_size * 1], 0xFF, sizeof(profile->A004) - 1 * record_size);

	/* Set KIC KID tags */
	profile->A004[header_size] = KIC_TAG;

	/* Set KID TAG */
	profile->A004[header_size + KEY_SIZE] = KID_TAG;
}

uint8_t ss_hex_to_uint8(const char *hex)
{
	char hex_str[3] = {0};
	hex_str[0] = hex[0];
	hex_str[1] = hex[1];
	return (hex_str[0] % 32 + 9) % 25 * 16 + (hex_str[1] % 32 + 9) % 25;
}

void ss_hex_string_to_bytes(const uint8_t *hex, size_t hex_len, uint8_t bytes[static hex_len / 2])
{
	for (int i = 0; i < hex_len / 2; i++) {
		bytes[i] = ss_hex_to_uint8((char *)&hex[i * 2]);
	}
}
