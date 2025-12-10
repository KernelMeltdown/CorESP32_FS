#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "corefs.h"

static const char* TAG = "main";

void app_main(void) {
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║  CoreFS Ultimate v1.0 - Test App      ║\n");
    printf("╚════════════════════════════════════════╝\n\n");
    
    // Find CoreFS partition
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 
        ESP_PARTITION_SUBTYPE_DATA_SPIFFS, 
        "corefs"
    );
    
    if (!partition) {
        ESP_LOGE(TAG, "CoreFS partition not found!");
        return;
    }
    
    ESP_LOGI(TAG, "Found CoreFS partition at 0x%lX, size %lu KB",
             partition->address, partition->size / 1024);
    
    // Format
    ESP_LOGI(TAG, "Formatting CoreFS...");
    esp_err_t ret = corefs_format(partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // Mount
    ESP_LOGI(TAG, "Mounting...");
    ret = corefs_mount(partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mount failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // Get info
    corefs_info_t info;
    if (corefs_info(&info) == ESP_OK) {
        ESP_LOGI(TAG, "Filesystem: %llu / %llu KB used",
                 info.used_bytes / 1024, info.total_bytes / 1024);
    }
    
    // ====================
    // FILE OPERATIONS TEST
    // ====================
    
    ESP_LOGI(TAG, "\n=== Testing File Operations ===");
    
    // Test 1: Create and write file
    ESP_LOGI(TAG, "Test 1: Write file");
    corefs_file_t* f1 = corefs_open("/test.txt", COREFS_O_CREAT | COREFS_O_WRONLY);
    if (f1) {
        const char* msg = "Hello CoreFS! This is a test file.";
        int written = corefs_write(f1, msg, strlen(msg));
        ESP_LOGI(TAG, "  Wrote %d bytes", written);
        corefs_close(f1);
    } else {
        ESP_LOGE(TAG, "  Failed to create file");
    }
    
    // Test 2: Read file
    ESP_LOGI(TAG, "Test 2: Read file");
    corefs_file_t* f2 = corefs_open("/test.txt", COREFS_O_RDONLY);
    if (f2) {
        char buf[128];
        memset(buf, 0, sizeof(buf));
        int read_bytes = corefs_read(f2, buf, sizeof(buf) - 1);
        ESP_LOGI(TAG, "  Read %d bytes: '%s'", read_bytes, buf);
        ESP_LOGI(TAG, "  File size: %zu bytes", corefs_size(f2));
        corefs_close(f2);
    } else {
        ESP_LOGE(TAG, "  Failed to open file");
    }
    
    // Test 3: Append to file
    ESP_LOGI(TAG, "Test 3: Append to file");
    corefs_file_t* f3 = corefs_open("/test.txt", COREFS_O_WRONLY | COREFS_O_APPEND);
    if (f3) {
        const char* more = " More data appended!";
        int written = corefs_write(f3, more, strlen(more));
        ESP_LOGI(TAG, "  Appended %d bytes", written);
        corefs_close(f3);
    }
    
    // Test 4: Read again
    ESP_LOGI(TAG, "Test 4: Read updated file");
    corefs_file_t* f4 = corefs_open("/test.txt", COREFS_O_RDONLY);
    if (f4) {
        char buf[128];
        memset(buf, 0, sizeof(buf));
        int read_bytes = corefs_read(f4, buf, sizeof(buf) - 1);
        ESP_LOGI(TAG, "  Read %d bytes: '%s'", read_bytes, buf);
        corefs_close(f4);
    }
    
    // Test 5: Check file existence
    ESP_LOGI(TAG, "Test 5: File existence");
    ESP_LOGI(TAG, "  /test.txt exists: %s", corefs_exists("/test.txt") ? "YES" : "NO");
    ESP_LOGI(TAG, "  /missing.txt exists: %s", corefs_exists("/missing.txt") ? "YES" : "NO");
    
    // Final stats
    if (corefs_info(&info) == ESP_OK) {
        ESP_LOGI(TAG, "\nFinal stats: %llu / %llu KB used (%llu%% free)",
                 info.used_bytes / 1024, 
                 info.total_bytes / 1024,
                 (info.free_bytes * 100) / info.total_bytes);
    }
    
    ESP_LOGI(TAG, "\n✓ CoreFS test complete!");
}
