#include "aurora/aso.h"
#include "aurora/memory.h"
#include "aurora/scheduler.h"

#include <assert.h>
#include <stdio.h>

static struct aurora_src_contract test_contract(uint32_t latency_us)
{
    struct aurora_src_contract c = {
        .latency_us_target = latency_us,
        .cpu_burst_us_max = latency_us,
        .memory_pressure_policy = 0u,
        .accelerator_mask = 0u,
        .privacy_flags = AURORA_PRIVACY_LOCAL_ONLY,
        .energy_budget_nj = 0u,
    };
    return c;
}

static void setup(
    struct aurora_telemetry_ring *telemetry,
    struct aurora_policy_ledger *ledger,
    struct aurora_psl_gate *psl,
    struct aurora_scheduler *sched,
    struct aurora_aso *aso)
{
    aurora_telemetry_init(telemetry);
    aurora_ledger_init(ledger);
    aurora_psl_gate_init(psl, telemetry, ledger);
    aurora_sched_init(sched, psl, telemetry, ledger);
    aurora_aso_init(aso, sched, telemetry, ledger);
}

static void test_src_drives_deterministic_pick(void)
{
    struct aurora_telemetry_ring telemetry;
    struct aurora_policy_ledger ledger;
    struct aurora_psl_gate psl;
    struct aurora_scheduler sched;
    struct aurora_aso aso;
    setup(&telemetry, &ledger, &psl, &sched, &aso);

    struct aurora_src_contract c = test_contract(10000u);
    assert(
        aurora_sched_add_task(
            &sched, 1, "background", AURORA_SRC_BACKGROUND_SYNC, &c, 50, 1000000u) ==
        AURORA_OK);
    assert(
        aurora_sched_add_task(
            &sched, 2, "audio", AURORA_SRC_REALTIME_AUDIO, &c, 20, 1000000u) ==
        AURORA_OK);

    struct aurora_task *picked = aurora_sched_pick_next(&sched);
    assert(picked != NULL);
    assert(picked->pid == 2);
}

static void test_psl_hint_is_bounded_and_capability_checked(void)
{
    struct aurora_telemetry_ring telemetry;
    struct aurora_policy_ledger ledger;
    struct aurora_psl_gate psl;
    struct aurora_scheduler sched;
    struct aurora_aso aso;
    setup(&telemetry, &ledger, &psl, &sched, &aso);

    struct aurora_psl_hint hint = {
        .target_pid = 7,
        .issued_at_ns = 0u,
        .valid_until_ns = 1000000u,
        .confidence_ppm = 900000u,
        .requested_cpu_capacity = 250u,
        .prewarm_pages = 999999u,
    };

    assert(aurora_psl_submit_hint(&psl, &hint, 0u, 0u) == AURORA_ERR_DENIED);
    assert(aurora_psl_submit_hint(&psl, &hint, AURORA_CAP_PSL_SUBMIT, 0u) == AURORA_OK);
    assert(psl.count == 1u);
    assert(psl.hints[0].requested_cpu_capacity == psl.max_cpu_capacity);
    assert(psl.hints[0].prewarm_pages == psl.max_prewarm_pages);
    assert(aurora_psl_priority_boost(&psl, 7, 500000u) > 0);
    assert(aurora_psl_priority_boost(&psl, 7, 1000000u) == 0);
}

static void test_aso_scores_without_enforcement(void)
{
    struct aurora_telemetry_ring telemetry;
    struct aurora_policy_ledger ledger;
    struct aurora_psl_gate psl;
    struct aurora_scheduler sched;
    struct aurora_aso aso;
    setup(&telemetry, &ledger, &psl, &sched, &aso);

    struct aurora_src_contract c = test_contract(10000u);
    assert(
        aurora_sched_add_task(&sched, 42, "model", AURORA_SRC_AI_INFERENCE, &c, 50, 1000000u) ==
        AURORA_OK);

    struct aurora_syscall_sample sample = {
        .pid = 42,
        .syscall_id = AURORA_SYSCALL_MPROTECT,
        .flags = AURORA_SYSCALL_FLAG_WRITE_EXEC | AURORA_SYSCALL_FLAG_DMA_REQUEST |
                 AURORA_SYSCALL_FLAG_UNSIGNED_CODE,
        .arg_shape = 0xBEEFu,
        .timestamp_ns = 123u,
    };

    assert(aurora_aso_observe(&aso, &sample, 0u) == AURORA_ERR_DENIED);
    assert(aurora_aso_observe(&aso, &sample, AURORA_CAP_ASO_SUBMIT) == AURORA_OK);

    const struct aurora_process_profile *profile = aurora_aso_find_profile(&aso, 42);
    assert(profile != NULL);
    assert(profile->last_score >= aso.alert_threshold);

    const struct aurora_task *task = aurora_sched_find_task_const(&sched, 42);
    assert(task != NULL);
    assert(task->runnable);
    assert(task->risk_score == profile->last_score);
}

static void test_fallback_disables_hints(void)
{
    struct aurora_telemetry_ring telemetry;
    struct aurora_policy_ledger ledger;
    struct aurora_psl_gate psl;
    struct aurora_scheduler sched;
    struct aurora_aso aso;
    setup(&telemetry, &ledger, &psl, &sched, &aso);

    struct aurora_psl_hint hint = {
        .target_pid = 9,
        .issued_at_ns = 0u,
        .valid_until_ns = 1000000u,
        .confidence_ppm = 900000u,
        .requested_cpu_capacity = 100u,
        .prewarm_pages = 1u,
    };

    assert(aurora_psl_submit_hint(&psl, &hint, AURORA_CAP_PSL_SUBMIT, 0u) == AURORA_OK);
    assert(aurora_psl_priority_boost(&psl, 9, 1u) > 0);
    aurora_psl_disable(&psl, 2u, "unit test fallback");
    assert(!psl.enabled);
    assert(psl.count == 0u);
    assert(aurora_psl_priority_boost(&psl, 9, 3u) == 0);
}

static void test_semantic_memory_policy(void)
{
    struct aurora_telemetry_ring telemetry;
    struct aurora_policy_ledger ledger;
    struct aurora_psl_gate psl;
    struct aurora_scheduler sched;
    struct aurora_aso aso;
    struct aurora_memory_manager memory;
    setup(&telemetry, &ledger, &psl, &sched, &aso);
    aurora_memory_init(&memory, &sched, &telemetry, &ledger);

    struct aurora_src_contract c = test_contract(10000u);
    assert(
        aurora_sched_add_task(&sched, 55, "weights", AURORA_SRC_AI_INFERENCE, &c, 50, 1000000u) ==
        AURORA_OK);

    assert(
        aurora_memory_map_semantic(
            &memory,
            55,
            0x10000000u,
            4096u,
            AURORA_MEM_MODEL_WEIGHTS_READONLY,
            AURORA_MEM_FLAG_READ | AURORA_MEM_FLAG_WRITE) == AURORA_ERR_DENIED);

    assert(
        aurora_memory_map_semantic(
            &memory,
            55,
            0x10000000u,
            4096u,
            AURORA_MEM_MODEL_WEIGHTS_READONLY,
            AURORA_MEM_FLAG_READ | AURORA_MEM_FLAG_PINNED) == AURORA_OK);

    assert(aurora_memory_find_region(&memory, 55, 0x10000000u) != NULL);
    assert(aurora_memory_count_by_role(&memory, AURORA_MEM_MODEL_WEIGHTS_READONLY) == 1u);

    assert(
        aurora_memory_map_semantic(
            &memory,
            55,
            0x10000800u,
            4096u,
            AURORA_MEM_RECOMPUTABLE_CACHE,
            AURORA_MEM_FLAG_READ | AURORA_MEM_FLAG_RECLAIMABLE) == AURORA_ERR_INVALID);
}

int main(void)
{
    test_src_drives_deterministic_pick();
    test_psl_hint_is_bounded_and_capability_checked();
    test_aso_scores_without_enforcement();
    test_fallback_disables_hints();
    test_semantic_memory_policy();

    puts("aurora_tests: ok");
    return 0;
}
