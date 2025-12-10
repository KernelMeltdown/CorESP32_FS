#include "esp_log.h"
#include "esp_partition.h"
#include <string.h>
#include <stdint.h>

static const char* TAG = "corefs_boot";

#define COREFS_MAGIC 0x43524653
#define COREFS_BLOCK_SIZE 2048

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t blocks_used;
    uint32_t root_block;
    uint8_t reserved[4012];
    uint32_t checksum;
} corefs_superblock_boot_t;

// Minimal CoreFS mount for bootloader
esp_err_t corefs_boot_mount(uint32_t partition_offset) {
    ESP_LOGI(TAG, "Mounting CoreFS (bootloader mode)");

    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        NULL
    );

    if (!part) return ESP_ERR_NOT_FOUND;

    // Read superblock
    corefs_superblock_boot_t sb;
    esp_err_t ret = esp_partition_read(part, partition_offset, &sb, sizeof(sb));
    if (ret != ESP_OK) return ret;

    if (sb.magic != COREFS_MAGIC) {
        ESP_LOGE(TAG, "Invalid CoreFS magic: 0x%X", sb.magic);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "CoreFS mounted: %u blocks, %u KB used",
             sb.block_count, sb.blocks_used * 2);

    return ESP_OK;
}

// Read file by path (simplified for bootloader)
esp_err_t corefs_boot_read_file(const char* path, void* buf, size_t max_size) {
    ESP_LOGW(TAG, "corefs_boot_read_file not fully implemented");
    return ESP_ERR_NOT_SUPPORTED;
}
