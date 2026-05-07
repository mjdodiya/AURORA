#include "aurora/aso.h"
#include "aurora/memory.h"
#include "aurora/scheduler.h"

#include <stdio.h>

static struct aurora_src_contract contract(
    uint32_t latency_us,
    uint32_t burst_us,
    uint32_t privacy_flags,
    uint64_t energy_budget_nj)
{
    struct aurora_src_contract c = {
        .latency_us_target = latency_us,
        .cpu_burst_us_max = burst_us,
        .memory_pressure_policy = 0u,
        .accelerator_mask = 0u,
        .privacy_flags = privacy_flags,
        .energy_budget_nj = energy_budget_nj,
    };
    return c;
}

static void print_tasks(const struct aurora_scheduler *sched)
{
    puts("\nTASKS");
    for (size_t i = 0u; i < sched->task_count; ++i) {
        const struct aurora_task *task = &sched->tasks[i];
        printf(
            "  pid=%d name=%s class=%s base=%d effective=%d used=%llu/%llu risk=%u\n",
            task->pid,
            task->name,
            aurora_src_class_name(task->src_class),
            task->base_priority,
            task->effective_priority,
            (unsigned long long)task->cpu_used_ns,
            (unsigned long long)task->cpu_quota_ns,
            task->risk_score);
    }
}

static void print_telemetry(const struct aurora_telemetry_ring *telemetry)
{
    struct aurora_event events[AURORA_TELEMETRY_CAPACITY];
    size_t count = aurora_telemetry_snapshot(telemetry, events, AURORA_TELEMETRY_CAPACITY);

    puts("\nTELEMETRY");
    for (size_t i = 0u; i < count; ++i) {
        printf(
            "  t=%llu type=%s pid=%d arg0=%u arg1=%u msg=%s\n",
            (unsigned long long)events[i].timestamp_ns,
            aurora_event_type_name(events[i].type),
            events[i].pid,
            events[i].arg0,
            events[i].arg1,
            events[i].message);
    }
    printf("  dropped=%llu\n", (unsigned long long)telemetry->dropped);
}

static void print_ledger(const struct aurora_policy_ledger *ledger)
{
    puts("\nPOLICY LEDGER");
    for (size_t i = 0u; i < ledger->count; ++i) {
        const struct aurora_ledger_record *record = &ledger->records[i];
        printf(
            "  #%llu t=%llu category=%s decision=%s pid=%d reason=%s\n",
            (unsigned long long)record->seq,
            (unsigned long long)record->timestamp_ns,
            aurora_ledger_category_name(record->category),
            aurora_ledger_decision_name(record->decision),
            record->pid,
            record->reason);
    }
    printf("  dropped=%llu\n", (unsigned long long)ledger->dropped);
}

int main(void)
{
    struct aurora_telemetry_ring telemetry;
    struct aurora_policy_ledger ledger;
    struct aurora_psl_gate psl;
    struct aurora_scheduler sched;
    struct aurora_aso aso;
    struct aurora_memory_manager memory;

    aurora_telemetry_init(&telemetry);
    aurora_ledger_init(&ledger);
    aurora_psl_gate_init(&psl, &telemetry, &ledger);
    aurora_sched_init(&sched, &psl, &telemetry, &ledger);
    aurora_aso_init(&aso, &sched, &telemetry, &ledger);
    aurora_memory_init(&memory, &sched, &telemetry, &ledger);

    struct aurora_src_contract ui = contract(8000u, 12000u, AURORA_PRIVACY_LOCAL_ONLY, 0u);
    struct aurora_src_contract model = contract(60000u, 150000u, AURORA_PRIVACY_LOCAL_ONLY, 0u);
    struct aurora_src_contract audio = contract(3000u, 5000u, AURORA_PRIVACY_LOCAL_ONLY, 0u);
    struct aurora_src_contract sync = contract(250000u, 50000u, 0u, 250000000u);

    (void)aurora_sched_add_task(
        &sched, 101, "compositor", AURORA_SRC_INTERACTIVE_UI, &ui, 45, 5000000u);
    (void)aurora_sched_add_task(
        &sched, 202, "model-runner", AURORA_SRC_AI_INFERENCE, &model, 50, 5000000u);
    (void)aurora_sched_add_task(
        &sched, 303, "audio-engine", AURORA_SRC_REALTIME_AUDIO, &audio, 40, 5000000u);
    (void)aurora_sched_add_task(
        &sched, 404, "updater", AURORA_SRC_BACKGROUND_SYNC, &sync, 30, 5000000u);

    (void)aurora_memory_map_semantic(
        &memory,
        202,
        0x10000000u,
        128u * 1024u * 1024u,
        AURORA_MEM_MODEL_WEIGHTS_READONLY,
        AURORA_MEM_FLAG_READ | AURORA_MEM_FLAG_PINNED);
    (void)aurora_memory_map_semantic(
        &memory,
        101,
        0x20000000u,
        4u * 1024u * 1024u,
        AURORA_MEM_SECRET_TRANSIENT,
        AURORA_MEM_FLAG_READ | AURORA_MEM_FLAG_WRITE | AURORA_MEM_FLAG_NOSWAP);

    struct aurora_psl_hint ui_hint = {
        .target_pid = 101,
        .issued_at_ns = sched.now_ns,
        .valid_until_ns = 4000000u,
        .confidence_ppm = 800000u,
        .requested_cpu_capacity = 80u,
        .prewarm_pages = 512u,
        .preferred_numa_node = 0u,
        .thermal_sensitivity = 20u,
    };
    (void)aurora_psl_submit_hint(&psl, &ui_hint, AURORA_CAP_PSL_SUBMIT, sched.now_ns);

    struct aurora_psl_hint rejected_hint = {
        .target_pid = 404,
        .issued_at_ns = sched.now_ns,
        .valid_until_ns = 2000000u,
        .confidence_ppm = 900000u,
        .requested_cpu_capacity = 100u,
        .prewarm_pages = 10000u,
    };
    (void)aurora_psl_submit_hint(&psl, &rejected_hint, 0u, sched.now_ns);

    struct aurora_syscall_sample suspicious = {
        .pid = 202,
        .syscall_id = AURORA_SYSCALL_MPROTECT,
        .flags = AURORA_SYSCALL_FLAG_WRITE_EXEC | AURORA_SYSCALL_FLAG_DMA_REQUEST |
                 AURORA_SYSCALL_FLAG_UNSIGNED_CODE,
        .arg_shape = 0xA110u,
        .timestamp_ns = sched.now_ns,
    };
    (void)aurora_aso_observe(&aso, &suspicious, AURORA_CAP_ASO_SUBMIT);

    puts("AURORA-KERNEL-v0.1 deterministic advisory prototype");
    puts("Running eight scheduler quanta with PSL and ASO in advisory mode.");

    for (size_t i = 0u; i < 8u; ++i) {
        struct aurora_task *task = aurora_sched_tick(&sched, 1000000u);
        if (task != NULL) {
            printf(
                "tick=%zu picked pid=%d class=%s effective=%d\n",
                i,
                task->pid,
                aurora_src_class_name(task->src_class),
                task->effective_priority);
        }
    }

    print_tasks(&sched);
    print_telemetry(&telemetry);
    print_ledger(&ledger);

    aurora_psl_disable(&psl, sched.now_ns, "simulated advisory-plane failure");
    struct aurora_task *fallback_pick = aurora_sched_tick(&sched, 1000000u);
    printf(
        "\nFALLBACK PICK pid=%d effective=%d psl_enabled=%s\n",
        fallback_pick != NULL ? fallback_pick->pid : -1,
        fallback_pick != NULL ? fallback_pick->effective_priority : 0,
        psl.enabled ? "true" : "false");

    return 0;
}
