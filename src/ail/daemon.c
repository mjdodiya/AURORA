#include "aurora/daemon.h"

#include <stdio.h>
#include <string.h>

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0u) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    (void)snprintf(dst, dst_size, "%s", src);
}

static struct aurora_src_contract default_contract(
    enum aurora_src_class src_class,
    uint32_t latency_budget_us)
{
    uint32_t latency = latency_budget_us != 0u ? latency_budget_us : 100000u;
    uint32_t burst = latency < 1000u ? 1000u : latency;

    struct aurora_src_contract contract = {
        .latency_us_target = latency,
        .cpu_burst_us_max = burst,
        .memory_pressure_policy = 0u,
        .accelerator_mask = src_class == AURORA_SRC_AI_INFERENCE ? 1u : 0u,
        .privacy_flags = AURORA_PRIVACY_LOCAL_ONLY,
        .energy_budget_nj = src_class == AURORA_SRC_LOW_POWER_TASK ? 100000000u : 0u,
    };
    return contract;
}

static void append_replay(
    struct aurora_daemon *daemon,
    const struct aurora_ail_packet *packet,
    enum aurora_status status,
    enum aurora_policy_decision_code decision,
    const char *explanation)
{
    if (daemon == NULL) {
        return;
    }

    if (daemon->replay_count >= AURORA_DAEMON_REPLAY_CAPACITY) {
        memmove(
            &daemon->replay[0],
            &daemon->replay[1],
            sizeof(daemon->replay[0]) * (AURORA_DAEMON_REPLAY_CAPACITY - 1u));
        daemon->replay_count = AURORA_DAEMON_REPLAY_CAPACITY - 1u;
        daemon->replay_dropped++;
    }

    struct aurora_ail_replay_record *record = &daemon->replay[daemon->replay_count++];
    memset(record, 0, sizeof(*record));
    record->seq = daemon->next_replay_seq++;
    record->timestamp_ns = packet != NULL ? packet->timestamp_ns : 0u;
    record->pid = packet != NULL ? packet->pid : 0u;
    record->packet_seq = packet != NULL ? (uint32_t)packet->seq : 0u;
    record->message_type = packet != NULL ? packet->type : 0u;
    record->status = status;
    record->packet_checksum = packet != NULL ? packet->checksum : 0u;
    record->decision_code = (uint32_t)decision;
    copy_text(record->explanation, sizeof(record->explanation), explanation);
}

static struct aurora_daemon_client *find_client_mut(struct aurora_daemon *daemon, uint32_t pid)
{
    if (daemon == NULL) {
        return NULL;
    }

    for (size_t i = 0u; i < AURORA_DAEMON_MAX_CLIENTS; ++i) {
        if (daemon->clients[i].active && daemon->clients[i].pid == pid) {
            return &daemon->clients[i];
        }
    }
    return NULL;
}

static struct aurora_daemon_client *ensure_client(
    struct aurora_daemon *daemon,
    const struct aurora_ail_packet *packet)
{
    struct aurora_daemon_client *client = find_client_mut(daemon, packet->pid);
    if (client != NULL) {
        return client;
    }

    for (size_t i = 0u; i < AURORA_DAEMON_MAX_CLIENTS; ++i) {
        if (!daemon->clients[i].active) {
            client = &daemon->clients[i];
            memset(client, 0, sizeof(*client));
            client->active = true;
            client->pid = packet->pid;
            client->client_id = daemon->next_client_id++;
            client->src_class = AURORA_SRC_LOW_POWER_TASK;
            client->contract = default_contract(client->src_class, 0u);
            client->rate_window_start_ns = packet->timestamp_ns;
            copy_text(client->app_name, sizeof(client->app_name), packet->app_name);
            return client;
        }
    }

    return NULL;
}

static bool rate_limited(struct aurora_daemon_client *client, aurora_time_t now_ns)
{
    if (client == NULL) {
        return true;
    }

    if (now_ns < client->rate_window_start_ns) {
        now_ns = client->rate_window_start_ns;
    }

    if (now_ns - client->rate_window_start_ns >= AURORA_DAEMON_RATE_WINDOW_NS) {
        client->rate_window_start_ns = now_ns;
        client->rate_count = 0u;
    }

    client->rate_count++;
    if (client->rate_count > AURORA_DAEMON_RATE_MAX_EVENTS) {
        client->rejected_count++;
        return true;
    }

    return false;
}

static void explain_state(
    struct aurora_daemon *daemon,
    struct aurora_daemon_client *client,
    enum aurora_policy_decision_code decision,
    const char *reason,
    struct aurora_policy_state *state)
{
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->pid = client != NULL ? client->pid : 0u;
    state->src_class = client != NULL ? (uint32_t)client->src_class : 0u;
    state->thermal_state = AURORA_THERMAL_ACCEPTABLE;
    state->decision_code = (uint32_t)decision;
    state->causal_seq = daemon != NULL ? daemon->next_replay_seq : 0u;
    state->latency_violation_count = client != NULL ? client->latency_violation_count : 0u;
    copy_text(state->reason, sizeof(state->reason), reason);

    if (daemon != NULL && client != NULL) {
        const struct aurora_task *task =
            aurora_sched_find_task_const(&daemon->scheduler, (int)client->pid);
        if (task != NULL) {
            state->effective_priority = task->effective_priority;
            state->risk_score = task->risk_score;
        }
    }

    (void)snprintf(
        state->explanation,
        sizeof(state->explanation),
        "task=%u reason=%s src=%s thermal_state=acceptable decision=%s",
        client != NULL ? client->pid : 0u,
        reason != NULL ? reason : "none",
        client != NULL ? aurora_src_class_name(client->src_class) : "UNKNOWN_SRC",
        aurora_policy_decision_name(decision));

    if (client != NULL) {
        client->last_state = *state;
    }
}

static enum aurora_status ensure_core_task(
    struct aurora_daemon *daemon,
    struct aurora_daemon_client *client)
{
    if (aurora_sched_find_task(&daemon->scheduler, (int)client->pid) == NULL) {
        return aurora_sched_add_task(
            &daemon->scheduler,
            (int)client->pid,
            client->app_name,
            client->src_class,
            &client->contract,
            50,
            10000000u);
    }

    return aurora_sched_set_src(
        &daemon->scheduler,
        (int)client->pid,
        client->src_class,
        &client->contract,
        AURORA_CAP_SRC_ADMIN);
}

static struct aurora_daemon_task_marker *find_task_marker(
    struct aurora_daemon *daemon,
    uint32_t pid,
    uint64_t task_id)
{
    for (size_t i = 0u; i < AURORA_DAEMON_MAX_TASK_MARKERS; ++i) {
        struct aurora_daemon_task_marker *task = &daemon->tasks[i];
        if (task->active && task->pid == pid && task->task_id == task_id) {
            return task;
        }
    }
    return NULL;
}

static enum aurora_status store_task_marker(
    struct aurora_daemon *daemon,
    struct aurora_daemon_client *client,
    const struct aurora_ail_packet *packet)
{
    struct aurora_daemon_task_marker *task =
        find_task_marker(daemon, packet->pid, packet->task_id);
    if (task == NULL) {
        for (size_t i = 0u; i < AURORA_DAEMON_MAX_TASK_MARKERS; ++i) {
            if (!daemon->tasks[i].active) {
                task = &daemon->tasks[i];
                break;
            }
        }
    }

    if (task == NULL) {
        return AURORA_ERR_FULL;
    }

    memset(task, 0, sizeof(*task));
    task->active = true;
    task->pid = packet->pid;
    task->task_id = packet->task_id;
    task->start_ns = packet->timestamp_ns;
    task->latency_budget_us = client->latency_budget_us;
    copy_text(task->task_name, sizeof(task->task_name), packet->task_name);
    return AURORA_OK;
}

static enum aurora_policy_decision_code close_task_marker(
    struct aurora_daemon *daemon,
    struct aurora_daemon_client *client,
    const struct aurora_ail_packet *packet,
    const char **reason_out)
{
    struct aurora_daemon_task_marker *task =
        find_task_marker(daemon, packet->pid, packet->task_id);
    if (task == NULL) {
        *reason_out = "task_marker_missing";
        return AURORA_POLICY_REJECTED_INVALID;
    }

    enum aurora_policy_decision_code decision = AURORA_POLICY_TASK_MARKER_ACCEPTED;
    const char *reason = "task_completed_within_budget";
    uint64_t elapsed_us = 0u;
    if (packet->timestamp_ns >= task->start_ns) {
        elapsed_us = (packet->timestamp_ns - task->start_ns) / 1000u;
    }

    if (task->latency_budget_us != 0u && elapsed_us > task->latency_budget_us) {
        client->latency_violation_count++;
        reason = "latency_budget_exceeded";
        decision = AURORA_POLICY_LATENCY_VIOLATION_LOGGED;

        struct aurora_psl_hint hint = {
            .target_pid = (int)client->pid,
            .issued_at_ns = packet->timestamp_ns,
            .valid_until_ns = packet->timestamp_ns + 50000000ull,
            .confidence_ppm = 700000u,
            .requested_cpu_capacity = 60u,
            .prewarm_pages = 32u,
            .preferred_numa_node = 0u,
            .thermal_sensitivity = 20u,
        };

        if (aurora_psl_submit_hint(
                &daemon->psl_gate,
                &hint,
                AURORA_CAP_PSL_SUBMIT,
                packet->timestamp_ns) == AURORA_OK) {
            decision = AURORA_POLICY_PRIORITY_BOOST_HINT;
        }
    }

    task->active = false;
    *reason_out = reason;
    return decision;
}

static enum aurora_status handle_telemetry(
    struct aurora_daemon *daemon,
    const struct aurora_ail_packet *packet)
{
    if (packet->value0 >= AURORA_SYSCALL_OPEN && packet->value0 <= AURORA_SYSCALL_IPC) {
        struct aurora_syscall_sample sample = {
            .pid = (int)packet->pid,
            .syscall_id = (enum aurora_syscall_id)packet->value0,
            .flags = packet->value1,
            .arg_shape = packet->flags,
            .timestamp_ns = packet->timestamp_ns,
        };
        return aurora_aso_observe(&daemon->aso, &sample, AURORA_CAP_ASO_SUBMIT);
    }

    aurora_telemetry_emit(
        &daemon->telemetry,
        packet->timestamp_ns,
        AURORA_EVENT_AIL_ACCEPT,
        (int)packet->pid,
        packet->value0,
        packet->value1,
        packet->text);
    return AURORA_OK;
}

void aurora_daemon_init(struct aurora_daemon *daemon)
{
    if (daemon == NULL) {
        return;
    }

    memset(daemon, 0, sizeof(*daemon));
    aurora_telemetry_init(&daemon->telemetry);
    aurora_ledger_init(&daemon->ledger);
    aurora_psl_gate_init(&daemon->psl_gate, &daemon->telemetry, &daemon->ledger);
    aurora_sched_init(&daemon->scheduler, &daemon->psl_gate, &daemon->telemetry, &daemon->ledger);
    aurora_aso_init(&daemon->aso, &daemon->scheduler, &daemon->telemetry, &daemon->ledger);
    aurora_memory_init(&daemon->memory, &daemon->scheduler, &daemon->telemetry, &daemon->ledger);
    daemon->next_replay_seq = 1u;
    daemon->next_client_id = 1u;
}

enum aurora_status aurora_daemon_ingest_packet(
    struct aurora_daemon *daemon,
    const struct aurora_ail_packet *packet,
    struct aurora_policy_state *out_state)
{
    if (daemon == NULL || packet == NULL) {
        return AURORA_ERR_INVALID;
    }

    if (!aurora_ail_packet_valid(packet)) {
        struct aurora_policy_state state;
        explain_state(
            daemon,
            NULL,
            AURORA_POLICY_REJECTED_INVALID,
            "packet_checksum_or_version_invalid",
            &state);
        if (out_state != NULL) {
            *out_state = state;
        }
        append_replay(
            daemon,
            packet,
            AURORA_ERR_INVALID,
            AURORA_POLICY_REJECTED_INVALID,
            state.explanation);
        return AURORA_ERR_INVALID;
    }

    if (packet->pid == 0u || packet->type == 0u || packet->type > AURORA_AIL_MSG_GOODBYE) {
        return AURORA_ERR_INVALID;
    }

    struct aurora_daemon_client *client = ensure_client(daemon, packet);
    if (client == NULL) {
        return AURORA_ERR_FULL;
    }

    if (rate_limited(client, packet->timestamp_ns)) {
        struct aurora_policy_state state;
        explain_state(
            daemon,
            client,
            AURORA_POLICY_REJECTED_RATE_LIMIT,
            "client_rate_limit_exceeded",
            &state);
        if (out_state != NULL) {
            *out_state = state;
        }
        aurora_telemetry_emit(
            &daemon->telemetry,
            packet->timestamp_ns,
            AURORA_EVENT_AIL_REJECT,
            (int)packet->pid,
            client->rate_count,
            AURORA_DAEMON_RATE_MAX_EVENTS,
            "client rate limited");
        append_replay(
            daemon,
            packet,
            AURORA_ERR_DENIED,
            AURORA_POLICY_REJECTED_RATE_LIMIT,
            state.explanation);
        return AURORA_ERR_DENIED;
    }

    enum aurora_status status = AURORA_OK;
    enum aurora_policy_decision_code decision = AURORA_POLICY_NOOP;
    const char *reason = "accepted";

    switch ((enum aurora_ail_message_type)packet->type) {
    case AURORA_AIL_MSG_HELLO:
        decision = AURORA_POLICY_PROCESS_REGISTERED;
        reason = "process_registered";
        break;
    case AURORA_AIL_MSG_PROCESS_CLASS:
        if (packet->src_class >= AURORA_SRC_CLASS_COUNT) {
            status = AURORA_ERR_RANGE;
            decision = AURORA_POLICY_REJECTED_INVALID;
            reason = "invalid_semantic_class";
            break;
        }
        client->src_class = (enum aurora_src_class)packet->src_class;
        client->contract = default_contract(client->src_class, client->latency_budget_us);
        status = ensure_core_task(daemon, client);
        decision = status == AURORA_OK ? AURORA_POLICY_SRC_ADMITTED : AURORA_POLICY_REJECTED_INVALID;
        reason = status == AURORA_OK ? "semantic_class_admitted" : "semantic_class_rejected";
        break;
    case AURORA_AIL_MSG_LATENCY_BUDGET:
        if (packet->latency_budget_us == 0u || packet->latency_budget_us > 10000000u) {
            status = AURORA_ERR_RANGE;
            decision = AURORA_POLICY_REJECTED_INVALID;
            reason = "invalid_latency_budget";
            break;
        }
        client->latency_budget_us = packet->latency_budget_us;
        client->contract = default_contract(client->src_class, client->latency_budget_us);
        status = ensure_core_task(daemon, client);
        decision =
            status == AURORA_OK ? AURORA_POLICY_LATENCY_BUDGET_SET : AURORA_POLICY_REJECTED_INVALID;
        reason = status == AURORA_OK ? "latency_budget_set" : "latency_budget_rejected";
        break;
    case AURORA_AIL_MSG_TASK_BEGIN:
        status = ensure_core_task(daemon, client);
        if (status == AURORA_OK) {
            status = store_task_marker(daemon, client, packet);
        }
        decision =
            status == AURORA_OK ? AURORA_POLICY_TASK_MARKER_ACCEPTED : AURORA_POLICY_REJECTED_INVALID;
        reason = status == AURORA_OK ? "task_marker_started" : "task_marker_rejected";
        break;
    case AURORA_AIL_MSG_TASK_END:
        decision = close_task_marker(daemon, client, packet, &reason);
        status = decision == AURORA_POLICY_REJECTED_INVALID ? AURORA_ERR_INVALID : AURORA_OK;
        break;
    case AURORA_AIL_MSG_TELEMETRY:
        status = ensure_core_task(daemon, client);
        if (status == AURORA_OK) {
            status = handle_telemetry(daemon, packet);
        }
        decision = status == AURORA_OK ? AURORA_POLICY_TELEMETRY_OBSERVED :
                                         AURORA_POLICY_REJECTED_INVALID;
        reason = status == AURORA_OK ? "telemetry_observed" : "telemetry_rejected";
        break;
    case AURORA_AIL_MSG_POLICY_QUERY:
        decision = AURORA_POLICY_QUERY_SERVED;
        reason = "policy_query_served";
        break;
    case AURORA_AIL_MSG_GOODBYE:
        decision = AURORA_POLICY_PROCESS_REGISTERED;
        reason = "process_goodbye_recorded";
        break;
    }

    struct aurora_policy_state state;
    explain_state(daemon, client, decision, reason, &state);
    if (out_state != NULL) {
        *out_state = state;
    }

    aurora_telemetry_emit(
        &daemon->telemetry,
        packet->timestamp_ns,
        status == AURORA_OK ? AURORA_EVENT_POLICY_FEEDBACK : AURORA_EVENT_AIL_REJECT,
        (int)packet->pid,
        (uint32_t)decision,
        (uint32_t)status,
        reason);
    append_replay(daemon, packet, status, decision, state.explanation);
    return status;
}

size_t aurora_daemon_replay_snapshot(
    const struct aurora_daemon *daemon,
    struct aurora_ail_replay_record *out,
    size_t out_capacity)
{
    if (daemon == NULL || out == NULL || out_capacity == 0u) {
        return 0u;
    }

    size_t copied = daemon->replay_count < out_capacity ? daemon->replay_count : out_capacity;
    for (size_t i = 0u; i < copied; ++i) {
        out[i] = daemon->replay[i];
    }
    return copied;
}

const struct aurora_daemon_client *aurora_daemon_find_client(
    const struct aurora_daemon *daemon,
    uint32_t pid)
{
    if (daemon == NULL) {
        return NULL;
    }

    for (size_t i = 0u; i < AURORA_DAEMON_MAX_CLIENTS; ++i) {
        if (daemon->clients[i].active && daemon->clients[i].pid == pid) {
            return &daemon->clients[i];
        }
    }
    return NULL;
}

void aurora_daemon_print_replay(const struct aurora_daemon *daemon)
{
    if (daemon == NULL) {
        return;
    }

    puts("AIL REPLAY");
    for (size_t i = 0u; i < daemon->replay_count; ++i) {
        const struct aurora_ail_replay_record *record = &daemon->replay[i];
        printf(
            "  #%llu t=%llu pid=%u msg=%s status=%s decision=%s explanation=%s\n",
            (unsigned long long)record->seq,
            (unsigned long long)record->timestamp_ns,
            record->pid,
            aurora_ail_message_type_name((enum aurora_ail_message_type)record->message_type),
            aurora_status_name(record->status),
            aurora_policy_decision_name((enum aurora_policy_decision_code)record->decision_code),
            record->explanation);
    }
    printf("  dropped=%llu\n", (unsigned long long)daemon->replay_dropped);
}

