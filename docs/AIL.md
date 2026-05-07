# AURORA Application Interface Layer

AIL turns the closed simulator into a userspace orchestration platform.
Applications talk to `libaurora`; `libaurora` serializes fixed packets; the daemon
validates them and feeds the deterministic AURORA core.

```text
application
  aurora_set_process_class()
  aurora_task_begin()
  aurora_emit_telemetry()
        |
        v
libaurora SDK
  fixed packet ABI, monotonic sequence, checksum
        |
        v
IPC transport
  Linux/WSL2: Unix-domain socket
  tests/samples: in-process daemon sink
        |
        v
aurora_daemon
  registry, rate limits, replay log, explanation builder
        |
        v
AURORA core
  SRC, scheduler, PSL gate, ASO, telemetry, ledger, semantic memory
```

## Boundaries

- Applications declare semantic intent; they never set priority directly.
- The daemon normalizes declarations and submits only bounded inputs to the core.
- The PSL gate still validates any boost hint.
- ASO remains observational in v0.1.
- Linux remains authoritative. The daemon can later map outcomes to `nice`,
  affinity, or cgroups, but those are advisory experiments.

## Packet Model

`struct aurora_ail_packet` is fixed-size and checksumed. It contains:

- magic/version
- message type
- client sequence
- timestamp
- pid/client id
- SRC class
- latency budget
- task id/name
- compact telemetry fields
- checksum

Supported messages:

```text
HELLO
PROCESS_CLASS
LATENCY_BUDGET
TASK_BEGIN
TASK_END
TELEMETRY
POLICY_QUERY
GOODBYE
```

## Policy Feedback

Every accepted or rejected interaction returns `struct aurora_policy_state`:

```text
pid
src_class
effective_priority
risk_score
latency_violation_count
thermal_state
decision_code
reason
explanation
```

Example explanation:

```text
task=5101 reason=latency_budget_set src=INTERACTIVE_UI
thermal_state=acceptable decision=LATENCY_BUDGET_SET
```

## Failure Isolation

The daemon rejects malformed packets, rate-limits noisy clients, keeps replay
records for rejected inputs, and isolates rate limiting per client. A broken app
cannot mutate scheduler state directly because the only path into the core is the
daemon's validation logic.

## Replay

`aurora_daemon` appends an `aurora_ail_replay_record` for each interaction:

```text
daemon sequence
timestamp
pid
packet sequence
message type
status
packet checksum
decision code
explanation
```

This gives deterministic causal tracing without storing opaque application memory.

## Sample Workloads

`aurora_sample_apps` demonstrates:

- terminal/editor workflow
- AI inference runtime
- browser-like frame workload
- realtime audio pipeline

Each sample uses the public SDK and receives policy feedback from the daemon.

