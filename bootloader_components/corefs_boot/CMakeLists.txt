# Bootloader Component for CoreFS
# FIXED: Include directory is optional if header in same folder

idf_component_register(
    SRCS
    INCLUDE_DIRS "."  # Current directory (where .c and .h are)
    REQUIRES bootloader_support
)