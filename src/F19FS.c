#include "dyn_array.h"
#include "bitmap.h"
#include "block_store.h"
#include "inode.h"
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
};

F19FS_t *fs_format(const char *path) {
    if (!path || strlen(path) == 0) {
        return NULL;
    }
    F19FS_t* fs = (F19FS_t*)malloc(sizeof(F19FS_t));
    // create reagular blocks and block_bitmap
    fs->bs_root = block_store_create(path);

    // reserve for matadata
    for(size_t i = 0; i < BOLCK_STORE_NUM_MATADATA_BLOCK; i++) {
        block_store_allocate(fs->bs_root);
    }

    // allcate for inode bitmap
    size_t inode_bitmap = block_store_allocate(fs->bs_root);
    
    // allcate for inode table block
    size_t inode_table = block_store_allocate(fs->bs_root);
    for (size_t i = 0; i < BLOCK_STORE_NUM_INODE_TABLE_BLOCK - 1; i++) {
        block_store_allocate(fs->bs_root);
    }

    void* inode_bitmap_start = (void*)fs->bs_root + inode_bitmap * BLOCK_SIZE_BYTES;
    void* inode_block_start = (void*)fs->bs_root + inode_table * BLOCK_SIZE_BYTES;
    block_store_t* inode_bs = inode_table_create(inode_bitmap_start, inode_block_start);
    fs->bs_inode = inode_bs;
    
    return fs;
}

F19FS_t *fs_mount(const char *path) {
    if (!path || strlen(path) == 0) {
        return NULL;
    }
    F19FS_t* fs = (F19FS_t*)malloc(sizeof(F19FS_t));
    fs->bs_root = block_store_open(path);
    size_t inode_bitmap = 0;
    size_t inode_table = 1;
    fs->bs_inode = inode_table_create((void*)fs->bs_root + inode_bitmap * BLOCK_SIZE_BYTES, 
                                        (void*)fs->bs_root + inode_table * BLOCK_SIZE_BYTES);
    
    return fs;
}

int fs_unmount(F19FS_t *fs) {
    if (!fs) {
        return -1;
    }
    inode_table_destory(fs->bs_inode);
    block_store_destroy(fs->bs_root);
    free(fs);
    return 0;
}

/*
int fs_create(F19FS_t *fs, const char *path, file_t type) {
    1. empty check
    2. check full block_bitmap
    3. check full inode_bitmap
    4. recursively get dir
    5. create inode and set bitmap
    6. add inode number to dir

}

int fs_open(F19FS_t *fs, const char *path) {
    1. empty check
    2. recursively get dir
    3. get inode number of given filename
    4. create file_descriptor of the block and set file_dir bitmap
    4. return file_descriptor id
}

int fs_close(F19FS_t *fs, int fd) {
    1. empty check
    2. get file descriptor
    3. free file descriptor
    4. unset file desscriptor bitmap
}

dyn_array_t *fs_get_dir(F19FS_t *fs, const char *path) {
    1. empty check
    2. create dyn_array
    3. recursively get dir
    4. get all file_records in this dir 
}
*/