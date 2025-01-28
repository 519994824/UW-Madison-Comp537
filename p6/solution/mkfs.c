#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include "wfs.h"

#define MAX_DISKS 10

/**
 * Calculates the minimum size the disk image file must be.
 */
int calculate_min_size(int raid_mode, int num_inodes, int num_data_blocks, int num_disks) {
    int inode_bitmap_size = (num_inodes + 7) / 8;
    int data_blocks_per_disk;
    int inode_table_size = num_inodes * BLOCK_SIZE;

    if (raid_mode == 0) {
        data_blocks_per_disk = (num_data_blocks + num_disks - 1) / num_disks;
    } else {
        data_blocks_per_disk = num_data_blocks;
    }

    int data_bitmap_size_per_disk = (data_blocks_per_disk + 7) / 8;

    int min_size = sizeof(struct wfs_sb) +
                   inode_bitmap_size +
                   data_bitmap_size_per_disk +
                   inode_table_size +
                   data_blocks_per_disk * BLOCK_SIZE;

    return min_size;
}

/**
 * Initializes the superblock structure.
 */
void initialize_superblock(struct wfs_sb *superblock, int raid_mode, int num_inodes, int num_data_blocks, int num_disks, int inode_bitmap_size, int data_bitmap_size_per_disk, int inode_table_size) {
    superblock->num_inodes = num_inodes;
    superblock->num_data_blocks = num_data_blocks;
    superblock->i_bitmap_ptr = sizeof(struct wfs_sb);
    superblock->d_bitmap_ptr = superblock->i_bitmap_ptr + inode_bitmap_size;
    superblock->i_blocks_ptr = (superblock->d_bitmap_ptr + data_bitmap_size_per_disk + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);
    superblock->d_blocks_ptr = superblock->i_blocks_ptr + inode_table_size;
    superblock->magic = WFS_MAGIC;
    superblock->raid_mode = raid_mode;
    superblock->num_disks = num_disks;
    for (int i = 0; i < num_disks; i++) {
        superblock->disk_array[i] = i;
    }
}

/**
 * Initializes the root inode structure.
 */
void initialize_root_inode(struct wfs_inode *root_inode) {
    memset(root_inode, 0, sizeof(struct wfs_inode));
    root_inode->num = 0; // inode number for root
    root_inode->mode = __S_IFDIR | 0755; // rwx permissions
    root_inode->uid = getuid();
    root_inode->gid = getgid();
    root_inode->size = 0;    // root inode should have size 0
    root_inode->nlinks = 2;  // '.' and '..'
    time(&root_inode->atim);
    root_inode->mtim = root_inode->atim;
    root_inode->ctim = root_inode->atim;
}

/**
 * Initializes the inode and data bitmaps.
 */
void initialize_bitmaps(char *inode_bitmap, int inode_bitmap_size, char *data_bitmap, int data_bitmap_size) {
    memset(inode_bitmap, 0, inode_bitmap_size);
    inode_bitmap[0] = 1;  // Mark inode 0 as allocated
    memset(data_bitmap, 0, data_bitmap_size);
}

/**
 * Writes the filesystem structures to a disk.
 */
int write_filesystem_to_disk(const char *disk_name, struct wfs_sb *superblock, char *inode_bitmap, int inode_bitmap_size, char *data_bitmap, int data_bitmap_size_per_disk, struct wfs_inode *root_inode, int num_inodes, int raid_mode, int num_data_blocks, int num_disks, int disk_index) {
    int fd = open(disk_name, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        return -ENOENT;
    }

    int min_size = calculate_min_size(raid_mode, superblock->num_inodes, superblock->num_data_blocks, num_disks);

    // Check if disk image file is too small to accommodate the number of blocks
    if (lseek(fd, 0, SEEK_END) < min_size) {
        close(fd);
        return -1;
    }

    // Zero out disk efficiently
    lseek(fd, 0, SEEK_SET);
    if (ftruncate(fd, min_size) < 0) {
        close(fd);
        return -1;
    }

    // Write superblock
    lseek(fd, 0, SEEK_SET);
    write(fd, superblock, sizeof(struct wfs_sb));

    // Write inode bitmap to disk
    lseek(fd, superblock->i_bitmap_ptr, SEEK_SET);
    write(fd, inode_bitmap, inode_bitmap_size);

    // Write data bitmap to disk
    lseek(fd, superblock->d_bitmap_ptr, SEEK_SET);
    write(fd, data_bitmap, data_bitmap_size_per_disk);

    // Write root inode with padding
    char padding[BLOCK_SIZE - sizeof(struct wfs_inode)] = {0};
    lseek(fd, superblock->i_blocks_ptr, SEEK_SET);
    write(fd, root_inode, sizeof(struct wfs_inode));
    write(fd, padding, sizeof(padding));

    // Write other inodes with padding
    for (int j = 1; j < num_inodes; j++) {
        struct wfs_inode inode = {0};
        write(fd, &inode, sizeof(struct wfs_inode));
        write(fd, padding, sizeof(padding));
    }

    // Write data blocks based on RAID mode
    if (raid_mode == 0) {
        // RAID 0 (Striping)
        for (int j = 0; j < num_data_blocks; j++) {
            if (j % num_disks == disk_index) {
                char data[BLOCK_SIZE] = {0};
                lseek(fd, superblock->d_blocks_ptr + (j / num_disks) * BLOCK_SIZE, SEEK_SET);
                write(fd, data, BLOCK_SIZE);
            }
        }
    } else {
        // RAID 1 and RAID 2 (Mirroring)
        for (int j = 0; j < num_data_blocks; j++) {
            char data[BLOCK_SIZE] = {0};
            lseek(fd, superblock->d_blocks_ptr + j * BLOCK_SIZE, SEEK_SET);
            write(fd, data, BLOCK_SIZE);
        }
    }

    close(fd);
    return 0;
}

/**
 * Initializes the filesystem across all disks.
 */
int create_fs(int raid_mode, int num_inodes, int num_data_blocks, char **disks, int num_disks) {
    // Calculate sizes
    int inode_bitmap_size = (num_inodes + 7) / 8;
    int data_bitmap_size = (num_data_blocks + 7) / 8;
    int inode_table_size = num_inodes * BLOCK_SIZE;
    int data_blocks_per_disk;

    // Adjust data blocks per disk based on RAID mode
    if (raid_mode == 0) {
        data_blocks_per_disk = (num_data_blocks + num_disks - 1) / num_disks;
    } else {
        data_blocks_per_disk = num_data_blocks;
    }

    int data_bitmap_size_per_disk = (data_blocks_per_disk + 7) / 8;

    // Initialize superblock
    struct wfs_sb superblock;
    initialize_superblock(&superblock, raid_mode, num_inodes, num_data_blocks, num_disks, inode_bitmap_size, data_bitmap_size_per_disk, inode_table_size);

    // Initialize root inode
    struct wfs_inode root_inode;
    initialize_root_inode(&root_inode);

    // Initialize bitmaps
    char inode_bitmap[inode_bitmap_size];
    char data_bitmap[data_bitmap_size];
    initialize_bitmaps(inode_bitmap, inode_bitmap_size, data_bitmap, data_bitmap_size);

    // Initialize all disks
    for (int i = 0; i < num_disks; i++) {
        int result = write_filesystem_to_disk(disks[i], &superblock, inode_bitmap, inode_bitmap_size, data_bitmap, data_bitmap_size_per_disk, &root_inode, num_inodes, raid_mode, num_data_blocks, num_disks, i);
        if (result != 0) {
            return result;
        }
    }

    return 0;
}

/**
 * Parses command-line arguments and initializes the filesystem.
 */
int main(int argc, char **argv) {
    // Variables for passed-in arguments
    int raid_mode = -1; // Initialize to invalid
    char *disks[MAX_DISKS];
    int num_disks = 0;
    int num_inodes = 0;
    int num_data_blocks = 0;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        // '-r <raid mode>'
        if (!strcmp(argv[i], "-r") && (i + 1 < argc)) {
            if (!strcmp(argv[i + 1], "0") || !strcmp(argv[i + 1], "1") || !strcmp(argv[i + 1], "1v")) {
                if (!strcmp(argv[i + 1], "1v")) {
                    raid_mode = 2;
                } else {
                    raid_mode = atoi(argv[i + 1]);    // Get following arg
                }
                i++;    // Move onto next CLA
            } else {
                return 1;
            }
        }
        // '-d <disk>'
        else if (!strcmp(argv[i], "-d") && (i + 1 < argc)) {
            if (num_disks < MAX_DISKS) {
                disks[num_disks++] = argv[++i];
            } else {
                return 1;   // Too many disks
            }
        }
        // '-i <num inodes in filesystem>'
        else if (!strcmp(argv[i], "-i") && (i + 1 < argc)) {
            num_inodes = atoi(argv[++i]); // Get following arg
        }
        // '-b <num data blocks in system>'
        else if (!strcmp(argv[i], "-b") && (i + 1 < argc)) {
            num_data_blocks = atoi(argv[++i]); // Get following arg
        } else {
            // Ignore unknown arguments to match original behavior
            continue;
        }
    }

    // Must have at least two disks
    if (num_disks < 2) {
        return 1;   // Pre-run failure
    }

    // Check to see if a RAID mode was passed in
    if (raid_mode == -1) {
        return 1;
    }

    // Round up number of blocks to multiple of 32
    if ((num_data_blocks % 32) != 0) {
        num_data_blocks = ((num_data_blocks + 31) / 32) * 32;
    }
    num_inodes = ((num_inodes + 31) / 32) * 32;

    // Initialize file to empty filesystem
    return create_fs(raid_mode, num_inodes, num_data_blocks, disks, num_disks);
}
