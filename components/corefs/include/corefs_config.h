#pragma once

// Build configuration for 4MB Flash
#define COREFS_4MB_BUILD

#ifdef COREFS_4MB_BUILD
    #define COREFS_METADATA_BLOCKS     64
    #define COREFS_CACHE_SIZE_KB       16
    #define COREFS_TXN_LOG_SIZE        32
    #define COREFS_ENABLE_DMA          0
    #define COREFS_ENABLE_CRYPTO       0
    #define COREFS_ENABLE_MMAP         1
    #define COREFS_WEAR_THRESHOLD      80000
#else
    #define COREFS_METADATA_BLOCKS     128
    #define COREFS_CACHE_SIZE_KB       64
    #define COREFS_TXN_LOG_SIZE        128
    #define COREFS_ENABLE_DMA          1
    #define COREFS_ENABLE_CRYPTO       1
    #define COREFS_ENABLE_MMAP         1
    #define COREFS_WEAR_THRESHOLD      100000
#endif

// Debug
#define COREFS_DEBUG               1
#define COREFS_VERIFY_CHECKSUMS    1
#define COREFS_VERIFY_MAGIC        1
