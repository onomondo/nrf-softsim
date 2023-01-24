#pragma once

#include <stddef.h>
#include <fs/fs.h>
#include <fs/littlefs.h>
// wrapper for 'linux' style fs

typedef struct fs_file_t nrf_FILE;

nrf_FILE *nrf_fopen(const char *path, const char *mode);

int nrf_mkdir(const char *path, int mode);

int nrf_fclose(nrf_FILE *f);

char *nrf_fgets(char *str, int n, nrf_FILE *f);

int nrf_fseek(nrf_FILE *f, long int offset, int whence);

size_t nrf_fread(void *ptr, size_t size, size_t nbmemb, nrf_FILE *f);

long int nrf_ftell(nrf_FILE *f);

int nrf_fputc(int character, nrf_FILE *f);

int nrf_deinit();

size_t nrf_fwrite(const void *prt, size_t size, size_t count, nrf_FILE *f);

int nrf_remove(const char *path);

int nrf_access(const char *host_path, int accessType);