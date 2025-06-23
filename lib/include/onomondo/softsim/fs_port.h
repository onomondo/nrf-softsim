#ifndef FS_PORT_H
#define FS_PORT_H

#include <stddef.h>

typedef void *port_FILE;

char 	*port_fgets(char *str, int n, port_FILE fp);
int 	port_fclose(port_FILE fp);
int 	port_fseek(port_FILE fp, long offset, int whence);
long 	port_ftell(port_FILE fp);
int	port_fputc(int c, port_FILE fp);
int	port_access(const char *path, int amode);
size_t	port_fwrite(const void *ptr, size_t size, size_t count, port_FILE fp);
int	port_remove(const char *path);

/**
 * @brief Creates a "directory" specified by the given path
 *
 * @param path The full path to the "directory" to be created
 * @param mode The mode in which the directory should be created (currently ignored)
 *
 * @return 0 on success
 *
 * @note This function is currently not implemented and always returns 0.
 */
int port_mkdir(const char *path, int mode); /* TODO: Evaluate if this is needed */

/**
 * @brief Removes a "directory" specified by the given path
 *
 * @param path The full path to the "directory" to be removed
 *
 * @return 0 on success
 *
 * @note This function is currently not implemented and always returns 0.
 */
int port_rmdir(const char *path); /* TODO: Evaluate if this is needed */

/**
 * @brief Provides an implementation of the standard C fopen function
 *
 * This function opens a file specified by the given path and returns a pointer
 * to a file-like object. The mode parameter is currently ignored.
 *
 * @param path The full path to the file to be opened
 * @param mode The mode in which the file should be opened (currently ignored)
 *
 * @return A pointer to a "file" represented internally by a struct cache_entry,
 *         or NULL if the operation fails
 */
port_FILE port_fopen(char *path, char *mode);

/**
 * @brief Reads data from the given file into the specified memory buffer
 *
 * This function provides a custom implementation of the standard C fread,
 * utilizing an internal cache for optimized performance.
 *
 * @param ptr Pointer to the destination memory buffer where the data will be stored
 * @param size Size of each element to be read, in bytes
 * @param nmemb Number of elements to be read
 * @param fp Pointer to the file from which data is to be read
 *
 * @return The total number of elements successfully read, which may be less than
 *         nmemb if an error occurs or the end of the file is reached
 */
size_t port_fread(void *ptr, size_t size, size_t nmemb, port_FILE fp);

/**
 * @brief Initialize SoftSIM filesystem
 */
int ss_init_fs(void);

/**
 * @brief Deinitialize SoftSIM filesystem
 */
int ss_deinit_fs(void);

#endif /* FS_PORT_H */
