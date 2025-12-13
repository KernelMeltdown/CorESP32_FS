/**
 * CoreFS v1.0 - Complete Header
 * ALL structures match existing code
 */

#ifndef COREFS_H
#define COREFS_H

#include "esp_err.h"
#include "esp_partition.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// CONSTANTS
// ============================================

#define COREFS_MAGIC           0x43524653  // "CRFS"
#define COREFS_VERSION         0x0100      // v1.0
#define COREFS_BLOCK_MAGIC     0x424C4B00  // "BLK"
#define COREFS_BTREE_MAGIC     0x42545245  // "BTRE"
#define COREFS_FILE_MAGIC      0x46494C45  // "FILE"

#define COREFS_BLOCK_SIZE      2048
#define COREFS_SECTOR_SIZE     4096
#define COREFS_MAX_FILENAME    255
#define COREFS_MAX_PATH        512
#define COREFS_MAX_OPEN_FILES  16
#define COREFS_MAX_BLOCKS      128         // Max blocks per file
#define COREFS_BTREE_ORDER     8
#define COREFS_TXN_LOG_SIZE    128
#define COREFS_METADATA_BLOCKS 4

// File Flags
#define COREFS_O_RDONLY        0x01
#define COREFS_O_WRONLY        0x02
#define COREFS_O_RDWR          0x03
#define COREFS_O_CREAT         0x04
#define COREFS_O_TRUNC         0x08
#define COREFS_O_APPEND        0x10

// Seek Modes
#define COREFS_SEEK_SET        0
#define COREFS_SEEK_CUR        1
#define COREFS_SEEK_END        2

// ============================================
// DATA STRUCTURES
// ============================================

// Superblock (Block 0)
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t blocks_used;
    uint32_t root_block;
    uint32_t txn_log_block;
    uint32_t wear_table_block;
    uint32_t mount_count;
    uint32_t clean_unmount;
    uint8_t reserved[4000];
    uint32_t checksum;
} corefs_superblock_t;

// B-Tree Node
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t type;
    uint16_t count;
    uint32_t parent;
    uint32_t children[COREFS_BTREE_ORDER];
    struct {
        uint32_t inode_block;  // ← CORRECT field name
        uint32_t name_hash;
        char name[64];
    } entries[COREFS_BTREE_ORDER - 1];
    uint8_t padding[512];
} corefs_btree_node_t;

// Inode (File Metadata)
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t inode_num;
    uint64_t size;
    uint32_t blocks_used;
    uint32_t block_list[COREFS_MAX_BLOCKS];
    uint32_t created;
    uint32_t modified;
    uint16_t mode;
    uint16_t flags;
    char name[COREFS_MAX_FILENAME];  // ← ADD: filename in inode
    uint8_t reserved[1145];          // ← ADJUST padding
    uint32_t checksum;               // ← CORRECT field name
} corefs_inode_t;

// Transaction Entry
typedef struct {
    uint32_t op;
    uint32_t inode;
    uint32_t block;
    uint32_t timestamp;
} corefs_txn_entry_t;

// File Handle (In-Memory)
typedef struct {
    char path[COREFS_MAX_PATH];
    corefs_inode_t* inode;
    uint32_t inode_block;
    uint32_t position;    // ← ADD: current offset
    uint32_t flags;
    bool dirty;           // ← ADD: modified flag
    bool valid;
} corefs_file_t;

// Memory-Mapped File
typedef struct {
    const void* data;
    uint32_t size;
    uint32_t flash_addr;
} corefs_mmap_t;

// Filesystem Info
typedef struct {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t blocks_used;
    uint32_t mount_count;
} corefs_info_t;

// Context (Global State)
typedef struct {
    const esp_partition_t* partition;
    corefs_superblock_t* sb;
    uint8_t* block_bitmap;
    uint16_t* wear_table;
    corefs_file_t* open_files[COREFS_MAX_OPEN_FILES];
    uint32_t next_inode_num;
    bool mounted;
} corefs_ctx_t;

// ============================================
// PUBLIC API
// ============================================

// Lifecycle
esp_err_t corefs_format(const esp_partition_t* partition);
esp_err_t corefs_mount(const esp_partition_t* partition);
esp_err_t corefs_unmount(void);
bool corefs_is_mounted(void);

// File Operations
corefs_file_t* corefs_open(const char* path, uint32_t flags);
int corefs_read(corefs_file_t* file, void* buf, size_t size);
int corefs_write(corefs_file_t* file, const void* buf, size_t size);
int corefs_seek(corefs_file_t* file, int offset, int whence);
size_t corefs_tell(corefs_file_t* file);
size_t corefs_size(corefs_file_t* file);
esp_err_t corefs_close(corefs_file_t* file);

// File Management
esp_err_t corefs_unlink(const char* path);
bool corefs_exists(const char* path);

// Info
esp_err_t corefs_info(corefs_info_t* info);
esp_err_t corefs_check(void);

// Memory-Mapped Files
corefs_mmap_t* corefs_mmap(const char* path);
void corefs_munmap(corefs_mmap_t* mmap);

// VFS Integration
esp_err_t corefs_vfs_register(const char* base_path);
esp_err_t corefs_vfs_unregister(const char* base_path);

// ============================================
// INTERNAL API
// ============================================

// Superblock
esp_err_t corefs_superblock_read(corefs_ctx_t* ctx);
esp_err_t corefs_superblock_write(corefs_ctx_t* ctx);
esp_err_t corefs_superblock_init(corefs_ctx_t* ctx);

// Block Manager
esp_err_t corefs_block_init(corefs_ctx_t* ctx);
esp_err_t corefs_block_read(corefs_ctx_t* ctx, uint32_t block, void* buf);
esp_err_t corefs_block_write(corefs_ctx_t* ctx, uint32_t block, const void* buf);
uint32_t corefs_block_alloc(corefs_ctx_t* ctx);
void corefs_block_free(corefs_ctx_t* ctx, uint32_t block);
uint32_t corefs_block_get_flash_addr(corefs_ctx_t* ctx, uint32_t block);

// B-Tree
esp_err_t corefs_btree_init(corefs_ctx_t* ctx);
esp_err_t corefs_btree_load(corefs_ctx_t* ctx);  // ← ADD: load from flash
int32_t corefs_btree_find(corefs_ctx_t* ctx, const char* path);
esp_err_t corefs_btree_insert(corefs_ctx_t* ctx, const char* path, uint32_t inode_block);
esp_err_t corefs_btree_delete(corefs_ctx_t* ctx, const char* path);

// Inode
esp_err_t corefs_inode_read(corefs_ctx_t* ctx, uint32_t block, corefs_inode_t* inode);
esp_err_t corefs_inode_write(corefs_ctx_t* ctx, uint32_t block, const corefs_inode_t* inode);
esp_err_t corefs_inode_create(corefs_ctx_t* ctx, const char* filename, uint32_t* out_block);
esp_err_t corefs_inode_delete(corefs_ctx_t* ctx, uint32_t inode_block);

// Transaction
void corefs_txn_begin(void);
void corefs_txn_log(uint32_t op, uint32_t inode, uint32_t block);
esp_err_t corefs_txn_commit(corefs_ctx_t* ctx);
void corefs_txn_rollback(void);

// Wear Leveling
esp_err_t corefs_wear_load(corefs_ctx_t* ctx);
esp_err_t corefs_wear_save(corefs_ctx_t* ctx);
esp_err_t corefs_wear_check(corefs_ctx_t* ctx);
uint32_t corefs_wear_get_best_block(corefs_ctx_t* ctx);

// Recovery
esp_err_t corefs_recovery_scan(corefs_ctx_t* ctx);

// ============================================
// UTILITY
// ============================================

uint32_t crc32(const void* data, size_t len);

#ifdef __cplusplus
}
#endif

#endif // COREFS_H