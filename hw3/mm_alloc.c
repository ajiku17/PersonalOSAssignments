/*
 * mm_alloc.c
 *
 * Stub implementations of the mm_* routines.
 */

#include "mm_alloc.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static struct block_metadata* head = NULL;
static struct block_metadata* tail = NULL;

#define BLOCK_SIZE sizeof(struct block_metadata)

static struct block_metadata* find_free_block(size_t size);
static void append(struct block_metadata* dest, struct block_metadata* src);
static void split_block(struct block_metadata* block, size_t alloc_size);

void *mm_malloc(size_t size) {
    /* YOUR CODE HERE */
    if(size <= 0){
        return NULL;
    }

    struct block_metadata* block = find_free_block(size);

    if (block == NULL){
        // need to allocate more memory
        struct block_metadata* new_block = sbrk(BLOCK_SIZE + size);
        if((void*)new_block == -1){
            return NULL;
        }

        if(!head){
            new_block->prev = NULL;
            head = new_block;
        }
        new_block->free = 0;
        new_block->size = size;
        if(tail){
            tail->next = new_block;
            new_block->prev = tail;
        }
        new_block->next = NULL;
        tail = new_block;

        return (char*)new_block + BLOCK_SIZE;
    }else{
        split_block(block, size);
        block->free = 0;

        return (char*)block + BLOCK_SIZE;
    }

    return NULL;
}

void *mm_realloc(void *ptr, size_t size) {
    /* YOUR CODE HERE */

    if(ptr != NULL){
        struct block_metadata* prev_block = (char*)ptr - BLOCK_SIZE;
        size_t size_to_copy = (size < prev_block->size) ? size : prev_block->size;
        char buffer[size_to_copy];
        memcpy(buffer, ptr, size_to_copy);

        mm_free(ptr);

        if(size <= 0){
            return NULL;
        }
        void* res = mm_malloc(size);

        memcpy(res, buffer, size_to_copy);

        return res;
    }else{
        void* res = mm_malloc(size);
        return res;
    }

    return NULL;
}

void mm_free(void *ptr) {
    /* YOUR CODE HERE */
    if(!ptr){
        return;
    }

    struct block_metadata* block = (char*)ptr - BLOCK_SIZE;

    block->free = 1;

    if(block->next && block->next->free){
        append(block, block->next);
    }

    if(block->prev && block->prev->free){
        append(block->prev, block);
    }
}

static struct block_metadata* find_free_block(size_t size){
    struct block_metadata* block = head;
    while(block != NULL && (block->free == 0 || block->size < size)){
        block = block->next;
    }
    
    return block;
}


static void append(struct block_metadata* dest, struct block_metadata* src){
    dest->next = src->next;
    if(src->next){
        src->next->prev = dest;
    }
    if(src == tail){
        tail = dest;
    }
    dest->size += (src->size + BLOCK_SIZE);
    memset(src, 0, BLOCK_SIZE + src->size);
}

static void split_block(struct block_metadata* block, size_t needed_size){
    if(block->size > needed_size + BLOCK_SIZE){
        // we need to split it
        struct block_metadata* new_free_block = (char*)block + BLOCK_SIZE + needed_size;
        
        
        new_free_block->free = 1;
        new_free_block->size = block->size - needed_size - BLOCK_SIZE;
        if(block == tail){
            tail = new_free_block;
        }
        new_free_block->next = block->next;
        new_free_block->prev = block;
        if(block->next){
           block->next->prev = new_free_block;
        }
        block->next = new_free_block;
        block->size = needed_size;
    }
}