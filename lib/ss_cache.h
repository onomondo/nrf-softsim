/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _F_CACHE_H_
#define _F_CACHE_H_

#include <stddef.h>
#include <stdint.h>

/* Flag masks for ss_dir_entry.flags, i.e. the HIGH byte of the DIR id
 * ((id & 0xFF00) >> 8): a mask here must fit uint8_t. FS_READ_ONLY
 * corresponds to raw-id bit 8. */
#define FS_READ_ONLY       (1U << 0) /* Never commit to NVS on close */
#define FS_COMMIT_ON_CLOSE (1U << 7) /* Commit changes to NVS on close */

/* How many files may hold a content buffer at once. */
#define SS_MAX_ENTRIES 10

/* One directory record per file. The path itself is not kept: lookups compare
 * a 32-bit FNV-1a hash of the path instead, which is what makes the entry 8
 * bytes rather than 28-plus-a-string. ss_dir_table_from_blob() refuses a table
 * with colliding hashes, so within a table a hash identifies exactly one file;
 * the residual risk is a path that is NOT in the table hashing onto one that
 * is (~n/2^32 per lookup), which would be served instead of failing. */
struct ss_dir_entry {
	uint32_t hash; /* FNV-1a of the path string */
	uint16_t key;  /* NVS key (all 16 bits, including the flag byte) */
	uint8_t flags; /* Derived from (key >> 8); mutable at runtime */
	uint8_t hits;  /* Open count, saturating; biases eviction */
};

/* One buffered file. A slot with buf == NULL is free and its other fields are
 * meaningless. A file handle (ss_FILE) is a pointer to its slot and does not
 * survive eviction of that file; the storage backend opens one file at a time
 * and closes it before the next open, so an open file is never evicted. */
struct ss_cache_slot {
	uint8_t *buf;     /* Cached content; NULL = slot free */
	uint16_t dir_idx; /* Owning entry in the directory table */
	uint16_t _p;      /* Local 'file' pointer (ftell, fseek, etc.) */
	uint16_t _l;      /* Local 'file' length */
	uint16_t _b_size; /* Memory allocated for buf */
	uint8_t _b_dirty; /* Buf is divergent from NVS */
};

/**
 * @brief Build the directory table from the "DIR" file content.
 *
 * Each blob record is [name_len | id_hi | id_lo | name[name_len]]. A record
 * that runs past the end of the blob ends the parse (truncated flash content
 * must not be read past).
 *
 * @param blob Pointer to blob of data
 * @param size Size of blob
 * @param out Receives the allocated table (NULL when the return is <= 0)
 *
 * @return Number of entries, or -1 on allocation failure or when two paths
 *         hash identically (the table would serve the wrong file; fail loudly)
 */
int ss_dir_table_from_blob(const uint8_t *blob, size_t size, struct ss_dir_entry **out);

/**
 * @brief Find a directory entry by path.
 *
 * @param dir Directory table
 * @param count Number of entries in the table
 * @param name Path to look up
 *
 * @return Index of the entry, or -1 if not found
 */
int ss_dir_find(const struct ss_dir_entry *dir, size_t count, const char *name);

/**
 * @brief Find the slot buffering a given file, if any.
 *
 * @param slots Slot table
 * @param count Number of slots
 * @param dir_idx Directory index of the file
 *
 * @return Index of the slot, or -1 if the file is not buffered
 */
int ss_slot_find(const struct ss_cache_slot *slots, size_t count, uint16_t dir_idx);

/**
 * @brief Pick the slot to load a file into.
 *
 * A free slot is returned first (the cache grows to its capacity before
 * anything is evicted). Once full, the victim preference is: clean with a
 * buffer already big enough for want_len and fewest hits, then clean with
 * fewest hits, then simply fewest hits. The caller writes a dirty victim
 * back and reuses or frees its buffer.
 *
 * @param dir Directory table (source of the per-file hit counts)
 * @param slots Slot table
 * @param count Number of slots
 * @param want_len Length of the file about to be loaded
 *
 * @return Index of the slot to use, or -1 when count is 0
 */
int ss_slot_acquire(const struct ss_dir_entry *dir, const struct ss_cache_slot *slots, size_t count,
		    size_t want_len);

#endif /* _F_CACHE_H_ */
