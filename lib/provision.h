#ifndef _PROVISION_H_
#define _PROVISION_H_

#include <stdint.h>
#include <onomondo/softsim/list.h>

// TODO: rename - (fs_utils?)

/**
 * @brief TODO: move this function to a more appropriate place
 * It is used to generate the directory structure based on the content in the
 * "DIR" file. The DIR file encodes ID (used to locate actual file in flash) and
 * name of the file.
 *

 * @param dirs linked list to populate
 * @param blob pointer to blob of data
 * @param size size of blob
 */
void generate_dir_table_from_blob(struct ss_list* dirs, uint8_t* blob, size_t size);

#endif  // _PROVISION_H_
