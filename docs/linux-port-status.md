# Linux Dedicated-Server Port Overview

## Purpose

This repository ports Integrated Storage to a native Linux Palworld
dedicated server.

The goal is to reproduce the server-side behaviour of the original
Windows client/server mod without carrying across its Windows-only
detours, client UI hooks, executable pattern scans, or RPC transport.

The finished Linux port is intended to let storage owned by camps in the
same guild participate in one server-authoritative shared storage system.

## Target platform

The port currently targets:

- Palworld dedicated server running natively on Linux
- x86-64
- NullPrism RE-UE4SS-Linux
- Native C++ user mods loaded as `main.so`
- A dedicated-server-only execution model

Listen servers, single-player sessions, client UI modification, and
Windows builds are outside the current Linux scope.

## Technology

### NullPrism RE-UE4SS-Linux

NullPrism provides the native Linux UE4SS loader and the Unreal
reflection surface used by this port.

The mod is installed in the standard native-mod layout:

```text
Mods/ModIntegratedStorageCpp/
├── dlls/
│   └── main.so
└── enabled.txt
```

The library exports the standard native lifecycle functions:

```text
start_mod
uninstall_mod
```

### Unreal reflection

The implementation uses Unreal reflection rather than hard-coded Linux
object layouts wherever a reflected alternative is available.

Current reflected operations include:

- Discovering camp, chest, and storage objects
- Reading camp guild identifiers
- Reading camp module arrays
- Resolving chest ownership through
  `GetBaseCampModelBelongTo`
- Resolving registration through
  `OnAvailableConcreteModel_ServerInternal`
- Inspecting function parameter metadata
- Validating object-property type, size, offset, and accepted class
- Constructing zeroed parameter buffers from reflected metadata

The port does not assume that an input or return value is at byte offset
zero. Offsets and bounds are validated before use.

### Threading model

NullPrism's normal mod update callback is not the Unreal game thread.

The port therefore separates work into two paths:

- The worker-side update path observes world state and requests work.
- The EngineTick callback performs Unreal `ProcessEvent` calls and other
  game-thread-only operations.

Dedicated-server role checks, chest ownership queries, registration
metadata validation, and registration calls are executed on the Unreal
game thread.

Atomic state is used to coordinate requests and prevent overlapping
association passes.

### Process-lifetime module pin

The mod retains one additional `dlopen` reference to its own shared
library for the lifetime of the PalServer process.

This prevents NullPrism callback storage from referencing unmapped
native code if the original mod handle is released before deferred
callback cleanup completes.

Native hot reload is therefore intentionally unsupported.

## Server-side model

The Linux port builds the storage relationship from reflected server
objects.

### Camps and guilds

Each valid camp is associated with its 16-byte guild identifier.

Guild identifiers are handled as binary data rather than Linux
`wchar_t`, avoiding the platform-width mismatch with Unreal's
two-byte character representation.

### Storage modules

Every `PalBaseCampModuleItemStorage` module is collected from each valid
camp.

Camps with a storage module remain valid registration targets even when
they currently contain no chest models.

### Chests

Every `PalMapObjectItemChestModel` is discovered and associated with its
owning camp through the reflected
`GetBaseCampModelBelongTo` function.

### Registration plan

For each guild, the port creates every unique pair where:

- The chest and storage belong to the same guild.
- The storage belongs to a different camp from the chest.
- The chest, storage, camp, and guild relationships are valid.
- Duplicate pointers and duplicate exact pairs are rejected.
- Conflicting camp or guild ownership aborts plan acceptance.

The current populated-world validation plan contains 157 chests,
20 storage modules, and 285 same-guild foreign-camp registration pairs.

## Current implementation boundary

The port has demonstrated one controlled invocation of
`OnAvailableConcreteModel_ServerInternal` on an isolated populated-world
clone.

That call was protected by:

- An explicit arm file
- A default-disabled normal package
- Dedicated-server validation
- Unreal game-thread validation
- Same-guild validation
- Different-camp validation
- Storage-class validation
- Reflected function-parameter validation
- A process-lifetime one-shot guard

The complete 285-pair registration loop is not yet enabled.

The next engineering task is to observe the selected storage's readable
registration state before and after the call. This is required before
testing duplicate-call idempotency or adding reconciliation.

## Safety rules

Development follows these boundaries:

- No global `LD_PRELOAD`
- No second UE4SS installation
- No Windows detours or executable AOB hooks
- No Unreal `ProcessEvent` calls from worker threads
- No direct production mutation during development
- No full registration loop before single-pair observability and
  idempotency are proven
- No incoming chat `FText` mutation
- No assumption that a successful function return proves gameplay effect
- Full isolated-save and mod restoration after mutation tests
- Preserve upstream MIT attribution

## Intended release path

Before the first usable Linux release, the port still needs:

1. Readable registration-effect observability
2. Exact-pair idempotency validation
3. Complete registration-plan execution
4. Guarded periodic reconciliation
5. Stale registration and camp-change handling
6. Populated-world restart validation
7. Configuration and troubleshooting documentation
8. Production rollback instructions

Stage-by-stage build identities, hashes, test evidence, and acceptance
records are maintained separately in
[`validation-history.md`](validation-history.md).
