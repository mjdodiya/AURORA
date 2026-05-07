#ifndef AURORA_TELEMETRY_H
#define AURORA_TELEMETRY_H

#include "aurora/types.h"

enum aurora_event_type {
    AURORA_EVENT_TASK_ADDED = 1,
    AURORA_EVENT_SRC_SET = 2,
    AURORA_EVENT_SCHED_PICK = 3,
    AURORA_EVENT_PSL_ACCEPT = 4,
    AURORA_EVENT_PSL_REJECT = 5,
    AURORA_EVENT_ASO_SCORE = 6,
    AURORA_EVENT_ASO_ALERT = 7,
    AURORA_EVENT_FALLBACK = 8,
    AURORA_EVENT_MEMORY_MAP = 9,
    AURORA_EVENT_MEMORY_REJECT = 10
};

struct aurora_event {
    aurora_time_t timestamp_ns;
    enum aurora_event_type type;
    int pid;
    uint32_t arg0;
    uint32_t arg1;
    char message[64];
};

struct aurora_telemetry_ring {
    struct aurora_event events[AURORA_TELEMETRY_CAPACITY];
    size_t head;
    size_t count;
    uint64_t dropped;
};

void aurora_telemetry_init(struct aurora_telemetry_ring *ring);
void aurora_telemetry_emit(
    struct aurora_telemetry_ring *ring,
    aurora_time_t now_ns,
    enum aurora_event_type type,
    int pid,
    uint32_t arg0,
    uint32_t arg1,
    const char *message);
size_t aurora_telemetry_snapshot(
    const struct aurora_telemetry_ring *ring,
    struct aurora_event *out,
    size_t out_capacity);
const char *aurora_event_type_name(enum aurora_event_type type);

#endif
