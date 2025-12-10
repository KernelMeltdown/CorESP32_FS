#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Include ESP-IDF partition header
#include "esp_partition.h"

// Magic numbers
#define COREFS_MAGIC           0x43524653  // "CRFS"
#define COREFS_VERSION         0x0100      // v1.0
#define COREFS_BLOCK_MAGIC     0x424C4B00  // "BLK"
#define COREFS_INODE_MAGIC     0x494E4F44  // "INOD"

// ⚠️ KRITISCH: Flash Größen
#define COREFS_BLOCK_SIZE      2048        // Logical block (CoreFS unit)
#define COREFS_SECTOR_SIZE     4096        // Physical erase sector (ESP32 Flash)
#define COREFS_PAGE_SIZE       256         // Flash page

// Limits
#define COREFS_MAX_FILENAME    64
#define COREFS_MAX_PATH        512
#define COREFS_MAX_OPEN_FILES  16
#define COREFS_BTREE_ORDER     8
#define COREFS_MAX_FILE_BLOCKS 128         // Max 262 KB per file
#define COREFS_METADATA_BLOCKS 4           // Reserved blocks (0-3)
#define COREFS_TXN_LOG_SIZE    128

// File flags
#define COREFS_O_RDONLY        0x01
#define COREFS_O_WRONLY        0x02
#define COREFS_O_RDWR          0x03
#define COREFS_O_CREAT         0x04
#define COREFS_O_TRUNC         0x08
#define COREFS_O_APPEND        0x10

// Seek modes
#define COREFS_SEEK_SET        0
#define COREFS_SEEK_CUR        1
#define COREFS_SEEK_END        2

// Superblock (fits in one 2KB block)
typedef struct __attribute__((packed)) {
    uint32_t magic;                // COREFS_MAGIC
    uint16_t version;              // COREFS_VERSION
    uint16_t flags;                // Mount flags
    
    uint32_t block_size;           // Block size (2048)
    uint32_t block_count;          // Total blocks
    uint32_t blocks_used;          // Used blocks
    
    uint32_t root_block;           // B-Tree root block
    uint32_t txn_log_block;        // Transaction log block
    uint32_t wear_table_block;     // Wear leveling table block
    
    uint32_t mount_count;          // Boot counter
    uint32_t clean_unmount;        // Clean shutdown flag
    
    uint8_t reserved[2000];        // Future use, padding
    uint32_t crc32;                // CRC32 checksum (not "checksum"!)
} corefs_superblock_t;


// B-Tree node (fits in one 2KB block)
typedef struct __attribute__((packed)) {
    uint32_t magic;                           // COREFS_BLOCK_MAGIC
    uint16_t type;                            // 0=internal, 1=leaf
    uint16_t count;                           // Number of entries
    
    uint32_t parent;                          // Parent block number
    uint32_t children[COREFS_BTREE_ORDER];    // Child block pointers
    
    struct {
        uint32_t inode;                       // Inode block number
        uint32_t name_hash;                   // FNV-1a hash
        char name[COREFS_MAX_FILENAME];       // Filename
    } entries[COREFS_BTREE_ORDER - 1];        // Max 7 entries
    
    uint8_t padding[256];                     // Align to 2KB
} corefs_btree_node_t;

// Inode (file metadata)
typedef struct __attribute__((packed)) {
    uint32_t magic;                           // COREFS_INODE_MAGIC
    uint32_t inode_num;                       // Inode number
    
    uint64_t size;                            // File size in bytes
    uint32_t blocks_used;                     // Number of data blocks
    uint32_t block_list[COREFS_MAX_FILE_BLOCKS]; // Direct block pointers
    
    uint32_t created;                         // Creation timestamp
    uint32_t modified;                        // Modification timestamp
    
    uint16_t flags;                           // File flags
    uint8_t reserved[512];                    // Future use
    
    uint32_t crc32;                           // CRC32 checksum
} corefs_inode_t;

// File handle (in-memory only)
typedef struct {
    char path[COREFS_MAX_PATH];
    corefs_inode_t* inode;
    uint32_t inode_block;
    uint32_t offset;                          // Current read/write position
    uint32_t flags;                           // Open flags
    bool valid;                               // Handle valid flag
} corefs_file_t;

// Mount context (global state)
typedef struct corefs_ctx {
    const esp_partition_t* partition;         // Partition pointer (from ESP-IDF)
    corefs_superblock_t* sb;                  // Superblock
    
    uint8_t* block_bitmap;                    // Block allocation bitmap
    uint16_t* wear_table;                     // Wear count per block
    
    corefs_file_t* open_files[COREFS_MAX_OPEN_FILES]; // Open file handles
    uint32_t next_inode_num;                  // Next inode number to assign
    
    bool mounted;                             // Mount state
} corefs_ctx_t;

// Transaction log entry
typedef struct __attribute__((packed)) {
    uint32_t op;                              // Operation code
    uint32_t inode;                           // Target inode
    uint32_t block;                           // Target block
    uint32_t timestamp;                       // Timestamp
} corefs_txn_entry_t;

// Filesystem info
typedef struct {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint32_t total_blocks;
    uint32_t used_blocks;
    uint32_t free_blocks;
} corefs_info_t;

// Memory-mapped file handle
typedef struct {
    const void* data;
    size_t size;
    uint32_t flash_addr;
} corefs_mmap_t;