#include "dyn_array.h"
#include "bitmap.h"
#include "block_store.h"
#include "F19FS.h"

#define BLOCK_STORE_NUM_BLOCKS 65536   // 2^16 blocks.
#define BLOCK_STORE_AVAIL_BLOCKS 65528 // Last 8 blocks consumed by the FBM
#define BLOCK_SIZE_BITS 8192         // 2^10 BYTES per block *2^3 BITS per BYTES
#define BLOCK_SIZE_BYTES 1024         // 2^10 BYTES per block
#define BLOCK_STORE_NUM_BYTES (BLOCK_STORE_NUM_BLOCKS * BLOCK_SIZE_BYTES)  // 2^16 blocks of 2^10 bytes.
#define BOLCK_STORE_NUM_MATADATA_BLOCK 17
#define BLOCK_STORE_NUM_INODE_BITMAP_BLOCK 1
#define BLOCK_STORE_NUM_INODE_TABLE_BLOCK 16

struct F19FS {
    // the start ptr of the entire block store
    block_store_t* bs_root;
    // the start ptr of the inode sub_block store
    block_store_t* bs_inode;
    // the start ptr of free_block part
    block_store_t* bs_data;
};

F19FS_t *fs_format(const char *path) {
    if (!path || strlen(path) == 0) {
        return NULL;
    }
    F19FS_t* fs = (F19FS_t*)malloc(sizeof(F19FS_t));
    fs->bs_root = block_store_create(path);

    // reserve for matadata
    for(size_t i = 0; i < BOLCK_STORE_NUM_MATADATA_BLOCK; i++) {
        block_store_allocate(fs->bs_root);
    }

    // reserve for inode block and its corresponding bitmap
    for(size_t i = 0; i < BLOCK_STORE_NUM_INODE_BITMAP_BLOCK + BLOCK_STORE_NUM_INODE_TABLE_BLOCK; i++) {
        block_store_allocate(fs->bs_root);
    }

    return fs;
}

F19FS_t *fs_mount(const char *path) {
    if (!path || strlen(path) == 0) {
        return NULL;
    }
    F19FS_t* fs = (F19FS_t*)malloc(sizeof(F19FS_t));
    fs->bs_root = block_store_open(path);
    return fs;
}

int fs_unmount(F19FS_t *fs) {
    if (!fs) {
        return -1;
    }
    block_store_destroy(fs->bs_root);
    free(fs);
    return 0;
}