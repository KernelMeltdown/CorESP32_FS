/**
 * CoreFS - Core Implementation
 */

#include "corefs.h"
#include "corefs_types.h"
#include "esp_log.h"
#include "esp_crc.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "corefs";

// Global context (single instance)
static corefs_ctx_t g_ctx = {0};

// Forward declarations (from other files)
extern esp_err_t corefs_superblock_init(corefs_ctx_t* ctx);
extern esp_err_t corefs_superblock_read(corefs_ctx_t* ctx);
extern esp_err_t corefs_superblock_write(corefs_ctx_t* ctx);
extern esp_err_t corefs_block_init(corefs_ctx_t* ctx);
extern void corefs_block_cleanup(corefs_ctx_t* ctx);
extern esp_err_t corefs_btree_init(corefs_ctx_t* ctx);
extern esp_err_t corefs_btree_load(corefs_ctx_t* ctx);

// ============================================
// FORMAT
// ============================================

esp_err_t corefs_format(const esp_partition_t* partition) {
    if (!partition) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Formatting CoreFS at 0x%X, size %u KB", 
             partition->address, partition->size / 1024);
    
    // Validate partition alignment
    if (partition->size % COREFS_SECTOR_SIZE != 0) {
        ESP_LOGE(TAG, "Partition size not sector-aligned!");
        return ESP_ERR_INVALID_SIZE;
    }
    
    if (partition->address % COREFS_SECTOR_SIZE != 0) {
        ESP_LOGE(TAG, "Partition offset not sector-aligned!");
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Setup temporary context
    corefs_ctx_t ctx = {0};
    ctx.partition = partition;
    
    // Allocate superblock
    ctx.sb = calloc(1, sizeof(corefs_superblock_t));
    if (!ctx.sb) {
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize superblock
    ctx.sb->magic = COREFS_MAGIC;
    ctx.sb->version = COREFS_VERSION;
    ctx.sb->block_size = COREFS_BLOCK_SIZE;
    ctx.sb->block_count = partition->size / COREFS_BLOCK_SIZE;
    ctx.sb->blocks_used = COREFS_METADATA_BLOCKS;
    ctx.sb->root_block = 1;
    ctx.sb->txn_log_block = 2;
    ctx.sb->wear_table_block = 3;
    ctx.sb->mount_count = 0;
    ctx.sb->clean_unmount = 1;
    
    // Calculate checksum
    ctx.sb->checksum = 0;
    ctx.sb->checksum = esp_crc32_le(0, (const uint8_t*)ctx.sb, 
                                     sizeof(corefs_superblock_t));
    
    // Write superblock
    esp_err_t ret = esp_partition_erase_range(partition, 0, COREFS_SECTOR_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase superblock sector: %s", esp_err_to_name(ret));
        free(ctx.sb);
        return ret;
    }
    
    ret = esp_partition_write(partition, 0, ctx.sb, sizeof(corefs_superblock_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write superblock: %s", esp_err_to_name(ret));
        free(ctx.sb);
        return ret;
    }
    
    // Initialize block bitmap
    ret = corefs_block_init(&ctx);
    if (ret != ESP_OK) {
        free(ctx.sb);
        return ret;
    }
    
    // Initialize B-Tree root
    ret = corefs_btree_init(&ctx);
    if (ret != ESP_OK) {
        corefs_block_cleanup(&ctx);
        free(ctx.sb);
        return ret;
    }
    
    // Initialize wear table (all zeros)
    uint16_t* wear_table = calloc(ctx.sb->block_count, sizeof(uint16_t));
    if (wear_table) {
        // Erase wear table block
        uint32_t wear_offset = ctx.sb->wear_table_block * COREFS_BLOCK_SIZE;
        esp_partition_erase_range(partition, wear_offset, COREFS_SECTOR_SIZE);
        esp_partition_write(partition, wear_offset, wear_table, 
                           ctx.sb->block_count * sizeof(uint16_t));
        free(wear_table);
    }
    
    // Cleanup
    corefs_block_cleanup(&ctx);
    free(ctx.sb);
    
    ESP_LOGI(TAG, "Format complete: %u blocks total, %u KB free",
             ctx.sb->block_count, 
             (ctx.sb->block_count - COREFS_METADATA_BLOCKS) * 2);
    
    return ESP_OK;
}

// ============================================
// MOUNT
// ============================================

esp_err_t corefs_mount(const esp_partition_t* partition) {
    if (!partition) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (g_ctx.mounted) {
        ESP_LOGW(TAG, "Already mounted");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Mounting CoreFS at 0x%X", partition->address);
    
    // Setup context
    g_ctx.partition = partition;
    g_ctx.mounted = false;
    
    // Allocate superblock
    g_ctx.sb = calloc(1, sizeof(corefs_superblock_t));
    if (!g_ctx.sb) {
        return ESP_ERR_NO_MEM;
    }
    
    // Read superblock
    esp_err_t ret = esp_partition_read(partition, 0, g_ctx.sb, 
                                       sizeof(corefs_superblock_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read superblock: %s", esp_err_to_name(ret));
        free(g_ctx.sb);
        return ret;
    }
    
    // Verify magic
    if (g_ctx.sb->magic != COREFS_MAGIC) {
        ESP_LOGE(TAG, "Invalid magic: 0x%08X (expected 0x%08X)", 
                 g_ctx.sb->magic, COREFS_MAGIC);
        free(g_ctx.sb);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Verify checksum
    uint32_t stored_csum = g_ctx.sb->checksum;
    g_ctx.sb->checksum = 0;
    uint32_t calc_csum = esp_crc32_le(0, (const uint8_t*)g_ctx.sb, 
                                       sizeof(corefs_superblock_t));
    g_ctx.sb->checksum = stored_csum;
    
    if (stored_csum != calc_csum) {
        ESP_LOGE(TAG, "Checksum mismatch: 0x%08X != 0x%08X", 
                 stored_csum, calc_csum);
        free(g_ctx.sb);
        return ESP_ERR_INVALID_CRC;
    }
    
    // Check clean unmount
    if (g_ctx.sb->clean_unmount == 0) {
        ESP_LOGW(TAG, "Unclean unmount detected - may need recovery");
    }
    
    // Initialize block manager
    ret = corefs_block_init(&g_ctx);
    if (ret != ESP_OK) {
        free(g_ctx.sb);
        return ret;
    }
    
    // Load B-Tree
    ret = corefs_btree_load(&g_ctx);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load B-Tree: %s", esp_err_to_name(ret));
        // Continue anyway - B-Tree might be empty
    }
    
    // Mark as dirty (will be set to clean on unmount)
    g_ctx.sb->clean_unmount = 0;
    g_ctx.sb->mount_count++;
    
    // Initialize file handles
    memset(g_ctx.open_files, 0, sizeof(g_ctx.open_files));
    g_ctx.next_inode_num = 1;
    
    g_ctx.mounted = true;
    
    ESP_LOGI(TAG, "Mount complete: %u KB total, %u KB used, %u KB free",
             g_ctx.sb->block_count * 2,
             g_ctx.sb->blocks_used * 2,
             (g_ctx.sb->block_count - g_ctx.sb->blocks_used) * 2);
    
    return ESP_OK;
}

// ============================================
// UNMOUNT
// ============================================

esp_err_t corefs_unmount(void) {
    if (!g_ctx.mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Unmounting CoreFS...");
    
    // Close all open files
    for (int i = 0; i < COREFS_MAX_OPEN_FILES; i++) {
        if (g_ctx.open_files[i]) {
            corefs_close(g_ctx.open_files[i]);
        }
    }
    
    // Mark as clean
    g_ctx.sb->clean_unmount = 1;
    
    // Update superblock checksum
    g_ctx.sb->checksum = 0;
    g_ctx.sb->checksum = esp_crc32_le(0, (const uint8_t*)g_ctx.sb, 
                                       sizeof(corefs_superblock_t));
    
    // Write superblock
    esp_partition_erase_range(g_ctx.partition, 0, COREFS_SECTOR_SIZE);
    esp_partition_write(g_ctx.partition, 0, g_ctx.sb, 
                       sizeof(corefs_superblock_t));
    
    // Cleanup
    corefs_block_cleanup(&g_ctx);
    free(g_ctx.sb);
    g_ctx.sb = NULL;
    g_ctx.mounted = false;
    
    ESP_LOGI(TAG, "Unmount complete");
    return ESP_OK;
}

// ============================================
// STATUS
// ============================================

bool corefs_is_mounted(void) {
    return g_ctx.mounted;
}

esp_err_t corefs_info(corefs_info_t* info) {
    if (!g_ctx.mounted || !info) {
        return ESP_ERR_INVALID_STATE;
    }
    
    info->block_size = g_ctx.sb->block_size;
    info->block_count = g_ctx.sb->block_count;
    info->blocks_used = g_ctx.sb->blocks_used;
    
    info->total_bytes = (uint64_t)g_ctx.sb->block_count * COREFS_BLOCK_SIZE;
    info->used_bytes = (uint64_t)g_ctx.sb->blocks_used * COREFS_BLOCK_SIZE;
    info->free_bytes = info->total_bytes - info->used_bytes;
    
    return ESP_OK;
}

// ============================================
// MAINTENANCE
// ============================================

esp_err_t corefs_check(void) {
    if (!g_ctx.mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Running filesystem check...");
    
    // Verify superblock CRC
    uint32_t stored_csum = g_ctx.sb->checksum;
    g_ctx.sb->checksum = 0;
    uint32_t calc_csum = esp_crc32_le(0, (const uint8_t*)g_ctx.sb, 
                                       sizeof(corefs_superblock_t));
    g_ctx.sb->checksum = stored_csum;
    
    if (stored_csum != calc_csum) {
        ESP_LOGE(TAG, "Superblock corrupted!");
        return ESP_ERR_INVALID_CRC;
    }
    
    ESP_LOGI(TAG, "Filesystem check complete - OK");
    return ESP_OK;
}

esp_err_t corefs_wear_stats(uint16_t* min_wear, uint16_t* max_wear, uint16_t* avg_wear) {
    if (!g_ctx.mounted || !g_ctx.wear_table) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint16_t min = 0xFFFF;
    uint16_t max = 0;
    uint32_t total = 0;
    uint32_t count = g_ctx.sb->block_count - COREFS_METADATA_BLOCKS;
    
    for (uint32_t i = COREFS_METADATA_BLOCKS; i < g_ctx.sb->block_count; i++) {
        uint16_t wear = g_ctx.wear_table[i];
        if (wear < min) min = wear;
        if (wear > max) max = wear;
        total += wear;
    }
    
    if (min_wear) *min_wear = min;
    if (max_wear) *max_wear = max;
    if (avg_wear) *avg_wear = (uint16_t)(total / count);
    
    return ESP_OK;
}

// Export context for other modules
corefs_ctx_t* corefs_get_context(void) {
    return &g_ctx;
}