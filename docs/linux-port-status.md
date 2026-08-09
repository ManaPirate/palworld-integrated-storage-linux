# Linux Port Engineering Status

This is the **current-state dashboard** for the native Linux dedicated-server port
of Palworld Integrated Storage.

It is intentionally concise. The complete chronological engineering record is
preserved in [`linux-port-history.md`](linux-port-history.md), and major evidence
archives/checkpoints are indexed in
[`linux-port-evidence-index.md`](linux-port-evidence-index.md).

## 1. Current accepted checkpoint

```text
Branch:
linux/nullprism-dedicated-server

Engineering checkpoint entering the three-file documentation split:
b5720213f65a8190baba6284f7fb0dcca5e47f9a

Accepted Linux source SHA256:
4d8247d7beb1fea72df0d91cfd653dfb016b2d43deff299c3e7439baac984000

Accepted artifact SHA256:
10c2b8e3c60ba4e618c6709397c097694255ed7b0174bcdbd1d968e09645c594

Accepted artifact Build ID:
671730ac4ee16633a317409cd1e9c552b19baca3

Windows source SHA256:
de89622f5e6831f8ea24650f1f59e0d97580c05bc36e7efadfaae9c9cbc8107c

Build script SHA256:
0c31858af8dcd314cccc85e3f6a8b71310e5fba5892c02ec2155aee75aaf9288

Pre-split chronological runsheet SHA256:
45d7f2db1fe26192ffde7685a6f751c339c20950640c9f2bd842d6e2d38fb2eb
```

The accepted Linux source/artifact pair remains the Stage 4d.8a R2 implementation.
Stages after 4d.8a have been characterization, failure analysis, recovery, and
static/offline transport research unless explicitly stated otherwise.

## 2. Documentation authority

The engineering documentation now has three separate jobs:

- [`linux-port-status.md`](linux-port-status.md) — **current state**: accepted
  checkpoint, current safety boundaries, unresolved work, and immediate next
  action.
- [`linux-port-history.md`](linux-port-history.md) — **chronological record**:
  detailed stage-by-stage engineering history, including failed candidates,
  runtime observations, recovery actions, and accepted conclusions.
- [`linux-port-evidence-index.md`](linux-port-evidence-index.md) — **evidence
  index**: compact lookup table for major stages, evidence archives, commits,
  source/artifact identities, and classifications.

At the moment of this split, `linux-port-history.md` is a byte-for-byte copy of
the previous 7,455-line `linux-port-status.md`.

Future accepted engineering stages should:

1. update this status file in place;
2. append the detailed stage record to the history file;
3. add/update the evidence index entry;
4. commit source + documentation together whenever source is accepted;
5. keep docs-only checkpoints separate from source acceptance.

If any compact summary conflicts with detailed recorded evidence, the detailed
history plus the referenced evidence archive and Git object are authoritative.

## 3. Target and scope

Target:

```text
Palworld native Linux dedicated server
x86-64
NullPrism RE-UE4SS-Linux
native C++ user mod: main.so
branch: linux/nullprism-dedicated-server
```

Current project scope:

- Dedicated-server native Linux support.
- Windows clients continue using the upstream Windows client implementation.
- Native Linux client support is not part of this port.
- Windows source remains upstream-compatible and must not be modified by Linux
  port work.
- Production remains isolated from development/runtime probes unless a stage
  explicitly authorizes production deployment.

## 4. Pinned runtime/dependency baseline

NullPrism:

```text
Repository:
NullPrism/RE-UE4SS-Linux

Pinned tag:
linux-v0.1.0

Pinned commit:
5d33654755efed844336497e8a9a15e6716b5d6c

Official release loader SHA256:
26dffce875fb771fb2ac2a63325e7effb5551a03a35598810f13d2e6c854a1ff
```

The official release loader remains the accepted NullPrism runtime authority for
this port. A complete local NullPrism rebuild was not accepted because the
available Rust toolchain was older than a pinned dependency requirement.

## 5. Exact current PalServer ELF

Stage 4d.8g R3 established the exact installed PalServer binary:

```text
SHA256:
c508a28b06cebf0752296b38da5244c08a5688da44dad8f816eb2d726d82699e

size:
196297880 bytes

ELF Build ID:
787f7f8c15edb8fb
```

The host file, production-container mount, and temporary development-container
copy all matched byte-for-byte.

The 4d.8g R3 evidence archive SHA256 is:

```text
ab2998c0fca4aadb0168ec88a80f129e27c473d4ce1cbcf522c0dc632997b33c
```

## 6. Current accepted runtime implementation

The current accepted mod source/artifact is the Stage 4d.8a R2 implementation.

Runtime-proven surfaces include:

- dedicated-server role resolution;
- mature camp/guild/storage/chest planner;
- same-guild foreign-camp registration planning;
- bounded exact chest -> module -> container-id -> container-manager lookup;
- reflected `PalItemContainer` slot-array inspection for selected planner
  chests only;
- exact request/reply RPC metadata;
- exact camp GUID layout;
- exact `PalItemId.StaticId` FName layout;
- same-guild-minus-own-camp material-pool aggregation.

Stage 4d.8a bounded pool result:

```text
guild:
20f979c33446e7f1f8cea19499aad71a

requester camp:
0e254aa41a44c4715a9359a8f1a4ec41

foreign chests:
22

containers:
22

slot arrays:
22

slot objects:
850

positive slots:
288

fully read slots:
288

layout failures:
0

exceptions:
0

unique items:
272

total quantity:
69227

passed:
1
```

The accepted path does **not** enumerate all `PalItemContainer` instances.

## 7. Registration executor state

Stage 4d.7a proved the full one-shot executor:

```text
planned:
285

attempted:
285

completed:
285

blocked:
0

exceptions:
0

function mismatches:
0

guild mismatches:
0

camp mismatches:
0

storage-class mismatches:
0

game thread:
1

dedicated:
1

metadata:
1

RESULT:
PASS
```

Accepted Stage 4d.7a commit:

```text
7761f2507ce08adb1c3635e224132de1c3fa388a
```

Accepted Stage 4d.7a source:

```text
e968bc43d01008808cae58bb7dd9258dc2db2278e5f5ffe017d3fb5349e267b9
```

Accepted Stage 4d.7a artifact:

```text
2bbde02085d87d99acce5f0c3f7765e1e95ff6916e875dc25181883cca79c358
```

The current accepted 4d.8a source retains the previously accepted registration
architecture while adding bounded transport characterization code.

## 8. Dynamic topology requirement

A new same-guild base/camp created **after server startup** is an intentional
acceptance test, not contamination.

Stage 4d.7b proved that the mature planner dynamically expanded from:

```text
20 storages / 285 pairs
```

to:

```text
21 storages / 307 pairs
```

after the new camp was created.

However, the currently accepted executor is one-shot and did **not** execute the
new 307-pair plan.

Final practical acceptance therefore requires:

1. start with a known guild topology;
2. establish initial planner/registration state;
3. create a new same-guild base after startup;
4. mature planner discovers it;
5. registration pair cardinality expands correctly;
6. executor actually applies the new pairs;
7. the new camp participates in the functional client test;
8. PID/UDP/crash-free/restoration invariants survive.

A periodic or topology-fingerprint-aware reconcile executor is still required.

## 9. Functional client gate state

Stage 4d.7b tested a real Windows client against the isolated Linux server.

Engineering classification:

```text
CLIENT_BLOCKED
```

Observed behaviour:

- client connected through the UDP relay;
- literal Guild Chest resources appeared where the existing Windows client
  already understood them;
- ordinary remote-pool material was not presented to the client UI;
- the client therefore refused the action before a server-side consume path
  could execute.

Conclusion:

```text
server-side registration alone is insufficient for practical client use;
the upstream server-to-client material transport path is required.
```

Stage 4d.7b evidence SHA256:

```text
1ab94e147e650f02cb98fc1a6416355d9755eb902f2805df3693221f2c665560
```

## 10. Upstream transport architecture

The upstream Windows implementation uses:

```text
client -> server:
PalPlayerController:Debug_CheatCommand_ToServer(FString)
NetServer reliable

request sentinel:
ISREQ|

request:
ISREQ|<32-hex campGuid>

server -> client:
PalPlayerController:Debug_ReceiveCheatCommand_ToClient(FString)
NetClient reliable

reply sentinel:
IS1|

reply:
IS1|id:cnt,id:cnt,
```

The client supplies the current camp GUID.

The server:

1. resolves the requester camp;
2. identifies the requester's guild;
3. aggregates same-guild storage excluding the requester's own camp;
4. serializes item IDs as strings because FName indices are process-local;
5. sends the material pool to the Windows client.

The server-side transport is therefore required even though native Linux client
hooks/AOB detours remain out of scope.

## 11. Current FName serialization boundary

The remaining transport blocker is converting bounded-pool `FName` item IDs to
stable wire strings safely.

### Blocked public paths

Direct:

```text
RC::Unreal::FName::ToString()
```

is blocked on the pinned NullPrism runtime.

Stage 4d.8b R2 reached the previously accepted bounded pool successfully and
then died at the first FName `ToString()` operation with:

```text
FMallocBinned2 Attempt to realloc an unrecognized block
canary mismatch
Signal 11
Segmentation fault
```

This is an Unreal allocator fatal, not a catchable C++ exception.

`FName::GetPlainNameString()` is also blocked because Stage 4d.8c statically
proved that the pinned implementation directly calls `FName::ToString()`.

Do not runtime-probe either path again without genuinely new safety evidence.

### Lower-level C++ API

Stage 4d.8d found:

```text
FNameEntryId:
visible

GetComparisonIndex:
visible

GetDisplayIndex:
visible

GetNumber / number macros:
visible
```

but no complete C++:

```text
FNamePool
-> entry resolver
-> length/width
-> fixed-buffer copy
```

chain.

### patternsleuth FNamePool resolver

Stage 4d.8f R2 proved the exact pinned semantics.

For UE 4.23+:

```text
FNamePool(pub u64)
=
direct static address of the FNamePool object
```

For pre-4.23:

```text
FNamePool(pub u64)
=
address of the static GNames pointer (&GNames)
```

The current `patternsleuth_bind` / NullPrism `PsScanResults` bridge does **not**
export the FNamePool result, and the pinned resolver source does not contain a
usable FNameEntry decoder.

## 12. FName number semantics

Pinned number semantics are visible and accepted:

```text
NAME_NO_NUMBER_INTERNAL = 0
NAME_INTERNAL_TO_EXTERNAL(x) = x - 1
```

At least one already-observed bounded-pool FName had a non-zero number.

Any final string reconstruction must therefore preserve suffix semantics rather
than decoding only the base comparison/display name.

## 13. Offline scanner state

Stage 4d.8g R3 proved the existing offline C ABI:

```cpp
extern "C" bool ps_scan_file_ue4ss(
    const char* path,
    PsFileResolutionResults* results
);
```

The result layout includes:

```text
engine_version
guobject_array
fname_tostring
fname_ctor_wchar
gmalloc
static_construct_object_internal
ftext_fstring
fuobject_hash_tables_get
gnatives
console_manager_singleton
gameengine_tick
```

The accepted `libUE4SS.so` exports `ps_scan_file_ue4ss`, and the pinned
repository already contains a matching C++ consumer in
`tests/PalworldSignatureTests.cpp`.

Stage 4d.8g intentionally did **not** invoke the offline scanner.

## 14. Blocked and exhausted unsafe paths

Do not casually reopen these paths:

1. direct `ItemContainerMap_InServer` manager-map inspection — allocator
   corruption;
2. broad reflected graph / `TFieldRange` traversal — allocator corruption;
3. bulk `FindAllOf("PalItemContainer")` — allocator corruption;
4. selected-chest property guesses exhausted negative;
5. fixed accessor guesses exhausted negative;
6. standalone registration does not prove membership transition;
7. `OnReadyItemContainerGuildChest` transition path negative;
8. `OnUpdateItemContainerModule` transition path negative;
9. `OnUpdateItemContainer` transition path negative;
10. Stage 4d.0 lifecycle exact-name probes zero;
11. GuildChestModel module route controlled negative;
12. direct `FName::ToString()` — allocator fatal / SIGSEGV;
13. `FName::GetPlainNameString()` — direct dependency on blocked `ToString()`.

The accepted transport pool must continue using bounded planner-selected chest
lookups.

## 15. Current known-safe container path

Runtime-proven safe path:

```text
mature planner chest
    ->
GetItemContainerModule
    ->
GetContainerId / PalContainerId
    ->
PalItemContainerManager.GetContainer
    ->
selected PalItemContainer
    ->
reflected ItemSlotArray
    ->
bounded positive-slot inspection
```

For transport aggregation:

```text
known planner chests
+
same guild
-
requester camp
```

Only.

## 16. Current production continuity reference

Last proven during Stage 4d.8g R3:

```text
production container:
Palworld

PalServer PID:
83

StartedAt:
2026-08-08T18:30:02.661576192Z

RestartCount:
0

internal server UDP:
8211
```

This is a recorded continuity reference, not a claim that the PID can never
change in the future.

The internal UDP 8211 hexadecimal port value is:

```text
2013
```

Do not use the host-published 18211 hexadecimal value when validating the
socket from inside the container.

## 17. Isolated test environment

```text
container:
Palworld-NullPrism-Test

host UDP:
127.0.0.1:18211

container UDP:
8211

admin TCP:
127.0.0.1:35575 -> 25575

restart policy:
no

public lobby:
disabled
```

Known isolated world:

```text
world ID:
6E97A850BDBA49838BCFCC6190C15217

baseline Level SHA256:
a0c0464c33763a021727ae345aadda8df61ed6dd72fe7cd0e147fd965e32acf6

players:
19
```

The isolated server should remain stopped outside explicitly authorized runtime
stages.

## 18. Source and commit discipline

- Windows source must remain unchanged.
- Linux source and runsheet/history/evidence index should be committed together
  whenever Linux source is accepted.
- Static/read-only characterization can use docs-only checkpoints.
- Candidate source is not accepted merely because it compiles.
- Runtime-crashed candidates must be rejected and restored before proceeding.
- Evidence archive SHA256, source SHA256, artifact SHA256, Build ID, and Git
  commit are the authority.
- Push success must be proven before claiming origin is updated.
- Production must remain isolated unless a stage explicitly targets it.

## 19. Immediate next action — Stage 4d.8h

Next stage:

```text
offline PalServer FName resolver + bounded disassembly
```

This remains offline/static with respect to PalServer execution.

Required sequence:

1. re-verify PalServer SHA256:
   `c508a28b06cebf0752296b38da5244c08a5688da44dad8f816eb2d726d82699e`;
2. copy the exact ELF into `palworld-mod-dev`;
3. re-hash the copy;
4. compile a tiny offline helper against the already-proven
   `PsFileResolutionResults` C++ contract;
5. link only against the accepted/pinned `libUE4SS.so`;
6. call `ps_scan_file_ue4ss` exactly once against the copied ELF;
7. record engine version and every returned resolver field;
8. require engine version `5.1`;
9. require `fname_tostring != 0`;
10. require the returned address to fall inside an executable `PT_LOAD`;
11. require direct `objdump --start-address=<returned value>` to decode real
    instructions;
12. only then accept the returned value as an ELF virtual address;
13. disassemble a bounded window around the exact FName ToString
    implementation;
14. follow only bounded directly-called helpers necessary to recover the
    name-index decoder;
15. do not execute PalServer;
16. do not execute `FName::ToString()`;
17. do not modify accepted mod source;
18. if block/index/header/width/length/character layout is fully recovered,
    design a separate one-name runtime probe;
19. if any field remains ambiguous, stop as static incomplete.

## 20. Remaining path to practical success

After safe item-name serialization is solved, the remaining broad work is:

```text
server request hook
    ->
request camp lookup
    ->
bounded same-guild foreign-camp aggregation
    ->
safe item-id string serialization
    ->
IS1 reply send
    ->
Windows-client functional consume test
```

and independently:

```text
periodic/topology-aware registration reconcile
    ->
new same-guild camp created after startup
    ->
expanded registration plan actually executed
```

Final practical success requires both transport functionality and dynamic
post-startup topology reconciliation.
