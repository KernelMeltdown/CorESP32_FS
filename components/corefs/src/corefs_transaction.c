#include "corefs.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "corefs_txn";

// Forward declarations
extern esp_err_t corefs_block_read(corefs_ctx_t* ctx, uint32_t block, void* buf);
extern esp_err_t corefs_block_write(corefs_ctx_t* ctx, uint32_t block, const void* buf);

// Transaction operation codes
#define TXN_OP_NONE    0
#define TXN_OP_BEGIN   1
#define TXN_OP_WRITE   2
#define TXN_OP_DELETE  3
#define TXN_OP_COMMIT  4

// In-memory transaction log (ring buffer)
static corefs_txn_entry_t txn_log[COREFS_TXN_LOG_SIZE];
static uint32_t txn_count = 0;
static bool txn_active = false;

// Begin transaction
void corefs_txn_begin(void) {
    if (txn_active) {
        ESP_LOGW(TAG, "Transaction already active, rolling back previous");
        corefs_txn_rollback();
    }
    
    txn_count = 0;
    memset(txn_log, 0, sizeof(txn_log));
    
    // Log BEGIN entry
    corefs_txn_entry_t entry = {
        .op = TXN_OP_BEGIN,
        .inode = 0,
        .block = 0,
        .timestamp = esp_log_timestamp()
    };
    
    txn_log[txn_count++] = entry;
    txn_active = true;
    
    ESP_LOGD(TAG, "Transaction begun");
}

// Log transaction operation
void corefs_txn_log(uint32_t op, uint32_t inode, uint32_t block) {
    if (!txn_active) {
        ESP_LOGW(TAG, "Cannot log operation: no active transaction");
        return;
    }
    
    if (txn_count >= COREFS_TXN_LOG_SIZE) {
        ESP_LOGW(TAG, "Transaction log full (%d entries), cannot add more", 
                 COREFS_TXN_LOG_SIZE);
        return;
    }
    
    corefs_txn_entry_t entry = {
        .op = op,
        .inode = inode,
        .block = block,
        .timestamp = esp_log_timestamp()
    };
    
    txn_log[txn_count++] = entry;
    
    ESP_LOGD(TAG, "Logged operation %lu: inode=%lu, block=%lu", 
             op, inode, block);
}

// Commit transaction (write to flash atomically)
esp_err_t corefs_txn_commit(corefs_ctx_t* ctx) {
    if (!txn_active) {
        ESP_LOGW(TAG, "Cannot commit: no active transaction");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!ctx || !ctx->sb) {
        ESP_LOGE(TAG, "Invalid context for transaction commit");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Add COMMIT entry
    corefs_txn_entry_t commit_entry = {
        .op = TXN_OP_COMMIT,
        .inode = 0,
        .block = 0,
        .timestamp = esp_log_timestamp()
    };
    
    if (txn_count < COREFS_TXN_LOG_SIZE) {
        txn_log[txn_count++] = commit_entry;
    }
    
    // Write entire log to flash atomically
    esp_err_t ret = corefs_block_write(ctx, ctx->sb->txn_log_block, txn_log);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit transaction log: %s", 
                 esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Transaction committed with %lu operations", txn_count);
    
    // Clear log
    txn_count = 0;
    txn_active = false;
    memset(txn_log, 0, sizeof(txn_log));
    
    return ESP_OK;
}

// Rollback transaction (discard changes)
void corefs_txn_rollback(void) {
    if (!txn_active) {
        ESP_LOGD(TAG, "No active transaction to rollback");
        return;
    }
    
    ESP_LOGW(TAG, "Rolling back transaction with %lu operations", txn_count);
    
    // Simply discard in-memory log
    txn_count = 0;
    txn_active = false;
    memset(txn_log, 0, sizeof(txn_log));
}

// Check if transaction is active
bool corefs_txn_is_active(void) {
    return txn_active;
}