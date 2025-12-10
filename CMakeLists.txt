# CoreFS Project - ESP32-C6
cmake_minimum_required(VERSION 3.16)

# ============================================
# Component Directories
# ============================================
set(EXTRA_COMPONENT_DIRS
    "components/corefs"
    # Weitere Components hier hinzufügen
)

# ============================================
# ESP-IDF Project Setup
# ============================================
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(corefs_ultimate)

# ============================================
# Compiler Optimization
# ============================================
# Für 4MB Flash: Optimierung auf Größe
if(CONFIG_ESPTOOLPY_FLASHSIZE_4MB)
    target_compile_options(${CMAKE_PROJECT_NAME}.elf PRIVATE
        -Os                         # Optimize for size
        -ffunction-sections         # Each function in own section
        -fdata-sections             # Each variable in own section
        -Wl,--gc-sections          # Remove unused sections
    )
endif()

# ============================================
# Custom Build Messages
# ============================================
message(STATUS "========================================")
message(STATUS "CoreFS Ultimate v1.0")
message(STATUS "Target: ESP32-C6")
message(STATUS "Flash Size: 4MB")
message(STATUS "USB CDC: Enabled")
message(STATUS "========================================")