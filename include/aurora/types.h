#ifndef AURORA_TYPES_H
#define AURORA_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AURORA_MAX_TASKS 64u
#define AURORA_MAX_HINTS 64u
#define AURORA_MAX_MEMORY_REGIONS 128u
#define AURORA_TELEMETRY_CAPACITY 256u
#define AURORA_LEDGER_CAPACITY 256u
#define AURORA_NAME_MAX 32u
#define AURORA_MESSAGE_MAX 96u

typedef uint64_t aurora_time_t;
typedef uint32_t aurora_caps_t;

enum aurora_status {
    AURORA_OK = 0,
    AURORA_ERR_INVALID = -1,
    AURORA_ERR_DENIED = -2,
    AURORA_ERR_FULL = -3,
    AURORA_ERR_NOT_FOUND = -4,
    AURORA_ERR_EXPIRED = -5,
    AURORA_ERR_RANGE = -6
};

enum aurora_capability {
    AURORA_CAP_PSL_SUBMIT = 1u << 0,
    AURORA_CAP_TELEMETRY_READ = 1u << 1,
    AURORA_CAP_ASO_SUBMIT = 1u << 2,
    AURORA_CAP_SRC_ADMIN = 1u << 3
};

static inline bool aurora_has_cap(aurora_caps_t caps, enum aurora_capability cap)
{
    return (caps & (aurora_caps_t)cap) != 0u;
}

const char *aurora_status_name(enum aurora_status status);

#endif
