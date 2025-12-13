/**
 * CoreFS Superblock Management - FIXED VERSION
 * Field name: checksum (not crc32!)
 */

#include "corefs.h"
#include "esp_log.h"
#include "esp_partition.h"
#include <string.h>

static const char* TAG = "corefs_sb";

/**
 * Read superblock from flash and verify checksum
 */
esp_err_t corefs_superblock_read(corefs_ctx_t* ctx) {
    if (!ctx || !ctx->partition || !ctx->sb) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read from Block 0
    esp_err_t ret = esp_partition_read(
        ctx->partition,
        0,  // Offset 0 = Block 0
        ctx->sb,
        sizeof(corefs_superblock_t)
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read superblock: %s", esp_err_to_name(ret));
        return ret;
    }

    // Verify magic
    if (ctx->sb->magic != COREFS_MAGIC) {
        ESP_LOGE(TAG, "Invalid magic: 0x%08lX (expected 0x%08lX)",
                 ctx->sb->magic, COREFS_MAGIC);
        return ESP_ERR_INVALID_STATE;
    }

    // Verify checksum
    uint32_t stored_csum = ctx->sb->checksum;  // ✓ FIXED: checksum field
    ctx->sb->checksum = 0;
    uint32_t calc_csum = crc32(ctx->sb, sizeof(corefs_superblock_t));
    ctx->sb->checksum = stored_csum;

    if (stored_csum != calc_csum) {
        ESP_LOGE(TAG, "Checksum mismatch: 0x%08lX != 0x%08lX",
                 stored_csum, calc_csum);
        return ESP_ERR_INVALID_CRC;
    }

    ESP_LOGI(TAG, "Superblock read OK (version 0x%04X, %lu blocks)",
             ctx->sb->version, ctx->sb->block_count);

    return ESP_OK;
}

/**
 * Write superblock to flash with checksum
 */
esp_err_t corefs_superblock_write(corefs_ctx_t* ctx) {
    if (!ctx || !ctx->partition || !ctx->sb) {
        return ESP_ERR_INVALID_ARG;
    }

    // Calculate checksum
    ctx->sb->checksum = 0;  // ✓ FIXED: checksum field
    ctx->sb->checksum = crc32(ctx->sb, sizeof(corefs_superblock_t));

    ESP_LOGI(TAG, "Writing superblock (CRC: 0x%08lX)...", ctx->sb->checksum);

    // Erase sector (Block 0 = Offset 0)
    esp_err_t ret = esp_partition_erase_range(
        ctx->partition,
        0,
        COREFS_SECTOR_SIZE
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase superblock sector: %s",
                 esp_err_to_name(ret));
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

/**
 * Initialize fresh superblock (format only)
 */
esp_err_t corefs_superblock_init(corefs_ctx_t* ctx) {
    if (!ctx || !ctx->sb) {
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize fields
    ctx->sb->clean_unmount = 1;
    ctx->sb->mount_count = 0;

    ESP_LOGI(TAG, "Superblock initialized");
    return ESP_OK;
}