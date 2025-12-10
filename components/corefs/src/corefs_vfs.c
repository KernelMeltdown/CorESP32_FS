/**
 * corefs_vfs.c - VFS Integration (FIXED Signatures)
 * 
 * FIX: Remove void* ctx parameter from all callbacks
 * ESP-IDF VFS expects standard POSIX signatures without context
 */

#include "corefs.h"
#include "esp_vfs.h"
#include "esp_log.h"
#include <fcntl.h>
#include <string.h>

static const char* TAG = "corefs_vfs";

// VFS wrappers (stub implementation with CORRECT signatures)

// CORRECT: int open(const char* path, int flags, int mode)
static int vfs_open(const char* path, int flags, int mode) {
    (void)mode;
    
    ESP_LOGW(TAG, "VFS open not fully implemented: %s", path);
    
    // TODO: Convert flags and call corefs_open()
    return -1;
}

// CORRECT: ssize_t read(int fd, void* buf, size_t size)
static ssize_t vfs_read(int fd, void* buf, size_t size) {
    (void)fd;
    (void)buf;
    (void)size;
    
    ESP_LOGW(TAG, "VFS read not implemented");
    return -1;
}

// CORRECT: ssize_t write(int fd, const void* buf, size_t size)
static ssize_t vfs_write(int fd, const void* buf, size_t size) {
    (void)fd;
    (void)buf;
    (void)size;
    
    ESP_LOGW(TAG, "VFS write not implemented");
    return -1;
}

// CORRECT: int close(int fd)
static int vfs_close(int fd) {
    (void)fd;
    
    ESP_LOGW(TAG, "VFS close not implemented");
    return -1;
}

// CORRECT: off_t lseek(int fd, off_t offset, int whence)
static off_t vfs_lseek(int fd, off_t offset, int whence) {
    (void)fd;
    (void)offset;
    (void)whence;
    
    ESP_LOGW(TAG, "VFS lseek not implemented");
    return -1;
}

// CORRECT: int unlink(const char* path)
static int vfs_unlink(const char* path) {
    ESP_LOGW(TAG, "VFS unlink not implemented: %s", path);
    return -1;
}

// Register CoreFS with ESP-IDF VFS
esp_err_t corefs_vfs_register(const char* base_path) {
    esp_vfs_t vfs = {
        .flags = ESP_VFS_FLAG_DEFAULT,
        .open = &vfs_open,
        .read = &vfs_read,
        .write = &vfs_write,
        .close = &vfs_close,
        .lseek = &vfs_lseek,
        .unlink = &vfs_unlink,
    };
    
    ESP_LOGI(TAG, "Registering VFS at: %s", base_path);
    return esp_vfs_register(base_path, &vfs, NULL);
}

// Unregister VFS
esp_err_t corefs_vfs_unregister(const char* base_path) {
    ESP_LOGI(TAG, "Unregistering VFS: %s", base_path);
    return esp_vfs_unregister(base_path);
}