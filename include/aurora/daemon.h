#ifndef AURORA_DAEMON_H
#define AURORA_DAEMON_H

#include "aurora/ail.h"
#include "aurora/aso.h"
#include "aurora/memory.h"
#include "aurora/scheduler.h"

#define AURORA_DAEMON_MAX_CLIENTS 64u
#define AURORA_DAEMON_MAX_TASK_MARKERS 128u
#define AURORA_DAEMON_REPLAY_CAPACITY 512u
#define AURORA_DAEMON_RATE_WINDOW_NS 1000000000ull
#define AURORA_DAEMON_RATE_MAX_EVENTS 64u

struct aurora_daemon_client {
    bool active;
    uint32_t pid;
    uint32_t client_id;
    char app_name[AURORA_AIL_APP_NAME_MAX];
    enum aurora_src_class src_class;
    struct aurora_src_contract contract;
    uint32_t latency_budget_us;
    uint32_t latency_violation_count;
    aurora_time_t rate_window_start_ns;
    uint32_t rate_count;
    uint32_t rejected_count;
    uint64_t last_packet_seq;
    struct aurora_policy_state last_state;
};

struct aurora_daemon_task_marker {
    bool active;
    uint32_t pid;
    uint64_t task_id;
    aurora_time_t start_ns;
    uint32_t latency_budget_us;
    char task_name[AURORA_AIL_TASK_NAME_MAX];
};

struct aurora_ail_replay_record {
    uint64_t seq;
    aurora_time_t timestamp_ns;
    uint32_t pid;
    uint32_t packet_seq;
    uint32_t message_type;
    enum aurora_status status;
    uint32_t packet_checksum;
    uint32_t decision_code;
    char explanation[AURORA_AIL_EXPLANATION_MAX];
};

struct aurora_daemon {
    struct aurora_telemetry_ring telemetry;
    struct aurora_policy_ledger ledger;
    struct aurora_psl_gate psl_gate;
    struct aurora_scheduler scheduler;
    struct aurora_aso aso;
    struct aurora_memory_manager memory;
    struct aurora_daemon_client clients[AURORA_DAEMON_MAX_CLIENTS];
    struct aurora_daemon_task_marker tasks[AURORA_DAEMON_MAX_TASK_MARKERS];
    struct aurora_ail_replay_record replay[AURORA_DAEMON_REPLAY_CAPACITY];
    size_t replay_count;
    uint64_t next_replay_seq;
    uint64_t replay_dropped;
    uint32_t next_client_id;
};

void aurora_daemon_init(struct aurora_daemon *daemon);
enum aurora_status aurora_daemon_ingest_packet(
    struct aurora_daemon *daemon,
    const struct aurora_ail_packet *packet,
    struct aurora_policy_state *out_state);
size_t aurora_daemon_replay_snapshot(
    const struct aurora_daemon *daemon,
    struct aurora_ail_replay_record *out,
    size_t out_capacity);
const struct aurora_daemon_client *aurora_daemon_find_client(
    const struct aurora_daemon *daemon,
    uint32_t pid);
void aurora_daemon_print_replay(const struct aurora_daemon *daemon);

#endif

