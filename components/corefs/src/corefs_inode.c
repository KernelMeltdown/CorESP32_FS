/**
 * CoreFS Inode Management - FIXED VERSION
 * Uses crc32() instead of esp_crc32_le()
 */

#include "corefs.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "corefs_inode";

// External declarations (from other components)
extern esp_err_t corefs_block_read(corefs_ctx_t* ctx, uint32_t block, void* buf);
extern esp_err_t corefs_block_write(corefs_ctx_t* ctx, uint32_t block, const void* buf);
extern uint32_t corefs_block_alloc(corefs_ctx_t* ctx);
extern void corefs_block_free(corefs_ctx_t* ctx, uint32_t block);

/**
 * Create new inode
 */
esp_err_t corefs_inode_create(corefs_ctx_t* ctx, const char* filename,
                               uint32_t* out_inode_block) {
    if (!ctx || !filename || !out_inode_block) {
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate block for inode
    uint32_t inode_block = corefs_block_alloc(ctx);
    if (inode_block == 0) {
        ESP_LOGE(TAG, "Failed to allocate block for inode");
        return ESP_ERR_NO_MEM;
    }

    // Create inode structure
    corefs_inode_t* inode = calloc(1, sizeof(corefs_inode_t));
    if (!inode) {
        corefs_block_free(ctx, inode_block);
        return ESP_ERR_NO_MEM;
    }

    // Initialize inode
    inode->magic = COREFS_FILE_MAGIC;
    inode->inode_num = ctx->next_inode_num++;
    inode->size = 0;
    inode->blocks_used = 0;
    inode->created = esp_log_timestamp();
    inode->modified = inode->created;
    inode->mode = 0644;
    inode->flags = 0;

    // Copy filename into inode
    strncpy(inode->name, filename, COREFS_MAX_FILENAME - 1);
    inode->name[COREFS_MAX_FILENAME - 1] = '\0';

    // Calculate checksum (FIXED: use crc32())
    inode->checksum = 0;
    inode->checksum = crc32(inode, sizeof(corefs_inode_t));

    // Write inode to flash
    esp_err_t ret = corefs_block_write(ctx, inode_block, inode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write inode to block %lu", inode_block);
        corefs_block_free(ctx, inode_block);
        free(inode);
        return ret;
    }

    ESP_LOGI(TAG, "Created inode %lu at block %lu for '%s'",
             inode->inode_num, inode_block, filename);

    *out_inode_block = inode_block;
    free(inode);
    return ESP_OK;
}

/**
 * Read inode from flash
 */
esp_err_t corefs_inode_read(corefs_ctx_t* ctx, uint32_t inode_block,
                             corefs_inode_t* inode) {
    if (!ctx || !inode) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read inode block
    esp_err_t ret = corefs_block_read(ctx, inode_block, inode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read inode from block %lu", inode_block);
        return ret;
    }

    // Verify magic
    if (inode->magic != COREFS_FILE_MAGIC) {
        ESP_LOGE(TAG, "Invalid inode magic: 0x%08lX", inode->magic);
        return ESP_ERR_INVALID_STATE;
    }

    // Verify checksum (FIXED: use crc32())
    uint32_t stored_csum = inode->checksum;
    inode->checksum = 0;
    uint32_t calc_csum = crc32(inode, sizeof(corefs_inode_t));
    inode->checksum = stored_csum;

    if (stored_csum != calc_csum) {
        ESP_LOGE(TAG, "Inode checksum mismatch: 0x%08lX != 0x%08lX",
                 stored_csum, calc_csum);
        return ESP_ERR_INVALID_CRC;
    }

    ESP_LOGD(TAG, "Read inode %lu from block %lu", inode->inode_num, inode_block);
    return ESP_OK;
}

/**
 * Write inode to flash
 */
esp_err_t corefs_inode_write(corefs_ctx_t* ctx, uint32_t inode_block,
                              const corefs_inode_t* inode) {
    if (!ctx || !inode) {
        return ESP_ERR_INVALID_ARG;
    }

    // Create writable copy
    corefs_inode_t* inode_copy = malloc(sizeof(corefs_inode_t));
    if (!inode_copy) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(inode_copy, inode, sizeof(corefs_inode_t));

    // Update checksum (FIXED: use crc32())
    inode_copy->checksum = 0;
    inode_copy->checksum = crc32(inode_copy, sizeof(corefs_inode_t));

    // Write to flash
    esp_err_t ret = corefs_block_write(ctx, inode_block, inode_copy);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write inode %lu to block %lu",
                 inode_copy->inode_num, inode_block);
    } else {
        ESP_LOGD(TAG, "Wrote inode %lu to block %lu",
                 inode_copy->inode_num, inode_block);
    }

    free(inode_copy);
    return ret;
}

/**
 * Delete inode and free all associated blocks
 */
esp_err_t corefs_inode_delete(corefs_ctx_t* ctx, uint32_t inode_block) {
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read inode
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
        uint32_t block = inode->block_list[i];
        if (block != 0) {
            corefs_block_free(ctx, block);
            ESP_LOGD(TAG, "Freed data block %lu", block);
        }
    }

    // Free inode block
    corefs_block_free(ctx, inode_block);

    ESP_LOGI(TAG, "Deleted inode %lu (freed %lu blocks)",
             inode->inode_num, inode->blocks_used + 1);

    free(inode);
    return ESP_OK;
}