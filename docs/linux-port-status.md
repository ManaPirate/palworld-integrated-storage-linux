# Linux Dedicated-Server Port Status

Last updated: 6 August 2026

## Overview

This repository is the native Linux dedicated-server port of [Sarfflow's Palworld Integrated Storage](https://github.com/Sarfflow/palworld-integrated-storage).

The Linux implementation targets [NullPrism RE-UE4SS-Linux](https://github.com/NullPrism/RE-UE4SS-Linux) for a Palworld dedicated server running natively on Linux.

Development remains on `linux/nullprism-dedicated-server`.

The port is not yet ready for production deployment. Current work has validated discovery, ownership association, dedicated-server role handling, registration metadata, and the complete registration plan without performing any storage registration or item mutation.

## Port scope

The dedicated-server port retains:

- Dedicated-server and world validation
- Camp and guild discovery
- Camp storage-module discovery
- Chest-model discovery
- Chest-to-camp ownership association
- Same-guild cross-camp registration planning
- Registration-function metadata validation
- Future server-authoritative registration and reconciliation
- Configuration and diagnostic logging

Currently excluded:

- Windows API dependencies
- PolyHook and executable AOB detours
- Client inventory and crafting UI changes
- Client-side display-slot injection
- RPC transport from the combined Windows client/server mod
- Listen-server and single-player support
- HUD or widget modification

## Current safety boundary

The Linux port currently performs read-only discovery and planning.

It has not yet:

- Invoked `OnAvailableConcreteModel_ServerInternal`
- Registered a chest with a foreign storage module
- Transferred or routed an item
- Modified a storage container
- Modified an item array
- Intentionally changed a Palworld save
- Been installed on the production server

The native artifact contains two deliberate `ProcessEvent` call sites:

1. PalUtility server and dedicated-server role queries
2. `GetBaseCampModelBelongTo` chest ownership queries

The registration function is resolved and inspected but never invoked.

## Accepted stages

### Stage 4a — camp, guild, and storage discovery

```text
Camps:    20
Guilds:   8
Storages: 20
```

### Stage 4b.1 — chest discovery

```text
Chest objects: 157
Valid:         157
Null:          0
```

### Stage 4b.2 — chest ownership association

Chest ownership is resolved on the Unreal game thread through `GetBaseCampModelBelongTo`.

```text
Chest objects:       157
Associated chests:   157
Unassociated chests: 0
Guilds:              8
Chest-owning camps:  17
```

Three valid camps contain no chest models.

### Stage 4c.1e — game-thread role hardening

```text
ROLE THREAD=GAME
ROLE server=1 dedicated=1
ROLE RESULT=PASS
```

Accepted commit:

```text
b0017c8b48c2e84acdb1de74c5beff146df889fe
fix(linux): resolve dedicated role on game thread
```

### Stage 4c.1f — registration metadata validation

Resolved function:

```text
OnAvailableConcreteModel_ServerInternal
```

Observed runtime layout:

```text
Parameter bytes:   8
Input parameters:  1
Object inputs:     1
Object offset:     0
Object size:       8
Property flags:    0x18001000000280
Property class:    valid
Chest compatible:  yes
```

The function was not invoked.

Accepted commit:

```text
e53faa1adb639858f22220877d374b48b0cef706
feat(linux): validate registration metadata read-only
```

## Stage 4c.2 — deterministic would-register plan

Stage 4c.2 constructs the complete same-guild, foreign-camp registration plan while remaining read-only.

It:

- Groups every associated chest by guild and owning camp
- Groups every storage module by guild and owning camp
- Includes valid storage camps containing no chest models
- Creates pairs only within the same guild
- Excludes each chest's own-camp storage
- Deduplicates chest, storage, and exact pair pointers
- Detects camp and guild ownership conflicts
- Produces per-guild counts
- Produces order-independent process-local fingerprints
- Selects one planned pair for metadata validation
- Never invokes the registration function

### Accepted candidate identity

```text
Version:
0.1.0-linux-stage4c.2-would-register

Linux source SHA256:
12ee389499b6c5a5e944b7c4f34f70b378b775d7527df080ebc1e80cce5b8865

Linux main.so SHA256:
0d494b86751d317f102812622ecc5ff48b796d8f2f74cdeedaa2e22d41d2b1a3

ELF Build ID:
427acb4e36a79956ce3c96f97473b507f3a697e2
```

### Populated-world acceptance

The planner was validated across 25 repeated scans over a 180-second stability window.

```text
ROLE THREAD=GAME:       1
ROLE RESULT=PASS:       1
Planner passes:         25
Planner incomplete:     0
Metadata passes:        25
Metadata incomplete:    0
Chest passes:           25
Chest incomplete:       0
Invalid thread markers: 0
Exception markers:      0
Crash markers:          0
```

Stable global plan:

```text
Guilds:                 8
Guilds with pairs:      7
Associated chests:      157
Storage modules:        20
Foreign-camp pairs:     285
Own-camp combinations:  157
Duplicate chests:       0
Duplicate storages:     0
Duplicate pairs:        0
Camp conflicts:         0
Guild conflicts:        0
Invalid camps:          0
Missing guilds:         0
Camps without storage:  0
```

The one inactive guild contains one camp and therefore has no foreign storage target.

Stable process-local fingerprints:

```text
XOR: ac771a474f28c103
SUM: e6f7c6307a0416e9
```

These fingerprints include Unreal object addresses. They should remain stable across repeated scans in one PalServer process, but are not expected to remain identical after a restart.

### Per-guild plan

| Guild | Chests | Storages | Foreign pairs | Own-camp excluded |
|---|---:|---:|---:|---:|
| `20f979c33446e7f1f8cea19499aad71a` | 22 | 3 | 44 | 22 |
| `4fda64b78a4ae58954126eb13ec06dd3` | 3 | 1 | 0 | 3 |
| `5c21c345d94ea28f2dd2fb842cb20be4` | 32 | 3 | 64 | 32 |
| `64ad3b316644502f780ceebd2a31ff99` | 22 | 2 | 22 | 22 |
| `966b6b8eca48b42eaa08b3a92e673d00` | 15 | 3 | 30 | 15 |
| `9af4ac3e4a49def1993afeaced626523` | 31 | 4 | 93 | 31 |
| `a21c73d1fd4d4539161573b06df671f8` | 10 | 2 | 10 | 10 |
| `df4d6e7ea84f3b7db90b5ab07bc41b3e` | 22 | 2 | 22 | 22 |

### Restoration and isolation

```text
Previous isolated mod SHA256:
56efb4928b62b520845ab17d8bb5a2f8be1453e7c73a29c78a0127a4dcf1ed72

Baseline Level.sav SHA256:
a0c0464c33763a021727ae345aadda8df61ed6dd72fe7cd0e147fd965e32acf6

Player saves:
19
```

The production server remained running with the same PalServer process and container start time throughout the completed acceptance run.

## Next stage

### Stage 4c.3 — controlled single registration

The first mutation test will be restricted to:

- The isolated populated-world clone
- One real associated chest
- One foreign storage in the same guild
- One reflected parameter buffer
- One game-thread invocation
- No automatic reconciliation
- No production deployment
- Full save and mod snapshot restoration

Before invoking the function, the implementation must validate:

- Dedicated-server role
- Unreal game-thread execution
- Chest and storage validity
- Same guild
- Different owning camps
- Exactly one compatible object input
- Reflected parameter bounds
- A feature gate that defaults to disabled
- A one-shot execution guard

The test must establish whether the registration call is safe and idempotent before any complete registration loop is attempted.

## Later work

After controlled single-pair registration succeeds:

1. Validate repeated-call idempotency.
2. Register the complete planned pair set.
3. Add guarded periodic reconciliation.
4. Investigate stale registration and removal behaviour.
5. Validate world restarts and changing camp membership.
6. Add user-facing configuration.
7. Document installation, rollback, and troubleshooting.
8. Produce the first usable Linux release and merge normally into `main`.

## Development rules

- No global `LD_PRELOAD`
- No second UE4SS installation
- No incoming chat `FText` mutation
- No Windows detours or AOB hooks in the dedicated-server port
- No Unreal `ProcessEvent` calls from worker threads
- No registration mutation before explicit isolated-world acceptance
- No production deployment before rollback and reconciliation are proven
- Preserve upstream MIT attribution
