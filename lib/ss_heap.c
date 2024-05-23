#include <zephyr/kernel.h>
#include <onomondo/softsim/mem.h>

/**
 * @brief custom allocator
 */
void *port_malloc(size_t size)
{
    return k_malloc(size);
}

/**
 * @brief custom free
 */
void port_free(void * ptr)
{
    k_free(ptr);
}
