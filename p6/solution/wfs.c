#include "wfs.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <limits.h>
#include <stdarg.h>


// Global variables to store filesystem state
static int allocated_block_count = 0;
static struct wfs_sb *superblock = NULL;
static void *disk_images[MAX_DISK_NUM] = {NULL};
static size_t disk_sizes[MAX_DISK_NUM];
static int num_disks = 0;
static int raid_mode = 0;

void initialize_disks(int argc, char *argv[]) {
    num_disks = argc - 2; // Adjust for program name and mount point
    struct stat st;
    
    for (int i = 0; i < num_disks; i++) {
        int fd = open(argv[i + 1], O_RDWR);
        if (fd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }
        if (fstat(fd, &st) == -1) {
            perror("fstat");
            close(fd);
            exit(EXIT_FAILURE);
        }
        disk_sizes[i] = st.st_size;
        disk_images[i] = mmap(NULL, disk_sizes[i], PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (disk_images[i] == MAP_FAILED) {
            perror("mmap");
            close(fd);
            exit(EXIT_FAILURE);
        }
        close(fd);
    }
    
    // Read superblock from first disk
    superblock = (struct wfs_sb *)disk_images[0];
    raid_mode = superblock->raid_mode;
    
    // Verify number of disks matches superblock
    if (num_disks != superblock->num_disks) {
        fprintf(stderr, "Error: Number of disks doesn't match filesystem\n");
        exit(EXIT_FAILURE);
    }
}



int allocate_inode() {
    int inode_num = -1;
    // Find a free inode
    for (int i = 0; i < superblock->num_inodes; i++) {
        char *inode_bitmap = (char *)disk_images[0] + superblock->i_bitmap_ptr;
        if (!(inode_bitmap[i / 8] & (1 << (i % 8)))) {
            inode_num = i;
            break;
        }
    }
    if (inode_num < 0) {
        return -ENOSPC;
    }

    // Mark the inode as allocated on all disks
    for (int disk = 0; disk < num_disks; disk++) {
        char *inode_bitmap_disk = (char *)disk_images[disk] + superblock->i_bitmap_ptr;
        inode_bitmap_disk[inode_num / 8] |= (1 << (inode_num % 8));
    }
    return inode_num;
}

int read_data_block(off_t block_num, char *buffer) {
    if (raid_mode == 0) {
        // RAID 0: striping
        int disk_num = block_num % num_disks;
        off_t block_offset = block_num / num_disks;
        off_t disk_offset = superblock->d_blocks_ptr + block_offset * BLOCK_SIZE;
        memcpy(buffer, (char *)disk_images[disk_num] + disk_offset, BLOCK_SIZE);
    } else {
        // RAID 1 or RAID 1v
        off_t disk_offset = superblock->d_blocks_ptr + block_num * BLOCK_SIZE;
        if (raid_mode == 1) {
            // Simple mirroring, read from first disk
            memcpy(buffer, (char *)disk_images[0] + disk_offset, BLOCK_SIZE);
        } else 
        {
            // RAID 1v: Compare all copies
            char temp_buffers[MAX_DISK_NUM][BLOCK_SIZE];
            int votes[MAX_DISK_NUM] = {0};
            int max_votes = 0;
            int winning_disk = 0;

            // Read from all disks
            for (int i = 0; i < num_disks; i++) {
                memcpy(temp_buffers[i], (char *)disk_images[i] + disk_offset, BLOCK_SIZE);
            }

            // Vote counting
            for (int i = 0; i < num_disks; i++) {
                votes[i] = 1;  // self vote
                for (int j = i + 1; j < num_disks; j++) {
                    if (memcmp(temp_buffers[i], temp_buffers[j], BLOCK_SIZE) == 0) {
                        votes[i]++;
                        votes[j]++;
                    }
                }
                // 只在票数严格大于时更新获胜者
                if (votes[i] > max_votes) {
                    max_votes = votes[i];
                    winning_disk = i;
                }
            }
            memcpy(buffer, temp_buffers[winning_disk], BLOCK_SIZE);
        }
    }
    return 0;
}


int write_data_block(off_t block_num, const char *buffer) {
    if (raid_mode == 0) {
        // RAID 0: striping
        int disk_num = block_num % num_disks;
        off_t block_offset = block_num / num_disks;
        off_t disk_offset = superblock->d_blocks_ptr + block_offset * BLOCK_SIZE;
        memcpy((char *)disk_images[disk_num] + disk_offset, buffer, BLOCK_SIZE);
    } else {
        // RAID 1 and RAID 1v: Write to all disks
        off_t disk_offset = superblock->d_blocks_ptr + block_num * BLOCK_SIZE;
        for (int disk = 0; disk < num_disks; disk++) {
            memcpy((char *)disk_images[disk] + disk_offset, buffer, BLOCK_SIZE);
        }
    }
    return 0;
}

void free_inode(int inode_num) {
    for (int disk = 0; disk < num_disks; disk++) {
        char *inode_bitmap = (char *)disk_images[disk] + superblock->i_bitmap_ptr;
        inode_bitmap[inode_num / 8] &= ~(1 << (inode_num % 8));
    }
}

struct wfs_inode *get_inode(int inode_num) {
    return (struct wfs_inode *)((char *)disk_images[0] + superblock->i_blocks_ptr + inode_num * BLOCK_SIZE);
}

int allocate_data_block() {
    static int next_disk = 0;
    
    if (raid_mode == 0) {
        // Try each disk starting from next_disk
        int start_disk = next_disk;
        
        do {
            char *data_bitmap = (char *)disk_images[next_disk] + superblock->d_bitmap_ptr;
            
            // Look for a free block
            for (int i = 1; i < superblock->num_data_blocks / num_disks; i++) {
                if (!(data_bitmap[i / 8] & (1 << (i % 8)))) {
                    // Mark block as allocated on this disk
                    data_bitmap[i / 8] |= (1 << (i % 8));
                    
                    // Calculate global block number
                    int block_num = i * num_disks + next_disk;
                    
                    // Move to next disk for next allocation
                    next_disk = (next_disk + 1) % num_disks;
                    allocated_block_count++;
                    return block_num;
                }
            }
            
            next_disk = (next_disk + 1) % num_disks;
        } while (next_disk != start_disk);
        
        return -ENOSPC;
    } else {
        // RAID 1 code remains unchanged
        char *data_bitmap = (char *)disk_images[0] + superblock->d_bitmap_ptr;
        for (int i = 1; i < superblock->num_data_blocks; i++) {
            if (!(data_bitmap[i / 8] & (1 << (i % 8)))) {
                for (int disk = 0; disk < num_disks; disk++) {
                    char *disk_bitmap = (char *)disk_images[disk] + superblock->d_bitmap_ptr;
                    disk_bitmap[i / 8] |= (1 << (i % 8));
                }
                allocated_block_count++;
                return i;
            }
        }
        return -ENOSPC;
    }
}


void free_data_block(int block_num) {
    if (raid_mode == 0) {
        // RAID 0: Free block in specific disk
        int disk_num = block_num % num_disks;
        off_t block_offset = block_num / num_disks;
        char *data_bitmap = (char *)disk_images[disk_num] + superblock->d_bitmap_ptr;
        data_bitmap[block_offset / 8] &= ~(1 << (block_offset % 8));
    } else {
        // RAID 1: Free block in all disks
        for (int disk = 0; disk < num_disks; disk++) {
            char *data_bitmap = (char *)disk_images[disk] + superblock->d_bitmap_ptr;
            data_bitmap[block_num / 8] &= ~(1 << (block_num % 8));
        }
    }
    allocated_block_count--;
}

void update_inode_on_all_disks(struct wfs_inode *inode) {
    for (int disk = 0; disk < num_disks; disk++) {
        struct wfs_inode *inode_disk = (struct wfs_inode *)((char *)disk_images[disk] + superblock->i_blocks_ptr + inode->num * BLOCK_SIZE);
        *inode_disk = *inode;
    }
}

struct wfs_inode *resolve_path(const char *path) {
    if (strcmp(path, "/") == 0) {\
        return get_inode(0);
    }
    char *path_copy = strdup(path);
    char *token = strtok(path_copy, "/");
    struct wfs_inode *current_inode = get_inode(0); // Start from root
    while (token != NULL) {
        if (!S_ISDIR(current_inode->mode)) {
            free(path_copy);
            return NULL;
        }

        struct wfs_inode *next_inode = NULL;
        // Search for the token in the current directory
        for (int i = 0; i < D_BLOCK; i++) {
            if (current_inode->blocks[i] == 0) continue;
            char block_data[BLOCK_SIZE];
            read_data_block(current_inode->blocks[i], block_data);
            struct wfs_dentry *entries = (struct wfs_dentry *)block_data;
            for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
                if (entries[j].num == 0) continue;
                if (strcmp(entries[j].name, token) == 0) {
                    next_inode = get_inode(entries[j].num);
                    break;
                }
            }
            if (next_inode != NULL) break;
        }

        if (next_inode == NULL) {
            free(path_copy);
            return NULL;
        }

        current_inode = next_inode;
        token = strtok(NULL, "/");
    }
    free(path_copy);
    return current_inode;
}

int split_path(const char *path, char *parent, char *name) {
    char *last_slash = strrchr(path, '/');
    if (last_slash == NULL || strcmp(path, "/") == 0) {
        return -1;
    }

    size_t parent_len = last_slash - path;
    if (parent_len == 0) {
        strcpy(parent, "/");
    } else {
        strncpy(parent, path, parent_len);
        parent[parent_len] = '\0';
    }

    strncpy(name, last_slash + 1, MAX_NAME);
    name[MAX_NAME - 1] = '\0';
    return 0;
}

int add_directory_entry(struct wfs_inode *dir_inode, const char *name, int inode_num) {
    struct wfs_dentry entry;
    strncpy(entry.name, name, MAX_NAME);
    entry.name[MAX_NAME - 1] = '\0';
    entry.num = inode_num;

    for (int i = 0; i < D_BLOCK; i++) {
        if (dir_inode->blocks[i] > 0) { 
            char block_data[BLOCK_SIZE];
            read_data_block(dir_inode->blocks[i], block_data);
            struct wfs_dentry *entries = (struct wfs_dentry *)block_data;
            for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
                if (entries[j].num == 0) {
                    entries[j] = entry;
                    // Write the block data using the correct RAID handling
                    write_data_block(dir_inode->blocks[i], block_data);
                    return 0;
                }
            }
        }
    }

    for (int i = 0; i < D_BLOCK; i++) {
        if (dir_inode->blocks[i] == 0) {
            int block_num = allocate_data_block();
            if (block_num < 0) return -ENOSPC;

            dir_inode->blocks[i] = block_num;
            dir_inode->size += BLOCK_SIZE;

            char block_data[BLOCK_SIZE];
            memset(block_data, 0, BLOCK_SIZE);
            struct wfs_dentry *entries = (struct wfs_dentry *)block_data;
            entries[0] = entry;
            // Write the block data using the correct RAID handling
            write_data_block(dir_inode->blocks[i], block_data);
            // **Insert the call here to update dir_inode on all disks**
            update_inode_on_all_disks(dir_inode);
            return 0;
        }
    }

    return -ENOSPC;
}

int remove_directory_entry(struct wfs_inode *dir_inode, const char *name) {
    for (int i = 0; i < D_BLOCK; i++) {
        if (dir_inode->blocks[i] == 0) continue;
        
        char block_data[BLOCK_SIZE];
        read_data_block(dir_inode->blocks[i], block_data);
        struct wfs_dentry *entries = (struct wfs_dentry *)block_data;
        
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (entries[j].num != 0 && strcmp(entries[j].name, name) == 0) {
                // Clear the entry
                entries[j].num = 0;
                memset(entries[j].name, 0, MAX_NAME);
                
                // Write back the modified block
                write_data_block(dir_inode->blocks[i], block_data);
                return 0;
            }
        }
    }
    return -ENOENT;
}

void init_root_inode_if_needed() {
    char *inode_bitmap = (char *)disk_images[0] + superblock->i_bitmap_ptr;

    if (!(inode_bitmap[0] & 1)) {
        // Mark root inode as allocated in all disks
        for (int i = 0; i < num_disks; i++) {
            char *disk_bitmap = (char *)disk_images[i] + superblock->i_bitmap_ptr;
            disk_bitmap[0] |= 1;  // Mark inode 0 as allocated
        }

        // Initialize root inode
        struct wfs_inode root_inode;
        memset(&root_inode, 0, sizeof(struct wfs_inode));
        root_inode.num = 0;
        root_inode.mode = S_IFDIR | 0755;
        root_inode.uid = getuid();
        root_inode.gid = getgid();
        root_inode.size = 0;
        root_inode.nlinks = 2;  // . and ..
        root_inode.atim = root_inode.mtim = root_inode.ctim = time(NULL);
        
        // Allocate and initialize first data block for . and ..
        int block_num = allocate_data_block();
        if (block_num >= 0) {
            root_inode.blocks[0] = block_num;
            root_inode.size = BLOCK_SIZE;

            char block_data[BLOCK_SIZE];
            memset(block_data, 0, BLOCK_SIZE);
            struct wfs_dentry *entries = (struct wfs_dentry *)block_data;

            // Set up . entry
            strncpy(entries[0].name, ".", MAX_NAME);
            entries[0].num = 0;  // Root's inode number

            // Set up .. entry
            strncpy(entries[1].name, "..", MAX_NAME);
            entries[1].num = 0;  // Root's parent is itself

            // Write the directory entries
            write_data_block(block_num, block_data);
        }

        // Write root inode to all disks using the existing function
        update_inode_on_all_disks(&root_inode);
    }
}

int find_dir_entry(struct wfs_inode *dir_inode, const char *name) {
    for (int i = 0; i < D_BLOCK; i++) {
        if (dir_inode->blocks[i] == 0) continue;
        char block_data[BLOCK_SIZE];
        read_data_block(dir_inode->blocks[i], block_data);
        struct wfs_dentry *entries = (struct wfs_dentry *)block_data;
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (entries[j].num == 0) continue;
            if (strcmp(entries[j].name, name) == 0) {
                return entries[j].num;
            }
        }
    }
    return -ENOENT;
}

// Get attributes of a file/directory
static int wfs_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    struct wfs_inode *inode = resolve_path(path);
    if (inode == NULL) {
        return -ENOENT;
    }

    stbuf->st_mode = inode->mode;
    stbuf->st_nlink = inode->nlinks;
    stbuf->st_uid = inode->uid;
    stbuf->st_gid = inode->gid;
    stbuf->st_size = inode->size;
    stbuf->st_atime = inode->atim;
    stbuf->st_mtime = inode->mtim;
    stbuf->st_ctime = inode->ctim;

    return 0;
}

// Create a file node
static int wfs_mknod(const char *path, mode_t mode, dev_t rdev) {
    (void) rdev;
    char parent_path[PATH_MAX];
    char file_name[MAX_NAME];
    if (split_path(path, parent_path, file_name) != 0) {
        return -EINVAL;
    }

    struct wfs_inode *parent_inode = resolve_path(parent_path);
    if (parent_inode == NULL || !S_ISDIR(parent_inode->mode)) {
        return -ENOENT;
    }

    if (find_dir_entry(parent_inode, file_name) >= 0) {
        return -EEXIST;
    }

    int inode_num = allocate_inode();
    if (inode_num < 0) {
        return -ENOSPC;
    }

    struct wfs_inode *new_inode = get_inode(inode_num);
    memset(new_inode, 0, sizeof(struct wfs_inode));
    new_inode->num = inode_num;
    new_inode->mode = mode;
    new_inode->uid = getuid();
    new_inode->gid = getgid();
    new_inode->size = 0;
    new_inode->nlinks = 1;
    new_inode->atim = new_inode->mtim = new_inode->ctim = time(NULL);
    memset(new_inode->blocks, 0, sizeof(new_inode->blocks));
    update_inode_on_all_disks(new_inode);

    if (add_directory_entry(parent_inode, file_name, inode_num) != 0) {
        free_inode(inode_num);
        return -ENOSPC;
    }

    return 0;
}

// Create a directory
static int wfs_mkdir(const char *path, mode_t mode) {
    char parent_path[PATH_MAX];
    char dir_name[MAX_NAME];
    if (split_path(path, parent_path, dir_name) != 0) {
        return -EINVAL;
    }

    struct wfs_inode *parent_inode = resolve_path(parent_path);
    if (parent_inode == NULL || !S_ISDIR(parent_inode->mode)) {
        return -ENOENT;
    }

    if (find_dir_entry(parent_inode, dir_name) >= 0) {
        return -EEXIST;
    }

    int inode_num = allocate_inode();
    if (inode_num < 0) {
        return -ENOSPC;
    }

    // Initialize the new inode on all disks
    for (int disk = 0; disk < num_disks; disk++) {
        struct wfs_inode *new_inode = (struct wfs_inode *)((char *)disk_images[disk] + superblock->i_blocks_ptr + inode_num * BLOCK_SIZE);
        memset(new_inode, 0, sizeof(struct wfs_inode));
        new_inode->num = inode_num;
        new_inode->mode = S_IFDIR | mode;
        new_inode->uid = getuid();
        new_inode->gid = getgid();
        new_inode->size = 0;
        new_inode->nlinks = 1; // only itself
        new_inode->atim = new_inode->mtim = new_inode->ctim = time(NULL);
        memset(new_inode->blocks, 0, sizeof(new_inode->blocks));
    }

    if (add_directory_entry(parent_inode, dir_name, inode_num) != 0) {
        free_inode(inode_num);
        return -ENOSPC;
    }

    // Update parent inode's nlinks and write to all disks
    parent_inode->nlinks++;
    update_inode_on_all_disks(parent_inode);

    return 0;
}

// Remove a file
static int wfs_unlink(const char *path) {
    char parent_path[PATH_MAX];
    char file_name[MAX_NAME];
    if (split_path(path, parent_path, file_name) != 0) {
        return -EINVAL;
    }

    struct wfs_inode *parent_inode = resolve_path(parent_path);
    if (parent_inode == NULL || !S_ISDIR(parent_inode->mode)) {
        return -ENOENT;
    }

    int inode_num = find_dir_entry(parent_inode, file_name);
    if (inode_num < 0) {
        return -ENOENT;
    }

    struct wfs_inode *file_inode = get_inode(inode_num);
    if (S_ISDIR(file_inode->mode)) {
        return -EISDIR;
    }

    // First, remove directory entry from parent
    if (remove_directory_entry(parent_inode, file_name) != 0) {
        return -ENOENT;
    }

    // Free all direct data blocks
    for (int i = 0; i < D_BLOCK; i++) {
        if (file_inode->blocks[i] != 0) {
            free_data_block(file_inode->blocks[i]);
            file_inode->blocks[i] = 0;
        }
    }

    // Handle indirect blocks if present
    if (file_inode->blocks[D_BLOCK] != 0) {
        // Read indirect block
        char indirect_block[BLOCK_SIZE];
        read_data_block(file_inode->blocks[D_BLOCK], indirect_block);
        off_t *indirect_blocks = (off_t *)indirect_block;

        // Free all blocks pointed to by indirect block
        for (int i = 0; i < BLOCK_SIZE/sizeof(off_t); i++) {
            if (indirect_blocks[i] != 0) {
                free_data_block(indirect_blocks[i]);
            }
        }

        // Free the indirect block itself
        free_data_block(file_inode->blocks[D_BLOCK]);
        file_inode->blocks[D_BLOCK] = 0;
    }

    // Zero out inode contents before freeing
    memset(file_inode->blocks, 0, sizeof(file_inode->blocks));
    file_inode->size = 0;
    
    // Update the inode on all disks before freeing
    update_inode_on_all_disks(file_inode);
    
    // Finally free the inode
    free_inode(inode_num);

    // Update parent inode on all disks
    update_inode_on_all_disks(parent_inode);

    return 0;
}





// Remove a directory
static int wfs_rmdir(const char *path) {
    if (strcmp(path, "/") == 0) {
        return -EBUSY;
    }

    char parent_path[PATH_MAX];
    char dir_name[MAX_NAME];
    if (split_path(path, parent_path, dir_name) != 0) {
        return -EINVAL;
    }

    struct wfs_inode *parent_inode = resolve_path(parent_path);
    if (parent_inode == NULL || !S_ISDIR(parent_inode->mode)) {
        return -ENOENT;
    }

    int inode_num = find_dir_entry(parent_inode, dir_name);
    if (inode_num < 0) {
        return -ENOENT;
    }

    struct wfs_inode *dir_inode = get_inode(inode_num);
    if (!S_ISDIR(dir_inode->mode)) {
        return -ENOTDIR;
    }

    // Check if directory is empty (only '.' and '..' entries)
    for (int i = 0; i < D_BLOCK; i++) {
        if (dir_inode->blocks[i] != 0) {
            char block_data[BLOCK_SIZE];
            read_data_block(dir_inode->blocks[i], block_data);
            struct wfs_dentry *entries = (struct wfs_dentry *)block_data;
            
            for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
                if (entries[j].num != 0 && 
                    strcmp(entries[j].name, ".") != 0 && 
                    strcmp(entries[j].name, "..") != 0) {
                    return -ENOTEMPTY;
                }
            }

            // Free the data block after checking
            free_data_block(dir_inode->blocks[i]);
            dir_inode->blocks[i] = 0;
        }
    }

    // Remove directory entry from parent
    if (remove_directory_entry(parent_inode, dir_name) != 0) {
        return -ENOENT;
    }

    // Update parent's link count
    parent_inode->nlinks--;
    update_inode_on_all_disks(parent_inode);

    // Free the directory inode
    free_inode(inode_num);

    return 0;
}

// Read data from a file
static int wfs_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi) {
    (void) fi;
    struct wfs_inode *inode = resolve_path(path);
    if (inode == NULL) {
        return -ENOENT;
    }
    if (S_ISDIR(inode->mode)) {
        return -EISDIR;
    }

    if (offset >= inode->size) {
        return 0;
    }

    if (offset + size > inode->size) {
        size = inode->size - offset;
    }

    size_t bytes_read = 0;
    size_t block_size = BLOCK_SIZE;
    off_t current_offset = offset;

    while (bytes_read < size && current_offset < inode->size) {
        size_t bytes_to_read;
        int disk_num;
        off_t disk_offset;
        off_t block_idx = current_offset / block_size;
        off_t block_offset = current_offset % block_size;

        bytes_to_read = block_size - block_offset;
        if (bytes_to_read > size - bytes_read) {
            bytes_to_read = size - bytes_read;
        }
        if (current_offset + bytes_to_read > inode->size) {
            bytes_to_read = inode->size - current_offset;
        }

        if (raid_mode == 0) {
            // **RAID 0 Logic**
            
            off_t block_num;
            
            if (block_idx < D_BLOCK) {
                block_num = inode->blocks[block_idx];
            } else {
                // Handle indirect blocks for RAID0
                if (inode->blocks[D_BLOCK] == 0) {
                    // No indirect block allocated
                    memset(buf + bytes_read, 0, bytes_to_read);
                    bytes_read += bytes_to_read;
                    current_offset += bytes_to_read;
                    continue;
                }
                
                int indirect_idx = block_idx - D_BLOCK;
                if (indirect_idx >= BLOCK_SIZE / sizeof(off_t)) {
                    // Exceeded maximum file size
                    break;
                }
                
                // Read the indirect block first
                char indirect_block[BLOCK_SIZE];
                off_t indirect_block_num = inode->blocks[D_BLOCK];
                int indirect_disk = indirect_block_num % num_disks;
                off_t indirect_offset = (indirect_block_num / num_disks) * BLOCK_SIZE;
                memcpy(indirect_block, 
                       (char *)disk_images[indirect_disk] + superblock->d_blocks_ptr + indirect_offset, 
                       BLOCK_SIZE);
                
                // Get the actual data block number
                off_t *indirect_blocks = (off_t *)indirect_block;
                block_num = indirect_blocks[indirect_idx];
            }
            
            
            if (block_num == 0) {
                // Unallocated block, return zeros
                memset(buf + bytes_read, 0, bytes_to_read);
            } else {
                // Calculate disk number and offset
                disk_num = block_num % num_disks;
                off_t disk_block_offset = (block_num / num_disks) * block_size + block_offset;
                disk_offset = superblock->d_blocks_ptr + disk_block_offset;

                // Read data from the calculated disk and offset
                memcpy(buf + bytes_read, (char *)disk_images[disk_num] + disk_offset, bytes_to_read);
            }
        } else {
            // RAID 1 and RAID 1v 
            char block_data[BLOCK_SIZE];

            if (block_idx < D_BLOCK) {
                off_t block_num = inode->blocks[block_idx];
                if (block_num == 0) {
                    memset(buf + bytes_read, 0, bytes_to_read);
                } else {
                    read_data_block(block_num, block_data);
                    memcpy(buf + bytes_read, block_data + block_offset, bytes_to_read);
                }
            } else {
                if (inode->blocks[D_BLOCK] == 0) {
                    memset(buf + bytes_read, 0, bytes_to_read);
                } else {
                    int indirect_idx = block_idx - D_BLOCK;
                    if (indirect_idx >= block_size / sizeof(off_t)) {
                        break;
                    }

                    char indirect_block[BLOCK_SIZE];
                    read_data_block(inode->blocks[D_BLOCK], indirect_block);
                    off_t *indirect_blocks = (off_t *)indirect_block;
                    off_t block_num = indirect_blocks[indirect_idx];

                    if (block_num == 0) {
                        memset(buf + bytes_read, 0, bytes_to_read);
                    } else {
                        read_data_block(block_num, block_data);
                        memcpy(buf + bytes_read, block_data + block_offset, bytes_to_read);
                    }
                }
            }
        }

        bytes_read += bytes_to_read;
        current_offset += bytes_to_read;
    }

    return bytes_read;
}

// Write data to a file
static int wfs_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi) {
    (void) fi;
    struct wfs_inode *inode = resolve_path(path);
    if (inode == NULL) return -ENOENT;
    if (S_ISDIR(inode->mode)) return -EISDIR;

    size_t bytes_written = 0;
    off_t current_offset = offset;

    while (bytes_written < size) {
        int block_idx = current_offset / BLOCK_SIZE;
        int block_offset = current_offset % BLOCK_SIZE;
        size_t bytes_to_write = BLOCK_SIZE - block_offset;
        if (bytes_to_write > size - bytes_written) {
            bytes_to_write = size - bytes_written;
        }

        // Allocate new block if needed
        if (block_idx < D_BLOCK) {
            if (inode->blocks[block_idx] == 0) {
                int new_block = allocate_data_block();
                if (new_block < 0) return -ENOSPC;
                inode->blocks[block_idx] = new_block;
                
                // Initialize new block with zeros
                char zero_block[BLOCK_SIZE] = {0};
                write_data_block(new_block, zero_block);
            }
            
            // Read-modify-write if partial block
            char block_data[BLOCK_SIZE];
            if (block_offset > 0 || bytes_to_write < BLOCK_SIZE) {
                read_data_block(inode->blocks[block_idx], block_data);
            }
            
            memcpy(block_data + block_offset, buf + bytes_written, bytes_to_write);
            write_data_block(inode->blocks[block_idx], block_data);
        } else if (block_idx < D_BLOCK + BLOCK_SIZE/sizeof(off_t)) {
            // Handle indirect blocks
            if (inode->blocks[D_BLOCK] == 0) {
                int new_block = allocate_data_block();
                if (new_block < 0) return -ENOSPC;
                inode->blocks[D_BLOCK] = new_block;
                
                // Initialize indirect block
                char zero_block[BLOCK_SIZE] = {0};
                write_data_block(new_block, zero_block);
            }
            
            // Read indirect block
            char indirect_block[BLOCK_SIZE] = {0};
            read_data_block(inode->blocks[D_BLOCK], indirect_block);
            off_t *indirect_blocks = (off_t *)indirect_block;
            int indirect_idx = block_idx - D_BLOCK;
            
            // Allocate new data block if needed
            if (indirect_blocks[indirect_idx] == 0) {
                int new_block = allocate_data_block();
                if (new_block < 0) return -ENOSPC;
                indirect_blocks[indirect_idx] = new_block;
                write_data_block(inode->blocks[D_BLOCK], indirect_block);
                
                // Initialize new block with zeros
                char zero_block[BLOCK_SIZE] = {0};
                write_data_block(new_block, zero_block);
            }
            
            // Read-modify-write if partial block
            char block_data[BLOCK_SIZE];
            if (block_offset > 0 || bytes_to_write < BLOCK_SIZE) {
                read_data_block(indirect_blocks[indirect_idx], block_data);
            }
            
            memcpy(block_data + block_offset, buf + bytes_written, bytes_to_write);
            write_data_block(indirect_blocks[indirect_idx], block_data);
        } else {
            return -EFBIG;
        }

        bytes_written += bytes_to_write;
        current_offset += bytes_to_write;
    }

    // Update file size if needed
    if (current_offset > inode->size) {
        inode->size = current_offset;
    }
    inode->mtim = time(NULL);
    update_inode_on_all_disks(inode);

    return bytes_written;
}


// Read directory entries
static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi) {
    (void) offset;
    (void) fi;

    struct wfs_inode *inode = resolve_path(path);
    if (inode == NULL || !S_ISDIR(inode->mode)) {
        return -ENOENT;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    for (int i = 0; i < D_BLOCK; i++) {
        if (inode->blocks[i] == 0) continue;
        
        char block_data[BLOCK_SIZE];
        if (raid_mode == 0) {
            // RAID 0: Calculate disk and offset
            off_t block_num = inode->blocks[i];
            int disk_num = block_num % num_disks;
            off_t disk_block_offset = (block_num / num_disks) * BLOCK_SIZE;
            off_t disk_offset = superblock->d_blocks_ptr + disk_block_offset;
            
            // Read from the correct disk
            memcpy(block_data, (char *)disk_images[disk_num] + disk_offset, BLOCK_SIZE);
        } else {
            // RAID 1/1v: Use existing read_data_block
            read_data_block(inode->blocks[i], block_data);
        }
        
        struct wfs_dentry *entries = (struct wfs_dentry *)block_data;
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (entries[j].num != 0) {
                filler(buf, entries[j].name, NULL, 0);
            }
        }
    }

    return 0;
}

// FUSE operations structure
static struct fuse_operations ops = {
    .getattr = wfs_getattr,
    .mknod   = wfs_mknod,
    .mkdir   = wfs_mkdir,
    .unlink  = wfs_unlink,
    .rmdir   = wfs_rmdir,
    .read    = wfs_read,
    .write   = wfs_write,
    .readdir = wfs_readdir,
};


int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s disk1 [disk2 ...] mountpoint [FUSE options]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Initialize disks
    initialize_disks(argc - 1, argv);

    // Initialize root inode if needed
    init_root_inode_if_needed();

    // Prepare FUSE arguments
    struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
    if (fuse_opt_add_arg(&args, argv[0]) != 0) {
        fprintf(stderr, "Failed to add program name to FUSE args\n");
        return 1;
    }
    for (int i = 1 + num_disks; i < argc; i++) {
        fuse_opt_add_arg(&args, argv[i]);
    }

    // Run FUSE
    int ret = fuse_main(args.argc, args.argv, &ops, NULL);

    // Cleanup
    fuse_opt_free_args(&args);
    for (int i = 0; i < num_disks; i++) {
        munmap(disk_images[i], disk_sizes[i]);
    }

    return ret;
}