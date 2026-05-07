#include "aurora/scheduler.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static int task_score(const struct aurora_scheduler *sched, const struct aurora_task *task)
{
    int score = task->base_priority;
    score += aurora_src_class_base_boost(task->src_class);
    score += aurora_psl_priority_boost(sched->psl_gate, task->pid, sched->now_ns);

    if (task->contract.energy_budget_nj > 0u && task->cpu_used_ns > task->cpu_quota_ns / 2u) {
        score -= 5;
    }

    return score;
}

void aurora_sched_init(
    struct aurora_scheduler *sched,
    struct aurora_psl_gate *psl_gate,
    struct aurora_telemetry_ring *telemetry,
    struct aurora_policy_ledger *ledger)
{
    if (sched == NULL) {
        return;
    }

    memset(sched, 0, sizeof(*sched));
    sched->psl_gate = psl_gate;
    sched->telemetry = telemetry;
    sched->ledger = ledger;
}

enum aurora_status aurora_sched_add_task(
    struct aurora_scheduler *sched,
    int pid,
    const char *name,
    enum aurora_src_class src_class,
    const struct aurora_src_contract *contract,
    int base_priority,
    uint64_t cpu_quota_ns)
{
    if (sched == NULL || pid <= 0 || name == NULL || contract == NULL) {
        return AURORA_ERR_INVALID;
    }

    if (!aurora_src_contract_valid(contract)) {
        return AURORA_ERR_RANGE;
    }

    if (sched->task_count >= AURORA_MAX_TASKS) {
        return AURORA_ERR_FULL;
    }

    if (aurora_sched_find_task(sched, pid) != NULL) {
        return AURORA_ERR_INVALID;
    }

    struct aurora_task *task = &sched->tasks[sched->task_count++];
    memset(task, 0, sizeof(*task));
    task->pid = pid;
    (void)snprintf(task->name, sizeof(task->name), "%s", name);
    task->src_class = src_class;
    task->contract = *contract;
    task->base_priority = base_priority;
    task->effective_priority = base_priority + aurora_src_class_base_boost(src_class);
    task->cpu_quota_ns = cpu_quota_ns;
    task->runnable = true;

    aurora_telemetry_emit(
        sched->telemetry,
        sched->now_ns,
        AURORA_EVENT_TASK_ADDED,
        pid,
        (uint32_t)src_class,
        (uint32_t)base_priority,
        "task admitted");

    return AURORA_OK;
}

enum aurora_status aurora_sched_set_src(
    struct aurora_scheduler *sched,
    int pid,
    enum aurora_src_class src_class,
    const struct aurora_src_contract *contract,
    aurora_caps_t caller_caps)
{
    if (sched == NULL || contract == NULL || src_class >= AURORA_SRC_CLASS_COUNT) {
        return AURORA_ERR_INVALID;
    }

    if (!aurora_has_cap(caller_caps, AURORA_CAP_SRC_ADMIN)) {
        return AURORA_ERR_DENIED;
    }

    if (!aurora_src_contract_valid(contract)) {
        return AURORA_ERR_RANGE;
    }

    struct aurora_task *task = aurora_sched_find_task(sched, pid);
    if (task == NULL) {
        return AURORA_ERR_NOT_FOUND;
    }

    task->src_class = src_class;
    task->contract = *contract;
    task->effective_priority = task->base_priority + aurora_src_class_base_boost(src_class);

    aurora_telemetry_emit(
        sched->telemetry,
        sched->now_ns,
        AURORA_EVENT_SRC_SET,
        pid,
        (uint32_t)src_class,
        0u,
        "SRC contract updated");
    aurora_ledger_append(
        sched->ledger,
        sched->now_ns,
        AURORA_LEDGER_SRC,
        AURORA_DECISION_ACCEPT,
        pid,
        "SRC contract updated by privileged caller");

    return AURORA_OK;
}

struct aurora_task *aurora_sched_find_task(struct aurora_scheduler *sched, int pid)
{
    if (sched == NULL) {
        return NULL;
    }

    for (size_t i = 0u; i < sched->task_count; ++i) {
        if (sched->tasks[i].pid == pid) {
            return &sched->tasks[i];
        }
    }

    return NULL;
}

const struct aurora_task *aurora_sched_find_task_const(
    const struct aurora_scheduler *sched,
    int pid)
{
    if (sched == NULL) {
        return NULL;
    }

    for (size_t i = 0u; i < sched->task_count; ++i) {
        if (sched->tasks[i].pid == pid) {
            return &sched->tasks[i];
        }
    }

    return NULL;
}

struct aurora_task *aurora_sched_pick_next(struct aurora_scheduler *sched)
{
    if (sched == NULL) {
        return NULL;
    }

    aurora_psl_expire_old(sched->psl_gate, sched->now_ns);

    struct aurora_task *best = NULL;
    int best_score = INT_MIN;
    for (size_t i = 0u; i < sched->task_count; ++i) {
        struct aurora_task *task = &sched->tasks[i];
        if (!task->runnable || task->cpu_used_ns >= task->cpu_quota_ns) {
            continue;
        }

        int score = task_score(sched, task);
        task->effective_priority = score;

        if (best == NULL || score > best_score ||
            (score == best_score && task->last_run_ns < best->last_run_ns) ||
            (score == best_score && task->last_run_ns == best->last_run_ns &&
             task->pid < best->pid)) {
            best = task;
            best_score = score;
        }
    }

    return best;
}

struct aurora_task *aurora_sched_tick(struct aurora_scheduler *sched, uint64_t quantum_ns)
{
    if (sched == NULL || quantum_ns == 0u) {
        return NULL;
    }

    struct aurora_task *task = aurora_sched_pick_next(sched);
    if (task == NULL) {
        sched->now_ns += quantum_ns;
        return NULL;
    }

    uint64_t remaining = task->cpu_quota_ns - task->cpu_used_ns;
    uint64_t charged = quantum_ns < remaining ? quantum_ns : remaining;
    task->cpu_used_ns += charged;
    task->last_run_ns = sched->now_ns;

    aurora_telemetry_emit(
        sched->telemetry,
        sched->now_ns,
        AURORA_EVENT_SCHED_PICK,
        task->pid,
        (uint32_t)task->effective_priority,
        (uint32_t)(charged / 1000u),
        "deterministic scheduler pick");
    aurora_ledger_append(
        sched->ledger,
        sched->now_ns,
        AURORA_LEDGER_SCHED,
        AURORA_DECISION_ACCEPT,
        task->pid,
        "task selected by deterministic scheduler");

    sched->now_ns += charged;
    return task;
}

void aurora_sched_note_risk(struct aurora_scheduler *sched, int pid, uint32_t risk_score)
{
    struct aurora_task *task = aurora_sched_find_task(sched, pid);
    if (task == NULL) {
        return;
    }

    task->risk_score = risk_score;
}

