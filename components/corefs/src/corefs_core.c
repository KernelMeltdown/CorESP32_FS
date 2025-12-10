/**
 * corefs_core.c - Core Lifecycle (Format/Mount/Unmount)
 * 
 * CRITICAL FIX:
 * - This is the ONLY place where format/mount/unmount exist
 * - Proper B-Tree initialization during format
 * - Proper B-Tree loading during mount
 * - Clean error handling
 */

#include "corefs.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "corefs";

// Global context - THE ONLY INSTANCE
corefs_ctx_t g_ctx = {0};

// ============================================================================
// FORMAT
// ============================================================================

esp_err_t corefs_format(const esp_partition_t* partition) {
    if (!partition) {
        ESP_LOGE(TAG, "Invalid partition");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Formatting CoreFS at 0x%lx, size %lu KB", 
             partition->address, partition->size / 1024);
    
    // Clear context
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.partition = partition;
    
    // Allocate superblock
    g_ctx.sb = calloc(1, sizeof(corefs_superblock_t));
    if (!g_ctx.sb) {
        ESP_LOGE(TAG, "Failed to allocate superblock");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize superblock
    g_ctx.sb->magic = COREFS_MAGIC;
    g_ctx.sb->version = COREFS_VERSION;
    g_ctx.sb->block_size = COREFS_BLOCK_SIZE;
    g_ctx.sb->block_count = partition->size / COREFS_BLOCK_SIZE;
    g_ctx.sb->blocks_used = COREFS_METADATA_BLOCKS;
    g_ctx.sb->root_block = 1;
    g_ctx.sb->txn_log_block = 2;
    g_ctx.sb->wear_table_block = 3;
    g_ctx.sb->mount_count = 0;
    g_ctx.sb->clean_unmount = 1;
    
    ESP_LOGI(TAG, "Superblock: %lu blocks, block size %lu", 
             g_ctx.sb->block_count, g_ctx.sb->block_size);
    
    // Erase superblock sector
    esp_err_t ret = esp_partition_erase_range(partition, 0, COREFS_BLOCK_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase superblock: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Initialize block manager
    ret = corefs_block_init(&g_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init block manager: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Initialize wear leveling
    ret = corefs_wear_init(&g_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init wear leveling: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // *** CRITICAL: Initialize B-Tree ***
    ESP_LOGI(TAG, "Initializing B-Tree at block %lu...", g_ctx.sb->root_block);
    ret = corefs_btree_init(&g_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init B-Tree: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Write superblock to flash
    ret = corefs_superblock_write(&g_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write superblock: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "Format complete: %lu blocks total, %lu KB free",
             g_ctx.sb->block_count,
             (g_ctx.sb->block_count - g_ctx.sb->blocks_used) * 
             COREFS_BLOCK_SIZE / 1024);
    
    ret = ESP_OK;
    
cleanup:
    // Free temporary structures
    if (g_ctx.sb) {
        free(g_ctx.sb);
        g_ctx.sb = NULL;
    }
    if (g_ctx.block_bitmap) {
        free(g_ctx.block_bitmap);
        g_ctx.block_bitmap = NULL;
    }
    if (g_ctx.wear_table) {
        free(g_ctx.wear_table);
        g_ctx.wear_table = NULL;
    }
    
    return ret;
}

// ============================================================================
// MOUNT
// ============================================================================

esp_err_t corefs_mount(const esp_partition_t* partition) {
    if (!partition) {
        ESP_LOGE(TAG, "Invalid partition");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Mounting CoreFS at 0x%lx", partition->address);
    
    if (g_ctx.mounted) {
        ESP_LOGW(TAG, "Already mounted");
        return ESP_OK;
    }
    
    // Clear context
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.partition = partition;
    g_ctx.next_inode_num = 1;
    
    // Allocate superblock
    g_ctx.sb = calloc(1, sizeof(corefs_superblock_t));
    if (!g_ctx.sb) {
        ESP_LOGE(TAG, "Failed to allocate superblock");
        return ESP_ERR_NO_MEM;
    }
    
    // Read superblock
    esp_err_t ret = corefs_superblock_read(&g_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read superblock: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Verify magic
    if (g_ctx.sb->magic != COREFS_MAGIC) {
        ESP_LOGE(TAG, "Invalid magic: 0x%lX (expected 0x%lX)", 
                 g_ctx.sb->magic, COREFS_MAGIC);
        ret = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "Superblock valid: version 0x%04X, %lu blocks",
             g_ctx.sb->version, g_ctx.sb->block_count);
    
    // Check for unclean shutdown
    if (!g_ctx.sb->clean_unmount) {
        ESP_LOGW(TAG, "Unclean shutdown detected, running recovery...");
        ret = corefs_recovery_scan(&g_ctx);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Recovery failed: %s", esp_err_to_name(ret));
            // Continue anyway - recovery is best-effort
        }
    }
    
    // Initialize block manager
    ret = corefs_block_init(&g_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init block manager: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Initialize wear leveling
    ret = corefs_wear_init(&g_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init wear leveling: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // *** CRITICAL: Load B-Tree from flash ***
    ESP_LOGI(TAG, "Loading B-Tree from block %lu...", g_ctx.sb->root_block);
    ret = corefs_btree_load(&g_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load B-Tree: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Mark as mounted
    g_ctx.mounted = true;
    g_ctx.sb->mount_count++;
    g_ctx.sb->clean_unmount = 0;  // Mark as dirty until unmount
    
    // Update superblock
    ret = corefs_superblock_write(&g_ctx);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update superblock");
        // Not critical, continue
    }
    
    ESP_LOGI(TAG, "Mount complete: %lu KB total, %lu KB used, %lu KB free",
             g_ctx.sb->block_count * COREFS_BLOCK_SIZE / 1024,
             g_ctx.sb->blocks_used * COREFS_BLOCK_SIZE / 1024,
             (g_ctx.sb->block_count - g_ctx.sb->blocks_used) * 
             COREFS_BLOCK_SIZE / 1024);
    
    return ESP_OK;
    
cleanup:
    if (g_ctx.sb) {
        free(g_ctx.sb);
        g_ctx.sb = NULL;
    }
    if (g_ctx.block_bitmap) {
        free(g_ctx.block_bitmap);
        g_ctx.block_bitmap = NULL;
    }
    if (g_ctx.wear_table) {
        free(g_ctx.wear_table);
        g_ctx.wear_table = NULL;
    }
    
    g_ctx.mounted = false;
    return ret;
}

// ============================================================================
// UNMOUNT
// ============================================================================

esp_err_t corefs_unmount(void) {
    if (!g_ctx.mounted) {
        ESP_LOGW(TAG, "Not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Unmounting CoreFS...");
    
    // Close all open files
    for (int i = 0; i < COREFS_MAX_OPEN_FILES; i++) {
        if (g_ctx.open_files[i]) {
            ESP_LOGW(TAG, "Force-closing file %d", i);
            corefs_close(g_ctx.open_files[i]);
        }
    }
    
    // Mark clean unmount
    g_ctx.sb->clean_unmount = 1;
    esp_err_t ret = corefs_superblock_write(&g_ctx);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to mark clean unmount: %s", esp_err_to_name(ret));
    }
    
    // Free all structures
    if (g_ctx.sb) {
        free(g_ctx.sb);
        g_ctx.sb = NULL;
    }
    if (g_ctx.block_bitmap) {
        free(g_ctx.block_bitmap);
        g_ctx.block_bitmap = NULL;
    }
    if (g_ctx.wear_table) {
        free(g_ctx.wear_table);
        g_ctx.wear_table = NULL;
    }
    
    // Clear context
    memset(&g_ctx, 0, sizeof(g_ctx));
    
    ESP_LOGI(TAG, "Unmounted");
    return ESP_OK;
}

// ============================================================================
// STATUS
// ============================================================================

bool corefs_is_mounted(void) {
    return g_ctx.mounted;
}

esp_err_t corefs_info(corefs_info_t* info) {
    if (!g_ctx.mounted || !info) {
        return ESP_ERR_INVALID_STATE;
    }
    
    info->total_blocks = g_ctx.sb->block_count;
    info->used_blocks = g_ctx.sb->blocks_used;
    info->free_blocks = info->total_blocks - info->used_blocks;
    
    info->total_bytes = info->total_blocks * COREFS_BLOCK_SIZE;
    info->used_bytes = info->used_blocks * COREFS_BLOCK_SIZE;
    info->free_bytes = info->free_blocks * COREFS_BLOCK_SIZE;
    
    return ESP_OK;
}