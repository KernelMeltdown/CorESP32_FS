/**
 * CoreFS - B-Tree Directory Index
 * FIXED: Correct array bounds for entry names
 */

#include "corefs.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "corefs_btree";

// Forward declarations
extern esp_err_t corefs_block_read(corefs_ctx_t* ctx, uint32_t block, void* buf);
extern esp_err_t corefs_block_write(corefs_ctx_t* ctx, uint32_t block, const void* buf);

// ============================================
// HASH FUNCTION (FNV-1a)
// ============================================

static uint32_t hash_name(const char* name) {
    uint32_t hash = 2166136261u;
    while (*name) {
        hash ^= (uint8_t)*name++;
        hash *= 16777619u;
    }
    return hash;
}

// ============================================
// INITIALIZATION
// ============================================

esp_err_t corefs_btree_init(corefs_ctx_t* ctx) {
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Create empty root node
    corefs_btree_node_t* root = calloc(1, sizeof(corefs_btree_node_t));
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    
    root->magic = COREFS_BTREE_MAGIC;
    root->type = 1;  // Leaf node
    root->count = 0;
    root->parent = 0;
    
    // Write to block 1
    esp_err_t ret = corefs_block_write(ctx, ctx->sb->root_block, root);
    free(root);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "B-Tree initialized");
    }
    
    return ret;
}

esp_err_t corefs_btree_load(corefs_ctx_t* ctx) {
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Read root node to verify it exists
    corefs_btree_node_t* root = calloc(1, sizeof(corefs_btree_node_t));
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t ret = corefs_block_read(ctx, ctx->sb->root_block, root);
    if (ret == ESP_OK) {
        if (root->magic != COREFS_BTREE_MAGIC) {
            ESP_LOGE(TAG, "Invalid B-Tree root magic");
            ret = ESP_ERR_INVALID_STATE;
        } else {
            ESP_LOGI(TAG, "B-Tree loaded: %u entries", root->count);
        }
    }
    
    free(root);
    return ret;
}

// ============================================
// FIND
// ============================================

int32_t corefs_btree_find(corefs_ctx_t* ctx, const char* path) {
    if (!ctx || !path || path[0] != '/') {
        return -1;
    }
    
    // Extract filename (skip leading '/')
    const char* filename = path + 1;
    if (strlen(filename) == 0) {
        return -1;  // Root directory not supported yet
    }
    
    // Read root node
    corefs_btree_node_t* node = calloc(1, sizeof(corefs_btree_node_t));
    if (!node) {
        return -1;
    }
    
    esp_err_t ret = corefs_block_read(ctx, ctx->sb->root_block, node);
    if (ret != ESP_OK) {
        free(node);
        return -1;
    }
    
    // Search in root (simple linear search for now)
    uint32_t hash = hash_name(filename);
    
    for (int i = 0; i < node->count; i++) {
        if (node->entries[i].name_hash == hash &&
            strcmp(node->entries[i].name, filename) == 0) {
            
            uint32_t inode_block = node->entries[i].inode_block;
            free(node);
            return inode_block;
        }
    }
    
    free(node);
    return -1;  // Not found
}

// ============================================
// INSERT
// ============================================

esp_err_t corefs_btree_insert(corefs_ctx_t* ctx, const char* path, uint32_t inode_block) {
    if (!ctx || !path || path[0] != '/') {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Extract filename
    const char* filename = path + 1;
    
    // ✓ FIXED: Check against actual array size (64), not COREFS_MAX_FILENAME (255)
    if (strlen(filename) >= 64) {
        ESP_LOGE(TAG, "Filename too long (max 63 chars): %s", filename);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Read root node
    corefs_btree_node_t* node = calloc(1, sizeof(corefs_btree_node_t));
    if (!node) {
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t ret = corefs_block_read(ctx, ctx->sb->root_block, node);
    if (ret != ESP_OK) {
        free(node);
        return ret;
    }
    
    // Check if node is full
    if (node->count >= (COREFS_BTREE_ORDER - 1)) {
        ESP_LOGE(TAG, "B-Tree node full (split not implemented)");
        free(node);
        return ESP_ERR_NO_MEM;
    }
    
    // Check for duplicate
    uint32_t hash = hash_name(filename);
    for (int i = 0; i < node->count; i++) {
        if (node->entries[i].name_hash == hash &&
            strcmp(node->entries[i].name, filename) == 0) {
            free(node);
            return ESP_ERR_INVALID_STATE;  // Already exists
        }
    }
    
    // Insert entry
    int idx = node->count;
    node->entries[idx].inode_block = inode_block;
    node->entries[idx].name_hash = hash;
    
    // ✓ FIXED: Use correct array size (64 bytes)
    strncpy(node->entries[idx].name, filename, 63);
    node->entries[idx].name[63] = '\0';
    
    node->count++;
    
    // Write back
    ret = corefs_block_write(ctx, ctx->sb->root_block, node);
    free(node);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Inserted '%s' -> inode block %u", filename, inode_block);
    }
    
    return ret;
}

// ============================================
// DELETE
// ============================================

esp_err_t corefs_btree_delete(corefs_ctx_t* ctx, const char* path) {
    if (!ctx || !path || path[0] != '/') {
        return ESP_ERR_INVALID_ARG;
    }
    
    const char* filename = path + 1;
    
    // Read root node
    corefs_btree_node_t* node = calloc(1, sizeof(corefs_btree_node_t));
    if (!node) {
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t ret = corefs_block_read(ctx, ctx->sb->root_block, node);
    if (ret != ESP_OK) {
        free(node);
        return ret;
    }
    
    // Find entry
    uint32_t hash = hash_name(filename);
    int found_idx = -1;
    
    for (int i = 0; i < node->count; i++) {
        if (node->entries[i].name_hash == hash &&
            strcmp(node->entries[i].name, filename) == 0) {
            found_idx = i;
            break;
        }
    }
    
    if (found_idx < 0) {
        free(node);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Shift remaining entries
    if (found_idx < node->count - 1) {
        memmove(&node->entries[found_idx], 
                &node->entries[found_idx + 1],
                (node->count - found_idx - 1) * sizeof(node->entries[0]));
    }
    node->count--;
    
    // Write back
    ret = corefs_block_write(ctx, ctx->sb->root_block, node);
    free(node);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Deleted '%s'", filename);
    }
    
    return ret;
}