/**
 * corefs_superblock.c - Superblock Management
 * 
 * Handles:
 * - Superblock read/write
 * - CRC32 validation
 * - Magic number verification
 */

#include "corefs.h"
#include "esp_log.h"
#include "esp_partition.h"
#include <string.h>

static const char* TAG = "corefs_sb";

// ============================================================================
// READ
// ============================================================================

esp_err_t corefs_superblock_read(corefs_ctx_t* ctx) {
    if (!ctx || !ctx->partition || !ctx->sb) {
        ESP_LOGE(TAG, "Invalid context");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Reading superblock from offset 0x0...");
    
    // Read from partition offset 0
    esp_err_t ret = esp_partition_read(
        ctx->partition,
        0,  // Offset 0 = superblock
        ctx->sb,
        sizeof(corefs_superblock_t)
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read superblock: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Verify magic
    if (ctx->sb->magic != COREFS_MAGIC) {
        ESP_LOGE(TAG, "Invalid superblock magic: 0x%lX (expected 0x%lX)", 
                 ctx->sb->magic, COREFS_MAGIC);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Verify CRC32
    uint32_t stored_crc = ctx->sb->crc32;  // Gespeicherten CRC sichern
    ctx->sb->crc32 = 0;                     // CRC-Feld auf 0 setzen für Berechnung
    uint32_t calc_crc = crc32(ctx->sb, sizeof(corefs_superblock_t));  // CRC berechnen
    ctx->sb->crc32 = stored_crc;            // Originalen CRC wiederherstellen
    
    if (stored_crc != calc_crc) {
        ESP_LOGE(TAG, "Superblock CRC mismatch: 0x%08lX != 0x%08lX", 
                 stored_crc, calc_crc);
        return ESP_ERR_INVALID_CRC;
    }
    
    ESP_LOGI(TAG, "Superblock read OK: v%u.%u, %lu blocks, %lu used",
             ctx->sb->version >> 8, ctx->sb->version & 0xFF,
             ctx->sb->block_count, ctx->sb->blocks_used);
    
    return ESP_OK;
}


// ============================================================================
// WRITE
// ============================================================================

// corefs_superblock.c

esp_err_t corefs_superblock_write(corefs_ctx_t* ctx) {
    if (!ctx || !ctx->partition || !ctx->sb) {
        ESP_LOGE(TAG, "Invalid context");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Calculate CRC32 (excluding last field)
    ctx->sb->crc32 = 0;  // ← FIX: crc32 statt checksum
    ctx->sb->crc32 = crc32(ctx->sb, sizeof(corefs_superblock_t));  // ← FIX
    
    ESP_LOGI(TAG, "Writing superblock (CRC: 0x%08lX)...", ctx->sb->crc32);  // ← FIX
    
    // Erase GANZEN SEKTOR (4096 Bytes)
    esp_err_t ret = esp_partition_erase_range(
        ctx->partition,
        0,
        COREFS_SECTOR_SIZE  // 4096, nicht 2048!
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase superblock sector: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Write superblock
    ret = esp_partition_write(
        ctx->partition,
        0,
        ctx->sb,
        sizeof(corefs_superblock_t)
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write superblock: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Superblock written successfully");
    return ESP_OK;
}

// ============================================================================
// INIT (during format)
// ============================================================================

esp_err_t corefs_superblock_init(corefs_ctx_t* ctx) {
    if (!ctx || !ctx->sb) {
        ESP_LOGE(TAG, "Invalid context");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Set initial values
    ctx->sb->clean_unmount = 1;
    ctx->sb->mount_count = 0;
    
    ESP_LOGI(TAG, "Superblock initialized");
    return ESP_OK;
}