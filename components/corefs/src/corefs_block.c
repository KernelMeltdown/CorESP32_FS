/**
 * corefs_block.c - Block Management
 *
 * Handles:
 * - Block allocation/freeing
 * - Block read/write with auto-erase
 * - Bitmap management
 */

#include "corefs.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "corefs_blk";

// ============================================================================
// READ/WRITE
// ============================================================================

esp_err_t corefs_block_read(corefs_ctx_t *ctx, uint32_t block, void *buf)
{
    if (!ctx || !buf)
    {
        ESP_LOGE(TAG, "Invalid context or buffer");
        return ESP_ERR_INVALID_ARG;
    }

    if (block >= ctx->sb->block_count)
    {
        ESP_LOGE(TAG, "Block %lu out of range (max: %lu)",
                 block, ctx->sb->block_count - 1);
        return ESP_ERR_INVALID_ARG;
    }

    // ✅ FIX: Offset relativ zur Partition (nicht absolut!)
    uint32_t offset = block * COREFS_BLOCK_SIZE; // ← ÄNDERUNG

    return esp_partition_read(ctx->partition, offset, buf, COREFS_BLOCK_SIZE);
}

esp_err_t corefs_block_write(corefs_ctx_t *ctx, uint32_t block, const void *buf)
{
    if (!ctx || !buf)
    {
        ESP_LOGE(TAG, "Invalid context or buffer");
        return ESP_ERR_INVALID_ARG;
    }

    if (block >= ctx->sb->block_count)
    {
        ESP_LOGE(TAG, "Block %lu out of range (max: %lu)",
                 block, ctx->sb->block_count - 1);
        return ESP_ERR_INVALID_ARG;
    }

    // ✅ FIX: Offset relativ zur Partition
    uint32_t offset = block * COREFS_BLOCK_SIZE; // ← ÄNDERUNG

    // Auto-erase if block-aligned to sector boundary
    // Flash sectors are 4096 bytes, blocks are 2048 bytes
    // So erase every 2 blocks (wenn Offset % 4096 == 0)
    if ((offset % COREFS_SECTOR_SIZE) == 0)
    {
        ESP_LOGD(TAG, "Erasing sector at offset 0x%lX for block %lu", offset, block);

        esp_err_t ret = esp_partition_erase_range(
            ctx->partition,
            offset,
            COREFS_SECTOR_SIZE // 4096
        );

        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to erase sector: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    return esp_partition_write(ctx->partition, offset, buf, COREFS_BLOCK_SIZE);
}

uint32_t corefs_block_get_flash_addr(corefs_ctx_t *ctx, uint32_t block)
{
    if (!ctx || !ctx->partition)
    {
        return 0;
    }

    // ✅ FIX: Absolute Flash-Adresse
    return ctx->partition->address + (block * COREFS_BLOCK_SIZE);
}

// ============================================================================
// ALLOCATION
// ============================================================================

uint32_t corefs_block_alloc(corefs_ctx_t *ctx)
{
    if (!ctx || !ctx->block_bitmap)
    {
        ESP_LOGE(TAG, "Invalid context or uninitialized bitmap");
        return 0;
    }

    // Simple first-fit allocation
    for (uint32_t i = COREFS_METADATA_BLOCKS; i < ctx->sb->block_count; i++)
    {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;

        if (!(ctx->block_bitmap[byte_idx] & (1 << bit_idx)))
        {
            // Block is free - allocate it
            ctx->block_bitmap[byte_idx] |= (1 << bit_idx);
            ctx->sb->blocks_used++;

            ESP_LOGD(TAG, "Allocated block %lu (%lu used / %lu total)",
                     i, ctx->sb->blocks_used, ctx->sb->block_count);

            return i;
        }
    }

    ESP_LOGE(TAG, "No free blocks! (%lu used / %lu total)",
             ctx->sb->blocks_used, ctx->sb->block_count);
    return 0;
}

esp_err_t corefs_block_free(corefs_ctx_t *ctx, uint32_t block)
{
    if (!ctx || !ctx->block_bitmap)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (block < COREFS_METADATA_BLOCKS || block >= ctx->sb->block_count)
    {
        ESP_LOGE(TAG, "Cannot free block %lu (metadata or out of range)", block);
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t byte_idx = block / 8;
    uint32_t bit_idx = block % 8;

    // Check if already free
    if (!(ctx->block_bitmap[byte_idx] & (1 << bit_idx)))
    {
        ESP_LOGW(TAG, "Block %lu is already free", block);
        return ESP_OK;
    }

    ctx->block_bitmap[byte_idx] &= ~(1 << bit_idx);
    ctx->sb->blocks_used--;

    ESP_LOGD(TAG, "Freed block %lu (%lu used / %lu total)",
             block, ctx->sb->blocks_used, ctx->sb->block_count);

    return ESP_OK;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

esp_err_t corefs_block_init(corefs_ctx_t *ctx)
{
    if (!ctx || !ctx->sb)
    {
        ESP_LOGE(TAG, "Invalid context");
        return ESP_ERR_INVALID_ARG;
    }

    size_t bitmap_size = (ctx->sb->block_count + 7) / 8;
    ctx->block_bitmap = calloc(1, bitmap_size);

    if (!ctx->block_bitmap)
    {
        ESP_LOGE(TAG, "Failed to allocate bitmap (%zu bytes)", bitmap_size);
        return ESP_ERR_NO_MEM;
    }

    // Mark metadata blocks as used
    for (uint32_t i = 0; i < COREFS_METADATA_BLOCKS; i++)
    {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        ctx->block_bitmap[byte_idx] |= (1 << bit_idx);
    }

    ESP_LOGI(TAG, "Block manager initialized: %lu blocks", ctx->sb->block_count);
    return ESP_OK;
}