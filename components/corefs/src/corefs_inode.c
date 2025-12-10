/**
 * corefs_inode.c - Inode Management
 * 
 * Handles:
 * - Inode creation/deletion
 * - Inode read/write with CRC validation
 * - Block list management
 */

#include "corefs.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "corefs_inode";

// ============================================================================
// CREATE
// ============================================================================

esp_err_t corefs_inode_create(corefs_ctx_t* ctx, const char* path, 
                               corefs_inode_t** out_inode, uint32_t* out_block) {
    if (!ctx || !path || !out_inode || !out_block) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Allocate block for inode
    uint32_t block = corefs_block_alloc(ctx);
    if (block == 0) {
        ESP_LOGE(TAG, "No free blocks for inode");
        return ESP_ERR_NO_MEM;
    }
    
    // Create inode structure
    corefs_inode_t* inode = calloc(1, sizeof(corefs_inode_t));
    if (!inode) {
        ESP_LOGE(TAG, "Failed to allocate inode");
        corefs_block_free(ctx, block);
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize inode
    inode->magic = COREFS_INODE_MAGIC;
    inode->inode_num = ctx->next_inode_num++;
    inode->size = 0;
    inode->blocks_used = 0;
    inode->created = esp_log_timestamp();
    inode->modified = inode->created;
    inode->flags = 0;
    
    // Initialize block list
    for (int i = 0; i < COREFS_MAX_FILE_BLOCKS; i++) {
        inode->block_list[i] = 0;
    }
    
    // Calculate CRC
    inode->crc32 = crc32(inode, sizeof(corefs_inode_t) - 4);
    
    // Write to flash
    esp_err_t ret = corefs_inode_write(ctx, block, inode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write inode");
        free(inode);
        corefs_block_free(ctx, block);
        return ret;
    }
    
    *out_inode = inode;
    *out_block = block;
    
    ESP_LOGI(TAG, "Created inode %lu at block %lu for '%s'", 
             inode->inode_num, block, path);
    
    return ESP_OK;
}

// ============================================================================
// READ
// ============================================================================

esp_err_t corefs_inode_read(corefs_ctx_t* ctx, uint32_t block, corefs_inode_t* inode) {
    if (!ctx || !inode) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = corefs_block_read(ctx, block, inode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read inode from block %lu", block);
        return ret;
    }
    
    // Verify magic
    if (inode->magic != COREFS_INODE_MAGIC) {
        ESP_LOGE(TAG, "Invalid inode magic: 0x%lX (expected: 0x%lX)", 
                 inode->magic, COREFS_INODE_MAGIC);
        return ESP_ERR_INVALID_CRC;
    }
    
    // Verify CRC
    uint32_t stored_crc = inode->crc32;
    inode->crc32 = 0;
    uint32_t calc_crc = crc32(inode, sizeof(corefs_inode_t) - 4);
    inode->crc32 = stored_crc;
    
    if (stored_crc != calc_crc) {
        ESP_LOGE(TAG, "Inode CRC mismatch: 0x%08lX != 0x%08lX", 
                 stored_crc, calc_crc);
        return ESP_ERR_INVALID_CRC;
    }
    
    ESP_LOGD(TAG, "Read inode %lu from block %lu (size: %llu bytes)", 
             inode->inode_num, block, inode->size);
    
    return ESP_OK;
}

// ============================================================================
// WRITE
// ============================================================================

esp_err_t corefs_inode_write(corefs_ctx_t* ctx, uint32_t block, corefs_inode_t* inode) {
    if (!ctx || !inode) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Update timestamp
    inode->modified = esp_log_timestamp();
    
    // Update CRC
    inode->crc32 = 0;
    inode->crc32 = crc32(inode, sizeof(corefs_inode_t) - 4);
    
    ESP_LOGD(TAG, "Writing inode %lu to block %lu (size: %llu, CRC: 0x%08lX)", 
             inode->inode_num, block, inode->size, inode->crc32);
    
    return corefs_block_write(ctx, block, inode);
}

// ============================================================================
// DELETE
// ============================================================================

esp_err_t corefs_inode_delete(corefs_ctx_t* ctx, uint32_t block) {
    if (!ctx) {
        ESP_LOGE(TAG, "Invalid context");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Read inode first to get block list
    corefs_inode_t inode;
    esp_err_t ret = corefs_inode_read(ctx, block, &inode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read inode from block %lu", block);
        return ret;
    }
    
    ESP_LOGI(TAG, "Deleting inode %lu at block %lu (%llu bytes, %lu blocks)", 
             inode.inode_num, block, inode.size, inode.blocks_used);
    
    // Free all data blocks
    for (uint32_t i = 0; i < inode.blocks_used && i < COREFS_MAX_FILE_BLOCKS; i++) {
        if (inode.block_list[i] != 0) {
            ESP_LOGD(TAG, "Freeing data block %lu (index %lu)", 
                     inode.block_list[i], i);
            corefs_block_free(ctx, inode.block_list[i]);
        }
    }
    
    // Free inode block
    corefs_block_free(ctx, block);
    
    ESP_LOGI(TAG, "Deleted inode %lu", inode.inode_num);
    return ESP_OK;
}