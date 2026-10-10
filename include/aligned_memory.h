#ifndef ALIGNED_MEMORY_H
#define ALIGNED_MEMORY_H

#include <stddef.h>

#define CACHE_LINE_SIZE 64

void* aligned_malloc(size_t size, size_t alignment);
void aligned_free(void* ptr);

#endif // ALIGNED_MEMORY_H
