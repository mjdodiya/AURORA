#ifndef AURORA_PSL_H
#define AURORA_PSL_H

#include "aurora/ledger.h"
#include "aurora/telemetry.h"
#include "aurora/types.h"

struct aurora_psl_hint {
    int target_pid;
    aurora_time_t issued_at_ns;
    aurora_time_t valid_until_ns;
    uint32_t confidence_ppm;
    uint32_t requested_cpu_capacity;
    uint32_t prewarm_pages;
    uint32_t preferred_numa_node;
    uint32_t thermal_sensitivity;
};

struct aurora_psl_gate {
    struct aurora_psl_hint hints[AURORA_MAX_HINTS];
    size_t count;
    bool enabled;
    uint32_t min_confidence_ppm;
    uint32_t max_cpu_capacity;
    uint32_t max_prewarm_pages;
    struct aurora_telemetry_ring *telemetry;
    struct aurora_policy_ledger *ledger;
};

void aurora_psl_gate_init(
    struct aurora_psl_gate *gate,
    struct aurora_telemetry_ring *telemetry,
    struct aurora_policy_ledger *ledger);
enum aurora_status aurora_psl_submit_hint(
    struct aurora_psl_gate *gate,
    const struct aurora_psl_hint *hint,
    aurora_caps_t caller_caps,
    aurora_time_t now_ns);
int aurora_psl_priority_boost(
    const struct aurora_psl_gate *gate,
    int pid,
    aurora_time_t now_ns);
void aurora_psl_disable(
    struct aurora_psl_gate *gate,
    aurora_time_t now_ns,
    const char *reason);
void aurora_psl_expire_old(struct aurora_psl_gate *gate, aurora_time_t now_ns);

#endif

