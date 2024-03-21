#ifndef FS_PORT_H
#define FS_PORT_H

#include <stddef.h>

typedef void *port_FILE;

size_t 		port_fread(void *ptr, size_t size, size_t nmemb, port_FILE fp);
char		*port_fgets(char *str, int n, port_FILE fp);
int 		port_fclose(port_FILE);
port_FILE 	port_fopen(char *path, char *mode);
int 		port_fseek(port_FILE fp, long offset, int whence);
long 		port_ftell(port_FILE fp);
int 		port_fputc(int c, port_FILE);
int 		port_access(const char *path, int amode);
size_t 		port_fwrite(const void *prt, size_t size, size_t count, port_FILE f);
int 		port_mkdir(const char *, int);
int 		port_remove(const char *);
int 		port_rmdir(const char *);
int 		ss_init_fs();
int 		ss_deinit_fs();

#endif /* FS_PORT_H */
