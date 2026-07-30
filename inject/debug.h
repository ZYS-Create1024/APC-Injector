// SPDX-License-Identifier: MIT
// Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

#define ENABLE_ERROR_LOG_RELEASE 0

#ifdef DBG

    #ifndef LOG_INFO
    #define LOG_INFO(text, ...) \
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[INFO] " text "\n", ##__VA_ARGS__)
    #endif

#else

    #ifndef LOG_INFO
    #define LOG_INFO(text, ...) ((void)0)
    #endif
#endif


#ifdef DBG
    
    #ifndef LOG_ERROR
    #define LOG_ERROR(text, ...) \
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,"[ERROR] " text "\n", ##__VA_ARGS__)  //Please check if your compiler supports ##__VA_ARGS__
    #endif

#else
    #if ENABLE_ERROR_LOG_RELEASE == 1
        #ifndef LOG_ERROR
        #define LOG_ERROR(text, ...) \
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,"[ERROR] " text "\n", ##__VA_ARGS__)  //Please check if your compiler supports ##__VA_ARGS__
        #endif
    #else
        #ifndef LOG_ERROR
        #define LOG_ERROR(text, ...) ((void)0)
        #endif
    #endif
#endif

#undef ENABLE_ERROR_LOG_RELEASE