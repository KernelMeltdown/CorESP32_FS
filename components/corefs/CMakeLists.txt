# CoreFS Component

idf_component_register(
    SRCS 
        "src/corefs_core.c"
        "src/corefs_block.c"
        "src/corefs_btree.c"
        "src/corefs_inode.c"
        "src/corefs_file.c"
    INCLUDE_DIRS 
        "include"
    REQUIRES 
        esp_partition
        esp_timer
)