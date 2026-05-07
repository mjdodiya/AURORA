#include "aurora/memory.h"

#include <stdio.h>
#include <string.h>

static bool role_flags_valid(enum aurora_memory_role role, uint32_t flags)
{
    if ((flags & AURORA_MEM_FLAG_READ) == 0u) {
        return false;
    }

    if ((flags & AURORA_MEM_FLAG_WRITE) != 0u && (flags & AURORA_MEM_FLAG_EXEC) != 0u) {
        return false;
    }

    switch (role) {
    case AURORA_MEM_MODEL_WEIGHTS_READONLY:
        return (flags & AURORA_MEM_FLAG_WRITE) == 0u;
    case AURORA_MEM_SECRET_TRANSIENT:
        return (flags & AURORA_MEM_FLAG_NOSWAP) != 0u &&
               (flags & AURORA_MEM_FLAG_EXEC) == 0u;
    case AURORA_MEM_RECOMPUTABLE_CACHE:
        return (flags & AURORA_MEM_FLAG_RECLAIMABLE) != 0u;
    case AURORA_MEM_HOT_INTERACTIVE:
    case AURORA_MEM_STREAMING_BUFFER:
    case AURORA_MEM_LOW_POWER_COLD:
        return true;
    }

    return false;
}

static bool overlaps(uintptr_t a_base, size_t a_len, uintptr_t b_base, size_t b_len)
{
    uintptr_t a_end = a_base + a_len;
    uintptr_t b_end = b_base + b_len;

    if (a_end < a_base || b_end < b_base) {
        return true;
    }

    return a_base < b_end && b_base < a_end;
}

void aurora_memory_init(
    struct aurora_memory_manager *memory,
    struct aurora_scheduler *scheduler,
    struct aurora_telemetry_ring *telemetry,
    struct aurora_policy_ledger *ledger)
{
    if (memory == NULL) {
        return;
    }

    memset(memory, 0, sizeof(*memory));
    memory->scheduler = scheduler;
    memory->telemetry = telemetry;
    memory->ledger = ledger;
}

enum aurora_status aurora_memory_map_semantic(
    struct aurora_memory_manager *memory,
    int pid,
    uintptr_t base,
    size_t length,
    enum aurora_memory_role role,
    uint32_t flags)
{
    if (memory == NULL || pid <= 0 || base == 0u || length == 0u) {
        return AURORA_ERR_INVALID;
    }

    if (aurora_sched_find_task(memory->scheduler, pid) == NULL) {
        return AURORA_ERR_NOT_FOUND;
    }

    if (!role_flags_valid(role, flags)) {
        aurora_telemetry_emit(
            memory->telemetry,
            memory->scheduler != NULL ? memory->scheduler->now_ns : 0u,
            AURORA_EVENT_MEMORY_REJECT,
            pid,
            (uint32_t)role,
            flags,
            "semantic memory policy rejected");
        aurora_ledger_append(
            memory->ledger,
            memory->scheduler != NULL ? memory->scheduler->now_ns : 0u,
            AURORA_LEDGER_MEMORY,
            AURORA_DECISION_REJECT,
            pid,
            "invalid semantic memory flags");
        return AURORA_ERR_DENIED;
    }

    for (size_t i = 0u; i < memory->count; ++i) {
        const struct aurora_memory_region *existing = &memory->regions[i];
        if (existing->pid == pid && overlaps(existing->base, existing->length, base, length)) {
            return AURORA_ERR_INVALID;
        }
    }

    if (memory->count >= AURORA_MAX_MEMORY_REGIONS) {
        return AURORA_ERR_FULL;
    }

    struct aurora_memory_region *region = &memory->regions[memory->count++];
    region->pid = pid;
    region->base = base;
    region->length = length;
    region->role = role;
    region->flags = flags;

    aurora_telemetry_emit(
        memory->telemetry,
        memory->scheduler != NULL ? memory->scheduler->now_ns : 0u,
        AURORA_EVENT_MEMORY_MAP,
        pid,
        (uint32_t)role,
        flags,
        "semantic memory region admitted");
    aurora_ledger_append(
        memory->ledger,
        memory->scheduler != NULL ? memory->scheduler->now_ns : 0u,
        AURORA_LEDGER_MEMORY,
        AURORA_DECISION_ACCEPT,
        pid,
        "semantic memory region admitted");

    return AURORA_OK;
}

const struct aurora_memory_region *aurora_memory_find_region(
    const struct aurora_memory_manager *memory,
    int pid,
    uintptr_t base)
{
    if (memory == NULL) {
        return NULL;
    }

    for (size_t i = 0u; i < memory->count; ++i) {
        const struct aurora_memory_region *region = &memory->regions[i];
        if (region->pid == pid && region->base == base) {
            return region;
        }
    }

    return NULL;
}

size_t aurora_memory_count_by_role(
    const struct aurora_memory_manager *memory,
    enum aurora_memory_role role)
{
    if (memory == NULL) {
        return 0u;
    }

    size_t count = 0u;
    for (size_t i = 0u; i < memory->count; ++i) {
        if (memory->regions[i].role == role) {
            count++;
        }
    }
    return count;
}

