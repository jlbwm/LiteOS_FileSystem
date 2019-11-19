#ifndef FD_H__
#define FD_H__

#ifdef __cplusplus
extern "C" 
{
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <block_store.h>

#ifndef BLOCK_STORE_STRUCT
typedef struct block_store block_store_t;
#endif

typedef struct fileDescriptor {
    uint8_t inodeNum;
    // usage, locate_order and locate_offset together locate the exact byte at which the cursor is 
    uint8_t usage; // inode pointer usage info. Only the lower 3 digits will be used. 1 for direct, 2 for indirect, 4 for dbindirect
    uint16_t locate_order; // serial number or index of the block within direct, indirect, or dbindirect range
    uint16_t locate_offset; // offset of the cursor within a block
}fd_t;
    ///
    /// Creates a new back_store file for file descriptor at the specified location
    ///  and returns a back_store object linked to it
    block_store_t* fd_table_create();

    /// Destroys the provided block storage device
    /// This is an idempotent operation, so there is no return value
    /// \param bs BS device
    ///
    void fd_table_destory(block_store_t *const bs);

    /// Release the provided block given by the block_id
    /// \param bs BS device
    /// \param block_id the index of the bs->data_block array
    void fd_table_release(block_store_t *const bs, const size_t block_id);

    /// read the provided block given by the block_id to the given buffer
    /// \param bs BS device
    /// \param block_id the index of the bs->data_block array
    /// \param buffer the buffer store the read result
    size_t fd_table_read(const block_store_t *const bs, const size_t block_id, void *buffer);

    /// write to the provided block given by the block_id to the given buffer
    /// \param bs BS device
    /// \param block_id the index of the bs->data_block array
    /// \param buffer the buffer to be writen from
    size_t fd_table_write(const block_store_t *const bs, const size_t block_id, void *buffer);


    bool fd_table_test(const block_store_t* const bs, const size_t fd_id);



#ifdef __cplusplus
}
#endif

#endif





