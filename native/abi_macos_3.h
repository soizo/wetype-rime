/* Private integration ABI for Mac 3.0.1 (680), arm64. Offsets checked against
 * the app's call sites and Swift field descriptors. Not a public SDK. */
#ifndef WETYPE_ABI_MACOS_3_H
#define WETYPE_ABI_MACOS_3_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
    void *logger;                       /* 00 */
    uint8_t log_without_time;             /* 08 */
    uint8_t pad09[7];
    const char *logger_config_json;       /* 10 */
    const char *work_dir;                 /* 18 */
    const char *syllable_prefix_file_name;/* 20 */
    uint8_t custom_syllable_prefix_file_name; /* 28 */
    uint8_t safe_mode;                    /* 29 */
    uint8_t pad2a[6];
    void *user_callback;                  /* 30 */
    int32_t device_performance;           /* 38 */
    uint8_t test_mode;                    /* 3c */
    uint8_t disable_low_performance;      /* 3d */
    uint8_t pad3e[2];
    const char *uin;                      /* 40 */
    uint32_t uin_len;                     /* 48 */
    int32_t log_level;                    /* 4c */
    int32_t platform;                     /* 50 */
    uint8_t pad54[4];
    const char *app_ver;                  /* 58 */
    uint32_t app_ver_len;                 /* 60 */
    uint8_t pad64[4];
    const char *device_name;              /* 68 */
    uint32_t device_name_len;             /* 70 */
    uint8_t pad74[4];
    const char *system_version;           /* 78 */
    uint32_t system_version_len;          /* 80 */
    uint8_t pad84[4];
    void *report_exception;               /* 88 */
    void *report_data_mirror;             /* 90 */
    uint8_t report_probability;           /* 98 */
    uint8_t pad99[7];
    void *check_low_performance;          /* a0 */
    uint8_t cli_ab_test_info[16];         /* a8 */
    float cpu_score;                     /* b8 */
    float gpu_score;                     /* bc */
    int32_t package_type;                /* c0 */
    uint8_t padc4[4];
    void *platform_init;                 /* c8 */
} WetypeConfig;
_Static_assert(sizeof(WetypeConfig) == 0xd0, "config ABI size");
_Static_assert(offsetof(WetypeConfig, work_dir) == 0x18, "work_dir ABI");
_Static_assert(offsetof(WetypeConfig, platform) == 0x50, "platform ABI");
_Static_assert(offsetof(WetypeConfig, cpu_score) == 0xb8, "device score ABI");
#endif
