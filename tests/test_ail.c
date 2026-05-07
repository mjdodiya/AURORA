#include "aurora/daemon.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static enum aurora_status daemon_sink(
    const struct aurora_ail_packet *packet,
    void *user,
    struct aurora_policy_state *state)
{
    return aurora_daemon_ingest_packet((struct aurora_daemon *)user, packet, state);
}

static void attach(struct aurora_client *client, struct aurora_daemon *daemon)
{
    aurora_client_set_sink(client, daemon_sink, daemon);
}

static void test_sdk_daemon_policy_feedback(void)
{
    struct aurora_daemon daemon;
    struct aurora_client client;
    struct aurora_policy_state state;

    aurora_daemon_init(&daemon);
    aurora_client_init(&client, 1001u, "editor");
    attach(&client, &daemon);

    assert(aurora_register_process(&client) == AURORA_OK);
    assert(aurora_set_process_class(&client, AURORA_SRC_INTERACTIVE_UI) == AURORA_OK);
    assert(aurora_set_latency_budget(&client, 1000u) == AURORA_OK);

    assert(aurora_task_begin(&client, 77u, "render-keystroke") == AURORA_OK);
    aurora_time_t start_ns = 0u;
    for (size_t i = 0u; i < AURORA_DAEMON_MAX_TASK_MARKERS; ++i) {
        if (daemon.tasks[i].active && daemon.tasks[i].pid == 1001u &&
            daemon.tasks[i].task_id == 77u) {
            start_ns = daemon.tasks[i].start_ns;
            break;
        }
    }
    assert(start_ns != 0u);

    struct aurora_ail_packet end;
    aurora_ail_packet_init(&end, AURORA_AIL_MSG_TASK_END, 1001u, 0u, 5u, "editor");
    end.task_id = 77u;
    end.timestamp_ns = start_ns + 3000000u;
    aurora_ail_packet_seal(&end);
    assert(aurora_daemon_ingest_packet(&daemon, &end, &state) == AURORA_OK);
    client.seq = 5u;

    assert(state.latency_violation_count == 1u);
    assert(state.decision_code == AURORA_POLICY_PRIORITY_BOOST_HINT);
    assert(strstr(state.explanation, "latency_budget_exceeded") != NULL);
    assert(daemon.psl_gate.count == 1u);

    assert(aurora_query_policy_state(&client, &state) == AURORA_OK);
    assert(state.pid == 1001u);
    assert(state.src_class == AURORA_SRC_INTERACTIVE_UI);
    assert(daemon.replay_count >= 5u);
}

static void test_malformed_packet_rejected_and_replayable(void)
{
    struct aurora_daemon daemon;
    struct aurora_policy_state state;
    struct aurora_ail_packet packet;

    aurora_daemon_init(&daemon);
    aurora_ail_packet_init(&packet, AURORA_AIL_MSG_HELLO, 222u, 0u, 1u, "bad-client");
    aurora_ail_packet_seal(&packet);
    packet.checksum ^= 0x55u;

    assert(aurora_daemon_ingest_packet(&daemon, &packet, &state) == AURORA_ERR_INVALID);
    assert(state.decision_code == AURORA_POLICY_REJECTED_INVALID);
    assert(daemon.replay_count == 1u);
    assert(daemon.replay[0].status == AURORA_ERR_INVALID);
}

static void test_rate_limit_isolated_to_client(void)
{
    struct aurora_daemon daemon;
    struct aurora_client noisy;
    struct aurora_client quiet;
    struct aurora_policy_state state;

    aurora_daemon_init(&daemon);
    aurora_client_init(&noisy, 3001u, "noisy");
    aurora_client_init(&quiet, 3002u, "quiet");
    attach(&noisy, &daemon);
    attach(&quiet, &daemon);

    assert(aurora_register_process(&noisy) == AURORA_OK);

    enum aurora_status last = AURORA_OK;
    for (size_t i = 0u; i < AURORA_DAEMON_RATE_MAX_EVENTS + 2u; ++i) {
        last = aurora_emit_telemetry(&noisy, 0u, 900u, (uint32_t)i, "flood");
    }
    assert(last == AURORA_ERR_DENIED);

    assert(aurora_register_process(&quiet) == AURORA_OK);
    assert(aurora_set_process_class(&quiet, AURORA_SRC_BACKGROUND_SYNC) == AURORA_OK);
    assert(aurora_query_policy_state(&quiet, &state) == AURORA_OK);
    assert(state.decision_code == AURORA_POLICY_QUERY_SERVED);

    const struct aurora_daemon_client *noisy_state = aurora_daemon_find_client(&daemon, 3001u);
    assert(noisy_state != NULL);
    assert(noisy_state->rejected_count > 0u);
}

static void test_app_telemetry_feeds_aso_without_enforcement(void)
{
    struct aurora_daemon daemon;
    struct aurora_client client;
    struct aurora_policy_state state;

    aurora_daemon_init(&daemon);
    aurora_client_init(&client, 4001u, "inference");
    attach(&client, &daemon);

    assert(aurora_register_process(&client) == AURORA_OK);
    assert(aurora_set_process_class(&client, AURORA_SRC_AI_INFERENCE) == AURORA_OK);
    assert(
        aurora_emit_telemetry(
            &client,
            0xA110u,
            AURORA_SYSCALL_MPROTECT,
            AURORA_SYSCALL_FLAG_WRITE_EXEC | AURORA_SYSCALL_FLAG_DMA_REQUEST |
                AURORA_SYSCALL_FLAG_UNSIGNED_CODE,
            "jit-page-transition") == AURORA_OK);

    assert(aurora_query_policy_state(&client, &state) == AURORA_OK);
    assert(state.risk_score >= daemon.aso.alert_threshold);

    const struct aurora_task *task = aurora_sched_find_task_const(&daemon.scheduler, 4001);
    assert(task != NULL);
    assert(task->runnable);
}

int main(void)
{
    test_sdk_daemon_policy_feedback();
    test_malformed_packet_rejected_and_replayable();
    test_rate_limit_isolated_to_client();
    test_app_telemetry_feeds_aso_without_enforcement();

    puts("aurora_ail_tests: ok");
    return 0;
}
