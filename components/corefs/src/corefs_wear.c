/**
 * corefs_wear.c - Wear Leveling Management
 * 
 * Tracks write/erase cycles per block to extend flash lifetime
 */

#include "corefs.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "corefs_wear";

// ============================================================================
// INITIALIZATION
// ============================================================================

esp_err_t corefs_wear_init(corefs_ctx_t* ctx) {
    if (!ctx || !ctx->sb) {
        ESP_LOGE(TAG, "Invalid context");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Allocate wear table (2 bytes per block)
    size_t table_size = ctx->sb->block_count * sizeof(uint16_t);
    ctx->wear_table = calloc(1, table_size);
    
    if (!ctx->wear_table) {
        ESP_LOGE(TAG, "Failed to allocate wear table (%zu bytes)", table_size);
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize all wear counts to 0 (calloc does this)
    ESP_LOGI(TAG, "Wear leveling initialized: %lu blocks tracked", 
             ctx->sb->block_count);
    
    return ESP_OK;
}

// ============================================================================
// BLOCK SELECTION
// ============================================================================

uint32_t corefs_wear_get_best_block(corefs_ctx_t* ctx) {
    if (!ctx || !ctx->wear_table || !ctx->block_bitmap) {
        ESP_LOGE(TAG, "Invalid context for wear leveling");
        return 0;
    }
    
    uint32_t best_block = 0;
    uint16_t min_wear = 0xFFFF;
    
    // Search for free block with lowest wear count
    for (uint32_t i = COREFS_METADATA_BLOCKS; i < ctx->sb->block_count; i++) {
        // Check if block is free
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        
        if (!(ctx->block_bitmap[byte_idx] & (1 << bit_idx))) {
            // Block is free - check wear count
            if (ctx->wear_table[i] < min_wear) {
                min_wear = ctx->wear_table[i];
                best_block = i;
            }
        }
    }
    
    if (best_block > 0) {
        ESP_LOGD(TAG, "Best block: %lu (wear count: %u)", best_block, min_wear);
    } else {
        ESP_LOGW(TAG, "No free blocks available");
    }
    
    return best_block;
}

// ============================================================================
// WEAR TRACKING
// ============================================================================

void corefs_wear_increment(corefs_ctx_t* ctx, uint32_t block) {
    if (!ctx || !ctx->wear_table) {
        return;
    }
    
    if (block >= ctx->sb->block_count) {
        ESP_LOGE(TAG, "Invalid block %lu for wear increment", block);
        return;
    }
    
    if (ctx->wear_table[block] < 0xFFFF) {
        ctx->wear_table[block]++;
        ESP_LOGD(TAG, "Block %lu wear count: %u", block, ctx->wear_table[block]);
    } else {
        ESP_LOGW(TAG, "Block %lu wear count saturated at %u", block, 0xFFFF);
    }
}

// ============================================================================
// PERSISTENCE (Optional - not currently used)
// ============================================================================

esp_err_t corefs_wear_load(corefs_ctx_t* ctx) {
    if (!ctx || !ctx->wear_table || !ctx->sb) {
        ESP_LOGE(TAG, "Invalid context for wear table load");
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t table_size = ctx->sb->block_count * sizeof(uint16_t);
    
    // Allocate temporary buffer
    uint8_t* buf = malloc(COREFS_BLOCK_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate read buffer");
        return ESP_ERR_NO_MEM;
    }
    
    // Read wear table from flash (block 3)
    esp_err_t ret = corefs_block_read(ctx, ctx->sb->wear_table_block, buf);
    if (ret == ESP_OK) {
        // Copy to wear table (limit to available data)
        size_t copy_size = (table_size < COREFS_BLOCK_SIZE) ? table_size : COREFS_BLOCK_SIZE;
        memcpy(ctx->wear_table, buf, copy_size);
        
        ESP_LOGI(TAG, "Wear table loaded from block %lu (%zu bytes)", 
                 ctx->sb->wear_table_block, copy_size);
    } else {
        ESP_LOGW(TAG, "Failed to load wear table, using zeros");
        memset(ctx->wear_table, 0, table_size);
    }
    
    free(buf);
    return ret;
}

esp_err_t corefs_wear_save(corefs_ctx_t* ctx) {
    if (!ctx || !ctx->wear_table || !ctx->sb) {
        ESP_LOGE(TAG, "Invalid context for wear table save");
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t table_size = ctx->sb->block_count * sizeof(uint16_t);
    
    // For now, assume table fits in one block
    if (table_size > COREFS_BLOCK_SIZE) {
        ESP_LOGW(TAG, "Wear table size %zu exceeds block size %d, truncating", 
                 table_size, COREFS_BLOCK_SIZE);
        table_size = COREFS_BLOCK_SIZE;
    }
    
    // Write wear table to flash
    esp_err_t ret = corefs_block_write(ctx, ctx->sb->wear_table_block, ctx->wear_table);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Wear table saved to block %lu (%zu bytes)", 
                 ctx->sb->wear_table_block, table_size);
    } else {
        ESP_LOGE(TAG, "Failed to save wear table: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

// ============================================================================
// STATISTICS & HEALTH CHECK
// ============================================================================

esp_err_t corefs_wear_check(corefs_ctx_t* ctx) {
    if (!ctx || !ctx->wear_table || !ctx->sb) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t min_wear = 0xFFFF;
    uint16_t max_wear = 0;
    uint32_t total_wear = 0;
    uint32_t count = 0;
    
    // Calculate statistics
    for (uint32_t i = COREFS_METADATA_BLOCKS; i < ctx->sb->block_count; i++) {
        uint16_t wear = ctx->wear_table[i];
        if (wear < min_wear) min_wear = wear;
        if (wear > max_wear) max_wear = wear;
        total_wear += wear;
        count++;
    }
    
    uint16_t avg_wear = (count > 0) ? (total_wear / count) : 0;
    uint16_t deviation = max_wear - min_wear;
    
    ESP_LOGI(TAG, "Wear leveling stats:");
    ESP_LOGI(TAG, "  Min: %u, Max: %u, Avg: %u", min_wear, max_wear, avg_wear);
    ESP_LOGI(TAG, "  Deviation: %u", deviation);
    
    if (deviation > 1000) {
        ESP_LOGW(TAG, "High wear deviation detected (%u), rebalancing recommended", 
                 deviation);
        return ESP_ERR_INVALID_STATE;
    }
    
    return ESP_OK;
}