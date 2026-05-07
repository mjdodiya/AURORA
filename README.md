# AURORA-KERNEL-v0.1

AURORA is a minimal experimental kernel-architecture prototype. It is not a bootable
kernel yet. This repository models the hard boundary that the architecture depends
on: deterministic kernel decisions in one layer, userspace advisory intelligence in
another layer, and auditable validation between them.

The prototype is written in C17 with no external runtime dependencies.

## What This Builds

```text
apps / services
    semantic declarations, advisory hints, syscall behavior samples
            |
            v
libaurora + aurora_daemon
    SDK declarations, IPC packets, replayable policy feedback
            |
            v
deterministic core library
    scheduler, SRC metadata, PSL gate, ASO observer, telemetry, policy ledger
            |
            v
tools/aurora_sim
    executable demo of advisory orchestration with deterministic fallback
```

## Implemented Subsystems

- `src_manager`: semantic resource classes and contracts.
- `scheduler`: deterministic task selection with stable tie-breaking.
- `psl_gate`: bounded validation of predictive scheduler hints.
- `aso`: adaptive security observer that scores behavior but does not enforce.
- `memory_manager`: semantic memory-region admission and policy checks.
- `telemetry_ring`: fixed-size event stream with loss accounting.
- `policy_ledger`: append-only decision log for replay and debugging.
- `libaurora`: userspace SDK for semantic application declarations.
- `aurora_daemon`: userspace ingestion, rate limiting, replay, and feedback.

## Build

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure
.\build\aurora_sim.exe
.\build\aurora_sample_apps.exe
```

If your CMake defaults to another generator, omit `-G "MinGW Makefiles"`.

## Architecture Rule

The core library never trusts adaptive input directly. PSL hints are rejected or
clamped before the scheduler sees them. ASO risk scores are telemetry only in v0.1.
If advisory state is disabled, the scheduler continues with deterministic SRC and
priority policy.

## Application Integration Layer

The AIL design and protocol are documented in `docs/AIL.md`.

The public SDK is exposed through:

```c
#include "aurora/ail.h"

aurora_register_process(&client);
aurora_set_process_class(&client, AURORA_SRC_INTERACTIVE_UI);
aurora_set_latency_budget(&client, 6000);
aurora_task_begin(&client, 1, "render");
aurora_task_end(&client, 1);
aurora_query_policy_state(&client, &state);
```

On Linux/WSL2, `aurora_daemon` exposes a Unix-domain socket endpoint. In tests and
sample applications, the same packet path runs through an in-process daemon sink.
