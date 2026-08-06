# Linux Port Engineering Runsheet

This document is the detailed, chronological engineering record for the
native Linux dedicated-server port of Palworld Integrated Storage.

It records:

- What has been inspected
- What has been implemented
- What has been built
- What has been tested
- What failed and why
- What was accepted
- Current hashes and evidence locations
- The exact safety boundary
- What must happen next

This is a living technical runsheet rather than a public-facing project
overview. The repository README contains the high-level project
description.

---

## 1. Current position

### Current accepted stage

```text
Stage 4c.4e — BelongInfo and query parameter layout
```

Accepted commit:

```text
03e39072695e41a84dec13c5854e924f9c2e0726
```

Accepted candidate identity:

```text
Source SHA256:
5da6a23b59df2c068711d9f0399b4abeb05ba1ce743f6bb053b1406b1df37537

Artifact SHA256:
543db05157c815d95a718f9e1d284684dad7b32b20ea12c9e4e98a7dd634a48c

Build ID:
c6204f77b8d6cc55197df3701eeb0cecd50c8046
```

Accepted runtime evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c4e-deep-layout-20260806-121059
```

### What is currently proven

1. Native NullPrism loading and lifecycle operation on the Linux
   dedicated server.
2. Dedicated-server role resolution on the game thread.
3. Populated-world discovery of camps, guilds, camp storages and item
   chests.
4. Deterministic chest-to-camp-to-guild association.
5. A deterministic, read-only cross-camp registration plan.
6. Guarded execution of exactly one registration call in an isolated,
   armed test.
7. Deterministic selection of the planner guild's aggregate
   `UPalGuildItemStorage` through
   `ItemContainer.BelongInfo.GroupId`.
8. Read-only aggregate `ItemSlotArray` discovery with 54 object slots.
9. Exact reflected parameter layouts for the relevant item-container
   manager queries.

### What is not yet proven

1. The exact readable identity and item/count fields on
   `UPalItemSlot`.
2. A reliable semantic before/after fingerprint for registration.
3. That the one registration call changes aggregate guild-storage
   membership or item visibility.
4. Registration idempotency.
5. Removal behavior.
6. Full-plan registration.
7. Reconciliation after chest, camp or guild changes.
8. Persistence across restart.
9. Player-visible integrated crafting or storage behavior.

### Next stage

```text
Stage 4c.4f — read-only UPalItemSlot metadata and aggregate fingerprint probe
```

The next stage must remain unarmed, add no `ProcessEvent` call, and
invoke no reflected query function.

## 2. Repository and branch strategy

### Upstream

```text
Repository:
Sarfflow/palworld-integrated-storage

Licence:
MIT
```

### Linux fork

```text
Repository:
ManaPirate/palworld-integrated-storage-linux

Development branch:
linux/nullprism-dedicated-server
```

### Remote safety

```text
origin:
git@github.com:ManaPirate/palworld-integrated-storage-linux.git

upstream fetch:
git@github.com:Sarfflow/palworld-integrated-storage.git

upstream push:
disabled://upstream-push-is-blocked
```

A push hook blocks accidental pushes to upstream.

### Merge strategy

Development remains on:

```text
linux/nullprism-dedicated-server
```

until the Linux port has:

- Safe registration
- Observable effect
- Idempotency
- Full-plan execution
- Reconciliation
- Populated-world stability
- Restart validation
- Installation and rollback documentation

The branch will eventually be merged normally into the fork's `main`
branch. It will not replace `main` through a force push.

A first usable release is expected to use a Linux-specific tag such as:

```text
v0.1.0-linux
```

---

## 3. Environment

### Production server

```text
Host root:
/mnt/disk1/Servers/Palworld

Container root:
/serverdata/serverfiles

Active binary directory:
/mnt/disk1/Servers/Palworld/Pal/Binaries/Linux

Container binary directory:
/serverdata/serverfiles/Pal/Binaries/Linux

Docker container:
Palworld
```

Production image:

```text
Image:
ghcr.io/ich777/steamcmd:palworld

Image ID:
sha256:c80c48da27724e9c45cf404581af6bdbc29d2cc8de8b76af1ee68db58c6feb7b

Digest:
ghcr.io/ich777/steamcmd@sha256:43d0d487e27edf8992ce5b977b5084360fef21a016231f018dcb7a905be81fa9
```

Container environment:

```text
Debian:
13.2 trixie

Architecture:
x86-64

glibc:
2.41
```

Palworld version during current validation:

```text
v1.0.2.101103
```

### Development environment

```text
Development root:
/mnt/disk1/Development/palworld-linux-mods

Development container:
palworld-mod-dev

Image:
palworld-mod-dev:trixie

Repository:
/workspace/palworld-integrated-storage-linux

NullPrism source:
/workspace/RE-UE4SS-Linux

Live binary directory mount:
/palworld-live:ro
```

Toolchain includes:

- clang
- lld
- cmake
- ninja
- Rust
- GNU binutils

The complete NullPrism project build has not been accepted because the
current Rust toolchain is 1.85 and one dependency requires let-chains
stabilised in Rust 1.88.

The official release loader has been validated and is used as the
authoritative loader artifact.

### Isolated test environment

```text
Root:
/mnt/disk1/Development/palworld-linux-mods/runtime-test

Container:
Palworld-NullPrism-Test

UDP port:
18211

TCP port:
35575

Restart policy:
no

Public lobby:
disabled
```

World ID:

```text
6E97A850BDBA49838BCFCC6190C15217
```

Baseline populated save:

```text
Level.sav SHA256:
a0c0464c33763a021727ae345aadda8df61ed6dd72fe7cd0e147fd965e32acf6

Player saves:
19
```

Normal isolated mod restored after every mutation test:

```text
main.so SHA256:
56efb4928b62b520845ab17d8bb5a2f8be1453e7c73a29c78a0127a4dcf1ed72
```

### Production verification rule

Docker reporting `Running=true` is not sufficient by itself.

Where practical, production checks include:

- `PalServer-Linux-Shipping` child process exists
- UDP 8211 is listening
- Container `StartedAt` remains unchanged
- PalServer PID remains unchanged
- Expected mods load
- No restart-count change

`/proc/<pid>/maps` is not used as a loader-verification source because
container permissions produced false negatives.

---

## 4. NullPrism integration

### Canonical project

```text
https://github.com/NullPrism/RE-UE4SS-Linux
```

Pinned release:

```text
Tag:
linux-v0.1.0

Commit:
5d33654755efed844336497e8a9a15e6716b5d6c
```

Official loader identity:

```text
SHA256:
26dffce875fb771fb2ac2a63325e7effb5551a03a35598810f13d2e6c854a1ff

GNU Build ID:
13ef3e82b23ba8ef677a7aa747d3d725395a4789
```

### Native mod layout

```text
Mods/ModIntegratedStorageCpp/
├── dlls/
│   └── main.so
└── enabled.txt
```

Exports:

```text
start_mod
uninstall_mod
```

The mod derives from:

```text
RC::CppUserModBase
```

### Loader policy

The project must not:

- Install a second UE4SS
- Set global `LD_PRELOAD`
- Replace the validated NullPrism loader
- Modify production loader state during isolated tests

The production `user.sh` uses a process-scoped launcher patch rather than
a global preload.

Production launcher SHA256:

```text
3ef75beb1407af4f9a41e2d0aa5e2c1fa77b94b114b1a691e5aad5256c9d9ae7
```

---

## 5. Port scope

### Retained server-side behaviour

The Linux port is intended to retain:

- Dedicated-server role validation
- World lifecycle safety
- Camp discovery
- Guild discovery
- Camp storage-module discovery
- Chest discovery
- Chest ownership association
- Same-guild cross-camp registration
- Reconciliation
- Configuration
- Diagnostics
- Server-authoritative shared storage behaviour

### Excluded behaviour

The Linux dedicated-server port excludes:

- Windows API dependencies
- PolyHook
- x86 executable AOB hooks
- Structured Exception Handling
- Windows `wchar_t` assumptions
- Windows DLL export declarations
- Client inventory detours
- Client crafting UI hooks
- Client display-slot injection
- HUD changes
- RPC transport from the combined Windows client/server mod
- Listen-server support
- Single-player support

The upstream Windows implementation remains preserved in:

```text
src/dllmain.cpp
```

The Linux port must not overwrite or erase the upstream implementation.

---

## 6. Technical architecture

### Worker and game-thread split

NullPrism's normal update callback is a worker thread.

This was verified in Stage 4b.2a:

```text
initialized=1
game=0
```

Therefore:

- Worker-side update observes world state and requests work
- EngineTick executes Unreal calls
- `ProcessEvent` must not run from the worker callback

### EngineTick callback

NullPrism APIs used:

```text
IsEngineTickAvailable()
IsProcessEventAvailable()
RegisterEngineTickPreCallback(...)
UnregisterCallback(...)
```

Callback type:

```text
std::function<
    void(
        TCallbackIterationData<void>&,
        UEngine*,
        float,
        bool
    )
>
```

Callback ID:

```text
GlobalCallbackId = uint64_t
ERROR_ID = 0
```

`UnregisterCallback` must not be called inside the callback because that
can deadlock.

The mod unregisters in the destructor and waits for callback and
association activity to become quiescent.

### Process-lifetime module pin

NullPrism's wrapper can release the original shared-library handle after
`uninstall_mod`.

Callback garbage collection can destroy stored `std::function` objects
later.

If `main.so` were unloaded first, the callback destructor or invoker could
point into unmapped code.

The mod therefore:

1. Uses `dladdr` on its own symbol
2. Obtains its native library path
3. Calls `dlopen(path, RTLD_NOW | RTLD_LOCAL)`
4. Retains that additional handle for the PalServer process lifetime
5. Never calls `dlclose` on the retained handle

This intentionally disables native hot reload, which NullPrism does not
currently support safely for this callback path.

A standalone diagnostic verified:

```text
MODULE_PIN result=PASS
```

### Cross-DSO allocator precautions

Two crashes were traced to cross-module lifetime and allocator issues.

The following patterns are prohibited:

- Returning temporary strings that are destroyed in another runtime
- Passing a local `std::vector<UObject*>` to `FindAllOf` and allowing it
  to destruct across DSO allocator boundaries
- Passing named callback metadata strings across the mod/loader boundary

Current mitigations:

- Use class-pointer identity rather than `FName::ToString`
- Use process-lifetime heap discovery vectors
- Reuse vectors with `clear()` without releasing capacity
- Pass empty/default callback metadata strings
- Keep callback code mapped through the process-lifetime module pin

---

## 7. Data model

### Guild key

Palworld's guild identifier is handled as:

```cpp
std::array<std::uint8_t, 16>
```

This avoids Linux `wchar_t` width differences and treats the identifier as
binary data.

### Camp discovery

Discovery:

```cpp
FindAllOf(STR("PalBaseCampModel"), camps)
```

Validated fields:

```text
UPalBaseCampModel.ModuleArray
Observed offset: 0x180

GroupIdBelongTo
Observed offset: 0xE4
```

Storage class:

```text
PalBaseCampModuleItemStorage
```

Storage modules are recognised by reflected class-chain pointer identity.

### Chest discovery

Discovery:

```cpp
FindAllOf(STR("PalMapObjectItemChestModel"), chests)
```

Ownership function:

```text
GetBaseCampModelBelongTo
```

Ownership is resolved on the Unreal game thread.

The implementation:

- Resolves the function by name
- Reads `GetParmsSize()`
- Reads `GetReturnValueOffset()`
- Validates return bounds
- Uses a zeroed byte buffer
- Calls `ProcessEvent`
- Copies the returned `UObject*` through `memcpy`
- Validates the returned camp class

It does not use the raw upstream manager offset path.

### Storage registration function

Function:

```text
OnAvailableConcreteModel_ServerInternal
```

Observed parameter metadata:

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

The Linux implementation does not assume the input offset is zero even
though the current observed offset is zero.

The reflected offset and bounds are validated before each controlled
call.

---

## 8. Accepted commit history

```text
4bd97c9
docs: add NullPrism Linux port plan

ff39c1f
docs: identify project as Linux NullPrism port

1cc4482
feat(linux): add NullPrism lifecycle scaffold

82a65b28c1850532f758f42d0db3d40f86b6554d
feat(linux): validate populated storage discovery

debf0e1f20dbdcc4bff8319a641845ae57761412
feat(linux): associate chests with camps and guilds

b0017c8b48c2e84acdb1de74c5beff146df889fe
fix(linux): resolve dedicated role on game thread

e53faa1adb639858f22220877d374b48b0cef706
feat(linux): validate registration metadata read-only

153a3c35f0d14f4dbd679659daa6a246e18aa165
feat(linux): add deterministic registration planner
```

Stage 4c.3 is pending commit at the time this runsheet version was
prepared.

---

## 9. Stage-by-stage runsheet

## Stage 4a — camp, guild, and storage discovery

### Goal

Build a read-only dedicated-server discovery baseline.

### Implementation

- `std::chrono::steady_clock`
- 16-byte binary guild keys
- Reflected reads of `GroupIdBelongTo`
- Reflected reads of `ModuleArray`
- Storage class pointer identity
- Dedicated-server role detection
- Periodic diagnostic summaries
- No chest traversal
- No hooks
- No RPC
- No mutation

### Initial failures

#### Failure 1: `FName::ToString`

Using `FName::ToString` created a cross-runtime temporary-destruction path.

Fix:

```text
StaticFindObject<UClass*>(
    "/Script/Pal.PalBaseCampModuleItemStorage"
)
```

and class pointer identity.

#### Failure 2: local discovery vector

A local:

```cpp
std::vector<UObject*>
```

was passed to `FindAllOf` and later destructed through a cross-DSO
allocator path.

Fix:

- Process-lifetime heap vector
- Reuse with `clear()`
- Never release capacity during the process

### Accepted populated-world result

```text
Camps:    20
Guilds:   8
Storages: 20
```

Accepted candidate:

```text
Version:
0.1.0-linux-stage4a.2

Source SHA256:
ef58b4c4d1333301a148662ce0a755f883883b8156c3ed1a27cb58855938e38f

Artifact SHA256:
92f38aebcabf4d0f93809a2d14e5bdc0dbcc1154c969ab3b298c900426d5acf6

Build ID:
4ca5c6b413b64445df024f676c71cc57ba9c3544
```

Runtime:

```text
Populated camps:    20
Populated guilds:   8
Populated storages: 20
Stability window:   180 seconds
Crashes:            0
```

---

## Stage 4b.1 — chest object discovery

### Goal

Enumerate candidate chest objects without reading properties or invoking
functions.

### Implementation

- Second process-lifetime heap vector
- `FindAllOf(STR("PalMapObjectItemChestModel"), chests)`
- Count non-null and null pointers only
- No ownership association
- No raw manager offset
- No `ProcessEvent`
- No mutation

### Candidate

```text
Version:
0.1.0-linux-stage4b.1

Source SHA256:
7a69fa3e0e3bab8e95c5fdca377fd0f449e18dcee8a454c4bb9a017a39972239

Artifact SHA256:
9917e342938b1749d2d0b738b9cd808f8f5a4ee9018a7fd58d9ae42db808579e

Build ID:
45643b5e68135810f0726785cdd4497f5f767ea2
```

### Runtime acceptance

```text
Chest objects: 157
Valid:         157
Null:          0
Stable:        180 seconds
Summaries:     26
PASS markers:  24
Crashes:       0
```

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4b1-populated-20260806-062026
```

---

## Stage 4b.2a — callback thread probe

### Goal

Determine whether the normal native mod update callback is the Unreal
game thread.

### Result

```text
initialized=1
game=0
```

Conclusion:

```text
on_update() is a worker thread
```

All chest ownership and future registration `ProcessEvent` calls must be
handed to the EngineTick callback.

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4b2a-thread-20260806-064245
```

---

## Stage 4b.2 — chest-to-camp-to-guild association

### Goal

Resolve:

```text
chest model -> owning camp -> guild
```

without mutation.

### Implementation

- EngineTick callback
- Game-thread validation
- Reflected `GetBaseCampModelBelongTo`
- Reflected parameter size
- Reflected return offset
- Zeroed parameter buffer
- `memcpy` of returned `UObject*`
- Camp class validation
- Guild-key validation
- Reentrancy prevention
- Process-lifetime module pin
- Callback unregister outside callback
- Empty callback metadata strings

### Candidate identity

```text
Version:
0.1.0-linux-stage4b.2

Source SHA256:
62f50bb237b026fbea95e38f50995f649fdac3b9053c0703e938930fdb00cb90

Artifact SHA256:
c0eec3094676d047cd7152cbd0e8827c4a2891cf1323744a8760c7a75c89f582

Build ID:
01ca9ad63e65948fa887b12b53043614abafa4b6
```

### Runtime acceptance

```text
Chest objects:       157
Valid chests:        157
Associated chests:   157
Unassociated chests: 0
Guilds:              8
Chest-owning camps:  17
Missing function:    0
Invalid parameters:  0
Invalid camp:        0
Missing guild:       0
Zero guild:          0
Stable:              180 seconds
Crashes:             0
```

Three of the 20 valid camps have no chest models, explaining the
17 chest-owning camps.

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4b2-retry-20260806-071924
```

Accepted commit:

```text
debf0e1f20dbdcc4bff8319a641845ae57761412
feat(linux): associate chests with camps and guilds
```

---

## Stage 4c.1 — upstream registration-path survey

### Goal

Identify the actual server mutation path before implementing it.

### Key upstream finding

The Windows implementation does not directly mutate `ModuleArray`, chest
containers, or item arrays.

It registers foreign chest concrete models with storage modules:

```cpp
g_srvInjecting = true;

for (auto& guild_entry : fresh)
{
    GuildData& guild = guild_entry.second;

    for (UObject* storage : guild.storages)
    {
        UObject* storage_camp =
            guild.storageCamp[storage];

        for (UObject* model : guild.models)
        {
            if (guild.modelCamp[model] != storage_camp)
            {
                srvCall1(
                    storage,
                    STR(
                        "OnAvailableConcreteModel_"
                        "ServerInternal"
                    ),
                    model
                );
            }
        }
    }
}

g_srvInjecting = false;
```

Important implications:

- Every guild chest is grouped by owning camp
- Every camp storage is grouped by owning camp
- Empty camps with storage are valid targets
- Only foreign-camp pairs are registered
- Registration can re-fire storage events
- A reentrancy guard is required
- Upstream reconciles periodically
- No obvious paired unavailable/removal call was found textually

Textual absence does not prove no removal path exists at runtime.

---

## Stage 4c.1d — read-only registration metadata probe

### Goal

Resolve and inspect the real registration function without calling it.

### Candidate

```text
Version:
0.1.0-linux-stage4c.1d-metaprobe

Source SHA256:
6b79d8d8d0fecf766fdbec5e4037126a0ee19eb2f0c57b49d95e974fcddcf502

Artifact SHA256:
2b131761bedfc49aa1b0f7bab3a15d5018a69f1030c1f370f49e3f45dfe8bb4c

Build ID:
dba6b1d444ee8eed37228cdb3f4665663b23e1f1
```

### Runtime acceptance

A real associated chest and foreign same-guild storage were selected.

Observed on 25 stable passes:

```text
candidate=1
target=1
function=1
parms=8
inputs=1
object_inputs=1
offset=0
size=8
flags=0x18001000000280
property_class=1
compatible=1
```

The function was not invoked.

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c1d-metaprobe-20260806-070751
```

---

## Stage 4c.1e — game-thread role hardening

### Problem

The worker update path called the PalUtility role resolver, which used
`ProcessEvent`.

Role state also used plain integers.

### Fix

- `g_is_server` changed to `std::atomic_int`
- `g_is_dedicated` changed to `std::atomic_int`
- Added `g_role_probe_requested`
- Worker requests role resolution
- EngineTick resolves role
- Added a process-lifetime game-thread role camp buffer
- World reset clears role state and pending request
- Destructor clears pending request
- Dedicated role is authoritative
- Server role remains diagnostic

### Candidate

```text
Version:
0.1.0-linux-stage4c.1e-role-thread

Source SHA256:
36d186f648c4d8b1b97648a7fba64ef9b21c9a4b80961b2f7bb3a72da372ea1e

Artifact SHA256:
83f7e747888beb6e952b856418e39f036c7210e478cc9b7d5664aa7b85f81f63

Build ID:
cb7436c3e1b220ec18c0673dc990534a51c8421d
```

### Runtime acceptance

```text
ROLE THREAD=GAME
ROLE server=1 dedicated=1
ROLE RESULT=PASS
```

Chest association remained:

```text
157 / 157 associated
```

No:

- Post-pass no-context markers
- Not-dedicated markers
- Role mismatches
- Invalid thread markers
- Exceptions
- Crashes

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c1e-role-thread-20260806-073648
```

Accepted commit:

```text
b0017c8b48c2e84acdb1de74c5beff146df889fe
fix(linux): resolve dedicated role on game thread
```

---

## Stage 4c.1f — combined role and metadata validation

### Implementation

- `UnrealType.hpp`
- Independent registration-probe camp vector
- `ForEachProperty`
- `CPF_Parm` filtering
- `CPF_ReturnParm` exclusion
- `CastField<FObjectProperty>`
- Reflected property offset
- Reflected property size
- Reflected flags
- Reflected accepted class
- Same-guild foreign-camp candidate selection
- Metadata probe called before chest-run increment
- No registration invocation

### Candidate

```text
Version:
0.1.0-linux-stage4c.1f-role-metaprobe

Source SHA256:
e0d835a01a7bcc65941e88b941e3921848dcb52c8a1104683d981ccd06ad39ba

Artifact SHA256:
8830aa3ccea60c556ffae00204d6008ff9b738ca45dcad46c67fe0860ffc47c7

Build ID:
e4f66b2facd65e928b232ed69cf91ceae7261aaa
```

### Runtime acceptance

```text
ROLE THREAD=GAME:       1
ROLE RESULT=PASS:       1
REG_META PASS:          25
REG_META INCOMPLETE:    0
CHEST_ASSOC PASS:       25
CHEST_ASSOC INCOMPLETE: 0
Invalid thread:         0
Exceptions:             0
Crashes:                0
```

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c1f-combined-20260806-075812
```

Accepted commit:

```text
e53faa1adb639858f22220877d374b48b0cef706
feat(linux): validate registration metadata read-only
```

---

## Stage 4c.2 — deterministic would-register planner

### Goal

Build the complete cross-camp registration plan without invoking the
registration function.

### Implementation

- Enumerate all storage modules, not only the first
- Group associated chest pointers by guild and camp
- Group storage pointers by guild and camp
- Include storage camps with no chest models
- Create a pair only for:
  - Same guild
  - Different camps
- Deduplicate:
  - Chest pointers
  - Storage pointers
  - Exact storage/chest pairs
- Detect:
  - Chest camp conflicts
  - Storage camp conflicts
  - Chest guild conflicts
  - Storage guild conflicts
- Emit per-guild summaries
- Emit global summary
- Generate order-independent XOR and sum fingerprints
- Retain one pair for metadata validation
- Keep registration invocation disabled

### Initial build failure

The first build used:

```cpp
guild_hex.c_str()
```

on:

```cpp
std::array<char, 33>
```

Fix:

```cpp
guild_hex.data()
```

The accepted Stage 4c.1f package was restored automatically on failure.

### Candidate identity

```text
Version:
0.1.0-linux-stage4c.2-would-register

Source SHA256:
12ee389499b6c5a5e944b7c4f34f70b378b775d7527df080ebc1e80cce5b8865

Artifact SHA256:
0d494b86751d317f102812622ecc5ff48b796d8f2f74cdeedaa2e22d41d2b1a3

Build ID:
427acb4e36a79956ce3c96f97473b507f3a697e2
```

Source safety inventory:

```text
ProcessEvent source calls:
2

Registration function calls:
0
```

The two valid `ProcessEvent` calls were:

1. PalUtility role query
2. Chest ownership query

### Runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c2-plan-20260806-083809
```

Results:

```text
ROLE THREAD=GAME:       1
ROLE RESULT=PASS:       1
Planner passes:         25
Planner incomplete:     0
Planner variants:       1
Per-guild lines:        200
Per-guild runs:         25
Unique guild plans:     8
Metadata passes:        25
Metadata incomplete:    0
Chest passes:           25
Chest incomplete:       0
Invalid thread:         0
Exceptions:             0
Crashes:                0
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
Null camps:             0
Invalid camps:          0
Missing guilds:         0
Zero guilds:            0
Camps without storage:  0
```

One guild has only one camp and therefore no foreign target.

Per-guild plan:

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

The fingerprints are process-local because they include Unreal object
addresses.

Accepted commit:

```text
153a3c35f0d14f4dbd679659daa6a246e18aa165
feat(linux): add deterministic registration planner
```

---

## Stage 4c.3 — controlled single registration

### Goal

Make the smallest possible real registration mutation:

```text
one chest -> one foreign same-guild storage
```

### Safety design

The Stage 4c.3 candidate adds:

- A default-disabled normal package
- Adjacent arm file:
  `dlls/main.so.stage4c3-arm`
- A process-lifetime one-shot guard
- Planner-completion validation
- Dedicated-server validation
- Unreal game-thread validation
- Same-guild validation
- Different-camp validation
- Storage-class validation
- Reflected registration metadata validation
- Zeroed parameter buffer
- `memcpy` of chest pointer at reflected offset
- One registration `ProcessEvent` call site
- No full loop
- No reconciliation
- No routing test
- No production deployment

### Candidate identity

```text
Version:
0.1.0-linux-stage4c.3-single-registration

Source SHA256:
3ef9fd7d9c0452ed750fb65fd97811a225b55bea0ca76164b57d65bbb4cfd5f6

Artifact SHA256:
f86c7a27b7b5273a572a835448a035da856bc5c48b5763ef5bc90856da073e32

Build ID:
c10b62417231f2dea11e9256d43d9704c769ae6f
```

Build exports:

```text
start_mod:
0000000000003970

uninstall_mod:
0000000000003ba0
```

Source inventory:

```text
ProcessEvent source calls:
3

Controlled registration ProcessEvent calls:
1
```

### Static build acceptance

Warnings were limited to the previously observed SDK warnings:

- Two `Atomic.hpp` switch warnings
- Two deprecated enum-conversion warnings
- One undefined-inline warning in `UnrealType.hpp`

The build completed and linked.

Verified:

- Stage 4c.3 version string
- Registration function string
- Armed gate marker
- Disabled gate marker
- Called marker
- Blocked marker
- Exception marker
- Arm-file suffix
- Planner marker
- Metadata marker
- Role marker
- No arm file in normal staged package
- No `FName::ToString` import
- No UObject discovery-vector destructor import

### Unarmed runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c3-unarmed-20260806-095324
```

Results:

```text
Gate disabled markers:   1
Gate armed markers:      0
Registration called:     0
Registration blocked:    0
Registration exceptions: 0
Planner passes:          25
Planner incomplete:      0
Metadata passes:         25
Metadata incomplete:     0
Chest passes:            25
Chest incomplete:        0
Invalid thread markers:  0
Other exceptions:        0
Crash markers:           0
```

Final plan remained:

```text
guilds=8
active_guilds=7
chests=157
storages=20
pairs=285
own_camp=157
all conflict and error counts=0
```

The isolated mod and save were restored exactly.

Production remained:

```text
PalServer PID:
171

Container StartedAt:
2026-08-06T08:33:34.425634823Z
```

### Armed runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c3-armed-20260806-100441
```

An arm file was created only inside the isolated candidate:

```text
dlls/main.so.stage4c3-arm
```

Results:

```text
Gate armed markers:         1
Gate disabled markers:      0
Registration detail lines:  1
Registration called:        1
Registration blocked:       0
Registration exceptions:    0
Post-call planner passes:    24
Planner incomplete:         0
Post-call metadata passes:   24
Metadata incomplete:        0
Post-call chest passes:      25
Chest incomplete:           0
Invalid thread markers:      0
Other exception markers:     0
Crash markers:               0
New crash files:             0
```

Controlled call detail:

```text
run=1
plan=1
chest=1
chest_camp=1
target=1
target_camp=1
different_camps=1
same_guild=1
storage_class=1
game_thread=1
dedicated=1
metadata=1
parms=8
offset=0
size=8
```

The server remained stable for 180 seconds after the call.

The one-shot guard prevented additional registration calls across later
scans.

The pre-restoration `Level.sav` hash changed:

```text
e6a3a55f272bcc3535827e4c707e97397bc8726626a13847ff366f71b54cd436
```

This is not treated as proof of registration persistence because a
running Palworld server normally updates its save and no equal-duration
unarmed control save was compared.

After the test:

```text
Restored isolated mod SHA256:
56efb4928b62b520845ab17d8bb5a2f8be1453e7c73a29c78a0127a4dcf1ed72

Restored Level.sav SHA256:
a0c0464c33763a021727ae345aadda8df61ed6dd72fe7cd0e147fd965e32acf6

Restored player saves:
19
```

Production remained unchanged:

```text
PalServer PID:
171

Container StartedAt:
2026-08-06T08:33:34.425634823Z
```

### Stage 4c.3 acceptance statement

Accepted:

- One real reflected registration call completed
- The call used the Unreal game thread
- All planned safety checks passed
- Exactly one call occurred
- The server remained stable for 180 seconds
- No native exception or crash evidence appeared
- The isolated environment was restored
- Production was unchanged

Not accepted yet:

- Observable storage membership change
- Duplicate-call idempotency
- Complete pair registration
- Periodic reconciliation
- Restart persistence
- Stale registration removal
- Production use

---

## 10. Known operational mistakes and lessons

The following failures occurred during development and are retained here
to prevent recurrence.

### Shell and harness issues

- Nested quoting expanded variables under `set -u`
- Large heredocs were mangled by chat rendering
- Embedded Markdown fences ended shell blocks early
- `fc -ln -1` was not suitable for recovering multiline heredocs
- Long terminal echo could look corrupted even when execution succeeded
- Host has no `python3`; Python must run in the development container
- `grep | head | tee` under `set -o pipefail` produced SIGPIPE status 141
- Evidence report generation must not invalidate an otherwise accepted
  runtime test

### Runtime harness issues

- An early harness checked `UE4SS.log` instead of Docker stderr
- Startup checks initially had no grace period
- Initial populated-world runs stopped at transient `EMPTY`
- Save-clone assumptions failed repeatedly
- A broad grep for `archive` matched unrelated `FArchiveState`
- Post-slice summary counts can be one lower than PASS markers when the
  slice begins at the first PASS line rather than its preceding summary

### Source patch issues

- Several patch scripts guessed source shape incorrectly
- Stage 4c.1d had a duplicate anchor
- A conditional marker helper caused literal decay problems
- A correct build was rolled back by an incorrect string-encoding audit
- Stage 4c.2 initially called `.c_str()` on `std::array<char, 33>`
- A survey searched for a contiguous registration string even though the
  C++ source split it across two adjacent literals
- The first Stage 4c.3 audit searched for the complete runtime arm
  filename in source even though source only stored the suffix

### Safety lessons

- Artifact-level `ProcessEvent` import does not prove registration
  mutation because role and ownership queries legitimately import it
- Source call-site inventory is authoritative for controlled mutation
- A successful `ProcessEvent` return does not prove gameplay effect
- Textual absence of a removal function is not proof of runtime absence
- An ambiguous standalone module-pin crash must not be treated as a
  definitive pin failure
- Production must be checked independently before claiming it remained
  unchanged
- Docker `Running` can survive a child process boot loop

---

## 11. Current safety rules

The following rules remain mandatory:

- No global `LD_PRELOAD`
- No second UE4SS installation
- No direct production mutation during development
- No Unreal `ProcessEvent` from worker threads
- No incoming chat `FText` mutation
- No direct `ModuleArray` mutation when a reflected server function exists
- No full 285-pair call loop before effect observability
- No duplicate-call test before effect observability
- No periodic reconciliation before exact-pair idempotency
- No claim of persistence from a changed save hash alone
- No production deployment before rollback and reconciliation are proven
- Preserve upstream MIT attribution
- Preserve `src/dllmain.cpp`
- Keep normal staged mutation candidates unarmed by default
- Snapshot and restore isolated mod and save around every mutation test
- Verify production PID and container start time after every isolated test

---

## 12. Stage 4c.4 plan — effect observability

### Objective

Determine whether the selected target storage exposes a readable state
showing that the selected foreign chest is registered.

### Required survey

Inspect:

- Upstream registration-related fields and helpers
- NullPrism property APIs
- `FArrayProperty`
- `FSetProperty`
- `FMapProperty`
- `FObjectProperty`
- Script array/set/map helpers
- Generated or SDK storage types
- PalServer UTF-16 and ASCII strings
- Possible read-only query functions
- Possible removal or refresh functions

### Candidate observation types

Preferred order:

1. Reflected query function returning membership
2. Reflected array or set of registered concrete models
3. Reflected map keyed by concrete model
4. Read-only count plus exact pointer scan
5. Carefully validated raw container read only when reflection provides
   the container metadata but no helper API

### Required validation

Before a second mutation call:

- Identify exact storage property or query
- Validate property type
- Validate element type
- Validate container bounds
- Read selected chest membership before registration
- Call the one-shot registration
- Read selected chest membership after registration
- Demonstrate a specific before/after change
- Repeat after later scans to confirm retention

### Prohibited during Stage 4c.4

- Full 285-pair mutation
- Periodic reconcile
- Item transfer tests
- Production deployment
- Guessing a raw property offset
- Writing directly into the observed collection
- Calling a second registration until the first effect is observable

### Acceptance target

A future Stage 4c.4 acceptance should report something equivalent to:

```text
Selected chest present before call:
0

Registration call:
1

Selected chest present immediately after call:
1

Selected chest present after stability window:
1

Crashes:
0

Thread violations:
0
```

Only after this result should exact-pair idempotency be tested.

---

## 13. Later stages

### Stage 4c.5 — exact-pair idempotency

- Observe membership before first call
- Call selected pair once
- Observe membership
- Call the same pair a second time
- Observe whether count and membership remain stable
- Confirm no duplicate entry
- Confirm no crash
- Restore isolated save

### Stage 4d — complete planned registration

- Explicit feature gate
- Execute all 285 deduplicated pairs
- Reentrancy guard
- Per-pair success/failure diagnostics
- No periodic reconciliation initially
- Observe resulting storage membership
- Long populated-world stability window
- Full restoration

### Stage 4e — reconciliation

- Periodic rebuild
- Idempotent repeat registration
- World-transition handling
- Camp and guild changes
- New chest handling
- New camp handling
- Stale pointer handling
- Removal-path investigation

### Stage 4f — restart and persistence

- Server restart
- World reload
- Rebuild registration state
- Confirm storage behaviour after restart
- Determine whether registration is runtime-only or serialised

### Stage 4g — user-facing configuration

- Enable/disable
- Reconcile interval
- Diagnostic verbosity
- Explicit dedicated-server guard
- Safe defaults

### First usable release gate

The first usable release requires:

- Observable registration effect
- Exact-pair idempotency
- Full-plan execution
- Reconciliation
- Populated-world stability
- Restart validation
- Installation guide
- Rollback guide
- Troubleshooting guide
- Production acceptance plan

---

## 14. Latest evidence paths

```text
Stage 4b.1:
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4b1-populated-20260806-062026

Stage 4b.2a:
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4b2a-thread-20260806-064245

Stage 4b.2:
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4b2-retry-20260806-071924

Stage 4c.1d:
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c1d-metaprobe-20260806-070751

Stage 4c.1e:
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c1e-role-thread-20260806-073648

Stage 4c.1f:
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c1f-combined-20260806-075812

Stage 4c.2:
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c2-plan-20260806-083809

Stage 4c.3 unarmed:
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c3-unarmed-20260806-095324

Stage 4c.3 armed:
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c3-armed-20260806-100441
```

---

## 15. Stage 4c.4a — class-specific observability survey

### Goal

Narrow the registration-effect search to the actual item-storage and
guild-storage classes.

### Result

The PalServer binary contained no usable full symbol table:

```text
nm lines:      0
mangled names: 46505
```

Only vtable names surfaced for:

```text
UPalBaseCampModuleItemStorage
UPalGuildItemStorage
UPalMapObjectItemStorageModel
UPalMapObjectConcreteModelModuleItemHolderInterface
```

Direct symbol-based disassembly of the registration handlers was
therefore unavailable.

Relevant PalServer strings included:

```text
CachedConcreteModel
ConcreteModel
GetItemContainer
GetItemContainer_ItemContainerAccessInterface
GuildItemStorage
ItemContainer
OnUpdateItemContainerInGuildItemStorage
OwnerConcreteModel
```

The survey made no source, build, runtime, save, or production change.

---

## 16. Stage 4c.4b — runtime reflection metadata

### Goal

Probe known candidate properties and function signatures on the selected
camp storage module, selected chest model, and a discovered
`UPalGuildItemStorage`.

No function was invoked.

### Candidate identity

```text
Version:
0.1.0-linux-stage4c.4b-observability-metadata

Source SHA256:
feaf429efa135c08abb9b4cd3fc814c777836dbea85df89720250ae99803e264

Artifact SHA256:
4176728c6403170f47e5a8f6a02db60ae8d14dc0ea8164e8380999283f1405a6

Build ID:
3a780fd2c9035654207fb076b027159b5dff05ee
```

### Runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c4b-metadata-20260806-110734
```

Results:

```text
UPalGuildItemStorage objects: 9
OBS_META PASS:                1
OBS_META INCOMPLETE:          0
OBS_META EXCEPTION:           0
Registration called:          0
Gate disabled:                1
Invalid thread:               0
Crash markers:                0
```

The selected camp storage module exposed both:

```text
OnAvailableConcreteModel_ServerInternal
OnNotAvailableConcreteModel_ServerInternal
```

Each has:

```text
Parameter bytes: 8
Inputs:          1 UObject
Returns:         0
```

This confirms a reflected paired removal path exists. Earlier statements
that no removal function had surfaced are superseded by this result.

A `UPalGuildItemStorage` object exposed:

```text
Property:
ItemContainer

Kind:
object

Offset:
72

Size:
8
```

The selected camp storage and chest did not expose direct properties
named `GuildItemStorage`, `ItemContainer`, `ConcreteModel`,
`CachedConcreteModel`, or `OwnerConcreteModel`.

The isolated environment was restored exactly and production remained
unchanged.

---

## 17. Stage 4c.4c — item-storage linkage probe

### Goal

Determine whether `UPalMapObjectItemStorageModel` is a separate bridge
between the selected chest and guild storage.

### Candidate identity

```text
Version:
0.1.0-linux-stage4c.4c-item-storage-linkage

Source SHA256:
fba351d81e32d0f9e23686335e67c22fdae1b3856ca5968204c1569cd106091d

Artifact SHA256:
294d500030e60b19493f62c9543dffb3495e73b1324880d878085aa3cd8cbcdd

Build ID:
9fd2d6ad35094c7598f9a426dbaa59f7a03e0c6d
```

### Runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c4c-linkage-20260806-111750
```

Results:

```text
Item-storage models:              157
Valid item-storage models:        157
Selected chest direct matches:    1
Separate linked models:           0
Conflicting links:                0
Guild-storage objects:            9
Valid guild-storage objects:      9
Guild ItemContainer properties:   9
Non-null guild ItemContainers:    9
Distinct guild ItemContainers:    9
Registration called:              0
Invalid thread:                   0
Crash markers:                    0
```

The selected chest itself is already a
`UPalMapObjectItemStorageModel`. There is no separate item-storage-model
bridge object.

The tested property and function names did not expose a direct
`ItemContainer` or `GuildItemStorage` link on the chest model.

### Accepted conclusion

The current observable path is:

```text
selected chest
    -> owning camp
    -> guild identifier
    -> matching UPalGuildItemStorage
    -> UPalGuildItemStorage.ItemContainer
```

The next task is to map each guild-storage object to its owning guild,
most likely through its UObject outer chain, and inspect the matched
guild `ItemContainer` for readable child-container or slot metadata.

The isolated environment was restored exactly and production remained
unchanged.

---

## 18. Stage 4c.4d — aggregate container query metadata

### Goal

Identify a readable aggregate guild-container surface and validate the
metadata needed to map container objects back to guilds.

No query function was invoked.

### Candidate identity

```text
Version:
0.1.0-linux-stage4c.4d-container-query-metadata

Source SHA256:
dfc02ebc8da9bf62d75dce749abf950b9e7973583cde02fecb6235a52b740faa

Artifact SHA256:
b71b262470613d6bc415f36faa3ab626753e9e1ba4cd59ecd8e056d399cc728b

Build ID:
b9ef931c62b0f218f0cae9a61e9158f01f502bf1
```

### Runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c4d-query-metadata-20260806-114102
```

Results:

```text
QUERY_META PASS:       1
QUERY_META INCOMPLETE: 0
QUERY_META EXCEPTION:  0
Registration called:   0
Gate disabled:         1
Invalid thread:        0
Crash markers:         0
```

The aggregate `UPalItemContainer` exposed:

```text
BelongInfo
    kind:   struct
    offset: 216
    size:   32

ItemSlotArray
    kind:         array
    offset:       112
    size:         16
    element size: 16
    inner type:   object
```

The single discovered `UPalItemContainerManager` exposed:

```text
ItemContainerMap_InServer
    kind:   map
    offset: 152
    size:   80
```

The following reflected query layouts were validated:

```text
GetGroupIdByItemContainerId
    parameter bytes:      40
    inputs:               2
    object inputs:        1
    struct inputs:        1
    return values:        1
    struct returns:       1
    first input offset:   0
    first input size:     8
    return offset:        24
    return size:          16

GetGroupIdByItemSlotId
    parameter bytes:      44
    inputs:               2
    object inputs:        1
    struct inputs:        1
    return values:        1
    struct returns:       1
    first input offset:   0
    first input size:     8
    return offset:        28
    return size:          16

GetContainer
    parameter bytes:      24
    inputs:               1
    struct inputs:        1
    object returns:       1
    first input offset:   0
    first input size:     16
    return offset:        16
    return size:          8

TryGetContainer
    parameter bytes:      25
    inputs:               2
    object inputs:        1
    struct inputs:        1
    return values:        1
    first input offset:   0
    first input size:     16
    return offset:        24
    return size:          1
```

### Accepted conclusion

There are now three credible read-only observability surfaces:

1. `UPalItemContainer.BelongInfo`
2. `UPalItemContainer.ItemSlotArray`
3. `UPalItemContainerManager.ItemContainerMap_InServer`

The strongest mapping route is likely:

```text
guild ItemContainer
    -> BelongInfo or container identifier
    -> UPalItemContainerManager
    -> GetGroupIdByItemContainerId
    -> 16-byte guild identifier
```

Before any query invocation, the nested `BelongInfo` layout, map key/value
types, array element type, and every parameter offset must be inspected
explicitly.

The isolated environment was restored exactly and production remained
unchanged.

---

## 19. Stage 4c.4e — BelongInfo and query parameter layout

### Goal

Map the selected planner guild to its `UPalGuildItemStorage`, inspect the
aggregate item-slot array, and validate every reflected manager-query
parameter layout without invoking any query.

### Candidate identity

```text
Version:
0.1.0-linux-stage4c.4e-belong-query-layout

Source SHA256:
5da6a23b59df2c068711d9f0399b4abeb05ba1ce743f6bb053b1406b1df37537

Artifact SHA256:
543db05157c815d95a718f9e1d284684dad7b32b20ea12c9e4e98a7dd634a48c

Build ID:
c6204f77b8d6cc55197df3701eeb0cecd50c8046
```

### Runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c4e-deep-layout-20260806-121059
```

Results:

```text
DEEP_LAYOUT PASS:       1
DEEP_LAYOUT INCOMPLETE: 0
DEEP_LAYOUT EXCEPTION:  0
Registration called:    0
Gate disabled:          1
Invalid thread:         0
Crash markers:          0
```

All nine `UPalGuildItemStorage.ItemContainer` objects exposed:

```text
BelongInfo
    struct: yes

BelongInfo.GroupId
    offset: 8
    size:   16

ItemSlotArray
    array:        yes
    inner object: yes
    slot count:   54
```

Exactly one guild-storage object matched the planner-selected guild:

```text
Selected guild:
20f979c33446e7f1f8cea19499aad71a

Matching storage index:
1

Selected storage matches:
1
```

`GroupId` and `GroupID` resolve to the same reflected member, so the two
reported matches represent aliases for one 16-byte field rather than two
different fields.

No tested container-ID member existed inside `BelongInfo`.

The exact reflected query layouts are:

```text
GetGroupIdByItemContainerId
    parameter bytes: 40

    ordinal 0:
        input UObject*
        offset 0
        size 8

    ordinal 1:
        input struct
        offset 8
        size 16

    ordinal 2:
        return struct
        offset 24
        size 16

GetGroupIdByItemSlotId
    parameter bytes: 44

    ordinal 0:
        input UObject*
        offset 0
        size 8

    ordinal 1:
        input struct
        offset 8
        size 20

    ordinal 2:
        return struct
        offset 28
        size 16

GetContainer
    parameter bytes: 24

    ordinal 0:
        input struct
        offset 0
        size 16

    ordinal 1:
        return UObject*
        offset 16
        size 8

TryGetContainer
    parameter bytes: 25

    ordinal 0:
        input struct
        offset 0
        size 16

    ordinal 1:
        input UObject*
        offset 16
        size 8

    ordinal 2:
        return bool
        offset 24
        size 1
```

### Accepted conclusion

The correct guild aggregate can now be selected deterministically without
calling a manager query:

```text
planner selected guild
    -> UPalGuildItemStorage.ItemContainer
    -> BelongInfo.GroupId
```

The next semantic-effect probe should observe the matched aggregate, but
first the 54 `ItemSlotArray` object elements and
`ItemContainerMap_InServer` key/value layouts must be characterised
read-only.

The isolated environment was restored exactly and production remained
unchanged.

---

## 20. Stage 4c.4f survey — slot-object and manager-map observability

### Survey status

Inspection-only survey completed on:

```text
03e39072695e41a84dec13c5854e924f9c2e0726
```

No source, build, runtime, save or production state was modified.

### Findings

The NullPrism SDK exposes the read-only object extraction API:

```text
FObjectPropertyBase::GetObjectPropertyValue(
    const void* PropertyValueAddress
)
```

The PalServer binary exposes the concrete slot UObject class:

```text
UPalItemSlot
```

Candidate reflected slot properties include:

```text
ItemSlotId
SlotId
ContainerId
ItemContainerId
ItemId
StaticItemId
ItemNum
ItemCount
StackCount
ItemData
```

Candidate reflected slot queries include:

```text
GetSlotId
GetContainerId
GetItemId
GetItemStackCount
GetStackCount
GetStaticItemData
```

The survey did not surface a sufficiently clear public
`FMapProperty` key/value accessor or `FScriptMapHelper` construction
surface in the searched NullPrism headers. Direct iteration of
`ItemContainerMap_InServer` must therefore remain deferred rather than
being guessed.

### Accepted interpretation

The safest next observability surface is the selected guild aggregate's
54-element `ItemSlotArray`.

The next runtime probe should:

1. Extract each array object through
   `FObjectPropertyBase::GetObjectPropertyValue`.
2. Validate each non-null object against `UPalItemSlot`.
3. Report candidate property metadata.
4. Report candidate function parameter layouts without invoking them.
5. Produce a deterministic aggregate slot-object fingerprint using only
   safe object identity, null/non-null state and validated primitive or
   fixed-size reflected fields.
6. Avoid manager-map iteration until the exact API is confirmed.

---

## 21. Immediate next action

Run Stage 4c.4f:

```text
read-only UPalItemSlot metadata and aggregate fingerprint probe
```

Required work:

1. Select the planner guild's aggregate `UPalItemContainer` through
   `BelongInfo.GroupId`.
2. Traverse its 54-element `ItemSlotArray` read-only.
3. Extract slot objects with
   `FObjectPropertyBase::GetObjectPropertyValue`.
4. Confirm the concrete `UPalItemSlot` class.
5. Inspect candidate slot-property metadata, offsets and sizes.
6. Inspect candidate slot-query parameter layouts without invoking any
   function.
7. Compute a deterministic read-only aggregate fingerprint.
8. Keep the package unarmed.
9. Add no `ProcessEvent` call.
10. Do not perform registration, removal, reconciliation, routing or
    item transfer.
