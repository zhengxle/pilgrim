#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "pilgrim_utils.h"
#include "pilgrim_mem_hooks.h"
#include "pilgrim_addr_avl.h"
#include "dlmalloc-2.8.6.h"
#include "uthash.h"
#include "utlist.h"


AvlTree addr_tree;
AddrIdNode *addr_id_list;               // free list of addr ids
static bool hook_installed = false;
static int allocated_addr_id = 0;

pthread_mutex_t avl_lock = PTHREAD_MUTEX_INITIALIZER;


// Three public available function in .h
void install_mem_hooks() {
    hook_installed = true;
    addr_tree = NULL;
    addr_id_list = NULL;
}

void uninstall_mem_hooks() {
    hook_installed = false;
    avl_destroy(addr_tree);

    AddrIdNode *node, *tmp;
    DL_FOREACH_SAFE(addr_id_list, node, tmp) {
        DL_DELETE(addr_id_list, node);
        pilgrim_free(node, sizeof(AddrIdNode));
    }
}

// Symbolic representation of memory addresses
int* addr2id(const void* buffer) {
#ifndef MEMORY_POINTERS
    return &allocated_addr_id;
#endif
    return &allocated_addr_id;

    AvlTree avl_node = avl_search(addr_tree, (intptr_t) buffer);
    if(avl_node == AVL_EMPTY) {
        // Not found in addr_tree suggests that this buffer is not dynamically allocated
        // Maybe a stack buffer so we don't know excatly the size
        // We assume it as a 1 byte memory area.
        avl_node = avl_insert(&addr_tree, (intptr_t)buffer, 1, false);
    }

    // Two possible cases:
    // 1. New created avl_node
    // 2. Already created but never used by MPI call
    // In both cases, need to assign it a addr id node.
    if(avl_node->id_node == NULL) {
        if(addr_id_list == NULL) {
            // free id list is empty? create one
            avl_node->id_node = (AddrIdNode*) pilgrim_malloc(sizeof(AddrIdNode));
            avl_node->id_node->id = allocated_addr_id++;
        } else {
            // free id list is not empty, get the first one and remove it from list
            avl_node->id_node = addr_id_list;
            DL_DELETE(addr_id_list, avl_node->id_node);
        }
    }

    return &(avl_node->id_node->id);
}

/**
 * Below are Wrappers for intercepting memory management calls.
 */
#ifdef MEMORY_POINTERS
void* malloc(size_t size) {
    if(!hook_installed)
        return dlmalloc(size);

    void* ptr = dlmalloc(size);

    pthread_mutex_lock(&avl_lock);
    avl_insert(&addr_tree, (intptr_t)ptr, size, true);
    pthread_mutex_unlock(&avl_lock);

    return ptr;
}

void* calloc(size_t nitems, size_t size) {
    if(!hook_installed)
        return dlcalloc(nitems, size);

    void *ptr = dlcalloc(nitems, size);

    pthread_mutex_lock(&avl_lock);
    avl_insert(&addr_tree, (intptr_t)ptr, size*nitems, true);
    pthread_mutex_unlock(&avl_lock);

    return ptr;
}

void* realloc(void *ptr, size_t size) {

    if(!hook_installed)
        return dlrealloc(ptr, size);

    void *new_ptr = dlrealloc(ptr, size);

    pthread_mutex_lock(&avl_lock);
    if(new_ptr == ptr) {
        AvlTree t = avl_search(addr_tree, (intptr_t)ptr);
        if(t != AVL_EMPTY) {
            t->size = size;
        }
    } else {
        avl_delete(&addr_tree, (intptr_t)ptr);
        avl_insert(&addr_tree, (intptr_t)new_ptr, size, true);
    }
    pthread_mutex_unlock(&avl_lock);

    return new_ptr;
}

// Note that do not use printf() inside this funciton
// as printf itself may allocate memory
void free(void *ptr) {
    if(!hook_installed) {
        dlfree(ptr);
        return;
    }

    pthread_mutex_lock(&avl_lock);
    AvlTree avl_node = avl_search(addr_tree, (intptr_t)ptr);

    if(AVL_EMPTY == avl_node) {
        if(ptr != NULL) {
            // TODO: potential memory leak. why
        }
    } else {
        if(avl_node->id_node)
            DL_APPEND(addr_id_list, avl_node->id_node);
        bool heap = avl_node->heap && (avl_node->addr==(intptr_t)ptr);
        avl_delete(&addr_tree, (intptr_t)ptr);
        if(heap)
            dlfree(ptr);
    }
    pthread_mutex_unlock(&avl_lock);
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    return dlposix_memalign(memptr, alignment, size);
}

void *valloc(size_t size) {
    return dlvalloc(size);
}

void *memalign(size_t alignment, size_t size) {
    return dlmemalign(alignment, size);
}

void *pvalloc(size_t size) {
    return dlpvalloc(size);
}

struct mallinfo mallinfo(void) {
    return dlmallinfo();
}

int malloc_trim(size_t pad) {
    return dlmalloc_trim(pad);
}

int mallopt(int param, int value) {
    return dlmallopt(param, value);
}

#endif
