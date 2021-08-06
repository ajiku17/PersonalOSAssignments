/*
 * mm_alloc.h
 *
 * A clone of the interface documented in "man 3 malloc".
 */

#pragma once

#include <stdlib.h>

struct block_metadata{
    size_t size;
    int free;
    struct block_metadata* next;
    struct block_metadata* prev;
};

void *mm_malloc(size_t size);
void *mm_realloc(void *ptr, size_t size);
void mm_free(void *ptr);
