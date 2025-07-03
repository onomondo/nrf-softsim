#ifndef _PROVISION_H_
#define _PROVISION_H_

#include <stdint.h>
#include <onomondo/softsim/list.h>

/**
 * @brief Generate the directory structure based on the content in the "DIR" file.
 *
 * The DIR file encodes ID (used to locate the actual file in flash) and the name
 * of the file.
 *
 * @param dirs Linked list to populate
 * @param blob Pointer to blob of data
 * @param size Size of blob
 */
void generate_dir_table_from_blob(struct ss_list* dirs, uint8_t* blob, size_t size);

#endif /* _PROVISION_H_ */
