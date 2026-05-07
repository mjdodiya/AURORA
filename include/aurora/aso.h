#ifndef AURORA_ASO_H
#define AURORA_ASO_H

#include "aurora/ledger.h"
#include "aurora/scheduler.h"
#include "aurora/telemetry.h"
#include "aurora/types.h"

enum aurora_syscall_id {
    AURORA_SYSCALL_OPEN = 1,
    AURORA_SYSCALL_EXEC = 2,
    AURORA_SYSCALL_MMAP = 3,
    AURORA_SYSCALL_MPROTECT = 4,
    AURORA_SYSCALL_IOCTL = 5,
    AURORA_SYSCALL_SETUID = 6,
    AURORA_SYSCALL_IPC = 7
};

enum aurora_syscall_flag {
    AURORA_SYSCALL_FLAG_PRIVILEGE_DELTA = 1u << 0,
    AURORA_SYSCALL_FLAG_WRITE_EXEC = 1u << 1,
    AURORA_SYSCALL_FLAG_DMA_REQUEST = 1u << 2,
    AURORA_SYSCALL_FLAG_CROSS_DOMAIN_IPC = 1u << 3,
    AURORA_SYSCALL_FLAG_UNSIGNED_CODE = 1u << 4
};

struct aurora_syscall_sample {
    int pid;
    enum aurora_syscall_id syscall_id;
    uint32_t flags;
    uint32_t arg_shape;
    aurora_time_t timestamp_ns;
};

struct aurora_process_profile {
    int pid;
    uint64_t syscall_count;
    uint32_t last_score;
    uint32_t fingerprint;
};

struct aurora_aso {
    struct aurora_process_profile profiles[AURORA_MAX_TASKS];
    size_t profile_count;
    uint32_t alert_threshold;
    struct aurora_scheduler *scheduler;
    struct aurora_telemetry_ring *telemetry;
    struct aurora_policy_ledger *ledger;
};

void aurora_aso_init(
    struct aurora_aso *aso,
    struct aurora_scheduler *scheduler,
    struct aurora_telemetry_ring *telemetry,
    struct aurora_policy_ledger *ledger);
enum aurora_status aurora_aso_observe(
    struct aurora_aso *aso,
    const struct aurora_syscall_sample *sample,
    aurora_caps_t caller_caps);
const struct aurora_process_profile *aurora_aso_find_profile(
    const struct aurora_aso *aso,
    int pid);

#endif

