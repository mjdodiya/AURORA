#include "aurora/daemon.h"

#include <stdio.h>

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

static void wait_ns(uint64_t duration_ns)
{
    aurora_time_t start = aurora_monotonic_time_ns();
    while (aurora_monotonic_time_ns() - start < duration_ns) {
    }
}

static void print_feedback(const char *label, const struct aurora_client *client)
{
    const struct aurora_policy_state *state = aurora_client_last_policy_state(client);
    printf(
        "%s pid=%u decision=%s priority=%d risk=%u violations=%u\n  %s\n",
        label,
        state->pid,
        aurora_policy_decision_name((enum aurora_policy_decision_code)state->decision_code),
        state->effective_priority,
        state->risk_score,
        state->latency_violation_count,
        state->explanation);
}

static void terminal_editor_workflow(struct aurora_daemon *daemon)
{
    struct aurora_client editor;
    aurora_client_init(&editor, 5101u, "terminal-editor");
    attach(&editor, daemon);

    (void)aurora_register_process(&editor);
    (void)aurora_set_process_class(&editor, AURORA_SRC_INTERACTIVE_UI);
    (void)aurora_set_latency_budget(&editor, 1000u);
    (void)aurora_task_begin(&editor, 1u, "keystroke-render");
    wait_ns(3000000u);
    (void)aurora_task_end(&editor, 1u);
    print_feedback("terminal/editor", &editor);
}

static void ai_inference_runtime(struct aurora_daemon *daemon)
{
    struct aurora_client inference;
    aurora_client_init(&inference, 5201u, "local-llm-runtime");
    attach(&inference, daemon);

    (void)aurora_register_process(&inference);
    (void)aurora_set_process_class(&inference, AURORA_SRC_AI_INFERENCE);
    (void)aurora_set_latency_budget(&inference, 40000u);
    (void)aurora_emit_telemetry(
        &inference,
        0xA110u,
        AURORA_SYSCALL_MPROTECT,
        AURORA_SYSCALL_FLAG_WRITE_EXEC | AURORA_SYSCALL_FLAG_UNSIGNED_CODE,
        "jit-cache-permission-change");
    (void)aurora_query_policy_state(&inference, NULL);
    print_feedback("ai inference", &inference);
}

static void browser_like_workload(struct aurora_daemon *daemon)
{
    struct aurora_client browser;
    aurora_client_init(&browser, 5301u, "browser-tab");
    attach(&browser, daemon);

    (void)aurora_register_process(&browser);
    (void)aurora_set_process_class(&browser, AURORA_SRC_INTERACTIVE_UI);
    (void)aurora_set_latency_budget(&browser, 16000u);
    (void)aurora_task_begin(&browser, 11u, "layout-frame");
    (void)aurora_emit_telemetry(&browser, 0u, 42u, 7u, "visible-tab-frame");
    (void)aurora_task_end(&browser, 11u);
    (void)aurora_query_policy_state(&browser, NULL);
    print_feedback("browser-like", &browser);
}

static void realtime_audio_pipeline(struct aurora_daemon *daemon)
{
    struct aurora_client audio;
    aurora_client_init(&audio, 5401u, "audio-engine");
    attach(&audio, daemon);

    (void)aurora_register_process(&audio);
    (void)aurora_set_process_class(&audio, AURORA_SRC_REALTIME_AUDIO);
    (void)aurora_set_latency_budget(&audio, 3000u);
    (void)aurora_task_begin(&audio, 21u, "mix-callback");
    (void)aurora_task_end(&audio, 21u);
    (void)aurora_query_policy_state(&audio, NULL);
    print_feedback("realtime audio", &audio);
}

int main(void)
{
    struct aurora_daemon daemon;
    aurora_daemon_init(&daemon);

    puts("AURORA AIL sample application integrations");
    terminal_editor_workflow(&daemon);
    ai_inference_runtime(&daemon);
    browser_like_workload(&daemon);
    realtime_audio_pipeline(&daemon);

    puts("");
    aurora_daemon_print_replay(&daemon);
    return 0;
}
