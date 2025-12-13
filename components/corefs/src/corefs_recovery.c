/**
 * corefs_recovery.c - Filesystem Recovery & Consistency Check
 * FIXED: Uses correct superblock field name (checksum)
 */

#include "corefs.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "corefs_recovery";

// Transaction operation codes (from corefs_transaction.c)
#define TXN_OP_NONE    0
#define TXN_OP_BEGIN   1
#define TXN_OP_WRITE   2
#define TXN_OP_DELETE  3
#define TXN_OP_COMMIT  4

// External declarations
extern corefs_ctx_t* corefs_get_context(void);
extern esp_err_t corefs_block_read(corefs_ctx_t* ctx, uint32_t block, void* buf);

// ============================================================================
// RECOVERY SCAN (called during mount on unclean shutdown)
// ============================================================================

esp_err_t corefs_recovery_scan(corefs_ctx_t* ctx) {
    if (!ctx || !ctx->sb) {
        ESP_LOGE(TAG, "Invalid context for recovery scan");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Starting filesystem recovery scan...");
    
    // Allocate buffer for transaction log
    corefs_txn_entry_t* txn_log = malloc(COREFS_BLOCK_SIZE);
    if (!txn_log) {
        ESP_LOGE(TAG, "Failed to allocate transaction log buffer");
        return ESP_ERR_NO_MEM;
    }
    
    // Read transaction log from flash
    esp_err_t ret = corefs_block_read(ctx, ctx->sb->txn_log_block, txn_log);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read transaction log: %s", esp_err_to_name(ret));
        free(txn_log);
        // Continue recovery even if log is unreadable
        return ESP_OK;
    }
    
    // Analyze transaction log for incomplete transactions
    bool incomplete = false;
    int txn_count = 0;
    int max_entries = COREFS_BLOCK_SIZE / sizeof(corefs_txn_entry_t);
    
    for (int i = 0; i < max_entries; i++) {
        if (txn_log[i].op == TXN_OP_BEGIN) {
            incomplete = true;
            txn_count = 0;
            ESP_LOGD(TAG, "Found transaction begin at entry %d", i);
        } else if (txn_log[i].op == TXN_OP_COMMIT) {
            incomplete = false;
            ESP_LOGD(TAG, "Found transaction commit at entry %d (%d ops)", 
                     i, txn_count);
        } else if (txn_log[i].op != TXN_OP_NONE) {
            txn_count++;
        }
    }
    
    if (incomplete) {
        ESP_LOGW(TAG, "Found incomplete transaction with %d operations", txn_count);
        ESP_LOGI(TAG, "Copy-on-write recovery: old data is still valid");
        // Copy-on-write means old data is still valid, nothing to do!
    } else {
        ESP_LOGI(TAG, "No incomplete transactions found");
    }
    
    free(txn_log);
    
    // Verify superblock CRC (✓ FIXED: checksum field)
    uint32_t stored_csum = ctx->sb->checksum;
    ctx->sb->checksum = 0;
    uint32_t calc_csum = crc32(ctx->sb, sizeof(corefs_superblock_t));
    ctx->sb->checksum = stored_csum;
    
    if (stored_csum != calc_csum) {
        ESP_LOGE(TAG, "Superblock CRC corrupted: 0x%08lX != 0x%08lX", 
                 stored_csum, calc_csum);
        return ESP_ERR_INVALID_CRC;
    }
    
    ESP_LOGI(TAG, "Superblock CRC verified: 0x%08lX", stored_csum);
    ESP_LOGI(TAG, "Recovery complete: filesystem consistent");
    
    return ESP_OK;
}

// ============================================================================
// FILESYSTEM CHECK (fsck)
// ============================================================================

esp_err_t corefs_check(void) {
    corefs_ctx_t* ctx = corefs_get_context();
    
    if (!ctx->mounted) {
        ESP_LOGE(TAG, "Filesystem not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Running filesystem check (fsck)...");
    
    // Verify superblock
    if (ctx->sb->magic != COREFS_MAGIC) {
        ESP_LOGE(TAG, "Invalid superblock magic: 0x%lX", ctx->sb->magic);
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "✓ Superblock magic valid");
    
    // Verify superblock CRC (✓ FIXED: checksum field)
    uint32_t stored_crc = ctx->sb->checksum;
    ctx->sb->checksum = 0;
    uint32_t calc_crc = crc32(ctx->sb, sizeof(corefs_superblock_t));
    ctx->sb->checksum = stored_crc;
    
    if (stored_crc != calc_crc) {
        ESP_LOGE(TAG, "Superblock CRC mismatch: 0x%08lX != 0x%08lX", 
                 stored_crc, calc_crc);
        return ESP_ERR_INVALID_CRC;
    }
    
    ESP_LOGI(TAG, "✓ Superblock CRC valid");
    
    // Verify wear leveling
    esp_err_t ret = corefs_wear_check(ctx);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠ Wear leveling issues detected");
    } else {
        ESP_LOGI(TAG, "✓ Wear leveling OK");
    }
    
    // TODO: Verify B-Tree structure
    ESP_LOGW(TAG, "⚠ B-Tree verification not implemented");
    
    // TODO: Verify all inode checksums
    ESP_LOGW(TAG, "⚠ Inode verification not implemented");
    
    // TODO: Check for orphaned blocks
    ESP_LOGW(TAG, "⚠ Orphan block detection not implemented");
    
    ESP_LOGI(TAG, "Filesystem check complete");
    return ESP_OK;
}