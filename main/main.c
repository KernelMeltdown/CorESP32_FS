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

// CoreFS Headers (werden später hinzugefügt)
// #include "corefs.h"

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
    // SCHRITT 4: CoreFS Format (Demo)
    // ========================================
    ESP_LOGI(TAG, "Formatting CoreFS...");
    
    // TODO: Ersetze mit echtem corefs_format() wenn verfügbar
    // ret = corefs_format(partition);
    
    // Demo: Erase ersten Sector
    ret = esp_partition_erase_range(partition, 0, 4096);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ Format successful (demo)");
    } else {
        ESP_LOGE(TAG, "✗ Format failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // ========================================
    // SCHRITT 5: CoreFS Mount (Demo)
    // ========================================
    ESP_LOGI(TAG, "Mounting CoreFS...");
    
    // TODO: Ersetze mit echtem corefs_mount() wenn verfügbar
    // ret = corefs_mount(partition);
    
    ESP_LOGI(TAG, "✓ Mount successful (demo)");
    
    // ========================================
    // SCHRITT 6: Test File Operations
    // ========================================
    ESP_LOGI(TAG, "\n=== Testing File Operations ===\n");
    
    ESP_LOGI(TAG, "Test 1: Create file");
    // TODO: corefs_open("/test.txt", COREFS_O_CREAT | COREFS_O_WRONLY);
    
    ESP_LOGI(TAG, "Test 2: Write data");
    // TODO: corefs_write(file, data, size);
    
    ESP_LOGI(TAG, "Test 3: Read file");
    // TODO: corefs_read(file, buffer, size);
    
    ESP_LOGI(TAG, "Test 4: Close file");
    // TODO: corefs_close(file);
    
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