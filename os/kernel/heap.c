/* First-fit free-list allocator with block coalescing. */
#include "heap.h"
#include "lib.h"
#include "cpu.h"

struct block {
    uint32_t size;          /* payload size in bytes */
    uint32_t free;
    struct block *next;
    uint32_t magic;
};

#define MAGIC 0xC0FFEE42
#define ALIGN(n) (((n) + 15) & ~15u)

static struct block *head;
static uint32_t heap_total;

void heap_init(uint32_t start, uint32_t end)
{
    start = ALIGN(start);
    head = (struct block *)start;
    head->size = end - start - sizeof(struct block);
    head->free = 1;
    head->next = NULL;
    head->magic = MAGIC;
    heap_total = end - start;
}

void *kmalloc(size_t n)
{
    n = ALIGN(n ? n : 1);
    for (struct block *b = head; b; b = b->next) {
        if (!b->free || b->size < n) continue;
        if (b->size >= n + sizeof(struct block) + 64) {
            struct block *nb = (struct block *)((uint8_t *)(b + 1) + n);
            nb->size = b->size - n - sizeof(struct block);
            nb->free = 1;
            nb->next = b->next;
            nb->magic = MAGIC;
            b->size = n;
            b->next = nb;
        }
        b->free = 0;
        return b + 1;
    }
    return NULL;
}

void *kcalloc(size_t n)
{
    void *p = kmalloc(n);
    if (p) memset(p, 0, n);
    return p;
}

void kfree(void *p)
{
    if (!p) return;
    struct block *b = (struct block *)p - 1;
    if (b->magic != MAGIC) panic("kfree: heap corruption");
    b->free = 1;
    /* coalesce every run of adjacent free blocks */
    for (struct block *c = head; c; c = c->next) {
        while (c->free && c->next && c->next->free) {
            c->size += sizeof(struct block) + c->next->size;
            c->next = c->next->next;
        }
    }
}

void *krealloc(void *p, size_t n)
{
    if (!p) return kmalloc(n);
    struct block *b = (struct block *)p - 1;
    if (b->size >= n) return p;
    void *np = kmalloc(n);
    if (!np) return NULL;
    memcpy(np, p, b->size);
    kfree(p);
    return np;
}

void heap_stats(uint32_t *total, uint32_t *used)
{
    uint32_t u = 0;
    for (struct block *b = head; b; b = b->next)
        if (!b->free) u += b->size + sizeof(struct block);
    *total = heap_total;
    *used = u;
}
