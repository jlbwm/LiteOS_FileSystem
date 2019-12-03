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
#include <file_descriptor.h>

#define NUM_FD 256
#define SIZE_FD 6

struct block_store {
    int fd;
    uint8_t *data_blocks;
    bitmap_t *fbm;
};

block_store_t *fd_table_create() {
    block_store_t* bs = (block_store_t*)malloc(sizeof(block_store_t));
    if (!bs) {
        return NULL;
    }
    bs->data_blocks = (uint8_t*)calloc(NUM_FD, SIZE_FD);
    bs->fbm = bitmap_create(NUM_FD);  
    return bs;
}

void fd_table_destory(block_store_t *const bs) {
    if (!bs) {
        return;
    }
    bitmap_destroy(bs->fbm);
	free(bs->data_blocks);
	free(bs);
}

void fd_table_release(block_store_t *const bs, const size_t block_id) {
    // reset the bitmap, no need to wipe the block content
    if (!bs || block_id > NUM_FD) {
        return;
    }
    bool success = bitmap_test(bs->fbm, block_id);
    if (success) {
        bitmap_reset(bs->fbm, block_id);
    }
}

size_t fd_table_read(const block_store_t *const bs, const size_t block_id, void *buffer) {
    if (!bs || !buffer || block_id > NUM_FD) {
        return 0;
    }
    memcpy(buffer, bs->data_blocks+block_id * SIZE_FD, SIZE_FD);
    return SIZE_FD;
}

size_t fd_table_write(const block_store_t *const bs, const size_t block_id, void *buffer) {
    if (!bs || !buffer || block_id > NUM_FD) {
        return 0;
    }
    memcpy(bs->data_blocks+block_id * SIZE_FD, buffer, SIZE_FD);
    return SIZE_FD;
}

bool fd_table_test(const block_store_t* const bs, const size_t fd_id) {
    if (!bs || fd_id >= NUM_FD) {
        return false;
    }
    return bitmap_test(bs->fbm, fd_id);
}