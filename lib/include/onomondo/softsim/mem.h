#ifndef MEM_H
#define MEM_H

#if CONFIG_CUSTOM_HEAP

void *port_malloc(size_t);
void port_free(void *);

#define SS_ALLOC(obj) port_malloc(sizeof(obj));
#define SS_ALLOC_N(n) port_malloc(n);
#define SS_FREE(obj) port_free(obj);

#else // default
#include <stdlib.h>

#define SS_ALLOC(obj) malloc(sizeof(obj));
#define SS_ALLOC_N(n) malloc(n);
#define SS_FREE(obj) free(obj)

#endif // CONFIG_CUSTOM_HEAP
#endif // MEM_H
