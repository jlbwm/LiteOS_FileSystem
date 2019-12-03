Milestone 0: Design ✔️

Milestone 1: Building Blocks
Due: Monday 11/11 11:59 PM

Milestone 2: Directories
Due: Monday 11/18 11:59 PM

Milestone 3: Data
Due: Monday 12/2 11:59 PM

# Block Store

1. truncate
- int truncate(const char *path, off_t length);
- int ftruncate(int fd, off_t length);

The truncate() and ftruncate() functions cause the regular file named by path or referenced by fd to be truncated to a size of precisely length bytes.

If the file previously was larger than this size, the extra data is lost. If the file previously was shorter, it is extended, and the extended part reads as null bytes ('\0').

The file offset is not changed.

On success, zero is returned. On error, -1 is returned, and errno is set appropriately.

2. mmap
- void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);

creates a new mapping in the virtual address space of the calling process.  

The starting address for the new mapping is specified in addr.  

The length argument specifies the length of the mapping (which must be greater than 0).

The prot argument describes the desired memory protection of the mapping (and must not conflict with the open mode of the file). 

The flags argument determines whether updates to the mapping are visible to other processes mapping the same region, and whether updates are carried through to the underlying file.

3. munmap
- int munmap(void *addr, size_t length);

The munmap() system call deletes the mappings for the specified address range, and causes further references to addresses within the range to generate invalid memory references.  

The region is also automatically unmapped when the process is terminated.  On the other hand, closing the file descriptor does not unmap the region.


# TEST

1. memset
- void* memset(void* ptr, int value, size_t num);

ptr: Pointer to the block of memory to fill
value: Value to be set. The value is passed as an int, but the function fills the block of memory using the unsigned char conversion of this value
num: Number of bytes to be set to the value.

2. memcpy
- void* memcpy(void* destination, const void* source, size_t num);

Copies the values of num bytes from the location pointed to by source directly to the memory block pointed to by destination. 


# FILE_DESCRIPTOR

1. off_t

This is a signed integer type used to represent file sizes. In the GNU C Library, this type is no narrower than int. If the source is compiled with _FILE_OFFSET_BITS == 64 this type is transparently replaced by off64_t.

So in 64 bits machine, off_t equals to size_t which is 8 byte

