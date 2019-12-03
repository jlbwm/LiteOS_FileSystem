#include "dyn_array.h"
#include "bitmap.h"
#include "block_store.h"
#include "F19FS.h"
#include <libgen.h>

#define BLOCK_STORE_NUM_BLOCKS 65536   // 2^16 blocks.
#define BLOCK_STORE_AVAIL_BLOCKS 65528 // Last 8 blocks consumed by the FBM
#define BLOCK_SIZE_BITS 8192         // 2^10 BYTES per block *2^3 BITS per BYTES
#define BLOCK_SIZE_BYTES 1024         // 2^10 BYTES per block
#define BLOCK_STORE_NUM_BYTES (BLOCK_STORE_NUM_BLOCKS * BLOCK_SIZE_BYTES)  // 2^16 blocks of 2^10 bytes.

#define number_inodes 256
#define inode_size 64

#define number_fd 256
#define fd_size 6	// any number as you see fit

#define folder_number_entries 31
#define number_pointers_per_block 512

#define BLOCK_STORE_NUM_INODE_BITMAP_BLOCK 1
#define BLOCK_STORE_NUM_INODE_TABLE_BLOCK 16
#define NUM_OF_ENTRIES 31
#define SIZE_OF_ENTRY_BYTE 33
#define NUM_DIRECT_PTR 6
#define NUM_INDIRECT_PTR 512
#define NUM_DOUBLE_DIRECT_PTR 512

// each inode represents a regular file or a directory file
struct inode {
    uint32_t vacantFile;    // this parameter is only for directory. Used as a bitmap denoting availibility of entries in a directory file.
    char owner[18];         // for alignment purpose only   

    char fileType;          // 'r' denotes regular file, 'd' denotes directory file

    size_t inodeNumber;			// for F19FS, the range should be 0-255
    size_t fileSize; 			  // the unit is in byte	
    size_t linkCount;

    // to realize the 16-bit addressing, pointers are acutally block numbers, rather than 'real' pointers.
    uint16_t directPointer[6];
    uint16_t indirectPointer[1];
    uint16_t doubleIndirectPointer;

};


struct fileDescriptor {
    uint8_t inodeNum;	// the inode # of the fd

    // usage, locate_order and locate_offset together locate the exact byte at which the cursor is 
    uint8_t usage; 		// inode pointer usage info. Only the lower 3 digits will be used. 1 for direct, 2 for indirect, 4 for dbindirect
    uint16_t locate_order;		// serial number or index of the block within direct, indirect, or dbindirect range
    uint16_t locate_offset;		// offset of the cursor within a block
};


struct directoryFile {
    char filename[32];
    uint8_t inodeNumber;
};


struct F19FS {
    block_store_t * BlockStore_whole;
    block_store_t * BlockStore_inode;
    block_store_t * BlockStore_fd;
};


// check if the input filename is valid or not
bool isValidFileName(const char *filename) {
    if(!filename || strlen(filename) == 0 || strlen(filename) > 31)		// some "big" number as you wish
    {
        return false;
    }

    // define invalid characters might be contained in filenames
    char *invalidCharacters = "!@#$%^&*?\"";
    int i = 0;
    int len = strlen(invalidCharacters);
    for( ; i < len; i++)
    {
        if(strchr(filename, invalidCharacters[i]) != NULL)
        {
            return false;
        }
    }
    return true;
}

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


// use str_split to decompose the input string into filenames along the path, '/' as delimiter
char** str_split(char* a_str, const char a_delim, size_t * count) {
    if(*a_str != '/')
    {
        return NULL;
    }
    char** result    = 0;
    char* tmp        = a_str;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = '\0';

    /* Count how many elements will be extracted. */
    while (*tmp)
    {
        if (a_delim == *tmp)
        {
            (*count)++;
        }
        tmp++;
    }

    result = (char**)calloc(1, sizeof(char*) * (*count));
    for(size_t i = 0; i < (*count); i++)
    {
        *(result + i) = (char*)calloc(1, 200);
    }

    if (result)
    {
        size_t idx  = 0;
        char* token = strtok(a_str, delim);

        while (token)
        {
            strcpy(*(result + idx++), token);
            //    *(result + idx++) = strdup(token);
            token = strtok(NULL, delim);
        }

    }
    return result;
}

// Looping parentPath using strtok through to the closeest parent directory, return this directory inode ID
size_t getParentDirInodeID(F19FS_t* fs, char* parentPath) {
    char* currentDir = strtok(parentPath, "/");
    
    // Default as the root rectory inode ID (e.g in "/new_file" case, we just get "/")
    size_t cur_inode_ID = 0;
    inode_t cur_dir_inode;
    directoryFile_t* cur_dir_block = (directoryFile_t *)calloc(1, BLOCK_SIZE_BYTES);

    while (currentDir) {
        // printf("%s\n", currentDir);
        if (block_store_inode_read(fs->BlockStore_inode, cur_inode_ID, &cur_dir_inode) == 0) {
            printf("return 1\n");
            return SIZE_MAX;
        }
        if (cur_dir_inode.fileType == 'r') {
            printf("return 2\n");
            return SIZE_MAX;
        }
        if (block_store_read(fs->BlockStore_whole, cur_dir_inode.directPointer[0], cur_dir_block) == 0) {
            printf("return 3\n");
            return SIZE_MAX;
        }
        bitmap_t* entry_bm = bitmap_overlay(NUM_OF_ENTRIES, &(cur_dir_inode.vacantFile));
        bool isFound = false;
        for (size_t i = 0; i < NUM_OF_ENTRIES; i++) {
            // if current i entry is set and fileName equals to the given parentPath
            size_t maxLength = strlen((cur_dir_block + i)->filename) >= strlen(currentDir) ? strlen((cur_dir_block + i)->filename) : strlen(currentDir);
            if (bitmap_test(entry_bm, i) && strncmp((cur_dir_block + i)->filename, currentDir, maxLength) == 0) {
                inode_t next_inode;
                if (block_store_inode_read(fs->BlockStore_inode, (cur_dir_block + i)->inodeNumber, &next_inode) != 0 && next_inode.fileType == 'd') {
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
    free(cur_dir_block);
    return cur_inode_ID;
}

bool checkFileExist(F19FS_t* fs, size_t parentDirInodeID, char* fileName) {
    
    inode_t parentDirInode;
    directoryFile_t* parentDir = (directoryFile_t *)calloc(1, BLOCK_SIZE_BYTES);
    if (block_store_inode_read(fs->BlockStore_inode, parentDirInodeID, &parentDirInode) == 0 || block_store_read(fs->BlockStore_whole, parentDirInode.directPointer[0], parentDir) == 0) {
        return false;
    }
    bitmap_t* parentBM = bitmap_overlay(NUM_OF_ENTRIES, &(parentDirInode.vacantFile));
    for (size_t i = 0; i < NUM_OF_ENTRIES; i++) {
        size_t maxLength = strlen((parentDir + i)->filename) >= strlen(fileName) ? strlen((parentDir + i)->filename) : strlen(fileName); 
        if (bitmap_test(parentBM, i) && strncmp((parentDir + i)->filename, fileName, maxLength) == 0) {
            bitmap_destroy(parentBM);
            return true;
        }
    }
    bitmap_destroy(parentBM);
    free(parentDir);
    return false;
}

size_t getFileInodeID(F19FS_t* fs, size_t parentDirInodeID, char* fileName) {
    inode_t parentDirInode;
    directoryFile_t* parentDir = (directoryFile_t *)calloc(1, BLOCK_SIZE_BYTES);
    if (block_store_inode_read(fs->BlockStore_inode, parentDirInodeID, &parentDirInode) == 0 || block_store_read(fs->BlockStore_whole, parentDirInode.directPointer[0], parentDir) == 0) {
        return 0;
    }
    bitmap_t* parentBM = bitmap_overlay(NUM_OF_ENTRIES, &(parentDirInode.vacantFile));
    for (size_t i = 0; i < NUM_OF_ENTRIES; i++) {
        size_t maxLength = strlen((parentDir + i)->filename) >= strlen(fileName) ? strlen((parentDir + i)->filename) : strlen(fileName); 
        if (bitmap_test(parentBM, i) && strncmp((parentDir + i)->filename, fileName, maxLength) == 0) {
            bitmap_destroy(parentBM);
            return (parentDir + i)->inodeNumber;
        }
    }
    bitmap_destroy(parentBM);
    free(parentDir);
    return 0;
}

/// Formats (and mounts) an F19FS file for use
/// \param fname The file to format
/// \return Mounted F19FS object, NULL on error
///
F19FS_t *fs_format(const char *path) {
    if(path != NULL && strlen(path) != 0)
    {
        F19FS_t * ptr_F19FS = (F19FS_t *)calloc(1, sizeof(F19FS_t));	// get started
        ptr_F19FS->BlockStore_whole = block_store_create(path);				// pointer to start of a large chunck of memory

        // reserve the 1st block for bitmap of inode
        size_t bitmap_ID = block_store_allocate(ptr_F19FS->BlockStore_whole);
        //		printf("bitmap_ID = %zu\n", bitmap_ID);

        // 2rd - 17th block for inodes, 16 blocks in total
        size_t inode_start_block = block_store_allocate(ptr_F19FS->BlockStore_whole);
        //		printf("inode_start_block = %zu\n", inode_start_block);		
        for(int i = 0; i < 15; i++)
        {
            block_store_allocate(ptr_F19FS->BlockStore_whole);
            //			printf("all the way with block %zu\n", block_store_allocate(ptr_F19FS->BlockStore_whole));
        }

        // install inode block store inside the whole block store
        ptr_F19FS->BlockStore_inode = block_store_inode_create(block_store_Data_location(ptr_F19FS->BlockStore_whole) + bitmap_ID * BLOCK_SIZE_BYTES, block_store_Data_location(ptr_F19FS->BlockStore_whole) + inode_start_block * BLOCK_SIZE_BYTES);

        // the first inode is reserved for root dir
        block_store_sub_allocate(ptr_F19FS->BlockStore_inode);
        //		printf("first inode ID = %zu\n", block_store_sub_allocate(ptr_F19FS->BlockStore_inode));

        // update the root inode info.
        uint8_t root_inode_ID = 0;	// root inode is the first one in the inode table
        inode_t * root_inode = (inode_t *) calloc(1, sizeof(inode_t));
        //		printf("size of inode_t = %zu\n", sizeof(inode_t));
        root_inode->vacantFile = 0x00000000;
        root_inode->fileType = 'd';								
        root_inode->inodeNumber = root_inode_ID;
        root_inode->linkCount = 1;
        //		root_inode->directPointer[0] = root_data_ID;	// not allocate date block for it until it has a sub-folder or file
        block_store_inode_write(ptr_F19FS->BlockStore_inode, root_inode_ID, root_inode);		
        free(root_inode);

        // now allocate space for the file descriptors
        ptr_F19FS->BlockStore_fd = block_store_fd_create();

        return ptr_F19FS;
    }

    return NULL;	
}



///
/// Mounts an F19FS object and prepares it for use
/// \param fname The file to mount

/// \return Mounted F19FS object, NULL on error

///
F19FS_t *fs_mount(const char *path) {
    if(path != NULL && strlen(path) != 0)
    {
        F19FS_t * ptr_F19FS = (F19FS_t *)calloc(1, sizeof(F19FS_t));	// get started
        ptr_F19FS->BlockStore_whole = block_store_open(path);	// get the chunck of data	

        // the bitmap block should be the 1st one
        size_t bitmap_ID = 0;

        // the inode blocks start with the 2nd block, and goes around until the 17th block, 16 in total
        size_t inode_start_block = 1;

        // attach the bitmaps to their designated place
        ptr_F19FS->BlockStore_inode = block_store_inode_create(block_store_Data_location(ptr_F19FS->BlockStore_whole) + bitmap_ID * BLOCK_SIZE_BYTES, block_store_Data_location(ptr_F19FS->BlockStore_whole) + inode_start_block * BLOCK_SIZE_BYTES);

        // since file descriptors are allocated outside of the whole blocks, we can simply reallocate space for it.
        ptr_F19FS->BlockStore_fd = block_store_fd_create();

        return ptr_F19FS;
    }

    return NULL;		
}




///
/// Unmounts the given object and frees all related resources
/// \param fs The F19FS object to unmount
/// \return 0 on success, < 0 on failure
///
int fs_unmount(F19FS_t *fs) {
    if(fs != NULL)
    {	
        block_store_inode_destroy(fs->BlockStore_inode);

        block_store_destroy(fs->BlockStore_whole);
        block_store_fd_destroy(fs->BlockStore_fd);

        free(fs);
        return 0;
    }
    return -1;
}

directoryFile_t* init_db(){
	directoryFile_t* current = calloc(1, BLOCK_SIZE_BYTES);

	for (size_t i = 0; i < NUM_OF_ENTRIES; i++){
        (current + i)->inodeNumber = 0;
        memset((current + i)->filename, '\0', FS_FNAME_MAX);
	}
	return current;
}


int fs_create2(F19FS_t *fs, const char *path, file_t type) {
    if (!fs || !path || strlen(path) == 0 || !(type == FS_REGULAR || type == FS_DIRECTORY)) {
        return -1;
    }
    if (strlen(path) <= 1) {
        // "/" condition
        return -2;
    }
    if (block_store_get_free_blocks(fs->BlockStore_inode) == 0) {
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
    directoryFile_t* parentDir = calloc(1, BLOCK_SIZE_BYTES);
    if (block_store_inode_read(fs->BlockStore_inode, parentDirInodeID, &parentDirInode) == 0 || block_store_read(fs->BlockStore_whole, parentDirInode.directPointer[0], parentDir) == 0) {
        free(parentDir);
        return -8;
    }
    bitmap_t* entry_bm = bitmap_overlay(NUM_OF_ENTRIES, &(parentDirInode.vacantFile));
    size_t total_set = bitmap_total_set(entry_bm);
    if (total_set == NUM_OF_ENTRIES) {
        bitmap_destroy(entry_bm);
        free(parentDir);
        return -9;
    }
    size_t next_entry = bitmap_ffz(entry_bm);
    bitmap_set(entry_bm, next_entry);
    bitmap_destroy(entry_bm);

    size_t fileInodeID = block_store_allocate(fs->BlockStore_inode);
    inode_t fileInode;

    fileInode.linkCount = 1;
    fileInode.inodeNumber = fileInodeID;
    // printf("new inodeID: %lu\n", fileInodeID);

    if (type == FS_DIRECTORY) {
        fileInode.vacantFile = 0x00000000;
        char* owner = "root";
        strncpy(fileInode.owner, owner, strlen(owner));
        fileInode.fileType = 'd';						

        size_t first_free_blocks = block_store_allocate(fs->BlockStore_whole); 
        if (first_free_blocks == SIZE_MAX) {
            free(parentDir);
            return -10;
        }
        fileInode.directPointer[0] = first_free_blocks;
        directoryFile_t* newDB = init_db();
        block_store_write(fs->BlockStore_whole, first_free_blocks, newDB);
        free(newDB);
        fileInode.fileSize = BLOCK_SIZE_BYTES;
    }
    if (type == FS_REGULAR) {
        fileInode.fileSize = 0;
        fileInode.fileType = 'r';
        for (size_t i = 0; i < NUM_DIRECT_PTR; i++) {
            fileInode.directPointer[i] = 0x0000;
        }
        fileInode.indirectPointer[0] = 0x0000;
        fileInode.doubleIndirectPointer = 0x0000;
    }
    // printf("inodeID: %lu\n", fileInodeID);
    if (block_store_inode_write(fs->BlockStore_inode, fileInodeID, &fileInode) != inode_size) {
        free(parentDir);
        return -11;
    }
    // we have change the parentInode's entry, so rewrite it
    if (block_store_inode_write(fs->BlockStore_inode, parentDirInodeID, &parentDirInode) != inode_size) {
        free(parentDir);
        return -12;
    }

    strcpy((parentDir + next_entry)->filename, fileName);
    (parentDir + next_entry)->inodeNumber = fileInodeID;

    if (block_store_write(fs->BlockStore_whole, parentDirInode.directPointer[0], parentDir) != BLOCK_SIZE_BYTES) {
        return -13;
        free(parentDir);
    }
    free(parentDir);
    return 0;
}

///
/// Creates a new file at the specified location
///   Directories along the path that do not exist are not created
/// \param fs The F19FS containing the file
/// \param path Absolute path to file to create
/// \param type Type of file to create (regular/directory)
/// \return 0 on success, < 0 on failure
///
int fs_create(F19FS_t *fs, const char *path, file_t type) {
    if(fs != NULL && path != NULL && strlen(path) != 0 && (type == FS_REGULAR || type == FS_DIRECTORY))
    {
        char* copy_path = (char*)calloc(1, 65535);
        strcpy(copy_path, path);
        char** tokens;		// tokens are the directory names along the path. The last one is the name for the new file or dir
        size_t count = 0;
        tokens = str_split(copy_path, '/', &count);
        free(copy_path);
        if(tokens == NULL)
        {
            return -1;
        }

        // let's check if the filenames are valid or not
        for(size_t n = 0; n < count; n++)
        {	
            if(isValidFileName(*(tokens + n)) == false)
            {
                // before any return, we need to free tokens, otherwise memory leakage
                for (size_t i = 0; i < count; i++)
                {
                    free(*(tokens + i));
                }
                free(tokens);
                return -1;
            }
        }

        size_t parent_inode_ID = 0;	// start from the 1st inode, ie., the inode for root directory
        // first, let's find the parent dir
        size_t indicator = 0;

        // we declare parent_inode and parent_data here since it will still be used after the for loop
        directoryFile_t * parent_data = (directoryFile_t *)calloc(1, BLOCK_SIZE_BYTES);
        inode_t * parent_inode = (inode_t *) calloc(1, sizeof(inode_t));	

        for(size_t i = 0; i < count - 1; i++)
        {
            block_store_inode_read(fs->BlockStore_inode, parent_inode_ID, parent_inode);	// read out the parent inode
            // in case file and dir has the same name
            if(parent_inode->fileType == 'd')
            {
                block_store_read(fs->BlockStore_whole, parent_inode->directPointer[0], parent_data);

                for(int j = 0; j < folder_number_entries; j++)
                {
                    if( ((parent_inode->vacantFile >> j) & 1) == 1 && strcmp((parent_data + j) -> filename, *(tokens + i)) == 0 )
                    {
                        parent_inode_ID = (parent_data + j) -> inodeNumber;
                        indicator++;
                    }					
                }
            }					
        }
        //		printf("indicator = %zu\n", indicator);
        //		printf("parent_inode_ID = %lu\n", parent_inode_ID);

        // read out the parent inode
        block_store_inode_read(fs->BlockStore_inode, parent_inode_ID, parent_inode);
        if(indicator == count - 1 && parent_inode->fileType == 'd')
        {
            // same file or dir name in the same path is intolerable
            for(int m = 0; m < folder_number_entries; m++)
            {
                // rid out the case of existing same file or dir name
                if( ((parent_inode->vacantFile >> m) & 1) == 1)
                {
                    // before read out parent_data, we need to make sure it does exist!
                    block_store_read(fs->BlockStore_whole, parent_inode->directPointer[0], parent_data);
                    if( strcmp((parent_data + m) -> filename, *(tokens + count - 1)) == 0 )
                    {
                        free(parent_data);
                        free(parent_inode);	
                        // before any return, we need to free tokens, otherwise memory leakage
                        for (size_t i = 0; i < count; i++)
                        {
                            free(*(tokens + i));
                        }
                        free(tokens);
                        return -1;											
                    }
                }
            }	

            // cannot declare k inside for loop, since it will be used later.
            int k = 0;
            for( ; k < folder_number_entries; k++)
            {
                if( ((parent_inode->vacantFile >> k) & 1) == 0 )
                    break;
            }

            // if k == 0, then we have to declare a new parent data block
            //			printf("k = %d\n", k);
            if(k == 0)
            {
                size_t parent_data_ID = block_store_allocate(fs->BlockStore_whole);
                //					printf("parent_data_ID = %zu\n", parent_data_ID);
                if(parent_data_ID < BLOCK_STORE_AVAIL_BLOCKS)
                {
                    parent_inode->directPointer[0] = parent_data_ID;
                }
                else
                {
                    free(parent_inode);
                    free(parent_data);
                    // before any return, we need to free tokens, otherwise memory leakage
                    for (size_t i = 0; i < count; i++)
                    {
                        free(*(tokens + i));
                    }
                    free(tokens);
                    return -1;												
                }
            }

            if(k < folder_number_entries)	// k == folder_number_entries means this directory is full
            {
                size_t child_inode_ID = block_store_allocate(fs->BlockStore_inode);
                // printf("new child_inode_ID = %zu\n", child_inode_ID);
                // ugh, inodes are used up
                if(child_inode_ID == SIZE_MAX)
                {
                    free(parent_data);
                    free(parent_inode);
                    // before any return, we need to free tokens, otherwise memory leakage
                    for (size_t i = 0; i < count; i++)
                    {
                        free(*(tokens + i));
                    }
                    free(tokens);
                    return -1;	
                }

                // wow, at last, we make it!				
                // update the parent inode
                parent_inode->vacantFile |= (1 << k);
                // in the following cases, we should allocate parent data first: 
                // 1)the parent dir is not the root dir; 
                // 2)the file or dir to create is to be the 1st in the parent dir

                block_store_inode_write(fs->BlockStore_inode, parent_inode_ID, parent_inode);	

                // update the parent directory file block
                block_store_read(fs->BlockStore_whole, parent_inode->directPointer[0], parent_data);
                strcpy((parent_data + k)->filename, *(tokens + count - 1));
                //				printf("the newly created file's name is: %s\n", (parent_data + k)->filename);
                (parent_data + k)->inodeNumber = child_inode_ID;
                block_store_write(fs->BlockStore_whole, parent_inode->directPointer[0], parent_data);

                // update the newly created inode
                inode_t * child_inode = (inode_t *) calloc(1, sizeof(inode_t));
                child_inode->vacantFile = 0;
                if(type == FS_REGULAR)
                {
                    child_inode->fileType = 'r';
                }
                else if(type == FS_DIRECTORY)
                {
                    child_inode->fileType = 'd';
                }	

                child_inode->inodeNumber = child_inode_ID;
                // printf("new_inode: %lu\n", child_inode_ID);
                child_inode->fileSize = 0;
                child_inode->linkCount = 1;
                block_store_inode_write(fs->BlockStore_inode, child_inode_ID, child_inode);

                //				printf("after creation, parent_inode->vacantFile = %d\n", parent_inode->vacantFile);



                // free the temp space
                free(parent_inode);
                free(parent_data);
                free(child_inode);
                // before any return, we need to free tokens, otherwise memory leakage
                for (size_t i = 0; i < count; i++)
                {
                    free(*(tokens + i));
                }
                free(tokens);					
                return 0;
            }				
        }
        // before any return, we need to free tokens, otherwise memory leakage
        for (size_t i = 0; i < count; i++)
        {
            free(*(tokens + i));
        }
        free(tokens); 
        free(parent_inode);	
        free(parent_data);
    }
    return -1;
}



///
/// Opens the specified file for use
///   R/W position is set to the beginning of the file (BOF)
///   Directories cannot be opened
/// \param fs The F19FS containing the file
/// \param path path to the requested file
/// \return file descriptor to the requested file, < 0 on error
///
int fs_open(F19FS_t *fs, const char *path) {
    if(fs != NULL && path != NULL && strlen(path) != 0)
    {
        char* copy_path = (char*)calloc(1, 65535);
        strcpy(copy_path, path);
        char** tokens;		// tokens are the directory names along the path. The last one is the name for the new file or dir
        size_t count = 0;
        tokens = str_split(copy_path, '/', &count);
        free(copy_path);
        if(tokens == NULL)
        {
            return -1;
        }

        // let's check if the filenames are valid or not
        for(size_t n = 0; n < count; n++)
        {	
            if(isValidFileName(*(tokens + n)) == false)
            {
                // before any return, we need to free tokens, otherwise memory leakage
                for (size_t i = 0; i < count; i++)
                {
                    free(*(tokens + i));
                }
                free(tokens);
                return -1;
            }
        }	

        size_t parent_inode_ID = 0;	// start from the 1st inode, ie., the inode for root directory
        // first, let's find the parent dir
        size_t indicator = 0;

        inode_t * parent_inode = (inode_t *) calloc(1, sizeof(inode_t));
        directoryFile_t * parent_data = (directoryFile_t *)calloc(1, BLOCK_SIZE_BYTES);			

        // locate the file
        for(size_t i = 0; i < count; i++)
        {		
            block_store_inode_read(fs->BlockStore_inode, parent_inode_ID, parent_inode);	// read out the parent inode
            if(parent_inode->fileType == 'd')
            {
                block_store_read(fs->BlockStore_whole, parent_inode->directPointer[0], parent_data);
                //printf("parent_inode->vacantFile = %d\n", parent_inode->vacantFile);
                for(int j = 0; j < folder_number_entries; j++)
                {
                    //printf("(parent_data + j) -> filename = %s\n", (parent_data + j) -> filename);
                    if( ((parent_inode->vacantFile >> j) & 1) == 1 && strcmp((parent_data + j) -> filename, *(tokens + i)) == 0 )
                    {
                        parent_inode_ID = (parent_data + j) -> inodeNumber;
                        indicator++;
                    }					
                }
            }					
        }		
        free(parent_data);			
        free(parent_inode);	
        //printf("indicator = %zu\n", indicator);
        //printf("count = %zu\n", count);
        // now let's open the file
        if(indicator == count)
        {
            size_t fd_ID = block_store_sub_allocate(fs->BlockStore_fd);
            //printf("fd_ID = %zu\n", fd_ID);
            // it could be possible that fd runs out
            if(fd_ID < number_fd)
            {
                size_t file_inode_ID = parent_inode_ID;
                inode_t * file_inode = (inode_t *) calloc(1, sizeof(inode_t));
                block_store_inode_read(fs->BlockStore_inode, file_inode_ID, file_inode);	// read out the file inode	

                // it's too bad if file to be opened is a dir 
                if(file_inode->fileType == 'd')
                {
                    free(file_inode);
                    // before any return, we need to free tokens, otherwise memory leakage
                    for (size_t i = 0; i < count; i++)
                    {
                        free(*(tokens + i));
                    }
                    free(tokens);
                    return -1;
                }

                // assign a file descriptor ID to the open behavior
                fileDescriptor_t * fd = (fileDescriptor_t *)calloc(1, sizeof(fileDescriptor_t));
                fd->inodeNum = file_inode_ID;
                fd->usage = 1;
                fd->locate_order = 0; // R/W position is set to the beginning of the file (BOF)
                fd->locate_offset = 0;
                block_store_fd_write(fs->BlockStore_fd, fd_ID, fd);

                free(file_inode);
                free(fd);
                // before any return, we need to free tokens, otherwise memory leakage
                for (size_t i = 0; i < count; i++)
                {
                    free(*(tokens + i));
                }
                free(tokens);			
                return fd_ID;
            }	
        }
        // before any return, we need to free tokens, otherwise memory leakage
        for (size_t i = 0; i < count; i++)
        {
            free(*(tokens + i));
        }
        free(tokens);
    }
    return -1;
}

///
/// Closes the given file descriptor
/// \param fs The F19FS containing the file
/// \param fd The file to close
/// \return 0 on success, < 0 on failure
///
int fs_close(F19FS_t *fs, int fd) {
    if(fs != NULL && fd >=0 && fd < number_fd)
    {
        // first, make sure this fd is in use
        if(block_store_sub_test(fs->BlockStore_fd, fd))
        {
            block_store_sub_release(fs->BlockStore_fd, fd);
            return 0;
        }	
    }
    return -1;
}

///
/// Populates a dyn_array with information about the files in a directory
///   Array contains up to 15 file_record_t structures
/// \param fs The F19FS containing the file
/// \param path Absolute path to the directory to inspect
/// \return dyn_array of file records, NULL on error
///
dyn_array_t *fs_get_dir(F19FS_t *fs, const char *path) {
    if(fs != NULL && path != NULL && strlen(path) != 0)
    {	
        char* copy_path = (char*)malloc(200);
        strcpy(copy_path, path);
        char** tokens;		// tokens are the directory names along the path. The last one is the name for the new file or dir
        size_t count = 0;
        tokens = str_split(copy_path, '/', &count);
        free(copy_path);

        if(strlen(*tokens) == 0)
        {
            // a spcial case: only a slash, no dir names
            count -= 1;
        }
        else
        {
            for(size_t n = 0; n < count; n++)
            {	
                if(isValidFileName(*(tokens + n)) == false)
                {
                    // before any return, we need to free tokens, otherwise memory leakage
                    for (size_t i = 0; i < count; i++)
                    {
                        free(*(tokens + i));
                    }
                    free(tokens);		
                    return NULL;
                }
            }			
        }		

        // search along the path and find the deepest dir
        size_t parent_inode_ID = 0;	// start from the 1st inode, ie., the inode for root directory
        // first, let's find the parent dir
        size_t indicator = 0;

        inode_t * parent_inode = (inode_t *) calloc(1, sizeof(inode_t));
        directoryFile_t * parent_data = (directoryFile_t *)calloc(1, BLOCK_SIZE_BYTES);
        for(size_t i = 0; i < count; i++)
        {
            block_store_inode_read(fs->BlockStore_inode, parent_inode_ID, parent_inode);	// read out the parent inode
            // in case file and dir has the same name. But from the test cases we can see, this case would not happen
            if(parent_inode->fileType == 'd')
            {			
                block_store_read(fs->BlockStore_whole, parent_inode->directPointer[0], parent_data);
                for(int j = 0; j < folder_number_entries; j++)
                {
                    if( ((parent_inode->vacantFile >> j) & 1) == 1 && strcmp((parent_data + j) -> filename, *(tokens + i)) == 0 )
                    {
                        parent_inode_ID = (parent_data + j) -> inodeNumber;
                        indicator++;
                    }					
                }	
            }					
        }	
        free(parent_data);
        free(parent_inode);	

        // now let's enumerate the files/dir in it
        if(indicator == count)
        {
            inode_t * dir_inode = (inode_t *) calloc(1, sizeof(inode_t));
            block_store_inode_read(fs->BlockStore_inode, parent_inode_ID, dir_inode);	// read out the file inode			
            if(dir_inode->fileType == 'd')
            {
                // prepare the data to be read out
                directoryFile_t * dir_data = (directoryFile_t *)calloc(1, BLOCK_SIZE_BYTES);
                block_store_read(fs->BlockStore_whole, dir_inode->directPointer[0], dir_data);

                // prepare the dyn_array to hold the data
                dyn_array_t * dynArray = dyn_array_create(15, sizeof(file_record_t), NULL);

                for(int j = 0; j < folder_number_entries; j++)
                {
                    if( ((dir_inode->vacantFile >> j) & 1) == 1 )
                    {
                        file_record_t* fileRec = (file_record_t *)calloc(1, sizeof(file_record_t));
                        strcpy(fileRec->name, (dir_data + j) -> filename);

                        // to know fileType of the member in this dir, we have to refer to its inode
                        inode_t * member_inode = (inode_t *) calloc(1, sizeof(inode_t));
                        block_store_inode_read(fs->BlockStore_inode, (dir_data + j) -> inodeNumber, member_inode);
                        if(member_inode->fileType == 'd')
                        {
                            fileRec->type = FS_DIRECTORY;
                        }
                        else if(member_inode->fileType == 'f')
                        {
                            fileRec->type = FS_REGULAR;
                        }

                        // now insert the file record into the dyn_array
                        dyn_array_push_front(dynArray, fileRec);
                        free(fileRec);
                        free(member_inode);
                    }					
                }
                free(dir_data);
                free(dir_inode);
                // before any return, we need to free tokens, otherwise memory leakage
                if(strlen(*tokens) == 0)
                {
                    // a spcial case: only a slash, no dir names
                    count += 1;
                }
                for (size_t i = 0; i < count; i++)
                {
                    free(*(tokens + i));
                }
                free(tokens);	
                return(dynArray);
            }
            free(dir_inode);
        }
        // before any return, we need to free tokens, otherwise memory leakage
        if(strlen(*tokens) == 0)
        {
            // a spcial case: only a slash, no dir names
            count += 1;
        }
        for (size_t i = 0; i < count; i++)
        {
            free(*(tokens + i));
        }
        free(tokens);	
    }
    return NULL;
}

size_t allocate_indirectPtr_block(F19FS_t* fs) {
    uint16_t indirectPtr_block_buffer[NUM_INDIRECT_PTR];
    size_t blockID = block_store_allocate(fs->BlockStore_whole);
    if (blockID == SIZE_MAX) {
        return SIZE_MAX;
    }
    memset(indirectPtr_block_buffer, '\0', BLOCK_SIZE_BYTES);
    block_store_write(fs->BlockStore_whole, blockID, indirectPtr_block_buffer);
    return blockID;
}

ssize_t write_direct_block(F19FS_t* fs, inode_t* inode, uint16_t fd_locator, uint16_t fd_offset, const void* src, size_t nbyte) {
    // 0 <= fd->locate_order <= 5

    // prepare the write buffer
    uint8_t currentBlock[BLOCK_SIZE_BYTES];
    memset(currentBlock, '\0', BLOCK_SIZE_BYTES);

    // calcute the blankSpace that are avaliable to write in this block
    // if the blankspace is larger than the nbyte, we just need to write this nbyte
    size_t blankSpace = BLOCK_SIZE_BYTES - fd_offset;
    if (blankSpace > nbyte) {
        blankSpace = nbyte;
    }
    
    // write the src to write buffer
    size_t blockID;
    if (inode->directPointer[fd_locator] == 0) {
        blockID = block_store_allocate(fs->BlockStore_whole);
        if (blockID == SIZE_MAX) {
            printf("file block run out\n");
            return 0;
        }
        inode->directPointer[fd_locator] = blockID;
        memcpy(currentBlock, src, BLOCK_SIZE_BYTES);
    } else {
        blockID = inode->directPointer[fd_locator];
        block_store_read(fs->BlockStore_whole, blockID, currentBlock);
        memcpy(currentBlock + fd_offset, src, blankSpace);
    }

    // store back to black store
    block_store_write(fs->BlockStore_whole, blockID, currentBlock);

    nbyte -= blankSpace;
    if (nbyte == 0) {
        return blankSpace;
    }
    if (fd_locator + 1 < NUM_DIRECT_PTR) {
        return blankSpace + write_direct_block(fs, inode, fd_locator + 1, 0, src + blankSpace, nbyte);
    } else {
        // allocate to indirect ptr
        size_t indirectBlockID;
        if (inode->indirectPointer[0] == 0) {
            indirectBlockID = allocate_indirectPtr_block(fs);
            if (indirectBlockID == SIZE_MAX) {
                printf("indirect block run out\n");
                return 0;
            }
            inode->indirectPointer[0] = indirectBlockID;
        } else {
            indirectBlockID = inode->indirectPointer[0];
        } 
        return blankSpace + write_indirect_block(fs, inode, fd_locator + 1, 0, src + blankSpace, nbyte, indirectBlockID);
    
    }
}

ssize_t write_indirect_block(F19FS_t* fs, inode_t* inode, uint16_t fd_locator, uint16_t fd_offset, const void* src, size_t nbyte, size_t indirectBlockID) {
    // prepare the indirect pointer buffer
    uint16_t indirectPtrBuffer[NUM_INDIRECT_PTR];
    block_store_read(fs->BlockStore_whole, indirectBlockID, indirectPtrBuffer);

    // get the index of current indirectPtr
    uint16_t indirectPtrID = (fd_locator - NUM_DIRECT_PTR) % NUM_DOUBLE_DIRECT_PTR;

    ssize_t sumOfWrittenByte = 0;

    // iteratively allocate and write the new block until the indirectBlockID exhausted or nbyte reached to 0
    while (indirectPtrID < NUM_INDIRECT_PTR && nbyte > 0) {
        // get or default the written block
        size_t blockID;
        if (indirectPtrBuffer[indirectPtrID] == 0) {
            blockID = block_store_allocate(fs->BlockStore_whole);
            if (blockID == SIZE_MAX) {
                block_store_write(fs->BlockStore_whole, indirectBlockID, indirectPtrBuffer);
                return sumOfWrittenByte; 
            }
            indirectPtrBuffer[indirectPtrID] = blockID;
        } else {
            blockID = indirectPtrBuffer[indirectPtrID];
        }

        // calcute the blankSpace that are avaliable to write in this block
        // if the blankspace is larger than the nbyte, we just need to write this nbyte
        size_t blankSpace = BLOCK_SIZE_BYTES - fd_offset;
        if (blankSpace > nbyte) {
            blankSpace = nbyte;
        }
        
        // prepare the file block write buffer
        uint8_t fileBlock_writeBuffer[BLOCK_SIZE_BYTES];
        block_store_read(fs->BlockStore_whole, blockID, fileBlock_writeBuffer);

        // copy the src + offset to the writeBuffer and write back to block store
        memcpy(fileBlock_writeBuffer + fd_offset, src, blankSpace);
        block_store_write(fs->BlockStore_whole, blockID, fileBlock_writeBuffer);

        // update the global variable
        nbyte -= blankSpace;
        indirectPtrID += 1;
        src += blankSpace;
        sumOfWrittenByte += blankSpace;

        fd_locator += 1;
        fd_offset = 0;
    }

    block_store_write(fs->BlockStore_whole, indirectBlockID, indirectPtrBuffer);

    if (nbyte == 0) {
        return sumOfWrittenByte;
    }

    return sumOfWrittenByte + write_double_direct_block(fs, inode, fd_locator, fd_offset, src, nbyte);

}
ssize_t write_double_direct_block(F19FS_t* fs, inode_t* inode, uint16_t fd_locator, uint16_t fd_offset, const void* src, size_t nbyte) {
    // get or default the indirectBlock's ID
    size_t double_direct_block_ID;
    if (inode->doubleIndirectPointer == 0) {
        double_direct_block_ID = allocate_indirectPtr_block(fs);
        if (double_direct_block_ID == SIZE_MAX) {
            return 0;
        }
        inode->doubleIndirectPointer = double_direct_block_ID;
    } else {
        double_direct_block_ID = inode->doubleIndirectPointer;
    }

    // prepare the doubke direct pointer buffer
    uint16_t doubleDirectPtrBuffer[NUM_DOUBLE_DIRECT_PTR];
    block_store_read(fs->BlockStore_whole, double_direct_block_ID, doubleDirectPtrBuffer);

    int index = (fd_locator - (NUM_DIRECT_PTR + NUM_INDIRECT_PTR)) / NUM_DOUBLE_DIRECT_PTR;

    size_t indirectBlockID;
    if (doubleDirectPtrBuffer[index] == 0) {
        indirectBlockID = block_store_allocate(fs->BlockStore_whole);
        if (indirectBlockID == SIZE_MAX) {
            return 0;
        }
        doubleDirectPtrBuffer[index] = indirectBlockID;
        block_store_write(fs->BlockStore_whole, double_direct_block_ID, doubleDirectPtrBuffer);
    } else {
        indirectBlockID = doubleDirectPtrBuffer[index];
    }
    return write_indirect_block(fs, inode, fd_locator, fd_offset, src, nbyte, indirectBlockID);
}

void updateFD(fileDescriptor_t* fileDescriptor, ssize_t nbyte) {
    uint16_t blankSpace = BLOCK_SIZE_BYTES - fileDescriptor->locate_offset;
    // printf("[updateFD] nbyte: %lu, blank space: %d\n", nbyte, blankSpace);
    if (blankSpace >= nbyte) {
        fileDescriptor->locate_offset += nbyte;
        if (fileDescriptor->locate_offset == 1024) {
            fileDescriptor->locate_order += 1;
            fileDescriptor->locate_offset = 0;
        }
        // printf("[updateFD] offset: %d\n", fileDescriptor->locate_offset);
    } else {
        nbyte -= blankSpace;
        fileDescriptor->locate_order += 1;
        fileDescriptor->locate_order += nbyte / BLOCK_SIZE_BYTES;
        fileDescriptor->locate_offset = nbyte % BLOCK_SIZE_BYTES;
        // printf("[updateFD] locator: %d\n", fileDescriptor->locate_order);
        // printf("[updateFD] offset: %d\n", fileDescriptor->locate_offset);
    }
}

ssize_t fs_write(F19FS_t* fs, int fd, const void* src, size_t nbyte) {
    if (!fs || fd < 0 || fd >= number_fd || !src) {
        return -1;
    }
    if(!bitmap_test(block_store_get_bm(fs->BlockStore_fd), fd)) { 
        return -2; 
    }
    // printf("total num of write data: %lu\n", nbyte);
    // prepare file descrptor
    fileDescriptor_t fileDescriptor;
    block_store_fd_read(fs->BlockStore_fd, fd, &fileDescriptor);
    uint16_t fd_locator = fileDescriptor.locate_order;
    uint16_t fd_offset = fileDescriptor.locate_offset;
    // printf("before write - inode number: %d, locator: %d, offset: %d\n", fileDescriptor.inodeNum, fd_locator, fd_offset);

    // get inode
    inode_t inode;
    block_store_inode_read(fs->BlockStore_inode, fileDescriptor.inodeNum, &inode);
    // printf("inode NUM: %lu\n", inode.inodeNumber);

    ssize_t sumOfWrittenByte = 0;

    if (fd_locator < NUM_DIRECT_PTR) {
        sumOfWrittenByte = write_direct_block(fs, &inode, fd_locator, fd_offset, src, nbyte);
    } else if (fd_locator >= NUM_DIRECT_PTR && fd_locator < NUM_INDIRECT_PTR + NUM_DIRECT_PTR) {
        size_t indirectBlockID;
        if (inode.indirectPointer[0] == 0) {
            indirectBlockID = allocate_indirectPtr_block(fs);
            if (indirectBlockID == SIZE_MAX) {
                return 0;
            }
            inode.indirectPointer[0] = indirectBlockID;
        } else {
            indirectBlockID = inode.indirectPointer[0];
        }
        sumOfWrittenByte = write_indirect_block(fs, &inode, fd_locator, fd_offset, src, nbyte, indirectBlockID);
        // printf("indirect write: %lu\n", sumOfWrittenByte);
    } else if (fd_locator >= NUM_INDIRECT_PTR + NUM_DIRECT_PTR){
        sumOfWrittenByte = write_double_direct_block(fs, &inode, fd_locator, fd_offset, src, nbyte);
        // printf("doubledirect write: %lu\n", sumOfWrittenByte);
    }
    // printf("after write: %lu\n", sumOfWrittenByte);

    // update fd
    updateFD(&fileDescriptor, sumOfWrittenByte);
    // printf("after write - inode number: %d, locator: %d, offset: %d\n", fileDescriptor.inodeNum, fileDescriptor.locate_order, fileDescriptor.locate_offset);

    block_store_fd_write(fs->BlockStore_fd, fd, &fileDescriptor);

    //update inode
    inode.fileSize += sumOfWrittenByte;
    
    block_store_inode_write(fs->BlockStore_inode, fileDescriptor.inodeNum, &inode);

    return sumOfWrittenByte;
}
int getFileIndexInDir(inode_t* parentDirInode, directoryFile_t* parentDir, char* fileName) {
    bitmap_t* parentBM = bitmap_overlay(NUM_OF_ENTRIES, &(parentDirInode->vacantFile));
    for (size_t i = 0; i < NUM_OF_ENTRIES; i++) {
        if (bitmap_test(parentBM, i) && strncmp((parentDir + i)->filename, fileName, strlen(fileName)) == 0) {
            bitmap_destroy(parentBM);
            return i;
        }
    }
    bitmap_destroy(parentBM);
    return -1;
}

bool isDirectoryEmpty(inode_t* directoryInode) {
    bitmap_t* directoryBM = bitmap_overlay(NUM_OF_ENTRIES, &(directoryInode->vacantFile));
    if (bitmap_total_set(directoryBM) != 0) {
        bitmap_destroy(directoryBM);
        return false;
    }
    bitmap_destroy(directoryBM);
    return true;
}

int fs_remove(F19FS_t *fs, const char *path) {
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

    inode_t* parentDirInode = (inode_t*)calloc(1, sizeof(inode_t));
    directoryFile_t* parentDir = (directoryFile_t *)calloc(1, BLOCK_SIZE_BYTES);
    if (block_store_inode_read(fs->BlockStore_inode, dirInodeID, parentDirInode) == 0 || block_store_read(fs->BlockStore_whole, parentDirInode->directPointer[0], parentDir) == 0) {
        return -6;
    }

    inode_t* fileInode = (inode_t*)calloc(1, sizeof(inode_t));
    if (block_store_inode_read(fs->BlockStore_inode, fileInodeID, fileInode) == 0) {
        return -7;
    }

    if (fileInode->fileType == 'r') {
        if (fileInode->linkCount > 1) {
            fileInode->linkCount -= 1;
            block_store_inode_write(fs->BlockStore_inode, fileInodeID, fileInode);
            return 0;
        }
        // delete all the file block
        for(int i = 0; i< NUM_DIRECT_PTR; i++){
            if(fileInode->directPointer[i] != 0){
                block_store_release(fs->BlockStore_whole, fileInode->directPointer[i]);
            }
        }
        // delete all the indirect related block
        if(fileInode->indirectPointer[0] != 0){
            uint16_t indirectPtrs[NUM_INDIRECT_PTR];
            block_store_read(fs->BlockStore_whole, fileInode->indirectPointer[0], indirectPtrs);

            for(int i = 0; i < NUM_INDIRECT_PTR; i++){
                if(indirectPtrs[i] != 0){
                    block_store_release(fs->BlockStore_whole, indirectPtrs[i]);
                }
            }
            block_store_release(fs->BlockStore_whole, fileInode->indirectPointer[0]);
        }
        // delete all the double indirect related block
        if(fileInode->doubleIndirectPointer != 0){

            uint16_t doubleIndirectPtrs[NUM_DOUBLE_DIRECT_PTR];
            block_store_read(fs->BlockStore_whole, fileInode->doubleIndirectPointer, doubleIndirectPtrs);

            for(int i = 0; i < NUM_DOUBLE_DIRECT_PTR; i++){
                if(doubleIndirectPtrs[i] != 0){
                    uint16_t indirectPtrs[NUM_INDIRECT_PTR];
                    block_store_read(fs->BlockStore_whole, doubleIndirectPtrs[i], indirectPtrs);
                    for(int j = 0; j < NUM_INDIRECT_PTR; j++){
                        if(indirectPtrs[j] != 0) {
                            block_store_release(fs->BlockStore_whole, indirectPtrs[j]);
                        }
                    }
                    block_store_release(fs->BlockStore_whole, doubleIndirectPtrs[i]);
                }
            }
            block_store_release(fs->BlockStore_whole, fileInode->doubleIndirectPointer);
        }

    } else {
        if (isDirectoryEmpty(fileInode) == false) {
            free(parentDirInode);
            free(parentDir);
            free(fileInode);
            return -8;
        }
        // get the current directory inode's directory block
        directoryFile_t* currentDir = (directoryFile_t *)calloc(1, BLOCK_SIZE_BYTES);
        if (block_store_read(fs->BlockStore_whole, fileInode->directPointer[0], currentDir) == 0) {
            free(parentDirInode);
            free(parentDir);
            free(fileInode);
            free(currentDir);
            return -9;
        }
        // free the current directory
        memset(currentDir, 0, sizeof(directoryFile_t));
        block_store_write(fs->BlockStore_whole, fileInode->directPointer[0], currentDir);
        block_store_release(fs->BlockStore_whole, fileInode->directPointer[0]);
        free(currentDir);
    }

    // clear the inode itself
    memset(fileInode, 0, sizeof(inode_t));
    block_store_inode_write(fs->BlockStore_inode, fileInodeID, fileInode);
    block_store_release(fs->BlockStore_inode, fileInodeID);
    free(fileInode);

    // update the parent directory
    int entryIndex = getFileIndexInDir(parentDirInode, parentDir, fileName);
    if (entryIndex == -1) {
        return -8;
    }
    (parentDir + entryIndex)->inodeNumber = 0;
    memset((parentDir + entryIndex)->filename, '\0', FS_FNAME_MAX);
    block_store_write(fs->BlockStore_whole, parentDirInode->directPointer[0], parentDir);

    // update the directory inode
    bitmap_t* parentBM = bitmap_overlay(NUM_OF_ENTRIES, &(parentDirInode->vacantFile));
    bitmap_reset(parentBM, entryIndex);
    bitmap_destroy(parentBM);
    block_store_inode_write(fs->BlockStore_inode, dirInodeID, parentDirInode);

    free(parentDir);
    free(parentDirInode);
    return 0;
}

off_t cutBoundary(int fileSize, off_t offset){
    if(offset <= 0){
        return 0;
    }else if(offset > fileSize){
        return fileSize;
    }else{
        return offset;
    }
}

off_t getPreviosOffset(fileDescriptor_t* fileDescriptor) {
    off_t result = 0;
    uint16_t location = fileDescriptor->locate_order;
    uint16_t offset = fileDescriptor->locate_offset;
    if (location == 0) {
        return offset;
    }
    if (offset != 0) {
        result += (location - 1) * BLOCK_SIZE_BYTES;
        result += offset;
    } else {
        result += location * BLOCK_SIZE_BYTES;
    }
    return result; 
}

off_t fs_seek(F19FS_t *fs, int fd, off_t offset, seek_t whence) {
    if (!fs || fd < 0 || fd >= number_fd) {
        return -1;
    }
    if(!bitmap_test(block_store_get_bm(fs->BlockStore_fd), fd)) { 
        return -2; 
    }
    if (!(whence == FS_SEEK_CUR || whence == FS_SEEK_END || whence == FS_SEEK_SET)) {
        return -3;
    }
    // prepare the file Descriptor
    fileDescriptor_t fileDescriptor;
    block_store_fd_read(fs->BlockStore_fd, fd, &fileDescriptor);

    // printf("[Before SEEK] fd: %d, location: %d, offset: %d\n", fd, fileDescriptor.locate_order, fileDescriptor.locate_offset);

    // prepre the file Inode
    size_t fileInodeID = fileDescriptor.inodeNum;
    inode_t fileInode;
    block_store_inode_read(fs->BlockStore_inode, fileInodeID, &fileInode);

    size_t fileSize = fileInode.fileSize;

    if (whence == FS_SEEK_SET) {
        offset = cutBoundary(fileSize, offset);
    } else if (whence == FS_SEEK_CUR) {
        off_t headToCurrent = getPreviosOffset(&fileDescriptor);
        // printf("the head to current size: %lu\n", headToCurrent);
        offset = cutBoundary(fileSize, offset + headToCurrent);
        // printf("offset after cut boundary: %lu\n", offset);
    } else if (whence == FS_SEEK_END) {
        offset = cutBoundary(fileSize, offset + fileSize);
    } 

    fileDescriptor.locate_order = 0;
    fileDescriptor.locate_offset = 0;
    updateFD(&fileDescriptor, offset);
    block_store_fd_write(fs->BlockStore_fd, fd, &fileDescriptor);

    // printf("[After  SEEK] fd: %d, location: %d, offset: %d\n", fd, fileDescriptor.locate_order, fileDescriptor.locate_offset);
    
    return offset;
}

ssize_t read_direct_block(F19FS_t* fs, inode_t* inode, uint16_t fd_locator, uint16_t fd_offset, void* dst, size_t nbyte) {
    uint16_t blockId = inode->directPointer[fd_locator];
    if (blockId == 0) {
        return 0;
    } 
    size_t blankSpace = BLOCK_SIZE_BYTES - fd_offset;
    if (blankSpace > nbyte) {
        blankSpace = nbyte;
    }
    
    uint8_t fileBlock[BLOCK_SIZE_BYTES];
    block_store_read(fs->BlockStore_whole, blockId, fileBlock);
    memcpy(dst, fileBlock + fd_offset, blankSpace);

    nbyte -= blankSpace;
    if (nbyte == 0) {
        return blankSpace;
    }

    if (fd_locator + 1 < NUM_DIRECT_PTR) {
        return blankSpace + read_direct_block(fs, inode, fd_locator + 1, 0, dst + blankSpace, nbyte);
    } else {
        return blankSpace + read_indirect_block(fs, inode, fd_locator + 1, 0, dst + blankSpace, nbyte, inode->indirectPointer[0]);
    }
}

ssize_t read_indirect_block(F19FS_t* fs, inode_t* inode, uint16_t fd_locator, uint16_t fd_offset, void* dst, size_t nbyte, uint16_t indirectBlockID) {
    if (indirectBlockID == 0) {
        return 0;
    }
    // prepare the indirect pointer buffer
    uint16_t indirectPtrBuffer[NUM_INDIRECT_PTR];
    block_store_read(fs->BlockStore_whole, indirectBlockID, indirectPtrBuffer);

    // get the index of current indirectPtr
    uint16_t indirectPtrID = (fd_locator - NUM_DIRECT_PTR) % NUM_DOUBLE_DIRECT_PTR;

    ssize_t sumOfReadByte = 0;

    while (indirectPtrID < NUM_INDIRECT_PTR && nbyte > 0) {
        size_t blockID = indirectPtrBuffer[indirectPtrID];
        if (blockID == 0) {
            return sumOfReadByte;
        }
        uint8_t blockBuffer[BLOCK_SIZE_BYTES];
        block_store_read(fs->BlockStore_whole, blockID, blockBuffer);

        size_t blankSpace = BLOCK_SIZE_BYTES - fd_offset;
        if (blankSpace > nbyte) {
            blankSpace = nbyte;
        }

        memcpy(dst, blockBuffer + fd_offset, blankSpace);

        // update the global variable
        nbyte -= blankSpace;
        indirectPtrID += 1;
        dst += blankSpace;
        sumOfReadByte += blankSpace;

        fd_locator += 1;
        fd_offset = 0;
    }

    if (nbyte == 0) {
        return sumOfReadByte;
    }
    return sumOfReadByte + read_doubleDirect_block(fs, inode, fd_locator, fd_offset, dst, nbyte);
}

ssize_t read_doubleDirect_block(F19FS_t* fs, inode_t* inode, uint16_t fd_locator, uint16_t fd_offset, void* dst, size_t nbyte) {
    if (inode->doubleIndirectPointer == 0) {
        return 0;
    }
    uint16_t doubleDirectPtrBuffer[NUM_DOUBLE_DIRECT_PTR];
    block_store_read(fs->BlockStore_whole, inode->doubleIndirectPointer, doubleDirectPtrBuffer);

    int index = (fd_locator - (NUM_DIRECT_PTR + NUM_INDIRECT_PTR)) / NUM_DOUBLE_DIRECT_PTR; 
    if (doubleDirectPtrBuffer[index] == 0) {
        return 0;
    }
    return read_indirect_block(fs, inode, fd_locator, fd_offset, dst, nbyte, doubleDirectPtrBuffer[index]);
}

ssize_t fs_read(F19FS_t *fs, int fd, void *dst, size_t nbyte) {
    if (!fs || fd < 0 || fd >= number_fd || !dst) {
        return -1;
    }
    if(!bitmap_test(block_store_get_bm(fs->BlockStore_fd), fd)) { 
        return -2; 
    }
    if(nbyte == 0){
        return 0;
    }

    // prepare the file descriptor
    fileDescriptor_t fileDescriptor;
    block_store_fd_read(fs->BlockStore_fd, fd, &fileDescriptor);
    uint16_t fd_locator = fileDescriptor.locate_order;
    uint16_t fd_offset = fileDescriptor.locate_offset;
    // printf("[Before READ] fd: %d, location: %d, offset: %d\n", fd, fd_locator, fd_offset);

    // prepare file inode
    uint8_t fileInodeID = fileDescriptor.inodeNum;
    inode_t fileInode;
    block_store_inode_read(fs->BlockStore_inode, fileInodeID, &fileInode);

    off_t headToCurrent = getPreviosOffset(&fileDescriptor);
    if(headToCurrent + nbyte > fileInode.fileSize) {
        nbyte = fileInode.fileSize - headToCurrent;;
    }

    ssize_t sumOfReadByte = 0;
    if (fd_locator < NUM_DIRECT_PTR) {
        sumOfReadByte = read_direct_block(fs, &fileInode, fd_locator, fd_offset, dst, nbyte);
    } else if (fd_locator >= NUM_DIRECT_PTR && fd_locator < NUM_INDIRECT_PTR + NUM_DIRECT_PTR) {
        sumOfReadByte = read_indirect_block(fs, &fileInode, fd_locator, fd_offset, dst, nbyte, fileInode.indirectPointer[0]);
    } else {
        sumOfReadByte = read_doubleDirect_block(fs, &fileInode, fd_locator, fd_offset, dst, nbyte);
    }

    updateFD(&fileDescriptor, sumOfReadByte);
    block_store_fd_write(fs->BlockStore_fd, fd, &fileDescriptor);

    // printf("[After  READ] fd: %d, location: %d, offset: %d\n", fd, fileDescriptor.locate_order, fileDescriptor.locate_offset);

    return sumOfReadByte;
}

int fs_move(F19FS_t *fs, const char *src, const char *dst) {
    if (!fs || !src || !dst) {
        return -1;
    }
    if (strlen(src) <= 1 || strlen(dst) <= 1) {
        // "/" condition
        return -2;
    }
    if (!isValidPath(src)) {
        return -3;
    }
    if (!isValidPath(dst)) {
        return -4;
    }
    if (strlen(src) == strlen(dst) && strncmp(src, dst, strlen(dst))) {
        return -11;
    }

    // printf("src: %s, dst: %s\n", src, dst);

    char src_prefix[strlen(src) + 1];
    strcpy(src_prefix, src);
    char* src_dirPath = dirname(src_prefix);

    char src_suffix[strlen(src) + 1];
    strcpy(src_suffix, src);
    char* src_fileName = basename(src_suffix);

    // printf("src_path: %s, src_fileName: %s\n", src_dirPath, src_fileName);
    
    if (strlen(src_fileName) >= FS_FNAME_MAX) {
        return -5;
    }

    char dst_prefix[strlen(dst) + 1];
    strcpy(dst_prefix, dst);
    char* dst_dirPath = dirname(dst_prefix);

    char dst_suffix[strlen(dst) + 1];
    strcpy(dst_suffix, dst);
    char* dst_fileName = basename(dst_suffix);

    // printf("dst_path: %s, dst_fileName: %s\n", dst_dirPath, dst_fileName);
    
    if (strlen(dst_fileName) >= FS_FNAME_MAX) {
        return -6;
    }

    // condition 1: src: /folder, dst: /folder/oh_no => move folder into folder
    char * dst_test = malloc(sizeof(char) * (strlen(dst_dirPath) + 1));
    strcpy(dst_test, dst_dirPath);
    char* token = strtok(dst_test, "/");
    while (token) {
        if (strncmp(src_fileName, token, strlen(src_fileName)) == 0) {
            // printf("conditon 1 applied\n");
            return -14;
        }
        token = strtok(NULL, "/");
    }

    size_t src_parentDirInodeID = getParentDirInodeID(fs, src_dirPath);
    if (src_parentDirInodeID == SIZE_MAX) {
        return -7;
    }

    if (!checkFileExist(fs, src_parentDirInodeID, src_fileName)) {
        return -8;
    }
    // printf("src_path: %s, src_parentDirInodeID: %lu\n", src_dirPath, src_parentDirInodeID);

    size_t dst_parentDirInodeID = getParentDirInodeID(fs, dst_dirPath);
    if (dst_parentDirInodeID == SIZE_MAX) {
        return -9;
    }
    // printf("dst_path: %s, dst_parentDirInodeID: %lu\n", dst_dirPath, dst_parentDirInodeID);
    // if dst directory exist, return the 
    if (!checkFileExist(fs, dst_parentDirInodeID, dst_fileName)) {
        if (0 != fs_create2(fs, dst, FS_DIRECTORY)) {
            return -10;
        }
        // printf("create a new directory %s.\n", dst_fileName);
    }

    inode_t src_parentDirInode;
    block_store_inode_read(fs->BlockStore_inode, src_parentDirInodeID, &src_parentDirInode);

    inode_t dst_parentDirInode;
    block_store_inode_read(fs->BlockStore_inode, dst_parentDirInodeID, &dst_parentDirInode);

    size_t src_fileInodeId = getFileInodeID(fs, src_parentDirInodeID, src_fileName);
    if (src_fileInodeId == SIZE_MAX) {
        return -11;
    }
    // printf("src_filename: %s, src_fileInodeID: %lu\n", src_fileName, src_fileInodeId);

    size_t dst_fileInodeId = getFileInodeID(fs, dst_parentDirInodeID, dst_fileName);
    if (dst_fileInodeId == SIZE_MAX) {
        return -12;
    }

    // printf("dst_filename: %s, dst_fileInodeID: %lu\n", dst_fileName, dst_fileInodeId);

    inode_t src_fileInode;
    block_store_inode_read(fs->BlockStore_inode, src_fileInodeId, &src_fileInode);

    inode_t dst_fileInode;
    block_store_inode_read(fs->BlockStore_inode, dst_fileInodeId, &dst_fileInode);

    // condition 2: src: /folder/file1  dst: folder => in same folder, do nothing
    if (src_parentDirInodeID == dst_fileInodeId) {
        return -14;
    }

    // condition 3: src: /folder/with_folder dst: /folder2
    directoryFile_t* src_dir_block = (directoryFile_t *)calloc(1, BLOCK_SIZE_BYTES);
    block_store_read(fs->BlockStore_whole, src_parentDirInode.directPointer[0], src_dir_block);


    int src_fileIndex = getFileIndexInDir(&src_parentDirInode, src_dir_block, src_fileName);
    if (src_fileIndex == -1) {
        return -13;
    }
    // remove the directory File Entry
    (src_dir_block + src_fileIndex)->inodeNumber = 0;
    memset((src_dir_block + src_fileIndex)->filename, '\0', FS_FNAME_MAX);
    block_store_write(fs->BlockStore_whole, src_parentDirInode.directPointer[0], src_dir_block);
    free(src_dir_block);

    // update the src directory inode
    bitmap_t* parentBM = bitmap_overlay(NUM_OF_ENTRIES, &(src_parentDirInode.vacantFile));
    bitmap_reset(parentBM, src_fileIndex);
    bitmap_destroy(parentBM);
    block_store_inode_write(fs->BlockStore_inode, src_parentDirInodeID, &src_parentDirInode);

    //get the dst directory file block
    directoryFile_t* dst_dir_block = (directoryFile_t *)calloc(1, BLOCK_SIZE_BYTES);
    block_store_read(fs->BlockStore_whole, dst_fileInode.directPointer[0], dst_dir_block);
    
    // update the dst directory file block
    bitmap_t* dst_file_BM = bitmap_overlay(NUM_OF_ENTRIES, &(dst_fileInode.vacantFile));
    size_t location = bitmap_ffz(dst_file_BM);
    (dst_dir_block + location)->inodeNumber = src_fileInodeId;
    strncpy((dst_dir_block + location)->filename, src_fileName, strlen(src_fileName));
    bitmap_destroy(dst_file_BM);
    block_store_write(fs->BlockStore_whole, dst_fileInode.directPointer[0], dst_dir_block);
    free(dst_dir_block);
    // printf("-----------------\n");
    return 0;
}

// 
int fs_link(F19FS_t *fs, const char *src, const char *dst) {
    if (!fs || !src || !dst) {
        return -1;
    }
    if (strlen(src) <= 1 || strlen(dst) <= 1) {
        // "/" condition
        return -2;
    }
    if (!isValidPath(src)) {
        return -3;
    }
    if (!isValidPath(dst)) {
        return -4;
    }
    if (strlen(dst) == 1 && strcmp(dst, "/") == 0) {
        return -11;
    }

    // printf("src: %s, dst: %s\n", src, dst);

    char src_prefix[strlen(src) + 1];
    strcpy(src_prefix, src);
    char* src_dirPath = dirname(src_prefix);

    char src_suffix[strlen(src) + 1];
    strcpy(src_suffix, src);
    char* src_fileName = basename(src_suffix);

    // printf("src_path: %s, src_fileName: %s\n", src_dirPath, src_fileName);
    
    if (strlen(src_fileName) >= FS_FNAME_MAX) {
        return -5;
    }

    char dst_prefix[strlen(dst) + 1];
    strcpy(dst_prefix, dst);
    char* dst_dirPath = dirname(dst_prefix);

    char dst_suffix[strlen(dst) + 1];
    strcpy(dst_suffix, dst);
    char* dst_fileName = basename(dst_suffix);

    // printf("dst_path: %s, dst_fileName: %s\n", dst_dirPath, dst_fileName);
    
    if (strlen(dst_fileName) >= FS_FNAME_MAX) {
        return -6;
    }

    size_t src_parentDirInodeID = getParentDirInodeID(fs, src_dirPath);
    if (src_parentDirInodeID == SIZE_MAX) {
        return -7;
    }

    if (!checkFileExist(fs, src_parentDirInodeID, src_fileName)) {
        return -8;
    }
    // printf("src_path: %s, src_parentDirInodeID: %lu\n", src_dirPath, src_parentDirInodeID);

    size_t dst_parentDirInodeID = getParentDirInodeID(fs, dst_dirPath);
    if (dst_parentDirInodeID == SIZE_MAX) {
        return -9;
    }
    if (checkFileExist(fs, dst_parentDirInodeID, dst_fileName)) {
        return -10;
    }
    // printf("dst_path: %s, dst_parentDirInodeID: %lu\n", dst_dirPath, dst_parentDirInodeID);

    inode_t src_parentDirInode;
    block_store_inode_read(fs->BlockStore_inode, src_parentDirInodeID, &src_parentDirInode);

    inode_t dst_parentDirInode;
    block_store_inode_read(fs->BlockStore_inode, dst_parentDirInodeID, &dst_parentDirInode);

    directoryFile_t* dst_directoryFile = calloc(1, BLOCK_SIZE_BYTES);
    block_store_read(fs->BlockStore_whole, dst_parentDirInode.directPointer[0], dst_directoryFile);
    bitmap_t* dirBM = bitmap_overlay(NUM_OF_ENTRIES, &(dst_parentDirInode.vacantFile));
    if (bitmap_total_set(dirBM) == NUM_OF_ENTRIES) {
        return -13;
    }
    bitmap_destroy(dirBM);

    size_t src_fileInodeId = getFileInodeID(fs, src_parentDirInodeID, src_fileName);
    if (src_fileInodeId == SIZE_MAX) {
        return -11;
    }
    // printf("src_filename: %s, src_fileInodeID: %lu\n", src_fileName, src_fileInodeId);

    size_t dst_fileInodeId = getFileInodeID(fs, dst_parentDirInodeID, dst_fileName);
    if (dst_fileInodeId == SIZE_MAX) {
        return -12;
    }

    // printf("dst_filename: %s, dst_fileInodeID: %lu\n", dst_fileName, dst_fileInodeId);

    inode_t src_fileInode;
    block_store_inode_read(fs->BlockStore_inode, src_fileInodeId, &src_fileInode);

    if (src_fileInode.linkCount >= 255) {
        return -14;
    }

    inode_t dst_fileInode;
    block_store_inode_read(fs->BlockStore_inode, dst_fileInodeId, &dst_fileInode);

    bitmap_t* dst_dirBM = bitmap_overlay(NUM_OF_ENTRIES, &(dst_parentDirInode.vacantFile));
    size_t index = bitmap_ffz(dst_dirBM);
    bitmap_set(dst_dirBM, index);
    bitmap_destroy(dst_dirBM);

    (dst_directoryFile + index)->inodeNumber = src_fileInodeId;
    strncpy((dst_directoryFile + index)->filename, dst_fileName, FS_FNAME_MAX);

    src_fileInode.linkCount += 1;
    if (src_fileInodeId == dst_parentDirInodeID) {
        src_fileInode.linkCount += 1;
    }

    block_store_inode_write(fs->BlockStore_inode, src_fileInodeId, &src_fileInode);
    block_store_inode_write(fs->BlockStore_inode, dst_parentDirInodeID, &dst_parentDirInode);
    block_store_write(fs->BlockStore_whole, dst_parentDirInode.directPointer[0], dst_directoryFile);

    free(dst_directoryFile);
    return 0;
}


