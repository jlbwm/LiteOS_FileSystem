#ifndef INODE_H__
#define INODE_H__

#ifdef __cplusplus
extern "C" 
{
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <block_store.h>

#define number_inodes 256
#define inode_size 64
#define BLOCK_STORE_NUM_INODE_BITMAP_BLOCK 1
#define BLOCK_STORE_NUM_INODE_TABLE_BLOCK 16

#ifndef BLOCK_STORE_STRUCT
typedef struct block_store block_store_t;
#endif

typedef struct inode {
    uint32_t entryBitmap;    // if it is directory inode, use 32bit bitmap denote the entry avalibility
    char owner[18];         // for alignment purpose only   

    // don't use file_t cause it oppuied 4 byte as an integer
    char fileType; // 'r' denotes regular file, 'd' denotes directory file

    size_t inodeNumber;
    size_t fileSize; // the unit is in byte	
    size_t linkCount; 

    // use 16bit address rather real pointer
    uint16_t directPointer[6];
    uint16_t indirectPointer;
    uint16_t doubleIndirectPointer;
}inode_t;

    ///
    /// Creates a new back_store file for inode at the specified location
    ///  and returns a back_store object linked to it
    /// \param bitmap_buffer a buffer to store the bitmp for the inode table
    /// \param block_buffer the blocks to store all the inode
    /// \return a pointer to the new object, NULL on error
    block_store_t* inode_table_create(void *const bitmap_buffer, void *const block_buffer);

    /// Destroys the provided block storage device
    /// This is an idempotent operation, so there is no return value
    /// \param bs BS device
    ///
    void inode_table_destory(block_store_t *const bs);


    /// Release the provided block given by the block_id
    /// \param bs BS device
    /// \param block_id the index of the bs->data_block array
    void inode_table_release(block_store_t *const bs, const size_t block_id);

    /// read the provided block given by the block_id to the given buffer
    /// \param bs BS device
    /// \param block_id the index of the bs->data_block array
    /// \param buffer the buffer store the read result
    /// \return Number of bytes read, 0 on error
    size_t inode_table_read(const block_store_t *const bs, const size_t block_id, void *buffer);
    
    /// write to the provided block given by the block_id to the given buffer
    /// \param bs BS device
    /// \param block_id the index of the bs->data_block array
    /// \param buffer the buffer to be writen from
    /// \return Number of bytes written, 0 on error
    size_t inode_table_write(block_store_t *const bs, const size_t block_id, const void *buffer);

#ifdef __cplusplus
}
#endif

#endif
