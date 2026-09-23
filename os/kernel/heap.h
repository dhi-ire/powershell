#ifndef HEAP_H
#define HEAP_H

#include "types.h"

void heap_init(uint32_t start, uint32_t end);
void *kmalloc(size_t n);
void *kcalloc(size_t n);
void *krealloc(void *p, size_t n);
void kfree(void *p);
void heap_stats(uint32_t *total, uint32_t *used);

#endif
