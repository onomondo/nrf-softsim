#include <zephyr/kernel.h>

#include <onomondo/softsim/mem.h>

/**
 * @brief Custom allocator
 *
 * @param size Size of memory to allocate
 *
 * @return Pointer to allocated memory
 */
void *port_malloc(size_t size)
{
	return k_malloc(size);
}

/**
 * @brief Custom free
 *
 * @param ptr Pointer to memory to free
 */
void port_free(void *ptr)
{
	k_free(ptr);
}
