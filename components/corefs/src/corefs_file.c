/**
 * corefs_file.c - File Operations (Open/Read/Write/Close)
 * 
 * CRITICAL FIX: Removed duplicate corefs_format/mount/unmount
 * Those are now ONLY in corefs_core.c
 * 
 * This file only contains file-level operations
 */

#include "corefs.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "corefs_file";

// External global context (defined in corefs_core.c)
extern corefs_ctx_t g_ctx;

// ============================================================================
// FILE OPERATIONS
// ============================================================================

/**
 * Open file
 */
corefs_file_t* corefs_open(const char* path, uint32_t flags) {
    if (!g_ctx.mounted || !path) {
        ESP_LOGE(TAG, "Not mounted or invalid path");
        return NULL;
    }
    
    // Validate path
    if (path[0] != '/') {
        ESP_LOGE(TAG, "Path must start with /: %s", path);
        return NULL;
    }
    
    // Find free file handle
    int fd = -1;
    for (int i = 0; i < COREFS_MAX_OPEN_FILES; i++) {
        if (g_ctx.open_files[i] == NULL) {
            fd = i;
            break;
        }
    }
    
    if (fd < 0) {
        ESP_LOGE(TAG, "Too many open files");
        return NULL;
    }
    
    // Try to find existing file
    uint32_t inode_block = corefs_btree_find(&g_ctx, path);
    
    corefs_file_t* file = calloc(1, sizeof(corefs_file_t));
    if (!file) {
        ESP_LOGE(TAG, "Failed to allocate file handle");
        return NULL;
    }
    
    strncpy(file->path, path, COREFS_MAX_PATH - 1);
    file->path[COREFS_MAX_PATH - 1] = '\0';
    file->flags = flags;
    file->offset = 0;
    file->valid = true;
    
    if (inode_block == 0) {
        // File doesn't exist
        if (!(flags & COREFS_O_CREAT)) {
            ESP_LOGE(TAG, "File not found: %s", path);
            free(file);
            return NULL;
        }
        
        // Create new file
        corefs_inode_t* inode = NULL;
        esp_err_t ret = corefs_inode_create(&g_ctx, path, &inode, &inode_block);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create inode for %s", path);
            free(file);
            return NULL;
        }
        
        file->inode = inode;
        file->inode_block = inode_block;
        
        // Add to B-Tree
        ret = corefs_btree_insert(&g_ctx, path, inode_block);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to insert into B-Tree");
            free(inode);
            free(file);
            return NULL;
        }
        
        ESP_LOGI(TAG, "Created file '%s' at inode block %lu", path, inode_block);
    } else {
        // File exists - load inode
        corefs_inode_t* inode = malloc(sizeof(corefs_inode_t));
        if (!inode) {
            ESP_LOGE(TAG, "Failed to allocate inode");
            free(file);
            return NULL;
        }
        
        esp_err_t ret = corefs_inode_read(&g_ctx, inode_block, inode);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read inode from block %lu", inode_block);
            free(inode);
            free(file);
            return NULL;
        }
        
        file->inode = inode;
        file->inode_block = inode_block;
        
        // Handle truncate flag
        if (flags & COREFS_O_TRUNC) {
            inode->size = 0;
            inode->blocks_used = 0;
            ret = corefs_inode_write(&g_ctx, inode_block, inode);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to truncate file");
            }
        }
        
        // Handle append flag
        if (flags & COREFS_O_APPEND) {
            file->offset = inode->size;
        }
        
        ESP_LOGI(TAG, "Opened file '%s' (size %llu bytes)", path, inode->size);
    }
    
    g_ctx.open_files[fd] = file;
    return file;
}

/**
 * Read from file
 */
int corefs_read(corefs_file_t* file, void* buf, size_t size) {
    if (!file || !file->valid || !buf || !file->inode) {
        ESP_LOGE(TAG, "Invalid file handle or buffer");
        return -1;
    }
    
    if (file->offset >= file->inode->size) {
        return 0;  // EOF
    }
    
    // Limit read to available data
    size_t to_read = size;
    if (file->offset + to_read > file->inode->size) {
        to_read = file->inode->size - file->offset;
    }
    
    size_t total_read = 0;
    uint8_t* dest = (uint8_t*)buf;
    
    while (to_read > 0) {
        uint32_t block_idx = file->offset / COREFS_BLOCK_SIZE;
        uint32_t block_offset = file->offset % COREFS_BLOCK_SIZE;
        
        if (block_idx >= file->inode->blocks_used) {
            break;  // No more blocks
        }
        
        uint32_t block_num = file->inode->block_list[block_idx];
        if (block_num == 0) {
            ESP_LOGW(TAG, "Null block in file at index %lu", block_idx);
            break;
        }
        
        // Read block
        uint8_t block_buf[COREFS_BLOCK_SIZE];
        esp_err_t ret = corefs_block_read(&g_ctx, block_num, block_buf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read block %lu", block_num);
            break;
        }
        
        // Copy data
        size_t chunk = COREFS_BLOCK_SIZE - block_offset;
        if (chunk > to_read) {
            chunk = to_read;
        }
        
        memcpy(dest, block_buf + block_offset, chunk);
        
        dest += chunk;
        file->offset += chunk;
        total_read += chunk;
        to_read -= chunk;
    }
    
    return total_read;
}

/**
 * Write to file
 */
int corefs_write(corefs_file_t* file, const void* buf, size_t size) {
    if (!file || !file->valid || !buf || !file->inode) {
        ESP_LOGE(TAG, "Invalid file handle or buffer");
        return -1;
    }
    
    size_t total_written = 0;
    const uint8_t* src = (const uint8_t*)buf;
    
    while (size > 0) {
        uint32_t block_idx = file->offset / COREFS_BLOCK_SIZE;
        uint32_t block_offset = file->offset % COREFS_BLOCK_SIZE;
        
        if (block_idx >= COREFS_MAX_FILE_BLOCKS) {
            ESP_LOGE(TAG, "File too large (max %u blocks = %u KB)", 
                     COREFS_MAX_FILE_BLOCKS, 
                     COREFS_MAX_FILE_BLOCKS * COREFS_BLOCK_SIZE / 1024);
            break;
        }
        
        // Allocate new block if needed
        if (block_idx >= file->inode->blocks_used) {
            uint32_t new_block = corefs_block_alloc(&g_ctx);
            if (new_block == 0) {
                ESP_LOGE(TAG, "No free blocks available");
                break;
            }
            
            file->inode->block_list[file->inode->blocks_used] = new_block;
            file->inode->blocks_used++;
            
            ESP_LOGD(TAG, "Allocated block %lu for file (index %lu)", 
                     new_block, block_idx);
        }
        
        uint32_t block_num = file->inode->block_list[block_idx];
        
        // Read existing block data (for partial writes)
        uint8_t block_buf[COREFS_BLOCK_SIZE];
        memset(block_buf, 0, COREFS_BLOCK_SIZE);
        
        if (block_offset > 0 || size < COREFS_BLOCK_SIZE) {
            esp_err_t ret = corefs_block_read(&g_ctx, block_num, block_buf);
            if (ret != ESP_OK) {
                // Block might be new, continue
                ESP_LOGD(TAG, "New block %lu, no need to read", block_num);
            }
        }
        
        // Copy new data
        size_t chunk = COREFS_BLOCK_SIZE - block_offset;
        if (chunk > size) {
            chunk = size;
        }
        
        memcpy(block_buf + block_offset, src, chunk);
        
        // Write block
        esp_err_t ret = corefs_block_write(&g_ctx, block_num, block_buf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write block %lu", block_num);
            break;
        }
        
        src += chunk;
        file->offset += chunk;
        total_written += chunk;
        size -= chunk;
        
        // Update file size
        if (file->offset > file->inode->size) {
            file->inode->size = file->offset;
        }
    }
    
    // Update inode on flash
    if (total_written > 0) {
        esp_err_t ret = corefs_inode_write(&g_ctx, file->inode_block, file->inode);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update inode");
        }
    }
    
    return total_written;
}

/**
 * Seek in file
 */
int corefs_seek(corefs_file_t* file, int offset, int whence) {
    if (!file || !file->valid || !file->inode) {
        return -1;
    }
    
    int new_offset;
    
    switch (whence) {
        case COREFS_SEEK_SET:
            new_offset = offset;
            break;
        case COREFS_SEEK_CUR:
            new_offset = file->offset + offset;
            break;
        case COREFS_SEEK_END:
            new_offset = file->inode->size + offset;
            break;
        default:
            ESP_LOGE(TAG, "Invalid whence: %d", whence);
            return -1;
    }
    
    if (new_offset < 0 || (size_t)new_offset > file->inode->size) {
        ESP_LOGE(TAG, "Seek out of bounds: %d (size: %llu)", 
                 new_offset, file->inode->size);
        return -1;
    }
    
    file->offset = new_offset;
    return 0;
}

/**
 * Get current position
 */
size_t corefs_tell(corefs_file_t* file) {
    if (!file || !file->valid) {
        return 0;
    }
    return file->offset;
}

/**
 * Get file size
 */
size_t corefs_size(corefs_file_t* file) {
    if (!file || !file->valid || !file->inode) {
        return 0;
    }
    return file->inode->size;
}

/**
 * Close file
 */
esp_err_t corefs_close(corefs_file_t* file) {
    if (!file || !file->valid) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find file in open files array
    for (int i = 0; i < COREFS_MAX_OPEN_FILES; i++) {
        if (g_ctx.open_files[i] == file) {
            g_ctx.open_files[i] = NULL;
            break;
        }
    }
    
    if (file->inode) {
        free(file->inode);
    }
    
    file->valid = false;
    free(file);
    
    ESP_LOGD(TAG, "File closed");
    return ESP_OK;
}

/**
 * Delete file
 */
esp_err_t corefs_unlink(const char* path) {
    if (!g_ctx.mounted || !path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t inode_block = corefs_btree_find(&g_ctx, path);
    if (inode_block == 0) {
        ESP_LOGE(TAG, "File not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Delete inode (frees all blocks)
    esp_err_t ret = corefs_inode_delete(&g_ctx, inode_block);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete inode");
        return ret;
    }
    
    // Remove from B-Tree
    ret = corefs_btree_delete(&g_ctx, path);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to remove from B-Tree");
    }
    
    ESP_LOGI(TAG, "Deleted file '%s'", path);
    return ESP_OK;
}

/**
 * Check if file exists
 */
bool corefs_exists(const char* path) {
    if (!g_ctx.mounted || !path) {
        return false;
    }
    
    return (corefs_btree_find(&g_ctx, path) != 0);
}