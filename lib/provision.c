
#include "provision.h"

#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/printk.h>
#include <onomondo/softsim/mem.h>
#include "f_cache.h"
char storage_path[] = "";


#ifdef BOOTSTRAP_TEST

/**
 * Represents stuff that is FLASHED in production
 */
struct bootstrap_entry {
  uint32_t id;
  char *name;
  char *content;
};

/**
 * This is the content of a template profile
 *
 * The entries are ordered by frequency of access
 *
 * i.e.
 * 3f00/a003 (pin states) is basically always accessed
 * 3f00.def (fcp of master file) etc
 *
 * The ID encodes special properties as well.
 */
const struct bootstrap_entry this_is_flashed_in_the_future[] = {
    {.id = 2,
     .name = "/3f00/a003",
     .content =
         "0003000a000131323334ffffffff31323334353637380003000a008131323334fffff"
         "fff31323334353637380103000a000a31323334ffffffff3132333435363738"},
    {.id = 3,
     .name = "/3f00.def",
     .content = "62108202782183023f008a01058b032f060f"},
    {.id = 4,
     .name = "/3f00/a003.def",
     .content = "6219820542210016038302a0038a01058b032f0606800200428800"},
    {.id = 5,
     .name = "/3f00/a100.def",
     .content = "6216820241218302a0028a01058b032f0606800201088800"},
    {.id = 6,
     .name = "/3f00/7ff0.def",
     .content = "62228202782183027ff08410a0000000871002ffffffff89070900008a0105"
                "8b032f060f"},
    {.id = 7,
     .name = "/3f00/7ff0/6f06.def",
     .content = "621a8205422100281083026f068a01058b036f0602800202808801b8"},
    {.id = 8,
     .name = "/3f00/7ff0/6f06",
     .content =
         "8001019000800102a406830101950108800100a40683010a950108fffffffffffffff"
         "fffffffffff8001019000800102a40683010a950108800100a40683010a950108ffff"
         "ffffffffffffffffffffff8001019000800100a40683010a950108fffffffffffffff"
         "fffffffffffffffffffffffffffffffff800101a406830101950108800102a4068301"
         "01950108800100a40683010a950108ffffffffffffff800101a406830101950108800"
         "102a40683010a950108800100a40683010a950108ffffffffffffff800101a4068301"
         "0a950108800102a40683010a950108800100a40683010a950108fffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "ffffffffffffffffffffffffffffffffffffff"},
    {.id = 9,
     .name = "/3f00/2f06.def",
     .content = "621a8205422100281083022f068a01058b032f060280020280880130"},
    {.id = 10,
     .name = "/3f00/2f06",
     .content =
         "8001019000800102a406830101950108800100a40683010a950108fffffffffffffff"
         "fffffffffff8001019000800102a40683010a950108800100a40683010a950108ffff"
         "ffffffffffffffffffffff8001019000800100a40683010a950108fffffffffffffff"
         "fffffffffffffffffffffffffffffffff800101a406830101950108800102a4068301"
         "01950108800100a40683010a950108ffffffffffffff800101a406830101950108800"
         "102a40683010a950108800100a40683010a950108ffffffffffffff800101a4068301"
         "0a950108800102a40683010a950108800100a40683010a950108fffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "ffffffffffffffffffffffffffffffffffffff"},
    {.id = 11,
     .name = "/3f00/7ff0/6f42.def",
     .content = "62198205422100340283026f428a01058b036f0602800200688800"},
    {.id = 12 | (FS_READ_ONLY << 8) | (FS_SENSITIVE_DATA << 8) |
           (FS_PROTECTED_STORAGE << 8),
     .name = "/3f00/a001",
     .content =
         "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f00"},
    {.id = 13 | (FS_READ_ONLY << 8) | (FS_SENSITIVE_DATA << 8) |
           (FS_PROTECTED_STORAGE << 8),
     .name = "/3f00/a004",
     .content = "b0001106030300112233445566778899aabbccddeeff0123456789abcdef01"
                "23456701234567ffffffffffffffffffffffffffffffffffffffffffffffff"
                "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                "ffffffffffffffffffffffffffffffffffffffffff"},
    {.id = 14 | (FS_PROTECTED_STORAGE << 8),
     .name = "/3f00/2fe2",
     .content = "00112233445566778899"},
    {.id = 15 | (FS_PROTECTED_STORAGE << 8),
     .name = "/3f00/7ff0/6f07",
     .content = "080910100000000010"},
    {.id = 16 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a100",
     .content = "0000000000000000"},
    {.id = 17 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a101",
     .content = "0000000000000000"},
    {.id = 18 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a102",
     .content = "0000000000000000"},
    {.id = 19 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a103",
     .content = "0000000000000000"},
    {.id = 20 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a104",
     .content = "0000000000000000"},
    {.id = 21 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a105",
     .content = "0000000000000000"},
    {.id = 22 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a106",
     .content = "0000000000000000"},
    {.id = 23 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a107",
     .content = "0000000000000000"},
    {.id = 24 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a108",
     .content = "0000000000000000"},
    {.id = 25 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a109",
     .content = "0000000000000000"},
    {.id = 26 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a10a",
     .content = "0000000000000000"},
    {.id = 27 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a10b",
     .content = "0000000000000000"},
    {.id = 28 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a10c",
     .content = "0000000000000000"},
    {.id = 29 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a10d",
     .content = "0000000000000000"},
    {.id = 30 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a10e",
     .content = "0000000000000000"},
    {.id = 31 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a10f",
     .content = "0000000000000000"},
    {.id = 32 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a110",
     .content = "0000000000000000"},
    {.id = 33 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a111",
     .content = "0000000000000000"},
    {.id = 34 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a112",
     .content = "0000000000000000"},
    {.id = 35 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a113",
     .content = "0000000000000000"},
    {.id = 36 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a114",
     .content = "0000000000000000"},
    {.id = 37 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a115",
     .content = "0000000000000000"},
    {.id = 38 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a116",
     .content = "0000000000000000"},
    {.id = 39 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a117",
     .content = "0000000000000000"},
    {.id = 40 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a118",
     .content = "0000000000000000"},
    {.id = 41 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a119",
     .content = "0000000000000000"},
    {.id = 42 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a11a",
     .content = "0000000000000000"},
    {.id = 43 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a11b",
     .content = "0000000000000000"},
    {.id = 44 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a11c",
     .content = "0000000000000000"},
    {.id = 45 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a11d",
     .content = "0000000000000000"},
    {.id = 46 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a11e",
     .content = "0000000000000000"},
    {.id = 47 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a11f",
     .content = "0000000000000000"},
    {.id = 48 | (FS_COMMIT_ON_CLOSE << 8),
     .name = "/3f00/a120",
     .content = "0000000010000000"},
    {.id = 49,
     .name = "/3f00/2f00",
     .content = "61194f10a0000000871002ffffffff890709000050055553696d31ffffffff"
                "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                "ffffffffffffffffffffffffffff"},
    {.id = 50,
     .name = "/3f00/2f00.def",
     .content = "621a8205422100260283022f008a01058b032f06028002004c8801f0"},
    {.id = 51, .name = "/3f00/2f05", .content = "ffffffffffffffffffff"},
    {.id = 52,
     .name = "/3f00/2f05.def",
     .content = "62178202412183022f058a01058b032f06018002000a880128"},
    {.id = 53, .name = "/3f00/2f08", .content = "3c05020000"},
    {.id = 54,
     .name = "/3f00/2f08.def",
     .content = "62178202412183022f088a01058b032f060280020005880140"},
    {.id = 55,
     .name = "/3f00/2fe2.def",
     .content = "62178202412183022fe28a01058b032f06038002000a880110"},
    {.id = 56,
     .name = "/3f00/5f100001",
     .content =
         "ffff2fe2ffffffff2f052f06ffff2f08fffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffff2f00ffff"},
    {.id = 57,
     .name = "/3f00/5f100001.def",
     .content = "62108205022100021f83045f10000180013e"},
    {.id = 58,
     .name = "/3f00/a001.def",
     .content = "6216820241218302a0018a01058b032f0606800200218800"},
    {.id = 59,
     .name = "/3f00/a004.def",
     .content = "6219820542210026038302a0048a01058b032f0606800200728800"},
    {.id = 60,
     .name = "/3f00/a005",
     .content =
         "b00011ffffff0000000000ffffffffffffffffffffffffffffffffffffffffffff"},
    {.id = 61,
     .name = "/3f00/a005.def",
     .content = "621982054221000b038302a0058a01058b032f0606800200218800"},
    {.id = 62,
     .name = "/3f00/a1df1d01",
     .content =
         "a0000000871002ffffffff89070900007ff0fffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "ffffffffffffffffffffffff"},
    {.id = 63,
     .name = "/3f00/a1df1d01.def",
     .content = "6211820502210012108304a1df1d0180020120"},
    {.id = 64,
     .name = "/3f00/7ff0/5f100001",
     .content =
         "6fb76f056fe3ffffffff6f786f076f086f09ffff6f7e6f736f7bffff6f5b6f5cffff6"
         "f31ffffffffffffffff6f066fe4ffffffffffffffffffffffffffff"},
    {.id = 65,
     .name = "/3f00/7ff0/5f100001.def",
     .content = "62108205022100021f83045f10000180013e"},
    {.id = 66, .name = "/3f00/7ff0/6f05", .content = "ffffffffffffffffffff"},
    {.id = 67,
     .name = "/3f00/7ff0/6f05.def",
     .content = "62178202412183026f058a01058b036f06018002000a880110"},
    {.id = 68,
     .name = "/3f00/7ff0/6f07.def",
     .content = "62178202412183026f078a01058b036f060580020009880138"},
    {.id = 69,
     .name = "/3f00/7ff0/6f08",
     .content =
         "07ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {.id = 70,
     .name = "/3f00/7ff0/6f08.def",
     .content = "62178202412183026f088a01058b036f060480020021880140"},
    {.id = 71,
     .name = "/3f00/7ff0/6f09",
     .content =
         "07ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {.id = 72,
     .name = "/3f00/7ff0/6f09.def",
     .content = "62178202412183026f098a01058b036f060480020021880148"},
    {.id = 73, .name = "/3f00/7ff0/6f31", .content = "05"},
    {.id = 74,
     .name = "/3f00/7ff0/6f31.def",
     .content = "62178202412183026f318a01058b036f060580020001880190"},
    {.id = 75,
     .name = "/3f00/7ff0/6f38",
     .content = "0008000c2100000000001000000000"},
    {.id = 76,
     .name = "/3f00/7ff0/6f38.def",
     .content = "62168202412183026f388a01058b036f06058002000f8800"},
    {.id = 77,
     .name = "/3f00/7ff0/6f42",
     .content = "ffffffffffffffffffffffffffffffffffffffffffffffffe5ffffffffffff"
                "ffffffffffff0791447779078484ffffffffff00a8ffffffffffffffffffff"
                "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                "ffffffffffffffffffffff"},
    {.id = 78, .name = "/3f00/7ff0/6f5b", .content = "f00000f00000"},
    {.id = 79,
     .name = "/3f00/7ff0/6f5b.def",
     .content = "62178202412183026f5b8a01058b036f060480020006880178"},
    {.id = 80, .name = "/3f00/7ff0/6f5c", .content = "ffffff"},
    {.id = 81,
     .name = "/3f00/7ff0/6f5c.def",
     .content = "62178202412183026f5c8a01058b036f060580020003880180"},
    {.id = 82,
     .name = "/3f00/7ff0/6f73",
     .content = "ffffffffffffffffff000000ff01"},
    {.id = 83,
     .name = "/3f00/7ff0/6f73.def",
     .content = "62178202412183026f738a01058b036f06048002000e880160"},
    {.id = 84, .name = "/3f00/7ff0/6f78", .content = "03ff"},
    {.id = 85,
     .name = "/3f00/7ff0/6f78.def",
     .content = "62178202412183026f788a01058b036f060580020002880130"},
    {.id = 86,
     .name = "/3f00/7ff0/6f7b",
     .content = "ffffffffffffffffffffffff"},
    {.id = 87,
     .name = "/3f00/7ff0/6f7b.def",
     .content = "62178202412183026f7b8a01058b036f06048002000c880168"},
    {.id = 88, .name = "/3f00/7ff0/6f7e", .content = "ffffffffffffff0000ff01"},
    {.id = 89,
     .name = "/3f00/7ff0/6f7e.def",
     .content = "62178202412183026f7e8a01058b036f06048002000b880158"},
    {.id = 90, .name = "/3f00/7ff0/6fad", .content = "01000803"},
    {.id = 91,
     .name = "/3f00/7ff0/6fad.def",
     .content = "62168202412183026fad8a01058b036f0602800200048800"},
    {.id = 92,
     .name = "/3f00/7ff0/6fb7",
     .content = "ffffffffffffffffffffffffffffff00ffffffffffffffffffffffffffffff"
                "00ffffffffffffffffffffffffffffff00ffffffffffffffffffffffffffff"
                "ff00ffffffffffffffffffffffffffffff00"},
    {.id = 93,
     .name = "/3f00/7ff0/6fb7.def",
     .content = "621a8205422100100583026fb78a01058b036f060280020050880108"},
    {.id = 94,
     .name = "/3f00/7ff0/6fc4",
     .content =
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
         "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {.id = 95,
     .name = "/3f00/7ff0/6fc4.def",
     .content = "62168202412183026fc48a01058b036f0604800200408800"},
    {.id = 96,
     .name = "/3f00/7ff0/6fe3",
     .content = "ffffffffffffffffffffffffffffff000001"},
    {.id = 97,
     .name = "/3f00/7ff0/6fe3.def",
     .content = "62178202412183026fe38a01058b036f060480020012880118"},
    {.id = 98,
     .name = "/3f00/7ff0/6fe4",
     .content = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
                "ffffffffffffffffffffffffffffffffffffffffffffff"},
    {.id = 99,
     .name = "/3f00/7ff0/6fe4.def",
     .content = "621a8205422100360183026fe48a01058b036f0604800200368801c0"}};

/**
 * @brief Generate a blob of data that can be used to bootstrap the filesystem.
 * This is only for DEMO purposes and to be able to 'pull' out a clean template
 * image :)
 * @param data sets the data pointer to point at allocated mem.
 * @param size sets the size of the data
 */

void generate_dir_blob(uint8_t **data, size_t *size) {
  size_t data_len = 0, len = 0;

  for (int i = 0; i < sizeof(this_is_flashed_in_the_future) /
                          sizeof(struct bootstrap_entry);
       i++) {
    data_len += (strlen(this_is_flashed_in_the_future[i].name) + 2 +
                 1); // store 1 byte for len, 2 bytes for id, n bytes for data
  }

  uint8_t *buf = SS_ALLOC_N(data_len);
  *data = buf;

  // repeat process
  for (int i = 0; i < sizeof(this_is_flashed_in_the_future) /
                          sizeof(struct bootstrap_entry);
       i++) {
    len = strlen(this_is_flashed_in_the_future[i].name);

    // memory layout
    // [len][id][ data  ]
    // [ 1 ][2 ][  len  ]
    *buf++ = len;
    *buf++ = (this_is_flashed_in_the_future[i].id) >> 8;
    *buf++ = (this_is_flashed_in_the_future[i].id) & 0xff;
    memcpy(buf, this_is_flashed_in_the_future[i].name, len);
    buf += len;
  }

  *size = data_len;
}



char *getFilePointer(const char *path) {

  size_t idx = 0;
  for (; idx < sizeof(this_is_flashed_in_the_future) /
                   sizeof(*this_is_flashed_in_the_future);
       idx++) {
    if (strcmp(this_is_flashed_in_the_future[idx].name, path) == 0) {
      return this_is_flashed_in_the_future[idx].content;
    }
  }

  return NULL;
}

#endif // BOOTSTRAP_TEST

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
void generate_dir_table_from_blob(struct ss_list *dirs, uint8_t *blob,
                                  size_t size) {
  size_t cursor = 0;
  while (cursor < size) {
    uint8_t len = blob[cursor++];
    uint16_t id = (blob[cursor] << 8) | blob[cursor + 1];

    cursor += 2;

    char *name = SS_ALLOC_N(len + 1);
    memcpy(name, &blob[cursor], len);
    name[len] = '\0';
    cursor += (len);

    struct cache_entry *entry = SS_ALLOC(struct cache_entry);
    memset(entry, 0, sizeof(struct cache_entry));

    entry->key = id;
    entry->name = name;
    entry->_flags = (id & 0xFF00) >> 8;
    entry->buf = NULL;

    ss_list_put(dirs, &entry->list);
  }
}
