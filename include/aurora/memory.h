#ifndef AURORA_MEMORY_H
#define AURORA_MEMORY_H

#include "aurora/ledger.h"
#include "aurora/scheduler.h"
#include "aurora/src.h"
#include "aurora/telemetry.h"
#include "aurora/types.h"

struct aurora_memory_manager {
    struct aurora_memory_region regions[AURORA_MAX_MEMORY_REGIONS];
    size_t count;
    struct aurora_scheduler *scheduler;
    struct aurora_telemetry_ring *telemetry;
    struct aurora_policy_ledger *ledger;
};

void aurora_memory_init(
    struct aurora_memory_manager *memory,
    struct aurora_scheduler *scheduler,
    struct aurora_telemetry_ring *telemetry,
    struct aurora_policy_ledger *ledger);
enum aurora_status aurora_memory_map_semantic(
    struct aurora_memory_manager *memory,
    int pid,
    uintptr_t base,
    size_t length,
    enum aurora_memory_role role,
    uint32_t flags);
const struct aurora_memory_region *aurora_memory_find_region(
    const struct aurora_memory_manager *memory,
    int pid,
    uintptr_t base);
size_t aurora_memory_count_by_role(
    const struct aurora_memory_manager *memory,
    enum aurora_memory_role role);

#endif

