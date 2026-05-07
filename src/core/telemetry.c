#include "aurora/telemetry.h"

#include <stdio.h>
#include <string.h>

void aurora_telemetry_init(struct aurora_telemetry_ring *ring)
{
    if (ring == NULL) {
        return;
    }
    memset(ring, 0, sizeof(*ring));
}

void aurora_telemetry_emit(
    struct aurora_telemetry_ring *ring,
    aurora_time_t now_ns,
    enum aurora_event_type type,
    int pid,
    uint32_t arg0,
    uint32_t arg1,
    const char *message)
{
    if (ring == NULL) {
        return;
    }

    size_t index;
    if (ring->count < AURORA_TELEMETRY_CAPACITY) {
        index = (ring->head + ring->count) % AURORA_TELEMETRY_CAPACITY;
        ring->count++;
    } else {
        index = ring->head;
        ring->head = (ring->head + 1u) % AURORA_TELEMETRY_CAPACITY;
        ring->dropped++;
    }

    struct aurora_event *event = &ring->events[index];
    event->timestamp_ns = now_ns;
    event->type = type;
    event->pid = pid;
    event->arg0 = arg0;
    event->arg1 = arg1;

    if (message != NULL) {
        (void)snprintf(event->message, sizeof(event->message), "%s", message);
    } else {
        event->message[0] = '\0';
    }
}

size_t aurora_telemetry_snapshot(
    const struct aurora_telemetry_ring *ring,
    struct aurora_event *out,
    size_t out_capacity)
{
    if (ring == NULL || out == NULL || out_capacity == 0u) {
        return 0u;
    }

    size_t copied = ring->count < out_capacity ? ring->count : out_capacity;
    for (size_t i = 0u; i < copied; ++i) {
        size_t index = (ring->head + i) % AURORA_TELEMETRY_CAPACITY;
        out[i] = ring->events[index];
    }

    return copied;
}

const char *aurora_event_type_name(enum aurora_event_type type)
{
    switch (type) {
    case AURORA_EVENT_TASK_ADDED:
        return "TASK_ADDED";
    case AURORA_EVENT_SRC_SET:
        return "SRC_SET";
    case AURORA_EVENT_SCHED_PICK:
        return "SCHED_PICK";
    case AURORA_EVENT_PSL_ACCEPT:
        return "PSL_ACCEPT";
    case AURORA_EVENT_PSL_REJECT:
        return "PSL_REJECT";
    case AURORA_EVENT_ASO_SCORE:
        return "ASO_SCORE";
    case AURORA_EVENT_ASO_ALERT:
        return "ASO_ALERT";
    case AURORA_EVENT_FALLBACK:
        return "FALLBACK";
    case AURORA_EVENT_MEMORY_MAP:
        return "MEMORY_MAP";
    case AURORA_EVENT_MEMORY_REJECT:
        return "MEMORY_REJECT";
    }
    return "UNKNOWN_EVENT";
}
