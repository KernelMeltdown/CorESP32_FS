/**
 * CoreFS - Custom Filesystem für ESP32-C6
 * Public API Header
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================
// CONSTANTS
// ============================================

#define COREFS_VERSION          0x0100      // v1.0
#define COREFS_MAGIC            0x43524653  // "CRFS"
#define COREFS_BLOCK_SIZE       2048        // 2KB blocks
#define COREFS_SECTOR_SIZE      4096        // Flash sector
#define COREFS_MAX_FILENAME     64          // Max filename length
#define COREFS_MAX_OPEN_FILES   16          // Max concurrent open files

// File flags
#define COREFS_O_RDONLY         0x01
#define COREFS_O_WRONLY         0x02
#define COREFS_O_RDWR           0x03
#define COREFS_O_CREAT          0x04
#define COREFS_O_TRUNC          0x08
#define COREFS_O_APPEND         0x10

// Seek modes
#define COREFS_SEEK_SET         0
#define COREFS_SEEK_CUR         1
#define COREFS_SEEK_END         2

// ============================================
// TYPES
// ============================================

// Opaque file handle
typedef struct corefs_file_s corefs_file_t;

// Filesystem info
typedef struct {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t blocks_used;
} corefs_info_t;

// ============================================
// LIFECYCLE API
// ============================================

/**
 * Format partition as CoreFS
 * WARNING: Erases all data!
 */
esp_err_t corefs_format(const esp_partition_t* partition);

/**
 * Mount CoreFS partition
 * Must be called before any file operations
 */
esp_err_t corefs_mount(const esp_partition_t* partition);

/**
 * Unmount filesystem
 * Flushes all pending writes
 */
esp_err_t corefs_unmount(void);

/**
 * Check if filesystem is mounted
 */
bool corefs_is_mounted(void);

/**
 * Get filesystem information
 */
esp_err_t corefs_info(corefs_info_t* info);

// ============================================
// FILE API
// ============================================

/**
 * Open/Create file
 * 
 * @param path File path (e.g., "/test.txt")
 * @param flags COREFS_O_* flags
 * @return File handle or NULL on error
 */
corefs_file_t* corefs_open(const char* path, uint32_t flags);

/**
 * Read from file
 * 
 * @return Number of bytes read, or -1 on error
 */
int corefs_read(corefs_file_t* file, void* buf, size_t size);

/**
 * Write to file
 * 
 * @return Number of bytes written, or -1 on error
 */
int corefs_write(corefs_file_t* file, const void* buf, size_t size);

/**
 * Seek to position
 * 
 * @return New position, or -1 on error
 */
int corefs_seek(corefs_file_t* file, int offset, int whence);

/**
 * Get current position
 */
size_t corefs_tell(corefs_file_t* file);

/**
 * Get file size
 */
size_t corefs_size(corefs_file_t* file);

/**
 * Close file
 */
esp_err_t corefs_close(corefs_file_t* file);

// ============================================
// FILE MANAGEMENT API
// ============================================

/**
 * Delete file
 */
esp_err_t corefs_unlink(const char* path);

/**
 * Check if file exists
 */
bool corefs_exists(const char* path);

/**
 * Rename file
 */
esp_err_t corefs_rename(const char* old_path, const char* new_path);

// ============================================
// MAINTENANCE API
// ============================================

/**
 * Filesystem check (fsck)
 * Verifies integrity and repairs if possible
 */
esp_err_t corefs_check(void);

/**
 * Get wear leveling statistics
 */
esp_err_t corefs_wear_stats(uint16_t* min_wear, uint16_t* max_wear, uint16_t* avg_wear);

#ifdef __cplusplus
}
#endif