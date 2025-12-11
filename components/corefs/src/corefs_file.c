/**
 * CoreFS - File Operations
 */

#include "corefs.h"
#include "corefs_types.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "corefs_file";

// Forward declarations
extern corefs_ctx_t* corefs_get_context(void);
extern int32_t corefs_btree_find(corefs_ctx_t* ctx, const char* path);
extern esp_err_t corefs_btree_insert(corefs_ctx_t* ctx, const char* path, uint32_t inode_block);
extern esp_err_t corefs_btree_delete(corefs_ctx_t* ctx, const char* path);
extern esp_err_t corefs_inode_create(corefs_ctx_t* ctx, const char* filename, uint32_t* out_inode_block);
extern esp_err_t corefs_inode_read(corefs_ctx_t* ctx, uint32_t inode_block, corefs_inode_t* inode);
extern esp_err_t corefs_inode_write(corefs_ctx_t* ctx, uint32_t inode_block, const corefs_inode_t* inode);
extern esp_err_t corefs_inode_delete(corefs_ctx_t* ctx, uint32_t inode_block);
extern uint32_t corefs_block_alloc(corefs_ctx_t* ctx);
extern void corefs_block_free(corefs_ctx_t* ctx, uint32_t block);
extern esp_err_t corefs_block_read(corefs_ctx_t* ctx, uint32_t block, void* buf);
extern esp_err_t corefs_block_write(corefs_ctx_t* ctx, uint32_t block, const void* buf);

// ============================================
// OPEN
// ============================================

corefs_file_t* corefs_open(const char* path, uint32_t flags) {
    corefs_ctx_t* ctx = corefs_get_context();
    
    if (!ctx->mounted || !path) {
        return NULL;
    }
    
    // Find free file slot
    int slot = -1;
    for (int i = 0; i < COREFS_MAX_OPEN_FILES; i++) {
        if (ctx->open_files[i] == NULL) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        ESP_LOGE(TAG, "Too many open files");
        return NULL;
    }
    
    // Extract filename
    const char* filename = path + 1;  // Skip leading '/'
    
    // Try to find existing file
    int32_t inode_block = corefs_btree_find(ctx, path);
    
    // If not found and CREAT flag set, create new
    if (inode_block < 0 && (flags & COREFS_O_CREAT)) {
        uint32_t new_inode_block = 0;
        esp_err_t ret = corefs_inode_create(ctx, filename, &new_inode_block);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create inode: %s", esp_err_to_name(ret));
            return NULL;
        }
        
        // Insert into B-Tree
        ret = corefs_btree_insert(ctx, path, new_inode_block);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to insert into B-Tree: %s", esp_err_to_name(ret));
            corefs_inode_delete(ctx, new_inode_block);
            return NULL;
        }
        
        inode_block = new_inode_block;
    }
    
    if (inode_block < 0) {
        ESP_LOGE(TAG, "File not found: %s", path);
        return NULL;
    }
    
    // Allocate file handle
    corefs_file_t* file = calloc(1, sizeof(corefs_file_t));
    if (!file) {
        return NULL;
    }
    
    // Load inode
    file->inode = malloc(sizeof(corefs_inode_t));
    if (!file->inode) {
        free(file);
        return NULL;
    }
    
    esp_err_t ret = corefs_inode_read(ctx, inode_block, file->inode);
    if (ret != ESP_OK) {
        free(file->inode);
        free(file);
        return NULL;
    }
    
    // Setup file handle
    strncpy(file->path, path, sizeof(file->path) - 1);
    file->inode_block = inode_block;
    file->position = 0;
    file->flags = flags;
    file->dirty = false;
    
    // Truncate if requested
    if (flags & COREFS_O_TRUNC) {
        // Free all data blocks
        for (uint32_t i = 0; i < file->inode->blocks_used; i++) {
            if (file->inode->block_list[i] != 0) {
                corefs_block_free(ctx, file->inode->block_list[i]);
            }
        }
        file->inode->size = 0;
        file->inode->blocks_used = 0;
        memset(file->inode->block_list, 0, sizeof(file->inode->block_list));
        file->dirty = true;
    }
    
    // Append: seek to end
    if (flags & COREFS_O_APPEND) {
        file->position = file->inode->size;
    }
    
    ctx->open_files[slot] = file;
    
    ESP_LOGD(TAG, "Opened '%s' at inode block %u (size: %llu)", 
             path, inode_block, file->inode->size);
    
    return file;
}

// ============================================
// READ
// ============================================

int corefs_read(corefs_file_t* file, void* buf, size_t size) {
    if (!file || !file->inode || !buf) {
        return -1;
    }
    
    corefs_ctx_t* ctx = corefs_get_context();
    if (!ctx->mounted) {
        return -1;
    }
    
    // Check read permission
    if ((file->flags & COREFS_O_WRONLY) && !(file->flags & COREFS_O_RDWR)) {
        ESP_LOGE(TAG, "File not open for reading");
        return -1;
    }
    
    // Check EOF
    if (file->position >= file->inode->size) {
        return 0;
    }
    
    // Limit to available data
    size_t available = file->inode->size - file->position;
    if (size > available) {
        size = available;
    }
    
    size_t total_read = 0;
    uint8_t* dst = (uint8_t*)buf;
    
    // Allocate block buffer
    uint8_t* block_buf = malloc(COREFS_BLOCK_SIZE);
    if (!block_buf) {
        return -1;
    }
    
    while (size > 0 && file->position < file->inode->size) {
        uint32_t block_idx = file->position / COREFS_BLOCK_SIZE;
        uint32_t block_offset = file->position % COREFS_BLOCK_SIZE;
        
        if (block_idx >= file->inode->blocks_used) {
            break;
        }
        
        uint32_t block_num = file->inode->block_list[block_idx];
        if (block_num == 0) {
            break;
        }
        
        // Read block
        esp_err_t ret = corefs_block_read(ctx, block_num, block_buf);
        if (ret != ESP_OK) {
            free(block_buf);
            return -1;
        }
        
        // Copy data
        size_t to_read = COREFS_BLOCK_SIZE - block_offset;
        if (to_read > size) {
            to_read = size;
        }
        
        memcpy(dst, block_buf + block_offset, to_read);
        
        dst += to_read;
        file->position += to_read;
        total_read += to_read;
        size -= to_read;
    }
    
    free(block_buf);
    return (int)total_read;
}

// ============================================
// WRITE
// ============================================

int corefs_write(corefs_file_t* file, const void* buf, size_t size) {
    if (!file || !file->inode || !buf) {
        return -1;
    }
    
    corefs_ctx_t* ctx = corefs_get_context();
    if (!ctx->mounted) {
        return -1;
    }
    
    // Check write permission
    if ((file->flags & COREFS_O_RDONLY)) {
        ESP_LOGE(TAG, "File not open for writing");
        return -1;
    }
    
    size_t total_written = 0;
    const uint8_t* src = (const uint8_t*)buf;
    
    // Allocate block buffer
    uint8_t* block_buf = malloc(COREFS_BLOCK_SIZE);
    if (!block_buf) {
        return -1;
    }
    
    while (size > 0) {
        uint32_t block_idx = file->position / COREFS_BLOCK_SIZE;
        uint32_t block_offset = file->position % COREFS_BLOCK_SIZE;
        
        // Check if we need a new block
        if (block_idx >= file->inode->blocks_used) {
            if (block_idx >= COREFS_MAX_BLOCKS) {
                ESP_LOGE(TAG, "File too large (max %u blocks)", COREFS_MAX_BLOCKS);
                free(block_buf);
                return (total_written > 0) ? (int)total_written : -1;
            }
            
            // Allocate new block
            uint32_t new_block = corefs_block_alloc(ctx);
            if (new_block == 0) {
                ESP_LOGE(TAG, "No free blocks");
                free(block_buf);
                return (total_written > 0) ? (int)total_written : -1;
            }
            
            file->inode->block_list[block_idx] = new_block;
            file->inode->blocks_used++;
        }
        
        uint32_t block_num = file->inode->block_list[block_idx];
        
        // Read-modify-write
        memset(block_buf, 0, COREFS_BLOCK_SIZE);
        if (block_offset != 0 || size < COREFS_BLOCK_SIZE) {
            // Partial block write - read existing data
            corefs_block_read(ctx, block_num, block_buf);
        }
        
        // Copy new data
        size_t to_write = COREFS_BLOCK_SIZE - block_offset;
        if (to_write > size) {
            to_write = size;
        }
        
        memcpy(block_buf + block_offset, src, to_write);
        
        // Write block
        esp_err_t ret = corefs_block_write(ctx, block_num, block_buf);
        if (ret != ESP_OK) {
            free(block_buf);
            return (total_written > 0) ? (int)total_written : -1;
        }
        
        src += to_write;
        file->position += to_write;
        total_written += to_write;
        size -= to_write;
        
        // Update file size
        if (file->position > file->inode->size) {
            file->inode->size = file->position;
        }
    }
    
    free(block_buf);
    file->dirty = true;
    
    return (int)total_written;
}

// ============================================
// SEEK
// ============================================

int corefs_seek(corefs_file_t* file, int offset, int whence) {
    if (!file || !file->inode) {
        return -1;
    }
    
    int new_pos = file->position;
    
    switch (whence) {
        case COREFS_SEEK_SET:
            new_pos = offset;
            break;
        case COREFS_SEEK_CUR:
            new_pos += offset;
            break;
        case COREFS_SEEK_END:
            new_pos = file->inode->size + offset;
            break;
        default:
            return -1;
    }
    
    if (new_pos < 0) {
        new_pos = 0;
    }
    
    file->position = new_pos;
    return new_pos;
}

size_t corefs_tell(corefs_file_t* file) {
    return file ? file->position : 0;
}

size_t corefs_size(corefs_file_t* file) {
    return (file && file->inode) ? file->inode->size : 0;
}

// ============================================
// CLOSE
// ============================================

esp_err_t corefs_close(corefs_file_t* file) {
    if (!file) {
        return ESP_ERR_INVALID_ARG;
    }
    
    corefs_ctx_t* ctx = corefs_get_context();
    
    // Write inode if modified
    if (file->dirty) {
        corefs_inode_write(ctx, file->inode_block, file->inode);
    }
    
    // Remove from open files
    for (int i = 0; i < COREFS_MAX_OPEN_FILES; i++) {
        if (ctx->open_files[i] == file) {
            ctx->open_files[i] = NULL;
            break;
        }
    }
    
    // Free resources
    if (file->inode) {
        free(file->inode);
    }
    free(file);
    
    return ESP_OK;
}

// ============================================
// FILE MANAGEMENT
// ============================================

esp_err_t corefs_unlink(const char* path) {
    corefs_ctx_t* ctx = corefs_get_context();
    
    if (!ctx->mounted || !path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find file
    int32_t inode_block = corefs_btree_find(ctx, path);
    if (inode_block < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Delete inode (frees all data blocks)
    esp_err_t ret = corefs_inode_delete(ctx, inode_block);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Remove from B-Tree
    return corefs_btree_delete(ctx, path);
}

bool corefs_exists(const char* path) {
    corefs_ctx_t* ctx = corefs_get_context();
    
    if (!ctx->mounted || !path) {
        return false;
    }
    
    return corefs_btree_find(ctx, path) >= 0;
}

esp_err_t corefs_rename(const char* old_path, const char* new_path) {
    // TODO: Implement rename
    return ESP_ERR_NOT_SUPPORTED;
}