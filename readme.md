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

creates a new mapping in the <>virtual address space of the calling process.  

The starting address for the new mapping is specified in addr.  

The length argument specifies the length of the mapping (which must be greater than 0).

The prot argument describes the desired memory protection of the mapping (and must not conflict with the open mode of the file). 

The flags argument determines whether updates to the mapping are visible to other processes mapping the same region, and whether updates are carried through to the underlying file.

3. munmap
- int munmap(void *addr, size_t length);

The munmap() system call deletes the mappings for the specified address range, and causes further references to addresses within the range to generate invalid memory references.  

The region is also automatically unmapped when the process is terminated.  On the other hand, closing the file descriptor does not unmap the region.


# Note
Please note that:
1. All errors are treated as fatal errors. That means, failure on one ASSERT statement in a certain test will cause force break of execution, and the rest statements in that test have no opportunity to execute, and you have no way to pick up scores for those checkings. So please try to pass every ASSERT statement in each test.
2. Please uncomment the tests for corresponding milestones.
3. You can add more library functions for bitmap or blockstore as you see convenient. You cannot modify the tests.cpp except for the last milestone.
4. Please be careful about memory leaks. You, as programmers, are responsible for releasing memory after you're done. Smart pointers? good idea.
5. No cheating. Our plagiarism detection tool can easily find similar code. 
