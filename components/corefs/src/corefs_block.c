/**
 * CoreFS - Block Allocation & Management
 */

#include "corefs_types.h"
#include "esp_log.h"
#include "esp_partition.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "corefs_blk";

// ============================================
// INITIALIZATION
// ============================================

esp_err_t corefs_block_init(corefs_ctx_t* ctx) {
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Allocate bitmap (1 bit per block)
    uint32_t bitmap_size = (ctx->sb->block_count + 7) / 8;
    ctx->block_bitmap = calloc(1, bitmap_size);
    if (!ctx->block_bitmap) {
        return ESP_ERR_NO_MEM;
    }
    
    // Mark metadata blocks as used (blocks 0-3)
    for (uint32_t i = 0; i < COREFS_METADATA_BLOCKS; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        ctx->block_bitmap[byte_idx] |= (1 << bit_idx);
    }
    
    // Allocate wear table
    ctx->wear_table = calloc(ctx->sb->block_count, sizeof(uint16_t));
    if (!ctx->wear_table) {
        free(ctx->block_bitmap);
        ctx->block_bitmap = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    // Load wear table from flash (if available)
    uint32_t wear_offset = ctx->sb->wear_table_block * COREFS_BLOCK_SIZE;
    size_t wear_size = ctx->sb->block_count * sizeof(uint16_t);
    
    esp_err_t ret = esp_partition_read(ctx->partition, wear_offset, 
                                       ctx->wear_table, wear_size);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load wear table, using zeros");
        memset(ctx->wear_table, 0, wear_size);
    }
    
    ESP_LOGI(TAG, "Block manager initialized: %u blocks", ctx->sb->block_count);
    return ESP_OK;
}

void corefs_block_cleanup(corefs_ctx_t* ctx) {
    if (ctx->block_bitmap) {
        free(ctx->block_bitmap);
        ctx->block_bitmap = NULL;
    }
    if (ctx->wear_table) {
        free(ctx->wear_table);
        ctx->wear_table = NULL;
    }
}

// ============================================
// ALLOCATION
// ============================================

uint32_t corefs_block_alloc(corefs_ctx_t* ctx) {
    if (!ctx || !ctx->block_bitmap || !ctx->wear_table) {
        return 0;
    }
    
    // Find block with lowest wear count
    uint32_t best_block = 0;
    uint16_t min_wear = 0xFFFF;
    
    for (uint32_t i = COREFS_METADATA_BLOCKS; i < ctx->sb->block_count; i++) {
        // Check if free
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        
        if (!(ctx->block_bitmap[byte_idx] & (1 << bit_idx))) {
            // Free block found - check wear
            if (ctx->wear_table[i] < min_wear) {
                min_wear = ctx->wear_table[i];
                best_block = i;
            }
        }
    }
    
    if (best_block == 0) {
        ESP_LOGE(TAG, "No free blocks");
        return 0;
    }
    
    // Mark as allocated
    uint32_t byte_idx = best_block / 8;
    uint32_t bit_idx = best_block % 8;
    ctx->block_bitmap[byte_idx] |= (1 << bit_idx);
    ctx->sb->blocks_used++;
    
    ESP_LOGD(TAG, "Allocated block %u (wear: %u)", best_block, min_wear);
    return best_block;
}

void corefs_block_free(corefs_ctx_t* ctx, uint32_t block) {
    if (!ctx || !ctx->block_bitmap) {
        return;
    }
    
    if (block < COREFS_METADATA_BLOCKS || block >= ctx->sb->block_count) {
        ESP_LOGE(TAG, "Invalid block %u", block);
        return;
    }
    
    // Mark as free
    uint32_t byte_idx = block / 8;
    uint32_t bit_idx = block % 8;
    ctx->block_bitmap[byte_idx] &= ~(1 << bit_idx);
    
    if (ctx->sb->blocks_used > 0) {
        ctx->sb->blocks_used--;
    }
    
    ESP_LOGD(TAG, "Freed block %u", block);
}

bool corefs_block_is_allocated(corefs_ctx_t* ctx, uint32_t block) {
    if (!ctx || !ctx->block_bitmap) {
        return false;
    }
    
    if (block >= ctx->sb->block_count) {
        return false;
    }
    
    uint32_t byte_idx = block / 8;
    uint32_t bit_idx = block % 8;
    
    return (ctx->block_bitmap[byte_idx] & (1 << bit_idx)) != 0;
}

// ============================================
// I/O OPERATIONS
// ============================================

esp_err_t corefs_block_read(corefs_ctx_t* ctx, uint32_t block, void* buf) {
    if (!ctx || !buf) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (block >= ctx->sb->block_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t offset = block * COREFS_BLOCK_SIZE;
    return esp_partition_read(ctx->partition, offset, buf, COREFS_BLOCK_SIZE);
}

esp_err_t corefs_block_write(corefs_ctx_t* ctx, uint32_t block, const void* buf) {
    if (!ctx || !buf) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (block >= ctx->sb->block_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t offset = block * COREFS_BLOCK_SIZE;
    
    // Erase sector if this is first block in sector
    if (offset % COREFS_SECTOR_SIZE == 0) {
        esp_err_t ret = esp_partition_erase_range(ctx->partition, offset, 
                                                   COREFS_SECTOR_SIZE);
        if (ret != ESP_OK) {
            return ret;
        }
        
        // Increment wear count for both blocks in this sector
        if (ctx->wear_table) {
            ctx->wear_table[block]++;
            if (block + 1 < ctx->sb->block_count) {
                ctx->wear_table[block + 1]++;
            }
        }
    }
    
    return esp_partition_write(ctx->partition, offset, buf, COREFS_BLOCK_SIZE);
}

uint32_t corefs_block_get_flash_addr(corefs_ctx_t* ctx, uint32_t block) {
    if (!ctx || block >= ctx->sb->block_count) {
        return 0;
    }
    
    return ctx->partition->address + (block * COREFS_BLOCK_SIZE);
}