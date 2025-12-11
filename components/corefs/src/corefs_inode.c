/**
 * CoreFS - Inode Management
 */

#include "corefs_types.h"
#include "esp_log.h"
#include "esp_crc.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "corefs_inode";

// Forward declarations
extern esp_err_t corefs_block_read(corefs_ctx_t* ctx, uint32_t block, void* buf);
extern esp_err_t corefs_block_write(corefs_ctx_t* ctx, uint32_t block, const void* buf);
extern uint32_t corefs_block_alloc(corefs_ctx_t* ctx);
extern void corefs_block_free(corefs_ctx_t* ctx, uint32_t block);

// ============================================
// CREATE
// ============================================

esp_err_t corefs_inode_create(corefs_ctx_t* ctx, const char* filename, 
                              uint32_t* out_inode_block) {
    if (!ctx || !filename || !out_inode_block) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Allocate block for inode
    uint32_t inode_block = corefs_block_alloc(ctx);
    if (inode_block == 0) {
        ESP_LOGE(TAG, "No free blocks for inode");
        return ESP_ERR_NO_MEM;
    }
    
    // Create inode
    corefs_inode_t* inode = calloc(1, sizeof(corefs_inode_t));
    if (!inode) {
        corefs_block_free(ctx, inode_block);
        return ESP_ERR_NO_MEM;
    }
    
    inode->magic = COREFS_FILE_MAGIC;
    inode->inode_num = ctx->next_inode_num++;
    inode->size = 0;
    inode->blocks_used = 0;
    inode->created = (uint32_t)(esp_timer_get_time() / 1000);  // ms
    inode->modified = inode->created;
    inode->flags = 0;
    
    strncpy(inode->name, filename, COREFS_MAX_FILENAME - 1);
    inode->name[COREFS_MAX_FILENAME - 1] = '\0';
    
    // Calculate checksum
    inode->checksum = 0;
    inode->checksum = esp_crc32_le(0, (const uint8_t*)inode, sizeof(corefs_inode_t));
    
    // Write to flash
    esp_err_t ret = corefs_block_write(ctx, inode_block, inode);
    free(inode);
    
    if (ret != ESP_OK) {
        corefs_block_free(ctx, inode_block);
        return ret;
    }
    
    *out_inode_block = inode_block;
    
    ESP_LOGI(TAG, "Created inode %u at block %u for '%s'", 
             ctx->next_inode_num - 1, inode_block, filename);
    
    return ESP_OK;
}

// ============================================
// READ
// ============================================

esp_err_t corefs_inode_read(corefs_ctx_t* ctx, uint32_t inode_block, 
                            corefs_inode_t* inode) {
    if (!ctx || !inode) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Read from flash
    esp_err_t ret = corefs_block_read(ctx, inode_block, inode);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Verify magic
    if (inode->magic != COREFS_FILE_MAGIC) {
        ESP_LOGE(TAG, "Invalid inode magic at block %u", inode_block);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Verify checksum
    uint32_t stored_csum = inode->checksum;
    inode->checksum = 0;
    uint32_t calc_csum = esp_crc32_le(0, (const uint8_t*)inode, sizeof(corefs_inode_t));
    inode->checksum = stored_csum;
    
    if (stored_csum != calc_csum) {
        ESP_LOGE(TAG, "Inode checksum mismatch at block %u", inode_block);
        return ESP_ERR_INVALID_CRC;
    }
    
    return ESP_OK;
}

// ============================================
// WRITE
// ============================================

esp_err_t corefs_inode_write(corefs_ctx_t* ctx, uint32_t inode_block, 
                             const corefs_inode_t* inode) {
    if (!ctx || !inode) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Make a copy to calculate checksum
    corefs_inode_t* inode_copy = malloc(sizeof(corefs_inode_t));
    if (!inode_copy) {
        return ESP_ERR_NO_MEM;
    }
    
    memcpy(inode_copy, inode, sizeof(corefs_inode_t));
    
    // Update modified time
    inode_copy->modified = (uint32_t)(esp_timer_get_time() / 1000);
    
    // Calculate checksum
    inode_copy->checksum = 0;
    inode_copy->checksum = esp_crc32_le(0, (const uint8_t*)inode_copy, 
                                         sizeof(corefs_inode_t));
    
    // Write to flash
    esp_err_t ret = corefs_block_write(ctx, inode_block, inode_copy);
    free(inode_copy);
    
    return ret;
}

// ============================================
// DELETE
// ============================================

esp_err_t corefs_inode_delete(corefs_ctx_t* ctx, uint32_t inode_block) {
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Read inode to get block list
    corefs_inode_t* inode = malloc(sizeof(corefs_inode_t));
    if (!inode) {
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t ret = corefs_inode_read(ctx, inode_block, inode);
    if (ret != ESP_OK) {
        free(inode);
        return ret;
    }
    
    // Free all data blocks
    for (uint32_t i = 0; i < inode->blocks_used; i++) {
        if (inode->block_list[i] != 0) {
            corefs_block_free(ctx, inode->block_list[i]);
        }
    }
    
    // Free inode block itself
    corefs_block_free(ctx, inode_block);
    
    free(inode);
    
    ESP_LOGD(TAG, "Deleted inode at block %u", inode_block);
    return ESP_OK;
}