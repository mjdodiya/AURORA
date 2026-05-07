#ifndef AURORA_AIL_H
#define AURORA_AIL_H

#include "aurora/ipc.h"
#include "aurora/src.h"
#include "aurora/types.h"

#include <stdatomic.h>

#define AURORA_AIL_MAGIC 0xA0110A01u
#define AURORA_AIL_VERSION 1u
#define AURORA_AIL_APP_NAME_MAX 32u
#define AURORA_AIL_TASK_NAME_MAX 32u
#define AURORA_AIL_TEXT_MAX 64u
#define AURORA_AIL_EXPLANATION_MAX 160u

enum aurora_ail_message_type {
    AURORA_AIL_MSG_HELLO = 1,
    AURORA_AIL_MSG_PROCESS_CLASS = 2,
    AURORA_AIL_MSG_LATENCY_BUDGET = 3,
    AURORA_AIL_MSG_TASK_BEGIN = 4,
    AURORA_AIL_MSG_TASK_END = 5,
    AURORA_AIL_MSG_TELEMETRY = 6,
    AURORA_AIL_MSG_POLICY_QUERY = 7,
    AURORA_AIL_MSG_GOODBYE = 8
};

enum aurora_policy_decision_code {
    AURORA_POLICY_NOOP = 0,
    AURORA_POLICY_PROCESS_REGISTERED = 1,
    AURORA_POLICY_SRC_ADMITTED = 2,
    AURORA_POLICY_LATENCY_BUDGET_SET = 3,
    AURORA_POLICY_TASK_MARKER_ACCEPTED = 4,
    AURORA_POLICY_PRIORITY_BOOST_HINT = 5,
    AURORA_POLICY_LATENCY_VIOLATION_LOGGED = 6,
    AURORA_POLICY_TELEMETRY_OBSERVED = 7,
    AURORA_POLICY_QUERY_SERVED = 8,
    AURORA_POLICY_REJECTED_INVALID = 9,
    AURORA_POLICY_REJECTED_RATE_LIMIT = 10
};

enum aurora_thermal_state {
    AURORA_THERMAL_UNKNOWN = 0,
    AURORA_THERMAL_ACCEPTABLE = 1,
    AURORA_THERMAL_CONSTRAINED = 2,
    AURORA_THERMAL_SATURATED = 3
};

struct aurora_ail_packet {
    uint32_t magic;
    uint32_t version;
    uint32_t type;
    uint32_t flags;
    uint64_t seq;
    aurora_time_t timestamp_ns;
    uint32_t pid;
    uint32_t client_id;
    uint32_t src_class;
    uint32_t latency_budget_us;
    uint64_t task_id;
    uint32_t value0;
    uint32_t value1;
    uint32_t payload_len;
    char app_name[AURORA_AIL_APP_NAME_MAX];
    char task_name[AURORA_AIL_TASK_NAME_MAX];
    char text[AURORA_AIL_TEXT_MAX];
    uint32_t checksum;
};

struct aurora_policy_state {
    uint32_t pid;
    uint32_t src_class;
    int32_t effective_priority;
    uint32_t risk_score;
    uint32_t latency_violation_count;
    uint32_t thermal_state;
    uint32_t decision_code;
    uint64_t causal_seq;
    char reason[AURORA_MESSAGE_MAX];
    char explanation[AURORA_AIL_EXPLANATION_MAX];
};

typedef enum aurora_status (*aurora_client_sink_fn)(
    const struct aurora_ail_packet *packet,
    void *user,
    struct aurora_policy_state *state);

struct aurora_client {
    uint32_t pid;
    uint32_t client_id;
    uint64_t seq;
    char app_name[AURORA_AIL_APP_NAME_MAX];
    atomic_flag lock;
    bool initialized;
    aurora_client_sink_fn sink;
    void *sink_user;
    struct aurora_ipc_endpoint ipc;
    struct aurora_policy_state last_state;
};

void aurora_client_init(struct aurora_client *client, uint32_t pid, const char *app_name);
void aurora_client_set_sink(
    struct aurora_client *client,
    aurora_client_sink_fn sink,
    void *user);
enum aurora_status aurora_client_connect_unix_socket(
    struct aurora_client *client,
    const char *path);
enum aurora_status aurora_register_process(struct aurora_client *client);
enum aurora_status aurora_set_process_class(
    struct aurora_client *client,
    enum aurora_src_class src_class);
enum aurora_status aurora_set_latency_budget(
    struct aurora_client *client,
    uint32_t latency_budget_us);
enum aurora_status aurora_task_begin(
    struct aurora_client *client,
    uint64_t task_id,
    const char *task_name);
enum aurora_status aurora_task_end(struct aurora_client *client, uint64_t task_id);
enum aurora_status aurora_emit_telemetry(
    struct aurora_client *client,
    uint32_t event_code,
    uint32_t value0,
    uint32_t value1,
    const char *text);
enum aurora_status aurora_query_policy_state(
    struct aurora_client *client,
    struct aurora_policy_state *out_state);
const struct aurora_policy_state *aurora_client_last_policy_state(
    const struct aurora_client *client);

void aurora_ail_packet_init(
    struct aurora_ail_packet *packet,
    enum aurora_ail_message_type type,
    uint32_t pid,
    uint32_t client_id,
    uint64_t seq,
    const char *app_name);
uint32_t aurora_ail_packet_checksum(const struct aurora_ail_packet *packet);
void aurora_ail_packet_seal(struct aurora_ail_packet *packet);
bool aurora_ail_packet_valid(const struct aurora_ail_packet *packet);
const char *aurora_ail_message_type_name(enum aurora_ail_message_type type);
const char *aurora_policy_decision_name(enum aurora_policy_decision_code code);
aurora_time_t aurora_monotonic_time_ns(void);

#endif

