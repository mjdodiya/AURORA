#ifndef AURORA_SCHEDULER_H
#define AURORA_SCHEDULER_H

#include "aurora/ledger.h"
#include "aurora/psl.h"
#include "aurora/src.h"
#include "aurora/telemetry.h"
#include "aurora/types.h"

struct aurora_task {
    int pid;
    char name[AURORA_NAME_MAX];
    enum aurora_src_class src_class;
    struct aurora_src_contract contract;
    int base_priority;
    int effective_priority;
    uint64_t cpu_quota_ns;
    uint64_t cpu_used_ns;
    uint32_t risk_score;
    bool runnable;
    aurora_time_t last_run_ns;
};

struct aurora_scheduler {
    struct aurora_task tasks[AURORA_MAX_TASKS];
    size_t task_count;
    aurora_time_t now_ns;
    struct aurora_psl_gate *psl_gate;
    struct aurora_telemetry_ring *telemetry;
    struct aurora_policy_ledger *ledger;
};

void aurora_sched_init(
    struct aurora_scheduler *sched,
    struct aurora_psl_gate *psl_gate,
    struct aurora_telemetry_ring *telemetry,
    struct aurora_policy_ledger *ledger);
enum aurora_status aurora_sched_add_task(
    struct aurora_scheduler *sched,
    int pid,
    const char *name,
    enum aurora_src_class src_class,
    const struct aurora_src_contract *contract,
    int base_priority,
    uint64_t cpu_quota_ns);
enum aurora_status aurora_sched_set_src(
    struct aurora_scheduler *sched,
    int pid,
    enum aurora_src_class src_class,
    const struct aurora_src_contract *contract,
    aurora_caps_t caller_caps);
struct aurora_task *aurora_sched_find_task(struct aurora_scheduler *sched, int pid);
const struct aurora_task *aurora_sched_find_task_const(
    const struct aurora_scheduler *sched,
    int pid);
struct aurora_task *aurora_sched_pick_next(struct aurora_scheduler *sched);
struct aurora_task *aurora_sched_tick(struct aurora_scheduler *sched, uint64_t quantum_ns);
void aurora_sched_note_risk(struct aurora_scheduler *sched, int pid, uint32_t risk_score);

#endif

