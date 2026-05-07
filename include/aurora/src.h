#ifndef AURORA_SRC_H
#define AURORA_SRC_H

#include "aurora/types.h"

enum aurora_src_class {
    AURORA_SRC_INTERACTIVE_UI = 0,
    AURORA_SRC_AI_INFERENCE = 1,
    AURORA_SRC_REALTIME_AUDIO = 2,
    AURORA_SRC_BACKGROUND_SYNC = 3,
    AURORA_SRC_LOW_POWER_TASK = 4,
    AURORA_SRC_SECURITY_CRITICAL = 5,
    AURORA_SRC_BATCH_COMPUTE = 6,
    AURORA_SRC_CLASS_COUNT = 7
};

enum aurora_memory_role {
    AURORA_MEM_HOT_INTERACTIVE = 0,
    AURORA_MEM_MODEL_WEIGHTS_READONLY = 1,
    AURORA_MEM_STREAMING_BUFFER = 2,
    AURORA_MEM_SECRET_TRANSIENT = 3,
    AURORA_MEM_RECOMPUTABLE_CACHE = 4,
    AURORA_MEM_LOW_POWER_COLD = 5
};

enum aurora_memory_flag {
    AURORA_MEM_FLAG_READ = 1u << 0,
    AURORA_MEM_FLAG_WRITE = 1u << 1,
    AURORA_MEM_FLAG_EXEC = 1u << 2,
    AURORA_MEM_FLAG_PINNED = 1u << 3,
    AURORA_MEM_FLAG_NOSWAP = 1u << 4,
    AURORA_MEM_FLAG_RECLAIMABLE = 1u << 5
};

enum aurora_privacy_flag {
    AURORA_PRIVACY_LOCAL_ONLY = 1u << 0,
    AURORA_PRIVACY_NO_TELEMETRY_PAYLOAD = 1u << 1,
    AURORA_PRIVACY_NO_REMOTE_DELEGATION = 1u << 2
};

struct aurora_src_contract {
    uint32_t latency_us_target;
    uint32_t cpu_burst_us_max;
    uint32_t memory_pressure_policy;
    uint32_t accelerator_mask;
    uint32_t privacy_flags;
    uint64_t energy_budget_nj;
};

struct aurora_memory_region {
    int pid;
    uintptr_t base;
    size_t length;
    enum aurora_memory_role role;
    uint32_t flags;
};

const char *aurora_src_class_name(enum aurora_src_class src_class);
const char *aurora_memory_role_name(enum aurora_memory_role role);
int aurora_src_class_base_boost(enum aurora_src_class src_class);
bool aurora_src_contract_valid(const struct aurora_src_contract *contract);

#endif
