/**
 * corefs_btree.c - B-Tree Directory Index
 * 
 * CRITICAL FIXES:
 * - Added corefs_btree_load() for mount
 * - Proper initialization writes to flash
 * - Better magic number checking
 * - Improved error messages
 */

#include "corefs.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "corefs_btree";

// FNV-1a hash function
static uint32_t hash_name(const char* name) {
    uint32_t hash = 2166136261u;
    while (*name) {
        hash ^= (uint8_t)*name++;
        hash *= 16777619u;
    }
    return hash;
}

// ============================================================================
// INITIALIZATION (called during format)
// ============================================================================

esp_err_t corefs_btree_init(corefs_ctx_t* ctx) {
    if (!ctx || !ctx->sb) {
        ESP_LOGE(TAG, "Invalid context");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing B-Tree root at block %lu", ctx->sb->root_block);
    
    // Allocate root node
    corefs_btree_node_t* root = calloc(1, sizeof(corefs_btree_node_t));
    if (!root) {
        ESP_LOGE(TAG, "Failed to allocate B-Tree root node");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize root node
    root->magic = COREFS_BLOCK_MAGIC;
    root->type = 1;  // Leaf node
    root->count = 0;
    root->parent = 0;
    
    // Initialize all children to 0
    for (int i = 0; i < COREFS_BTREE_ORDER; i++) {
        root->children[i] = 0;
    }
    
    // *** CRITICAL: Write root node to flash ***
    ESP_LOGI(TAG, "Writing B-Tree root to flash (magic: 0x%lX, type: %d)", 
             root->magic, root->type);
    
    esp_err_t ret = corefs_block_write(ctx, ctx->sb->root_block, root);
    free(root);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "B-Tree root initialized at block %lu", ctx->sb->root_block);
    } else {
        ESP_LOGE(TAG, "Failed to write B-Tree root: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

// ============================================================================
// LOAD (called during mount) - NEW!
// ============================================================================

esp_err_t corefs_btree_load(corefs_ctx_t* ctx) {
    if (!ctx || !ctx->sb) {
        ESP_LOGE(TAG, "Invalid context");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Loading B-Tree root from block %lu", ctx->sb->root_block);
    
    // Allocate node buffer
    corefs_btree_node_t* node = malloc(sizeof(corefs_btree_node_t));
    if (!node) {
        ESP_LOGE(TAG, "Failed to allocate B-Tree node");
        return ESP_ERR_NO_MEM;
    }
    
    // Read root node from flash
    esp_err_t ret = corefs_block_read(ctx, ctx->sb->root_block, node);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read B-Tree root: %s", esp_err_to_name(ret));
        free(node);
        return ret;
    }
    
    // Verify magic
    if (node->magic != COREFS_BLOCK_MAGIC) {
        ESP_LOGE(TAG, "B-Tree root corrupted (magic: 0x%lX, expected: 0x%lX)", 
                 node->magic, COREFS_BLOCK_MAGIC);
        free(node);
        return ESP_ERR_INVALID_CRC;
    }
    
    ESP_LOGI(TAG, "B-Tree loaded: type=%d, count=%d entries", 
             node->type, node->count);
    
    free(node);
    return ESP_OK;
}

// ============================================================================
// FIND
// ============================================================================

uint32_t corefs_btree_find(corefs_ctx_t* ctx, const char* path) {
    if (!ctx || !path || path[0] != '/' || !ctx->sb) {
        ESP_LOGE(TAG, "Invalid parameters for B-Tree find");
        return 0;
    }
    
    // Allocate node buffer
    corefs_btree_node_t* node = malloc(sizeof(corefs_btree_node_t));
    if (!node) {
        ESP_LOGE(TAG, "Failed to allocate B-Tree node");
        return 0;
    }
    
    // Read root node from flash
    esp_err_t ret = corefs_block_read(ctx, ctx->sb->root_block, node);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read B-Tree root: %s", esp_err_to_name(ret));
        free(node);
        return 0;
    }
    
    // Verify magic
    if (node->magic != COREFS_BLOCK_MAGIC) {
        ESP_LOGW(TAG, "B-Tree root not initialized (magic: 0x%lX), empty tree", 
                 node->magic);
        free(node);
        return 0;
    }
    
    // Compute hash
    uint32_t hash = hash_name(path);
    
    ESP_LOGD(TAG, "Searching for '%s' (hash: 0x%lX) in %d entries", 
             path, hash, node->count);
    
    // Linear search in root node
    for (int i = 0; i < node->count; i++) {
        if (node->entries[i].name_hash == hash && 
            strcmp(node->entries[i].name, path) == 0) {
            
            uint32_t inode_block = node->entries[i].inode;
            ESP_LOGD(TAG, "Found '%s' → inode block %lu", path, inode_block);
            free(node);
            return inode_block;
        }
    }
    
    // Not found
    ESP_LOGD(TAG, "File '%s' not found in B-Tree", path);
    free(node);
    return 0;
}

// ============================================================================
// INSERT
// ============================================================================

esp_err_t corefs_btree_insert(corefs_ctx_t* ctx, const char* path, uint32_t inode_block) {
    if (!ctx || !path || path[0] != '/' || !ctx->sb) {
        ESP_LOGE(TAG, "Invalid parameters for B-Tree insert");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Allocate node buffer
    corefs_btree_node_t* root = malloc(sizeof(corefs_btree_node_t));
    if (!root) {
        ESP_LOGE(TAG, "Failed to allocate B-Tree node");
        return ESP_ERR_NO_MEM;
    }
    
    // Read current root from flash
    esp_err_t ret = corefs_block_read(ctx, ctx->sb->root_block, root);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read B-Tree root: %s", esp_err_to_name(ret));
        free(root);
        return ret;
    }
    
    // Verify magic
    if (root->magic != COREFS_BLOCK_MAGIC) {
        ESP_LOGE(TAG, "B-Tree root corrupted (magic: 0x%lX)", root->magic);
        free(root);
        return ESP_ERR_INVALID_CRC;
    }
    
    // Check if node is full
    if (root->count >= COREFS_BTREE_ORDER - 1) {
        ESP_LOGE(TAG, "B-Tree node full (%d/%d), node split not implemented", 
                 root->count, COREFS_BTREE_ORDER - 1);
        free(root);
        return ESP_ERR_NO_MEM;
    }
    
    // Check for duplicate
    uint32_t hash = hash_name(path);
    for (int i = 0; i < root->count; i++) {
        if (root->entries[i].name_hash == hash && 
            strcmp(root->entries[i].name, path) == 0) {
            ESP_LOGW(TAG, "File '%s' already exists in B-Tree", path);
            free(root);
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    // Add new entry
    int idx = root->count;
    root->entries[idx].inode = inode_block;
    root->entries[idx].name_hash = hash;
    strncpy(root->entries[idx].name, path, sizeof(root->entries[idx].name) - 1);
    root->entries[idx].name[sizeof(root->entries[idx].name) - 1] = '\0';
    root->count++;
    
    ESP_LOGI(TAG, "Inserting '%s' → inode block %lu (entry %d/%d)", 
             path, inode_block, idx + 1, COREFS_BTREE_ORDER - 1);
    
    // Write back to flash
    ret = corefs_block_write(ctx, ctx->sb->root_block, root);
    free(root);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write B-Tree root: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

// ============================================================================
// DELETE
// ============================================================================

esp_err_t corefs_btree_delete(corefs_ctx_t* ctx, const char* path) {
    if (!ctx || !path || path[0] != '/' || !ctx->sb) {
        ESP_LOGE(TAG, "Invalid parameters for B-Tree delete");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Allocate node buffer
    corefs_btree_node_t* root = malloc(sizeof(corefs_btree_node_t));
    if (!root) {
        ESP_LOGE(TAG, "Failed to allocate B-Tree node");
        return ESP_ERR_NO_MEM;
    }
    
    // Read current root from flash
    esp_err_t ret = corefs_block_read(ctx, ctx->sb->root_block, root);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read B-Tree root: %s", esp_err_to_name(ret));
        free(root);
        return ret;
    }
    
    // Verify magic
    if (root->magic != COREFS_BLOCK_MAGIC) {
        ESP_LOGE(TAG, "B-Tree root corrupted (magic: 0x%lX)", root->magic);
        free(root);
        return ESP_ERR_INVALID_CRC;
    }
    
    // Find and remove entry
    uint32_t hash = hash_name(path);
    bool found = false;
    
    for (int i = 0; i < root->count; i++) {
        if (root->entries[i].name_hash == hash && 
            strcmp(root->entries[i].name, path) == 0) {
            
            ESP_LOGI(TAG, "Deleting '%s' from B-Tree (entry %d)", path, i);
            
            // Shift remaining entries left
            if (i < root->count - 1) {
                memmove(&root->entries[i], 
                        &root->entries[i + 1],
                        (root->count - i - 1) * sizeof(root->entries[0]));
            }
            
            // Clear last entry
            memset(&root->entries[root->count - 1], 0, sizeof(root->entries[0]));
            root->count--;
            found = true;
            break;
        }
    }
    
    if (!found) {
        ESP_LOGW(TAG, "File '%s' not found in B-Tree for deletion", path);
        free(root);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Write back to flash
    ret = corefs_block_write(ctx, ctx->sb->root_block, root);
    free(root);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write B-Tree root: %s", esp_err_to_name(ret));
    }
    
    return ret;
}