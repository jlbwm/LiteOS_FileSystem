#include "dyn_array.h"
#include "bitmap.h"
#include "block_store.h"
#include "F19FS.h"

#define BLOCK_STORE_NUM_BLOCKS 65536   // 2^16 blocks.
#define BLOCK_STORE_AVAIL_BLOCKS 65528 // Last 8 blocks consumed by the FBM
#define BLOCK_SIZE_BITS 8192         // 2^10 BYTES per block *2^3 BITS per BYTES
#define BLOCK_SIZE_BYTES 1024         // 2^10 BYTES per block
#define BLOCK_STORE_NUM_BYTES (BLOCK_STORE_NUM_BLOCKS * BLOCK_SIZE_BYTES)  // 2^16 blocks of 2^10 bytes.

