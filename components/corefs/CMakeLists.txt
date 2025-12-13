# CoreFS Component - FIXED CMakeLists.txt
# Ensures proper include paths and all source files

message(STATUS "========================================")
message(STATUS "CoreFS Component Registered")
message(STATUS "Include Directory: ${CMAKE_CURRENT_SOURCE_DIR}/include")
message(STATUS "========================================")

idf_component_register(
    SRCS
        "src/corefs_core.c"
        "src/corefs_superblock.c"
        "src/corefs_block.c"
        "src/corefs_inode.c"
        "src/corefs_btree.c"
        "src/corefs_transaction.c"
        "src/corefs_file.c"
        "src/corefs_mmap.c"
        "src/corefs_wear.c"
        "src/corefs_recovery.c"
        "src/corefs_vfs.c"
        "src/corefs_crc32.c"
    
    INCLUDE_DIRS
        "include"
        "src"  # For internal headers
    
    REQUIRES
        esp_partition
        spi_flash
        esp_timer
        vfs
        log
)

# Optimization flags for 4MB flash
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Os                          # Optimize for size
    -ffunction-sections          # Enable garbage collection
    -fdata-sections
    -fno-jump-tables
    -fno-tree-switch-conversion
)

# Add compile definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    COREFS_VERSION_STRING="v1.0"
    COREFS_BUILD_DATE=__DATE__
)