/**
 * CoreFS - Internal Types
 * NICHT public API!
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "corefs.h"

// ============================================
// INTERNAL CONSTANTS
// ============================================

#define COREFS_METADATA_BLOCKS  4           // Reserved: 0=SB, 1=Root, 2=TXN, 3=Wear
#define COREFS_BTREE_ORDER      8           // B-Tree order
#define COREFS_MAX_BLOCKS       128         // Max blocks per file
#define COREFS_TXN_LOG_SIZE     128         // Max transaction entries

// Magic numbers
#define COREFS_FILE_MAGIC       0x46494C45  // "FILE"
#define COREFS_BTREE_MAGIC      0x42545245  // "BTRE"

// ============================================
// SUPERBLOCK
// ============================================

typedef struct __attribute__((packed)) {
    uint32_t magic;                         // COREFS_MAGIC
    uint16_t version;                       // COREFS_VERSION
    uint16_t flags;                         // Reserved
    
    uint32_t block_size;                    // 2048
    uint32_t block_count;                   // Total blocks
    uint32_t blocks_used;                   // Used blocks
    
    uint32_t root_block;                    // B-Tree root (block 1)
    uint32_t txn_log_block;                 // Transaction log (block 2)
    uint32_t wear_table_block;              // Wear table (block 3)
    
    uint32_t mount_count;                   // Boot counter
    uint32_t clean_unmount;                 // 1=clean, 0=dirty
    
    uint8_t reserved[3960];                 // Pad to 4KB
    uint32_t checksum;                      // CRC32
} corefs_superblock_t;

// ============================================
// B-TREE NODE
// ============================================

typedef struct __attribute__((packed)) {
    uint32_t magic;                         // COREFS_BTREE_MAGIC
    uint16_t type;                          // 0=internal, 1=leaf
    uint16_t count;                         // Entries used
    
    uint32_t parent;                        // Parent block
    uint32_t children[COREFS_BTREE_ORDER];  // Child blocks
    
    struct {
        uint32_t inode_block;               // Inode block number
        uint32_t name_hash;                 // FNV-1a hash
        char name[COREFS_MAX_FILENAME];     // Filename
    } entries[COREFS_BTREE_ORDER - 1];
    
    uint8_t padding[256];                   // Pad to 2KB
} corefs_btree_node_t;

// ============================================
// INODE (File Metadata)
// ============================================

typedef struct __attribute__((packed)) {
    uint32_t magic;                         // COREFS_FILE_MAGIC
    uint32_t inode_num;                     // Inode number
    
    uint64_t size;                          // File size
    uint32_t blocks_used;                   // Blocks allocated
    uint32_t block_list[COREFS_MAX_BLOCKS]; // Block numbers
    
    uint32_t created;                       // Timestamp
    uint32_t modified;                      // Timestamp
    
    uint16_t flags;                         // File flags
    char name[COREFS_MAX_FILENAME];         // Filename
    
    uint8_t reserved[1340];                 // Pad to 2KB
    uint32_t checksum;                      // CRC32
} corefs_inode_t;

// ============================================
// TRANSACTION LOG
// ============================================

typedef struct __attribute__((packed)) {
    uint32_t op;                            // 1=BEGIN, 2=WRITE, 3=DELETE, 4=COMMIT
    uint32_t inode;                         // Target inode
    uint32_t block;                         // Target block
    uint32_t timestamp;                     // When
} corefs_txn_entry_t;

// ============================================
// FILE HANDLE (Internal)
// ============================================

struct corefs_file_s {
    char path[256];                         // File path
    corefs_inode_t* inode;                  // Inode (in RAM)
    uint32_t inode_block;                   // Inode block number
    uint32_t position;                      // Current offset
    uint32_t flags;                         // Open flags
    bool dirty;                             // Modified?
};

// ============================================
// GLOBAL CONTEXT
// ============================================

typedef struct {
    const esp_partition_t* partition;       // Flash partition
    corefs_superblock_t* sb;                // Superblock (in RAM)
    
    uint8_t* block_bitmap;                  // Allocation bitmap
    uint16_t* wear_table;                   // Wear counts
    
    corefs_file_t* open_files[COREFS_MAX_OPEN_FILES];
    
    uint32_t next_inode_num;                // Counter
    bool mounted;                           // Mount state
} corefs_ctx_t;