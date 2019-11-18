#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/types.h>
#include "block_store.h"
#include "bitmap.h"
#include "inode.h"

#define number_inodes 256
#define inode_size 64
#define BLOCK_STORE_NUM_INODE_BITMAP_BLOCK 1
#define BLOCK_STORE_NUM_INODE_TABLE_BLOCK 16
#define BLOCK_STORE_NUM_BYTES 1024

struct block_store {
    int fd;
    uint8_t *data_blocks;
    bitmap_t *fbm;
};


block_store_t *inode_table_create(void *const bitmap_buffer, void *const block_buffer) {
    block_store_t* bs = (block_store_t*)malloc(sizeof(block_store_t));
    if (!bs) {
        return NULL;
    }
    bs->fbm = bitmap_overlay(number_inodes, bitmap_buffer);
    bs->data_blocks = block_buffer;       
    return bs;
}

void inode_table_destory(block_store_t *const bs) {
    if (!bs) {
        return;
    }
    // destory bitmap except data part. 
    // Don't destory bs->data_blocs. It's a shared memory by the root Block_Store
    bitmap_destroy(bs->fbm);
    free(bs);
}

void inode_table_release(block_store_t *const bs, const size_t block_id) {
    if (!bs || block_id > number_inodes) {
        return;
    }
    bool success = bitmap_test(bs->fbm, block_id);
    if (success) {
        bitmap_reset(bs->fbm, block_id);
    }
}

size_t inode_table_read(const block_store_t *const bs, const size_t block_id, void *buffer) {
    if (!bs || !buffer || block_id > number_inodes) {
        return 0;
    }
    memcpy(buffer, bs->data_blocks + block_id * inode_size, 64);
    return inode_size;
}

size_t inode_table_write(block_store_t *const bs, const size_t block_id, const void *buffer) {
    if (!bs || !buffer || block_id > number_inodes) {
        return 0;
    }
    memcpy(bs->data_blocks + block_id * inode_size, buffer, inode_size);
    return inode_size;
}




