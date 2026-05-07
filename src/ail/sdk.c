#include "aurora/ail.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

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

static void client_lock(struct aurora_client *client)
{
    while (atomic_flag_test_and_set_explicit(&client->lock, memory_order_acquire)) {
    }
}

static void client_unlock(struct aurora_client *client)
{
    atomic_flag_clear_explicit(&client->lock, memory_order_release);
}

static void hash_u32(uint32_t *hash, uint32_t value)
{
    for (size_t i = 0u; i < sizeof(value); ++i) {
        *hash ^= (value >> (i * 8u)) & 0xffu;
        *hash *= 16777619u;
    }
}

static void hash_u64(uint32_t *hash, uint64_t value)
{
    for (size_t i = 0u; i < sizeof(value); ++i) {
        *hash ^= (uint32_t)((value >> (i * 8u)) & 0xffu);
        *hash *= 16777619u;
    }
}

static void hash_bytes(uint32_t *hash, const char *bytes, size_t len)
{
    for (size_t i = 0u; i < len; ++i) {
        *hash ^= (uint8_t)bytes[i];
        *hash *= 16777619u;
    }
}

static enum aurora_status ipc_sink(
    const struct aurora_ail_packet *packet,
    void *user,
    struct aurora_policy_state *state)
{
    return aurora_ipc_roundtrip((struct aurora_ipc_endpoint *)user, packet, state);
}

static enum aurora_status send_packet(
    struct aurora_client *client,
    struct aurora_ail_packet *packet,
    struct aurora_policy_state *out_state)
{
    if (client == NULL || packet == NULL || !client->initialized) {
        return AURORA_ERR_INVALID;
    }

    client_lock(client);

    packet->seq = ++client->seq;
    packet->pid = client->pid;
    packet->client_id = client->client_id;
    copy_text(packet->app_name, sizeof(packet->app_name), client->app_name);
    aurora_ail_packet_seal(packet);

    enum aurora_status status = AURORA_ERR_NOT_FOUND;
    struct aurora_policy_state state;
    memset(&state, 0, sizeof(state));

    if (client->sink != NULL) {
        status = client->sink(packet, client->sink_user, &state);
        client->last_state = state;
        if (out_state != NULL) {
            *out_state = state;
        }
    }

    client_unlock(client);
    return status;
}

aurora_time_t aurora_monotonic_time_ns(void)
{
#if defined(_WIN32)
    return (aurora_time_t)GetTickCount64() * 1000000ull;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (aurora_time_t)ts.tv_sec * 1000000000ull + (aurora_time_t)ts.tv_nsec;
    }
    return (aurora_time_t)time(NULL) * 1000000000ull;
#else
    return (aurora_time_t)time(NULL) * 1000000000ull;
#endif
}

void aurora_client_init(struct aurora_client *client, uint32_t pid, const char *app_name)
{
    if (client == NULL) {
        return;
    }

    memset(client, 0, sizeof(*client));
    client->pid = pid;
    copy_text(client->app_name, sizeof(client->app_name), app_name);
    atomic_flag_clear(&client->lock);
    aurora_ipc_endpoint_init(&client->ipc);
    client->initialized = true;
}

void aurora_client_set_sink(
    struct aurora_client *client,
    aurora_client_sink_fn sink,
    void *user)
{
    if (client == NULL) {
        return;
    }
    client->sink = sink;
    client->sink_user = user;
}

enum aurora_status aurora_client_connect_unix_socket(
    struct aurora_client *client,
    const char *path)
{
    if (client == NULL) {
        return AURORA_ERR_INVALID;
    }

    enum aurora_status status = aurora_ipc_connect_unix(&client->ipc, path);
    if (status == AURORA_OK) {
        aurora_client_set_sink(client, ipc_sink, &client->ipc);
    }
    return status;
}

enum aurora_status aurora_register_process(struct aurora_client *client)
{
    struct aurora_ail_packet packet;
    aurora_ail_packet_init(
        &packet,
        AURORA_AIL_MSG_HELLO,
        client != NULL ? client->pid : 0u,
        client != NULL ? client->client_id : 0u,
        0u,
        client != NULL ? client->app_name : NULL);
    return send_packet(client, &packet, NULL);
}

enum aurora_status aurora_set_process_class(
    struct aurora_client *client,
    enum aurora_src_class src_class)
{
    struct aurora_ail_packet packet;
    aurora_ail_packet_init(
        &packet,
        AURORA_AIL_MSG_PROCESS_CLASS,
        client != NULL ? client->pid : 0u,
        client != NULL ? client->client_id : 0u,
        0u,
        client != NULL ? client->app_name : NULL);
    packet.src_class = (uint32_t)src_class;
    return send_packet(client, &packet, NULL);
}

enum aurora_status aurora_set_latency_budget(
    struct aurora_client *client,
    uint32_t latency_budget_us)
{
    struct aurora_ail_packet packet;
    aurora_ail_packet_init(
        &packet,
        AURORA_AIL_MSG_LATENCY_BUDGET,
        client != NULL ? client->pid : 0u,
        client != NULL ? client->client_id : 0u,
        0u,
        client != NULL ? client->app_name : NULL);
    packet.latency_budget_us = latency_budget_us;
    return send_packet(client, &packet, NULL);
}

enum aurora_status aurora_task_begin(
    struct aurora_client *client,
    uint64_t task_id,
    const char *task_name)
{
    struct aurora_ail_packet packet;
    aurora_ail_packet_init(
        &packet,
        AURORA_AIL_MSG_TASK_BEGIN,
        client != NULL ? client->pid : 0u,
        client != NULL ? client->client_id : 0u,
        0u,
        client != NULL ? client->app_name : NULL);
    packet.task_id = task_id;
    copy_text(packet.task_name, sizeof(packet.task_name), task_name);
    return send_packet(client, &packet, NULL);
}

enum aurora_status aurora_task_end(struct aurora_client *client, uint64_t task_id)
{
    struct aurora_ail_packet packet;
    aurora_ail_packet_init(
        &packet,
        AURORA_AIL_MSG_TASK_END,
        client != NULL ? client->pid : 0u,
        client != NULL ? client->client_id : 0u,
        0u,
        client != NULL ? client->app_name : NULL);
    packet.task_id = task_id;
    return send_packet(client, &packet, NULL);
}

enum aurora_status aurora_emit_telemetry(
    struct aurora_client *client,
    uint32_t event_code,
    uint32_t value0,
    uint32_t value1,
    const char *text)
{
    struct aurora_ail_packet packet;
    aurora_ail_packet_init(
        &packet,
        AURORA_AIL_MSG_TELEMETRY,
        client != NULL ? client->pid : 0u,
        client != NULL ? client->client_id : 0u,
        0u,
        client != NULL ? client->app_name : NULL);
    packet.value0 = value0;
    packet.value1 = value1;
    packet.flags = event_code;
    copy_text(packet.text, sizeof(packet.text), text);
    return send_packet(client, &packet, NULL);
}

enum aurora_status aurora_query_policy_state(
    struct aurora_client *client,
    struct aurora_policy_state *out_state)
{
    struct aurora_ail_packet packet;
    aurora_ail_packet_init(
        &packet,
        AURORA_AIL_MSG_POLICY_QUERY,
        client != NULL ? client->pid : 0u,
        client != NULL ? client->client_id : 0u,
        0u,
        client != NULL ? client->app_name : NULL);
    return send_packet(client, &packet, out_state);
}

const struct aurora_policy_state *aurora_client_last_policy_state(
    const struct aurora_client *client)
{
    if (client == NULL) {
        return NULL;
    }
    return &client->last_state;
}

void aurora_ail_packet_init(
    struct aurora_ail_packet *packet,
    enum aurora_ail_message_type type,
    uint32_t pid,
    uint32_t client_id,
    uint64_t seq,
    const char *app_name)
{
    if (packet == NULL) {
        return;
    }

    memset(packet, 0, sizeof(*packet));
    packet->magic = AURORA_AIL_MAGIC;
    packet->version = AURORA_AIL_VERSION;
    packet->type = (uint32_t)type;
    packet->pid = pid;
    packet->client_id = client_id;
    packet->seq = seq;
    packet->timestamp_ns = aurora_monotonic_time_ns();
    copy_text(packet->app_name, sizeof(packet->app_name), app_name);
}

uint32_t aurora_ail_packet_checksum(const struct aurora_ail_packet *packet)
{
    if (packet == NULL) {
        return 0u;
    }

    uint32_t hash = 2166136261u;
    hash_u32(&hash, packet->magic);
    hash_u32(&hash, packet->version);
    hash_u32(&hash, packet->type);
    hash_u32(&hash, packet->flags);
    hash_u64(&hash, packet->seq);
    hash_u64(&hash, packet->timestamp_ns);
    hash_u32(&hash, packet->pid);
    hash_u32(&hash, packet->client_id);
    hash_u32(&hash, packet->src_class);
    hash_u32(&hash, packet->latency_budget_us);
    hash_u64(&hash, packet->task_id);
    hash_u32(&hash, packet->value0);
    hash_u32(&hash, packet->value1);
    hash_u32(&hash, packet->payload_len);
    hash_bytes(&hash, packet->app_name, sizeof(packet->app_name));
    hash_bytes(&hash, packet->task_name, sizeof(packet->task_name));
    hash_bytes(&hash, packet->text, sizeof(packet->text));
    return hash;
}

void aurora_ail_packet_seal(struct aurora_ail_packet *packet)
{
    if (packet == NULL) {
        return;
    }
    packet->checksum = aurora_ail_packet_checksum(packet);
}

bool aurora_ail_packet_valid(const struct aurora_ail_packet *packet)
{
    if (packet == NULL) {
        return false;
    }
    return packet->magic == AURORA_AIL_MAGIC && packet->version == AURORA_AIL_VERSION &&
           packet->checksum == aurora_ail_packet_checksum(packet);
}

const char *aurora_ail_message_type_name(enum aurora_ail_message_type type)
{
    switch (type) {
    case AURORA_AIL_MSG_HELLO:
        return "HELLO";
    case AURORA_AIL_MSG_PROCESS_CLASS:
        return "PROCESS_CLASS";
    case AURORA_AIL_MSG_LATENCY_BUDGET:
        return "LATENCY_BUDGET";
    case AURORA_AIL_MSG_TASK_BEGIN:
        return "TASK_BEGIN";
    case AURORA_AIL_MSG_TASK_END:
        return "TASK_END";
    case AURORA_AIL_MSG_TELEMETRY:
        return "TELEMETRY";
    case AURORA_AIL_MSG_POLICY_QUERY:
        return "POLICY_QUERY";
    case AURORA_AIL_MSG_GOODBYE:
        return "GOODBYE";
    }
    return "UNKNOWN_AIL_MSG";
}

const char *aurora_policy_decision_name(enum aurora_policy_decision_code code)
{
    switch (code) {
    case AURORA_POLICY_NOOP:
        return "NOOP";
    case AURORA_POLICY_PROCESS_REGISTERED:
        return "PROCESS_REGISTERED";
    case AURORA_POLICY_SRC_ADMITTED:
        return "SRC_ADMITTED";
    case AURORA_POLICY_LATENCY_BUDGET_SET:
        return "LATENCY_BUDGET_SET";
    case AURORA_POLICY_TASK_MARKER_ACCEPTED:
        return "TASK_MARKER_ACCEPTED";
    case AURORA_POLICY_PRIORITY_BOOST_HINT:
        return "PRIORITY_BOOST_HINT";
    case AURORA_POLICY_LATENCY_VIOLATION_LOGGED:
        return "LATENCY_VIOLATION_LOGGED";
    case AURORA_POLICY_TELEMETRY_OBSERVED:
        return "TELEMETRY_OBSERVED";
    case AURORA_POLICY_QUERY_SERVED:
        return "QUERY_SERVED";
    case AURORA_POLICY_REJECTED_INVALID:
        return "REJECTED_INVALID";
    case AURORA_POLICY_REJECTED_RATE_LIMIT:
        return "REJECTED_RATE_LIMIT";
    }
    return "UNKNOWN_POLICY_DECISION";
}

