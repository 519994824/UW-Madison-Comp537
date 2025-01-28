// Flags for wmap
#ifndef WMAP_H
#define WMAP_H
#include "types.h"
#define MAP_SHARED 0x0002
#define MAP_ANONYMOUS 0x0004
#define MAP_FIXED 0x0008

// When any system call fails, returns -1
#define FAILED -1
#define SUCCESS 0

// for `getwmapinfo`
#define MAX_WMMAP_INFO 16
struct wmapinfo {
    int total_mmaps;                    // Total number of wmap regions
    int addr[MAX_WMMAP_INFO];           // Starting address of mapping
    int length[MAX_WMMAP_INFO];         // Size of mapping
    int n_loaded_pages[MAX_WMMAP_INFO]; // Number of pages physically loaded into memory
};

struct wmap_region {
    uint addr;      // Start address
    int length;     // Length in bytes
    int flags;      // Flags
    struct file *f; // File pointer for file-backed mappings
};

#endif // WMAP_H


