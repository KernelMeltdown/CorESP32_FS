/**
 * corefs.h - CoreFS Public API + Internal Function Declarations
 * 
 * FIX: Alle fehlenden Function Declarations hinzugefügt
 * Date: 7. Dezember 2025
 * Issue: Build failures wegen implicit declarations
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_partition.h"

// Include types AFTER esp_partition.h
#include "corefs_types.h"

// ============================================================================
// PUBLIC API - User-facing functions
// ============================================================================

/**
 * Format a partition with CoreFS
 * @param partition ESP partition to format
 * @return ESP_OK on success
 */
esp_err_t corefs_format(const esp_partition_t* partition);

/**
 * Mount CoreFS partition
 * @param partition ESP partition to mount
 * @return ESP_OK on success
 */
esp_err_t corefs_mount(const esp_partition_t* partition);

/**
 * Unmount CoreFS
 * @return ESP_OK on success
 */
esp_err_t corefs_unmount(void);

/**
 * Check if CoreFS is mounted
 * @return true if mounted
 */
bool corefs_is_mounted(void);

/**
 * Get filesystem info
 * @param info Output buffer for filesystem stats
 * @return ESP_OK on success
 */
esp_err_t corefs_info(corefs_info_t* info);

/**
 * Open a file
 * @param path File path (must start with /)
 * @param flags Open flags (COREFS_O_*)
 * @return File handle or NULL on error
 */
corefs_file_t* corefs_open(const char* path, uint32_t flags);

/**
 * Read from file
 * @param file File handle
 * @param buf Buffer to read into
 * @param size Number of bytes to read
 * @return Number of bytes read, or -1 on error
 */
int corefs_read(corefs_file_t* file, void* buf, size_t size);

/**
 * Write to file
 * @param file File handle
 * @param buf Buffer to write from
 * @param size Number of bytes to write
 * @return Number of bytes written, or -1 on error
 */
int corefs_write(corefs_file_t* file, const void* buf, size_t size);

/**
 * Seek in file
 * @param file File handle
 * @param offset Offset to seek to
 * @param whence COREFS_SEEK_*
 * @return 0 on success, -1 on error
 */
int corefs_seek(corefs_file_t* file, int offset, int whence);

/**
 * Get current file position
 * @param file File handle
 * @return Current offset
 */
size_t corefs_tell(corefs_file_t* file);

/**
 * Get file size
 * @param file File handle
 * @return File size in bytes
 */
size_t corefs_size(corefs_file_t* file);

/**
 * Close file
 * @param file File handle
 * @return ESP_OK on success
 */
esp_err_t corefs_close(corefs_file_t* file);

/**
 * Delete file
 * @param path File path
 * @return ESP_OK on success
 */
esp_err_t corefs_unlink(const char* path);

/**
 * Check if file exists
 * @param path File path
 * @return true if file exists
 */
bool corefs_exists(const char* path);

// ============================================================================
// INTERNAL API - Function declarations for cross-module calls
// ============================================================================

// ----------------------------------------------------------------------------
// CRC32 (corefs_crc32.c)
// ----------------------------------------------------------------------------
/**
 * Calculate CRC32 checksum
 * @param data Data buffer
 * @param len Length in bytes
 * @return CRC32 value
 */
uint32_t crc32(const void* data, size_t len);

// ----------------------------------------------------------------------------
// Superblock (corefs_superblock.c)
// ----------------------------------------------------------------------------
/**
 * Read superblock from flash
 * @param ctx Filesystem context
 * @return ESP_OK on success
 */
esp_err_t corefs_superblock_read(corefs_ctx_t* ctx);

/**
 * Write superblock to flash
 * @param ctx Filesystem context
 * @return ESP_OK on success
 */
esp_err_t corefs_superblock_write(corefs_ctx_t* ctx);

/**
 * Initialize superblock (during format)
 * @param ctx Filesystem context
 * @return ESP_OK on success
 */
esp_err_t corefs_superblock_init(corefs_ctx_t* ctx);

// ----------------------------------------------------------------------------
// Block Management (corefs_block.c)
// ----------------------------------------------------------------------------
/**
 * Initialize block manager
 * @param ctx Filesystem context
 * @return ESP_OK on success
 */
esp_err_t corefs_block_init(corefs_ctx_t* ctx);

/**
 * Read block from flash
 * @param ctx Filesystem context
 * @param block Block number
 * @param buf Buffer to read into (must be COREFS_BLOCK_SIZE)
 * @return ESP_OK on success
 */
esp_err_t corefs_block_read(corefs_ctx_t* ctx, uint32_t block, void* buf);

/**
 * Write block to flash
 * @param ctx Filesystem context
 * @param block Block number
 * @param buf Buffer to write from (must be COREFS_BLOCK_SIZE)
 * @return ESP_OK on success
 */
esp_err_t corefs_block_write(corefs_ctx_t* ctx, uint32_t block, const void* buf);

/**
 * Allocate a free block
 * @param ctx Filesystem context
 * @return Block number, or 0 if no free blocks
 */
uint32_t corefs_block_alloc(corefs_ctx_t* ctx);

/**
 * Free a block
 * @param ctx Filesystem context
 * @param block Block number to free
 * @return ESP_OK on success
 */
esp_err_t corefs_block_free(corefs_ctx_t* ctx, uint32_t block);

/**
 * Get flash address of block (for debugging)
 * @param ctx Filesystem context
 * @param block Block number
 * @return Physical flash address
 */
uint32_t corefs_block_get_flash_addr(corefs_ctx_t* ctx, uint32_t block);

// ----------------------------------------------------------------------------
// B-Tree (corefs_btree.c)
// ----------------------------------------------------------------------------
/**
 * Initialize B-Tree (during format)
 * @param ctx Filesystem context
 * @return ESP_OK on success
 */
esp_err_t corefs_btree_init(corefs_ctx_t* ctx);

/**
 * Load B-Tree from flash (during mount)
 * @param ctx Filesystem context
 * @return ESP_OK on success
 */
esp_err_t corefs_btree_load(corefs_ctx_t* ctx);

/**
 * Find inode block by path
 * @param ctx Filesystem context
 * @param path File path
 * @return Inode block number, or 0 if not found
 */
uint32_t corefs_btree_find(corefs_ctx_t* ctx, const char* path);

/**
 * Insert entry into B-Tree
 * @param ctx Filesystem context
 * @param path File path
 * @param inode_block Inode block number
 * @return ESP_OK on success
 */
esp_err_t corefs_btree_insert(corefs_ctx_t* ctx, const char* path, uint32_t inode_block);

/**
 * Delete entry from B-Tree
 * @param ctx Filesystem context
 * @param path File path
 * @return ESP_OK on success
 */
esp_err_t corefs_btree_delete(corefs_ctx_t* ctx, const char* path);

// ----------------------------------------------------------------------------
// Inode (corefs_inode.c)
// ----------------------------------------------------------------------------
/**
 * Create new inode
 * @param ctx Filesystem context
 * @param path File path (for logging)
 * @param out_inode Output pointer to inode structure
 * @param out_block Output block number where inode is stored
 * @return ESP_OK on success
 */
esp_err_t corefs_inode_create(corefs_ctx_t* ctx, const char* path, 
                               corefs_inode_t** out_inode, uint32_t* out_block);

/**
 * Read inode from flash
 * @param ctx Filesystem context
 * @param block Block number where inode is stored
 * @param inode Buffer to read inode into
 * @return ESP_OK on success
 */
esp_err_t corefs_inode_read(corefs_ctx_t* ctx, uint32_t block, corefs_inode_t* inode);

/**
 * Write inode to flash
 * @param ctx Filesystem context
 * @param block Block number to write to
 * @param inode Inode structure to write
 * @return ESP_OK on success
 */
esp_err_t corefs_inode_write(corefs_ctx_t* ctx, uint32_t block, corefs_inode_t* inode);

/**
 * Delete inode (frees inode block and all data blocks)
 * @param ctx Filesystem context
 * @param block Inode block number
 * @return ESP_OK on success
 */
esp_err_t corefs_inode_delete(corefs_ctx_t* ctx, uint32_t block);

// ----------------------------------------------------------------------------
// Transaction Log (corefs_transaction.c)
// ----------------------------------------------------------------------------
/**
 * Begin transaction
 */
void corefs_txn_begin(void);

/**
 * Log transaction operation
 * @param op Operation code
 * @param inode Inode number
 * @param block Block number
 */
void corefs_txn_log(uint32_t op, uint32_t inode, uint32_t block);

/**
 * Commit transaction
 * @param ctx Filesystem context
 * @return ESP_OK on success
 */
esp_err_t corefs_txn_commit(corefs_ctx_t* ctx);

/**
 * Rollback transaction
 */
void corefs_txn_rollback(void);

/**
 * Check if transaction is active
 * @return true if transaction is active
 */
bool corefs_txn_is_active(void);

// ----------------------------------------------------------------------------
// Wear Leveling (corefs_wear.c)
// ----------------------------------------------------------------------------
/**
 * Initialize wear leveling
 * @param ctx Filesystem context
 * @return ESP_OK on success
 */
esp_err_t corefs_wear_init(corefs_ctx_t* ctx);

/**
 * Get block with lowest wear count
 * @param ctx Filesystem context
 * @return Block number with lowest wear
 */
uint32_t corefs_wear_get_best_block(corefs_ctx_t* ctx);

/**
 * Increment wear count for block
 * @param ctx Filesystem context
 * @param block Block number
 */
void corefs_wear_increment(corefs_ctx_t* ctx, uint32_t block);

/**
 * Load wear table from flash
 * @param ctx Filesystem context
 * @return ESP_OK on success
 */
esp_err_t corefs_wear_load(corefs_ctx_t* ctx);

/**
 * Save wear table to flash
 * @param ctx Filesystem context
 * @return ESP_OK on success
 */
esp_err_t corefs_wear_save(corefs_ctx_t* ctx);

/**
 * Check wear leveling balance
 * @param ctx Filesystem context
 * @return ESP_OK if balanced, ESP_ERR_INVALID_STATE if unbalanced
 */
esp_err_t corefs_wear_check(corefs_ctx_t* ctx);

// ----------------------------------------------------------------------------
// Recovery (corefs_recovery.c)
// ----------------------------------------------------------------------------
/**
 * Scan filesystem for corruption (called during mount)
 * @param ctx Filesystem context
 * @return ESP_OK on success
 */
esp_err_t corefs_recovery_scan(corefs_ctx_t* ctx);

/**
 * Full filesystem check (fsck)
 * @return ESP_OK if filesystem is consistent
 */
esp_err_t corefs_check(void);

// ----------------------------------------------------------------------------
// VFS Integration (corefs_vfs.c)
// ----------------------------------------------------------------------------
/**
 * Register CoreFS with ESP-IDF VFS
 * @param base_path Mount point (e.g., "/corefs")
 * @return ESP_OK on success
 */
esp_err_t corefs_vfs_register(const char* base_path);

/**
 * Unregister CoreFS from VFS
 * @param base_path Mount point
 * @return ESP_OK on success
 */
esp_err_t corefs_vfs_unregister(const char* base_path);

// ----------------------------------------------------------------------------
// Memory Mapping (corefs_mmap.c)
// ----------------------------------------------------------------------------
/**
 * Memory-map a file (zero-copy read access)
 * @param path File path
 * @return Pointer to mapped region, or NULL on error
 */
corefs_mmap_t* corefs_mmap(const char* path);

/**
 * Unmap file
 * @param mmap Memory-mapped region
 */
void corefs_munmap(corefs_mmap_t* mmap);

// ============================================================================
// GLOBAL CONTEXT (extern declaration)
// ============================================================================
// Defined in corefs_core.c, used by all other modules
extern corefs_ctx_t g_ctx;