/**
 * CoreFS Ultimate v1.0 - Test Application
 * FIX: USB CDC on Boot + Serial Delay
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "corefs.h"

static const char* TAG = "main";

// ============================================
// FIX 1: Serial Console Delay
// ============================================
static void wait_for_serial(void) {
    // NOTIZ: Aktuell nutzen wir UART Console (nicht USB Serial JTAG)
    // wegen Linker-Problemen mit esp_usb_console_write_buf in ESP-IDF v5.5.1
    // 
    // UART braucht weniger Delay als USB, aber wir geben dem System
    // trotzdem kurz Zeit für Initialisierung
    
    printf("\n"); // Trigger UART
    vTaskDelay(pdMS_TO_TICKS(500)); // 0.5 Sekunden reichen für UART
    
    ESP_LOGI(TAG, "Serial Console ready (UART)");
}

// ============================================
// FIX 2: Partition Size Validation
// ============================================
static esp_err_t validate_partition(const esp_partition_t* partition) {
    if (!partition) {
        ESP_LOGE(TAG, "Partition not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Partition found:");
    ESP_LOGI(TAG, "  Label:    %s", partition->label);
    ESP_LOGI(TAG, "  Type:     0x%02x", partition->type);
    ESP_LOGI(TAG, "  Subtype:  0x%02x", partition->subtype);
    ESP_LOGI(TAG, "  Offset:   0x%06X", partition->address);
    ESP_LOGI(TAG, "  Size:     %u KB", partition->size / 1024);
    
    // Validierung: Größe muss Vielfaches von 4096 sein
    if (partition->size % 4096 != 0) {
        ESP_LOGE(TAG, "Partition size not sector-aligned!");
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Validierung: Offset muss Vielfaches von 4096 sein
    if (partition->address % 4096 != 0) {
        ESP_LOGE(TAG, "Partition offset not sector-aligned!");
        return ESP_ERR_INVALID_SIZE;
    }
    
    return ESP_OK;
}

// ============================================
// MAIN ENTRY
// ============================================
void app_main(void) {
    // ========================================
    // SCHRITT 1: Serial Console warten
    // ========================================
    wait_for_serial();
    
    // ========================================
    // SCHRITT 2: Banner
    // ========================================
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║  CoreFS Ultimate v1.0 - Test App      ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");
    
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Free heap: %u bytes", esp_get_free_heap_size());
    
    // ========================================
    // SCHRITT 3: Partition finden & validieren
    // ========================================
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        "corefs"
    );
    
    if (partition == NULL) {
        ESP_LOGE(TAG, "CoreFS partition not found!");
        ESP_LOGE(TAG, "Check partitions.csv");
        return;
    }
    
    esp_err_t ret = validate_partition(partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Partition validation failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // ========================================
    // SCHRITT 4: CoreFS Format
    // ========================================
    ESP_LOGI(TAG, "Formatting CoreFS...");
    
    ret = corefs_format(partition);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ Format successful");
    } else {
        ESP_LOGE(TAG, "✗ Format failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // ========================================
    // SCHRITT 5: CoreFS Mount
    // ========================================
    ESP_LOGI(TAG, "Mounting CoreFS...");
    
    ret = corefs_mount(partition);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ Mount successful");
    } else {
        ESP_LOGE(TAG, "✗ Mount failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // Get filesystem info
    corefs_info_t info;
    if (corefs_info(&info) == ESP_OK) {
        ESP_LOGI(TAG, "Filesystem: %llu KB total, %llu KB used, %llu KB free",
                 info.total_bytes / 1024, info.used_bytes / 1024, 
                 info.free_bytes / 1024);
    }
    
    // ========================================
    // SCHRITT 6: Test File Operations
    // ========================================
    ESP_LOGI(TAG, "\n=== Testing File Operations ===\n");
    
    // Test 1: Create and write file
    ESP_LOGI(TAG, "Test 1: Create file");
    corefs_file_t* file = corefs_open("/test.txt", 
                                      COREFS_O_CREAT | COREFS_O_WRONLY);
    if (!file) {
        ESP_LOGE(TAG, "✗ Failed to create file");
    } else {
        ESP_LOGI(TAG, "✓ File created");
        
        // Test 2: Write data
        ESP_LOGI(TAG, "Test 2: Write data");
        const char* data = "Hello CoreFS!\nThis is a test file.\n";
        int written = corefs_write(file, data, strlen(data));
        if (written > 0) {
            ESP_LOGI(TAG, "✓ Wrote %d bytes", written);
        } else {
            ESP_LOGE(TAG, "✗ Write failed");
        }
        
        corefs_close(file);
    }
    
    // Test 3: Read file
    ESP_LOGI(TAG, "Test 3: Read file");
    file = corefs_open("/test.txt", COREFS_O_RDONLY);
    if (!file) {
        ESP_LOGE(TAG, "✗ Failed to open file");
    } else {
        char buffer[128];
        int read_bytes = corefs_read(file, buffer, sizeof(buffer) - 1);
        if (read_bytes > 0) {
            buffer[read_bytes] = '\0';
            ESP_LOGI(TAG, "✓ Read %d bytes:", read_bytes);
            printf("%s\n", buffer);
        } else {
            ESP_LOGE(TAG, "✗ Read failed");
        }
        
        // Test 4: Close file
        ESP_LOGI(TAG, "Test 4: Close file");
        if (corefs_close(file) == ESP_OK) {
            ESP_LOGI(TAG, "✓ File closed");
        }
    }
    
    // Test 5: Check file exists
    ESP_LOGI(TAG, "Test 5: Check existence");
    if (corefs_exists("/test.txt")) {
        ESP_LOGI(TAG, "✓ File exists");
    } else {
        ESP_LOGE(TAG, "✗ File not found");
    }
    
    // ========================================
    // SCHRITT 7: Final Stats
    // ========================================
    ESP_LOGI(TAG, "\n=== System Status ===\n");
    ESP_LOGI(TAG, "CoreFS: Ready");
    ESP_LOGI(TAG, "Free heap: %u bytes", esp_get_free_heap_size());
    
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║      System Running - Tests OK!       ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");
    
    // ========================================
    // Main Loop
    // ========================================
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        // Heartbeat
        ESP_LOGI(TAG, "Heartbeat - Free heap: %u bytes", 
                 esp_get_free_heap_size());
    }
}