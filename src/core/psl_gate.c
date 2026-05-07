#include "aurora/psl.h"

#include <stdio.h>
#include <string.h>

static size_t find_hint_index(const struct aurora_psl_gate *gate, int pid, bool *found)
{
    *found = false;
    if (gate == NULL) {
        return 0u;
    }

    for (size_t i = 0u; i < gate->count; ++i) {
        if (gate->hints[i].target_pid == pid) {
            *found = true;
            return i;
        }
    }

    return gate->count;
}

void aurora_psl_gate_init(
    struct aurora_psl_gate *gate,
    struct aurora_telemetry_ring *telemetry,
    struct aurora_policy_ledger *ledger)
{
    if (gate == NULL) {
        return;
    }

    memset(gate, 0, sizeof(*gate));
    gate->enabled = true;
    gate->min_confidence_ppm = 300000u;
    gate->max_cpu_capacity = 100u;
    gate->max_prewarm_pages = 4096u;
    gate->telemetry = telemetry;
    gate->ledger = ledger;
}

enum aurora_status aurora_psl_submit_hint(
    struct aurora_psl_gate *gate,
    const struct aurora_psl_hint *hint,
    aurora_caps_t caller_caps,
    aurora_time_t now_ns)
{
    if (gate == NULL || hint == NULL || hint->target_pid <= 0) {
        return AURORA_ERR_INVALID;
    }

    if (!gate->enabled) {
        aurora_telemetry_emit(
            gate->telemetry,
            now_ns,
            AURORA_EVENT_PSL_REJECT,
            hint->target_pid,
            0u,
            0u,
            "PSL gate disabled");
        aurora_ledger_append(
            gate->ledger,
            now_ns,
            AURORA_LEDGER_PSL,
            AURORA_DECISION_REJECT,
            hint->target_pid,
            "gate disabled");
        return AURORA_ERR_DENIED;
    }

    if (!aurora_has_cap(caller_caps, AURORA_CAP_PSL_SUBMIT)) {
        aurora_telemetry_emit(
            gate->telemetry,
            now_ns,
            AURORA_EVENT_PSL_REJECT,
            hint->target_pid,
            0u,
            0u,
            "missing PSL capability");
        aurora_ledger_append(
            gate->ledger,
            now_ns,
            AURORA_LEDGER_PSL,
            AURORA_DECISION_REJECT,
            hint->target_pid,
            "missing PSL capability");
        return AURORA_ERR_DENIED;
    }

    if (hint->valid_until_ns <= now_ns || hint->valid_until_ns <= hint->issued_at_ns) {
        aurora_telemetry_emit(
            gate->telemetry,
            now_ns,
            AURORA_EVENT_PSL_REJECT,
            hint->target_pid,
            0u,
            0u,
            "expired hint");
        aurora_ledger_append(
            gate->ledger,
            now_ns,
            AURORA_LEDGER_PSL,
            AURORA_DECISION_REJECT,
            hint->target_pid,
            "expired hint");
        return AURORA_ERR_EXPIRED;
    }

    if (hint->confidence_ppm < gate->min_confidence_ppm) {
        aurora_telemetry_emit(
            gate->telemetry,
            now_ns,
            AURORA_EVENT_PSL_REJECT,
            hint->target_pid,
            hint->confidence_ppm,
            gate->min_confidence_ppm,
            "low confidence hint");
        aurora_ledger_append(
            gate->ledger,
            now_ns,
            AURORA_LEDGER_PSL,
            AURORA_DECISION_REJECT,
            hint->target_pid,
            "confidence below minimum");
        return AURORA_ERR_RANGE;
    }

    struct aurora_psl_hint sanitized = *hint;
    bool clamped = false;
    if (sanitized.requested_cpu_capacity > gate->max_cpu_capacity) {
        sanitized.requested_cpu_capacity = gate->max_cpu_capacity;
        clamped = true;
    }
    if (sanitized.prewarm_pages > gate->max_prewarm_pages) {
        sanitized.prewarm_pages = gate->max_prewarm_pages;
        clamped = true;
    }

    bool found;
    size_t index = find_hint_index(gate, sanitized.target_pid, &found);
    if (!found && gate->count >= AURORA_MAX_HINTS) {
        aurora_telemetry_emit(
            gate->telemetry,
            now_ns,
            AURORA_EVENT_PSL_REJECT,
            sanitized.target_pid,
            0u,
            0u,
            "hint table full");
        aurora_ledger_append(
            gate->ledger,
            now_ns,
            AURORA_LEDGER_PSL,
            AURORA_DECISION_REJECT,
            sanitized.target_pid,
            "hint table full");
        return AURORA_ERR_FULL;
    }

    if (!found) {
        gate->count++;
    }
    gate->hints[index] = sanitized;

    aurora_telemetry_emit(
        gate->telemetry,
        now_ns,
        AURORA_EVENT_PSL_ACCEPT,
        sanitized.target_pid,
        sanitized.requested_cpu_capacity,
        sanitized.confidence_ppm,
        clamped ? "hint accepted with clamp" : "hint accepted");
    aurora_ledger_append(
        gate->ledger,
        now_ns,
        AURORA_LEDGER_PSL,
        clamped ? AURORA_DECISION_CLAMP : AURORA_DECISION_ACCEPT,
        sanitized.target_pid,
        clamped ? "bounded advisory hint clamped" : "bounded advisory hint accepted");

    return AURORA_OK;
}

int aurora_psl_priority_boost(
    const struct aurora_psl_gate *gate,
    int pid,
    aurora_time_t now_ns)
{
    if (gate == NULL || !gate->enabled) {
        return 0;
    }

    for (size_t i = 0u; i < gate->count; ++i) {
        const struct aurora_psl_hint *hint = &gate->hints[i];
        if (hint->target_pid != pid || hint->valid_until_ns <= now_ns) {
            continue;
        }

        uint64_t weighted_capacity =
            (uint64_t)hint->requested_cpu_capacity * (uint64_t)hint->confidence_ppm;
        return (int)(weighted_capacity / 5000000u);
    }

    return 0;
}

void aurora_psl_disable(
    struct aurora_psl_gate *gate,
    aurora_time_t now_ns,
    const char *reason)
{
    if (gate == NULL) {
        return;
    }

    gate->enabled = false;
    gate->count = 0u;

    aurora_telemetry_emit(
        gate->telemetry,
        now_ns,
        AURORA_EVENT_FALLBACK,
        -1,
        0u,
        0u,
        reason != NULL ? reason : "PSL disabled");
    aurora_ledger_append(
        gate->ledger,
        now_ns,
        AURORA_LEDGER_FALLBACK,
        AURORA_DECISION_DISABLE,
        -1,
        reason != NULL ? reason : "PSL disabled");
}

void aurora_psl_expire_old(struct aurora_psl_gate *gate, aurora_time_t now_ns)
{
    if (gate == NULL) {
        return;
    }

    size_t write = 0u;
    for (size_t read = 0u; read < gate->count; ++read) {
        if (gate->hints[read].valid_until_ns > now_ns) {
            gate->hints[write++] = gate->hints[read];
        }
    }
    gate->count = write;
}

