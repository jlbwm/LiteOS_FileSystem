#include <libgen.h>
#include <dyn_array.h>
#include <bitmap.h>
#include <block_store.h>
#include <inode.h>
#include <file_descriptor.h>
#include <F19FS.h>

#define BLOCK_STORE_NUM_BLOCKS 65536   // 2^16 blocks.
#define BLOCK_STORE_AVAIL_BLOCKS 65528 // Last 8 blocks consumed by the FBM
#define BLOCK_SIZE_BITS 8192         // 2^10 BYTES per block *2^3 BITS per BYTES
#define BLOCK_SIZE_BYTES 1024         // 2^10 BYTES per block
#define BLOCK_STORE_NUM_BYTES (BLOCK_STORE_NUM_BLOCKS * BLOCK_SIZE_BYTES)  // 2^16 blocks of 2^10 bytes.
#define BOLCK_STORE_NUM_MATADATA_BLOCK 17
#define BLOCK_STORE_NUM_INODE_BITMAP_BLOCK 1
#define BLOCK_STORE_NUM_INODE_TABLE_BLOCK 16
#define NUM_OF_ENTRIES 31
#define SIZE_OF_ENTRY_BYTE 33
#define NUM_DIRECT_PTR 6

struct F19FS {
    // the start ptr of the entire block store
    block_store_t* bs_root;
    // the start ptr of the inode sub_block store
    block_store_t* bs_inode;
    // the start ptr of the file descriptor sub_block store
    block_store_t * bs_fd;
};

struct entry {
    char filename[FS_FNAME_MAX];
    uint8_t inodeNumber;
};

struct directory_block {
    uint8_t matadata;
    entry_t entries[NUM_OF_ENTRIES];
};

// private funcitons

// check whether the given path is valid (e.g "/" or "path/" "/path/")
bool isValidPath(const char* path) {
    if (!path) {
        return false;
    }
    char first = *path;
    char last = path[strlen(path) -1];
    if (first != '/' || last == '/') {
        // valid path: "/root/src/abc.c"
        return false;
    }
    return true;
}

// Looping parentPath using strtok through to the closeest parent directory, return this directory inode ID
size_t getParentDirInodeID(F19FS_t* fs, char* parentPath) {
    char* currentDir = strtok(parentPath, "/");
    
    // Default as the root rectory inode ID (e.g in "/new_file" case, we just get "/")
    size_t cur_inode_ID = 0;
    inode_t cur_dir_inode;
	db_t cur_dir_block;

    while (currentDir) {
        // printf("%s\n", currentDir);
        if (inode_table_read(fs->bs_inode, cur_inode_ID, &cur_dir_inode) == 0) {
            printf("return 1\n");
            return SIZE_MAX;
        }
        if (cur_dir_inode.fileType == 'r') {
            printf("return 2\n");
            return SIZE_MAX;
        }
        if (block_store_read(fs->bs_root, cur_dir_inode.directPointer[0], &cur_dir_block) == 0) {
            printf("return 3\n");
            return SIZE_MAX;
        }
        bitmap_t* entry_bm = bitmap_overlay(NUM_OF_ENTRIES, &(cur_dir_inode.entryBitmap));
        bool isFound = false;
        for (size_t i = 0; i < NUM_OF_ENTRIES; i++) {
            // if current i entry is set and fileName equals to the given parentPath
            if (bitmap_test(entry_bm, i) && strncmp(cur_dir_block.entries[i].filename, currentDir, strlen(currentDir)) == 0) {
                inode_t next_inode;
                if (inode_table_read(fs->bs_inode, cur_dir_block.entries[i].inodeNumber, &next_inode) != 0 && next_inode.fileType == 'd') {
                    cur_inode_ID = next_inode.inodeNumber;
                    isFound = true;
                }
            }
        }
        bitmap_destroy(entry_bm);

        if (!isFound) {
            return SIZE_MAX;
        }
        currentDir = strtok(NULL, "/");
    }
    return cur_inode_ID;
}

bool checkFileExist(F19FS_t* fs, size_t parentDirInodeID, char* fileName) {
    
    inode_t parentDirInode;
    db_t parentDir;
    if (inode_table_read(fs->bs_inode, parentDirInodeID, &parentDirInode) == 0 || block_store_read(fs->bs_root, parentDirInode.directPointer[0], &parentDir) == 0) {
        return false;
    }
    bitmap_t* parentBM = bitmap_overlay(NUM_OF_ENTRIES, &(parentDirInode.entryBitmap));
    for (size_t i = 0; i < NUM_OF_ENTRIES; i++) {
        if (bitmap_test(parentBM, i) && strncmp(parentDir.entries[i].filename, fileName, strlen(fileName)) == 0) {
            bitmap_destroy(parentBM);
            return true;
        }
    }
    bitmap_destroy(parentBM);
    return false;
}

size_t getFileInodeID(F19FS_t* fs, size_t parentDirInodeID, char* fileName) {
    inode_t parentDirInode;
    db_t parentDir;
    if (inode_table_read(fs->bs_inode, parentDirInodeID, &parentDirInode) == 0 || block_store_read(fs->bs_root, parentDirInode.directPointer[0], &parentDir) == 0) {
        return 0;
    }
    bitmap_t* parentBM = bitmap_overlay(NUM_OF_ENTRIES, &(parentDirInode.entryBitmap));
    for (size_t i = 0; i < NUM_OF_ENTRIES; i++) {
        if (bitmap_test(parentBM, i) && strncmp(parentDir.entries[i].filename, fileName, strlen(fileName)) == 0) {
            bitmap_destroy(parentBM);
            return parentDir.entries[i].inodeNumber;
        }
    }
    bitmap_destroy(parentBM);
    return 0;

}


entry_t init_entry(){
	entry_t current;
	memset(current.filename, '\0', FS_FNAME_MAX);
    current.inodeNumber = 0x00;
	return current;
}

db_t init_db(){
	db_t current;
    current.matadata = 'm';
	for (size_t i = 0; i < NUM_OF_ENTRIES; i++){
        current.entries[i] = init_entry();
	}
	return current;
}

F19FS_t *fs_format(const char *path) {
    if (!path || strlen(path) == 0) {
        return NULL;
    }
    F19FS_t* fs = (F19FS_t*)calloc(1, sizeof(F19FS_t));
    // create reagular blocks and block_bitmap
    fs->bs_root = block_store_create(path);
    
    // reserve for matadata
    for(size_t i = 0; i < BOLCK_STORE_NUM_MATADATA_BLOCK; i++) {
        block_store_allocate(fs->bs_root);
    }

    // allcate for inode bitmap for 1 block
    size_t inode_bitmap = block_store_allocate(fs->bs_root);
    
    // allcate for inode table block for 16 block
    size_t inode_table = block_store_allocate(fs->bs_root);
    for (size_t i = 0; i < BLOCK_STORE_NUM_INODE_TABLE_BLOCK - 1; i++) {
        block_store_allocate(fs->bs_root);
    }

    void* inode_bitmap_start = block_store_get_data(fs->bs_root) + inode_bitmap * BLOCK_SIZE_BYTES;
    void* inode_block_start = block_store_get_data(fs->bs_root) + inode_table * BLOCK_SIZE_BYTES;
    block_store_t* inode_bs = inode_table_create(inode_bitmap_start, inode_block_start);
    fs->bs_inode = inode_bs;

    size_t first_free_blocks = block_store_allocate(fs->bs_root);

    // set first inode as our root directory
    uint8_t root_inode_ID = block_store_allocate(fs->bs_inode);
    // printf("root inodeID: %d\n", root_inode_ID);
    inode_t* root_inode = (inode_t *) calloc(1, sizeof(inode_t));
    root_inode->entryBitmap = 0x00000000;
    char* owner = "root";
    strncpy(root_inode->owner, owner, strlen(owner));
    root_inode->fileType = 'd';						
    root_inode->inodeNumber = root_inode_ID;
    root_inode->fileSize = BLOCK_SIZE_BYTES;
    root_inode->linkCount = 1;
    root_inode->directPointer[0] = first_free_blocks;
    inode_table_write(fs->bs_inode, root_inode_ID, root_inode);
    free(root_inode);

    fs->bs_fd = fd_table_create();

    return fs;
}

F19FS_t *fs_mount(const char *path) {
    if (!path || strlen(path) == 0) {
        return NULL;
    }
    F19FS_t* fs = (F19FS_t*)calloc(1, sizeof(F19FS_t));
    fs->bs_root = block_store_open(path);
    size_t inode_bitmap_id = BOLCK_STORE_NUM_MATADATA_BLOCK;
    size_t inode_table_id = inode_bitmap_id + 1;
    fs->bs_inode = inode_table_create(block_store_get_data(fs->bs_root) + inode_bitmap_id * BLOCK_SIZE_BYTES, 
                                        (block_store_get_data(fs->bs_root)) + inode_table_id * BLOCK_SIZE_BYTES);
    
    fs->bs_fd = fd_table_create();
    return fs;
}

int fs_unmount(F19FS_t *fs) {
    if (!fs) {
        return -1;
    }
    inode_table_destory(fs->bs_inode);
    block_store_destroy(fs->bs_root);
    fd_table_destory(fs->bs_fd);
    free(fs);
    return 0;
}

int fs_create(F19FS_t *fs, const char *path, file_t type) {
    if (!fs || !path || strlen(path) == 0 || !(type == FS_REGULAR || type == FS_DIRECTORY)) {
        return -1;
    }
    if (strlen(path) <= 1) {
        // "/" condition
        return -2;
    }
    if (block_store_get_free_blocks(fs->bs_inode) == 0) {
        // if no avaliable inode spot
        return -3;
    }
    if (!isValidPath(path)) {
        return -4;
    }
    char prefix[strlen(path) + 1];
    strcpy(prefix, path);
    char* dirPath = dirname(prefix);

    char suffix[strlen(path) + 1];
    strcpy(suffix, path);
    char* fileName = basename(suffix);
    
    if (strlen(fileName) >= FS_FNAME_MAX) {
        return -5;
    }

    size_t parentDirInodeID = getParentDirInodeID(fs, dirPath);
    if (parentDirInodeID == SIZE_MAX) {
        return -6;
    }
    if (checkFileExist(fs, parentDirInodeID, fileName)) {
        return -7;
    }
    inode_t parentDirInode;
    db_t parentDir;
    if (inode_table_read(fs->bs_inode, parentDirInodeID, &parentDirInode) == 0 || block_store_read(fs->bs_root, parentDirInode.directPointer[0], &parentDir) == 0) {
        return -8;
    }
    bitmap_t* entry_bm = bitmap_overlay(NUM_OF_ENTRIES, &(parentDirInode.entryBitmap));
    size_t total_set = bitmap_total_set(entry_bm);
    if (total_set == NUM_OF_ENTRIES) {
        bitmap_destroy(entry_bm);
        return -9;
    }
    size_t next_entry = bitmap_ffz(entry_bm);
    bitmap_set(entry_bm, next_entry);
    bitmap_destroy(entry_bm);

    size_t fileInodeID = block_store_allocate(fs->bs_inode);
    inode_t fileInode;

    fileInode.linkCount = 1;
    fileInode.inodeNumber = fileInodeID;

    if (type == FS_DIRECTORY) {
        fileInode.entryBitmap = 0x00000000;
        char* owner = "root";
        strncpy(fileInode.owner, owner, strlen(owner));
        fileInode.fileType = 'd';						

        size_t first_free_blocks = block_store_allocate(fs->bs_root); 
        if (first_free_blocks == SIZE_MAX) {
            return -10;
        }
        fileInode.directPointer[0] = first_free_blocks;
        db_t newDB = init_db();
        block_store_write(fs->bs_root, first_free_blocks, &newDB);
        fileInode.fileSize = BLOCK_SIZE_BYTES;
    }
    if (type == FS_REGULAR) {
        fileInode.fileSize = 0;
        fileInode.fileType = 'r';
        for (size_t i = 0; i < NUM_DIRECT_PTR; i++) {
            fileInode.directPointer[i] = 0x0000;
        }
        fileInode.indirectPointer = 0x0000;
        fileInode.doubleIndirectPointer = 0x0000;
    }
    // printf("inodeID: %lu\n", fileInodeID);
    if (inode_table_write(fs->bs_inode, fileInodeID, &fileInode) != inode_size) {
        return -11;
    }
    // we have change the parentInode's entry, so rewrite it
    if (inode_table_write(fs->bs_inode, parentDirInodeID, &parentDirInode) != inode_size) {
        return -12;
    }

    entry_t new_entry;
    strcpy(new_entry.filename, fileName);
    new_entry.inodeNumber = fileInodeID;
    parentDir.entries[next_entry] = new_entry;

    if (block_store_write(fs->bs_root, parentDirInode.directPointer[0], &parentDir) != BLOCK_SIZE_BYTES) {
        return -13;
    }
    return 0;
}

int fs_open(F19FS_t *fs, const char *path) {
    if (!fs || !path || strlen(path) == 0) {
        return -1;
    }
    if (!isValidPath(path)) {
        return -2;
    }
    char prefix[strlen(path) + 1];
    strcpy(prefix, path);
    char* dirPath = dirname(prefix);

    char suffix[strlen(path) + 1];
    strcpy(suffix, path);
    char* fileName = basename(suffix);
    
    if (strlen(fileName) >= FS_FNAME_MAX) {
        return -3;
    }
    size_t dirInodeID = getParentDirInodeID(fs, dirPath);
    if (dirInodeID == SIZE_MAX) {
        return -4;
    }

    size_t fileInodeID = getFileInodeID(fs, dirInodeID, fileName);
    if (fileInodeID == 0) {
        return -5;
    }

    inode_t fileInode;
    if (inode_table_read(fs->bs_inode, fileInodeID, &fileInode) == 0) {
        return -6; 
    }

    if (fileInode.fileType == 'd') {
        return -7;
    }
    size_t fdId = block_store_allocate(fs->bs_fd);
	fd_t fd;
	fd.inodeNum = fileInodeID;	
	fd.usage = 1;
	fd.locate_order = 0;
	fd.locate_offset = 0;
    if (fd_table_write(fs->bs_fd, fdId, &fd) == 0) {
        return -8;
    }
	return fdId;
}

int fs_close(F19FS_t *fs, int fd) {
    if (!fs || fd < 0) {
        return -1;
    }
    if (!fd_table_test(fs->bs_fd, fd)) {
        return -2;
    }
    fd_table_release(fs->bs_fd, fd);

    return 0;
}

dyn_array_t *fs_get_dir(F19FS_t *fs, const char *path) {
    if (!fs || !path || strlen(path) == 0) {
        // printf("return 1\n");
        return NULL;
    }
    // if (!isValidPath(path)) {
    //     printf("return 2\n");
    //     return NULL;
    // }
    char first = *path;
    if (first != '/') {
        // valid path: "/root/src/abc.c"
        // printf("return 2\n");
        return NULL;
    }

    char prefix[strlen(path) + 1];
    strcpy(prefix, path);
    char* dirPath = dirname(prefix);

    char suffix[strlen(path) + 1];
    strcpy(suffix, path);
    char* fileName = basename(suffix);

    size_t dirInodeId = 0;
    // "/" condition
    if (strlen(path) == 1 && strncmp(path, "/", strlen(path)) == 0) {
        dirInodeId = 0;
    } else {
        size_t parentInodeId = getParentDirInodeID(fs, dirPath);
        if (parentInodeId == SIZE_MAX) {
            // printf("return 3\n");
            return NULL;
        }
        dirInodeId = getFileInodeID(fs, parentInodeId, fileName);
        if (dirInodeId == 0) {
            // printf("return 4\n");
            return NULL;
        }
    }

    inode_t dirInode;
	db_t dirBlock;
    if (inode_table_read(fs->bs_inode, dirInodeId, &dirInode) == 0 || block_store_read(fs->bs_root, dirInode.directPointer[0], &dirBlock) == 0) {
        // printf("return 5\n");
        return NULL;
    }
    if (dirInode.fileType == 'r') {
        // printf("return 6\n");
        return NULL;
    }
    /*
    typedef struct file_record{
        // You can add more if you want
        // vvv just don't remove or rename these vvv
        char name[FS_FNAME_MAX];
        file_t type;
        size_t inode_id;
    } file_record_t;

    */
    dyn_array_t *dir_list = dyn_array_create(NUM_OF_ENTRIES, sizeof(file_record_t), NULL);
    if (!dir_list) {
        // printf("return 7\n");
        return NULL;
    }
    bitmap_t* entry_bm = bitmap_overlay(NUM_OF_ENTRIES, &(dirInode.entryBitmap));
    for (size_t i = 0; i < NUM_OF_ENTRIES; i++) {
        if (bitmap_test(entry_bm, i)) {
            entry_t entry = dirBlock.entries[i];

            file_record_t current;
            strncpy(current.name, entry.filename, FS_FNAME_MAX);

            inode_t currentInode;
            if (inode_table_read(fs->bs_inode, entry.inodeNumber, &currentInode) == 0) {
                bitmap_destroy(entry_bm);
                dyn_array_destroy(dir_list);
                // printf("return 8\n");
                return NULL;
            }
            current.inode_id = entry.inodeNumber;
            current.type = currentInode.fileType == FS_REGULAR ? FS_REGULAR : FS_DIRECTORY;

            if (!dyn_array_push_back(dir_list, &current)) {
                bitmap_destroy(entry_bm);
                dyn_array_destroy(dir_list);
                // printf("return 9\n");
                return NULL;
            }
        }
    }
    bitmap_destroy(entry_bm);
    return dir_list;
}



// off_t fs_seek(F19FS_t *fs, int fd, off_t offset, seek_t whence) {
//     1. empty check
//     2. find file inodeID according to fd
//     3. find the request file pointer accoding to the given offest and direct_ptr, indirect_ptr...
//     4. calculate the distance between curent place and new place
//     4. return the value of offest value 
// }

// ssize_t fs_read(F19FS_t *fs, int fd, void *dst, size_t nbyte) {
//     1. empty check
//     2. find file inodeID according to fd
//     3. use fs_seek to get the offest distance
//     4. get the data pointer according to the offest and direct_ptr....
//     5. copy the nbyte to dst buffer
// }

// ssize_t fs_write(F19FS_t *fs, int fd, const void *src, size_t nbyte) {
// //     1. empty check
// //     2. find file inodeID according to fd
// //     3. use fs_seek to get the offest distance
// //     4. get the data pointer according to the offest and direct_ptr....
// //     5. copy the nbyte from src buffer to the given data pointer
// }

// int fs_remove(F19FS_t *fs, const char *path) {
    // 1. empty check
    // 2. get dirInodeID, dirInode, dirBolck
    // 3. get fileInodeID, fileInode
    // 4. close all the file descriptor hold fileInodeID
    // 5. free all the block ptr
    // 6. free the fileInode, fileIndeID
    // 7. remove the file in dirBlock entry
// }

// int fs_move(F19FS_t *fs, const char *src, const char *dst) {
//     // 1. empty check
//     // 2. get dirInodeID, dirInode, dirBolck of src
//     // 3. get dirInodeID, dirInode, dirBolck of dst
//     // 4. get fileInodeID
//     // 5. remove the src dirBlock entry
//     // 6. add to the dst dirBlock entry
// }


