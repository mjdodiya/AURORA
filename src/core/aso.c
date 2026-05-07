#include "aurora/aso.h"

#include <stdio.h>
#include <string.h>

static struct aurora_process_profile *profile_for(struct aurora_aso *aso, int pid)
{
    for (size_t i = 0u; i < aso->profile_count; ++i) {
        if (aso->profiles[i].pid == pid) {
            return &aso->profiles[i];
        }
    }

    if (aso->profile_count >= AURORA_MAX_TASKS) {
        return NULL;
    }

    struct aurora_process_profile *profile = &aso->profiles[aso->profile_count++];
    memset(profile, 0, sizeof(*profile));
    profile->pid = pid;
    return profile;
}

static uint32_t fingerprint_update(uint32_t current, const struct aurora_syscall_sample *sample)
{
    uint32_t x = current == 0u ? 2166136261u : current;
    x ^= (uint32_t)sample->syscall_id;
    x *= 16777619u;
    x ^= sample->flags;
    x *= 16777619u;
    x ^= sample->arg_shape;
    x *= 16777619u;
    return x;
}

static uint32_t score_sample(const struct aurora_syscall_sample *sample)
{
    uint32_t score = 0u;

    if ((sample->flags & AURORA_SYSCALL_FLAG_PRIVILEGE_DELTA) != 0u) {
        score += 35u;
    }
    if ((sample->flags & AURORA_SYSCALL_FLAG_WRITE_EXEC) != 0u) {
        score += 30u;
    }
    if ((sample->flags & AURORA_SYSCALL_FLAG_DMA_REQUEST) != 0u) {
        score += 20u;
    }
    if ((sample->flags & AURORA_SYSCALL_FLAG_CROSS_DOMAIN_IPC) != 0u) {
        score += 10u;
    }
    if ((sample->flags & AURORA_SYSCALL_FLAG_UNSIGNED_CODE) != 0u) {
        score += 25u;
    }
    if (sample->syscall_id == AURORA_SYSCALL_SETUID) {
        score += 20u;
    }
    if (sample->syscall_id == AURORA_SYSCALL_MPROTECT &&
        (sample->flags & AURORA_SYSCALL_FLAG_WRITE_EXEC) != 0u) {
        score += 15u;
    }

    return score > 100u ? 100u : score;
}

void aurora_aso_init(
    struct aurora_aso *aso,
    struct aurora_scheduler *scheduler,
    struct aurora_telemetry_ring *telemetry,
    struct aurora_policy_ledger *ledger)
{
    if (aso == NULL) {
        return;
    }

    memset(aso, 0, sizeof(*aso));
    aso->alert_threshold = 70u;
    aso->scheduler = scheduler;
    aso->telemetry = telemetry;
    aso->ledger = ledger;
}

enum aurora_status aurora_aso_observe(
    struct aurora_aso *aso,
    const struct aurora_syscall_sample *sample,
    aurora_caps_t caller_caps)
{
    if (aso == NULL || sample == NULL || sample->pid <= 0) {
        return AURORA_ERR_INVALID;
    }

    if (!aurora_has_cap(caller_caps, AURORA_CAP_ASO_SUBMIT)) {
        return AURORA_ERR_DENIED;
    }

    if (aurora_sched_find_task(aso->scheduler, sample->pid) == NULL) {
        return AURORA_ERR_NOT_FOUND;
    }

    struct aurora_process_profile *profile = profile_for(aso, sample->pid);
    if (profile == NULL) {
        return AURORA_ERR_FULL;
    }

    profile->syscall_count++;
    profile->last_score = score_sample(sample);
    profile->fingerprint = fingerprint_update(profile->fingerprint, sample);

    aurora_sched_note_risk(aso->scheduler, sample->pid, profile->last_score);
    aurora_telemetry_emit(
        aso->telemetry,
        sample->timestamp_ns,
        AURORA_EVENT_ASO_SCORE,
        sample->pid,
        profile->last_score,
        profile->fingerprint,
        "ASO observation scored");
    aurora_ledger_append(
        aso->ledger,
        sample->timestamp_ns,
        AURORA_LEDGER_ASO,
        AURORA_DECISION_OBSERVE,
        sample->pid,
        "ASO scored behavior without enforcement");

    if (profile->last_score >= aso->alert_threshold) {
        aurora_telemetry_emit(
            aso->telemetry,
            sample->timestamp_ns,
            AURORA_EVENT_ASO_ALERT,
            sample->pid,
            profile->last_score,
            profile->fingerprint,
            "ASO alert threshold crossed");
    }

    return AURORA_OK;
}

const struct aurora_process_profile *aurora_aso_find_profile(
    const struct aurora_aso *aso,
    int pid)
{
    if (aso == NULL) {
        return NULL;
    }

    for (size_t i = 0u; i < aso->profile_count; ++i) {
        if (aso->profiles[i].pid == pid) {
            return &aso->profiles[i];
        }
    }

    return NULL;
}

