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
Stage 4d.2 — access-owner native class identity
```

Accepted pre-checkpoint repository baseline:

```text
Branch:
linux/nullprism-dedicated-server

HEAD / origin:
260b18bc8a91bc41185e92f22ffbf31df56ce8e2
```

Accepted Stage 4d.2 candidate identity:

```text
Version:
0.1.0-linux-stage4d.2-access-owner-native-class-identity

Source SHA256:
f0c83cfb73711c5fa3d98c4b435cfa46e28f7a15da13f073e1eb4fc847068b19

Build script SHA256:
0c31858af8dcd314cccc85e3f6a8b71310e5fba5892c02ec2155aee75aaf9288

Artifact SHA256:
aeb061c77fea73b055e7a0d88fe7850977894292589d542e75e4284e5e24ed76

Build ID:
ce4320a4141c2cc53dbee0a88122cdd694061bd7
```

Accepted Stage 4d.2 runtime evidence archive SHA256:

```text
5afabb231cb198ffe00fc70c34f7b14725caa873d3596751b435598348920b21
```

Current isolated populated-world baseline remains:

```text
Level.sav SHA256:
a0c0464c33763a021727ae345aadda8df61ed6dd72fe7cd0e147fd965e32acf6

Player saves:
19

Normal isolated mod SHA256:
56efb4928b62b520845ab17d8bb5a2f8be1453e7c73a29c78a0127a4dcf1ed72
```

Current production continuity reference during the accepted late-4c / 4d runs:

```text
PalServer PID:
82

Container StartedAt:
2026-08-06T18:30:02.767711895Z
```

### Mature planner invariant

The populated isolated world converges to:

```text
guilds=8
active_guilds=7
chests=157
storages=20
pairs=285
own_camp=157

duplicate_chests=0
duplicate_storages=0
duplicate_pairs=0
chest_camp_conflicts=0
storage_camp_conflicts=0
chest_guild_conflicts=0
storage_guild_conflicts=0
null_camps=0
invalid_camps=0
missing_guild=0
zero_guild=0
without_storage=0
```

The planner fingerprint is a runtime-family stability check. It is not treated
as a cross-restart semantic identity.

The controlled membership experiments use guild:

```text
20f979c33446e7f1f8cea19499aad71a
```

The selected physical chest's `PalContainerId` must be reacquired dynamically
on every PalServer run. It must not be frozen across restarts.

### What is currently proven

1. Native NullPrism loading and lifecycle operation work on the Linux
   dedicated server.
2. Dedicated-server role resolution occurs on the Unreal game thread.
3. Populated-world camp, guild, storage, chest, and same-guild foreign-camp
   planning are deterministic.
4. The complete planner contains 157 associated chests, 20 storages, and 285
   deduplicated foreign-camp registration pairs across seven active guilds.
5. A physical chest exposes an exact
   `UPalMapObjectItemContainerModule*` through `GetItemContainerModule`.
6. That module exposes the chest's exact nonzero 16-byte `PalContainerId`
   through `GetContainerId`.
7. `PalItemContainerManager.GetContainer(PalContainerId)` and
   `TryGetContainer(PalContainerId)` resolve the selected physical chest to an
   already-existing nonnull `PalItemContainer`.
8. `GetGroupIdByItemContainerId(object, PalContainerId)` is an independent
   guild-membership surface.
9. The exact membership query returns the zero Guid for the selected known
   unregistered physical chest and the selected guild Guid for a known
   registered selected-guild storage container.
10. The zero Guid is therefore an absence sentinel only. It must not be used as
    a semantic guild ID.
11. Container existence and guild membership are separate manager layers.
    The missing operation is guild association, not container creation.
12. `GetItemContainerAccess` and `GetItemChestContainerAccess` both return an
    exact 16-byte `PalMapObjectItemContainerAccessInterface`.
13. The two access getters return the same coherent nonnull backing
    UObject/interface pair. The backing UObject is not the physical chest.
14. The exact ready-callback argument can be assembled by copying the
    game-returned 16-byte interface value verbatim.
15. `OnAvailableConcreteModel_ServerInternal(chest)` does not create guild
    membership by itself.
16. `OnReadyItemContainerGuildChest(interface)` does not create guild
    membership by itself.
17. `OnUpdateItemContainerModule(module*)` does not create guild membership by
    itself.
18. `OnUpdateItemContainer(PalItemContainer*)` does not create guild membership
    by itself.
19. Stage 4d.0 found none of twelve binary-derived lifecycle names on the
    physical chest, target storage, item-container module, resolved
    `PalItemContainer`, or `PalItemContainerManager`.
20. Stage 4d.1 found none of those twelve names on the coherent backing UObject
    returned by `GetItemChestContainerAccess`.
21. That backing UObject matched none of the four originally predicted
    map-object model classes.
22. Stage 4d.2 identified the backing UObject as the exact native
    `PalMapObjectItemContainerModule` class, with direct native superclass
    `PalMapObjectConcreteModelModuleBase`.
23. All 2,005 native Palworld class candidates were queried read-only;
    2,003 resolved, with exactly one exact-class match and exactly one
    direct-superclass match.
24. The access-owner class and object both reported process-local FName
    comparison index `292821`; the direct superclass reported `287367`.
25. Accepted isolated runtime experiments complete the 180-second stability
    window, restore the isolated mod/save exactly, and leave production
    unchanged.

### Exact reflected layouts now accepted

```text
GetItemContainerModule
ParmsSize=8
return: offset 0, size 8
exact type: PalMapObjectItemContainerModule*
```

```text
PalMapObjectItemContainerModule.GetContainerId
ParmsSize=16
return: offset 0, size 16
exact type: PalContainerId
```

```text
PalItemContainerManager.GetContainer
input PalContainerId: offset 0, size 16
return PalItemContainer*: offset 16, size 8
```

```text
PalItemContainerManager.TryGetContainer
input PalContainerId: offset 0, size 16
out PalItemContainer*: offset 16
return bool: offset 24
```

```text
PalItemContainerManager.GetGroupIdByItemContainerId
ParmsSize=40
object input: offset 0, size 8
PalContainerId input: offset 8, size 16
Guid return: offset 24, size 16
```

```text
OnReadyItemContainerGuildChest
ParmsSize=16
input: exact PalMapObjectItemContainerAccessInterface
```

```text
OnUpdateItemContainerModule
ParmsSize=8
input: exact PalMapObjectItemContainerModule*
```

```text
OnUpdateItemContainer
ParmsSize=8
input: exact PalItemContainer*
```

```text
GetItemContainerAccess
GetItemChestContainerAccess

return: offset 0, size 16
exact type: PalMapObjectItemContainerAccessInterface
```

### Permanently blocked or exhausted routes

The following routes must not be casually reopened:

1. **`ItemContainerMap_InServer` / manager-map runtime inspection.**
   Three progressively reduced `FMapProperty` probes produced the same
   allocator-corruption failure followed by signal 11. Do not rerun direct
   manager-map inspection.
2. **Broad reflected graph / `TFieldRange` traversal.**
   This produced allocator corruption. Do not rerun broad graph traversal.
3. **Bulk `FindAllOf("PalItemContainer")` processing.**
   The query returned 9,874 objects, then allocator corruption occurred during
   the subsequent bulk processing path before the first per-container record.
   The bulk processing strategy is blocked. This does not prove the initial
   `FindAllOf` call alone is unsafe.
4. **Fixed selected-chest property guesses.**
   Sixteen exact property names were exhausted with negative results.
5. **Fixed selected-chest accessor guesses.**
   Thirty exact accessor names were exhausted with negative results.
6. **Standalone `OnAvailableConcreteModel_ServerInternal(chest)`.**
   Controlled negative; do not repeat blindly.
7. **Standalone `OnReadyItemContainerGuildChest(interface)`.**
   Controlled `NO_TRANSITION`; do not repeat standalone.
8. **Standalone `OnUpdateItemContainerModule(module*)`.**
   Controlled `NO_TRANSITION`; do not repeat standalone.
9. **Standalone `OnUpdateItemContainer(PalItemContainer*)`.**
   Controlled `NO_TRANSITION`; do not repeat standalone.
10. The historical no-call `QUERY_ASSEMBLY` incompleteness predates the
    accepted module-to-`PalContainerId` bridge and is not a current blocker.

### What is not yet proven

1. The exact operation that creates `PalContainerId -> GuildId` membership.
2. Whether the access-interface backing UObject is the exact same module
   instance returned by `GetItemContainerModule`.
3. How a built guild chest's item-container module and membership differ from
   an ordinary unregistered chest and a known registered storage control.
4. Whether association requires a multi-step setup/registration/lifecycle
   sequence.
5. Removal/unregistration behaviour.
6. Full 285-pair registration and reconciliation.
7. Persistence or reconstruction behaviour across restart.
8. Server-authoritative integrated material routing/consumption.
9. Player-visible integrated-storage behaviour on Linux.

### Next stage

```text
Stage 4d.3 — guild-storage anchor comparison
```

Stage 4d.3 remains read-only. It should compare an ordinary unregistered
physical chest, a built guild chest, and a known registered storage control
through the already accepted module / `PalContainerId` / manager-membership /
access-interface path.

The first new question is whether the object backing
`GetItemChestContainerAccess` is pointer-identical to the exact
`PalMapObjectItemContainerModule` returned by `GetItemContainerModule`.
The comparison must not use manager-map traversal, broad reflected graphs,
bulk `PalItemContainer` processing, lifecycle candidate calls, or production
mutation.

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

- `4bd97c99872be1e549e43373d5b320b28da7612d` — docs: add NullPrism Linux port plan
- `ff39c1fb7cd9ad8fee68b321a22b55871491c11b` — docs: identify project as Linux NullPrism port
- `1cc4482fdf57cb5194b6701630304189262070d1` — feat(linux): add NullPrism lifecycle scaffold
- `82a65b28c1850532f758f42d0db3d40f86b6554d` — feat(linux): validate populated storage discovery
- `0a66c8d20d409a528dd9abeb72a42466cf766938` — docs: explain native Linux dedicated-server port
- `debf0e1f20dbdcc4bff8319a641845ae57761412` — feat(linux): associate chests with camps and guilds
- `b0017c8b48c2e84acdb1de74c5beff146df889fe` — fix(linux): resolve dedicated role on game thread
- `e53faa1adb639858f22220877d374b48b0cef706` — feat(linux): validate registration metadata read-only
- `153a3c35f0d14f4dbd679659daa6a246e18aa165` — feat(linux): add deterministic registration planner
- `6c8f0ffe644da81c210fc635b1101a72ab746464` — feat(linux): validate controlled single registration
- `ea84ea69cbfa351a50cd3da14ff7650895a9df42` — docs: align project overview and port runsheet
- `7a76231e42afd690c4dbf0b0fa3cb8c36cd91cb7` — feat(linux): map storage observability surfaces
- `00ffb619444497da31ba28b0a998358a32d28249` — feat(linux): map aggregate container queries
- `03e39072695e41a84dec13c5854e924f9c2e0726` — feat(linux): map guild aggregate layout
- `f5005b44a8e5d1b120e102259c5c47e38e7a0e60` — docs: repair active Linux port status
- `ea9fbca0ba90f46ad1c1fea6b10d1f9d07da568e` — feat(linux): fingerprint aggregate item slots
- `def2cf9a754ec40d815b1a68f138089bca0c9fa3` — feat(linux): map nested slot identity
- `ab94c5374ce9ecc04839b5b7ace45c052a8bfb9b` — docs: record ordinal identity survey
- `f344f608ba91b7b6cd9f0b591b8cdb46ee7444c1` — feat(linux): map ordinal item identity
- `586d6d76840aba614982560f5ede74b9774de5c6` — feat(linux): validate semantic fingerprint repeatability

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
- Conditional or ternary selection of marker literals decays them to
  `const char*`; literal-only `emit_marker` calls must use explicit branches
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

- A semantic fingerprint containing an embedded `FName` is
  same-process evidence only. Runtime harnesses must not compare it with a
  fingerprint produced by another PalServer process.
- A pre-readiness `SINGLE_REGISTER RESULT=BLOCKED` scan performs no
  `ProcessEvent` call. Harnesses must distinguish registration detail lines
  from the sole `RESULT=CALLED` mutation.

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

### Accepted Stage 4c.4j semantic observation

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/
integrated-storage-stage4c4j-semantic-observation-20260806-161619
```

Accepted result:

```text
SEMANTIC_OBSERVATION RESULT=UNCHANGED
```

### Later accepted evidence identities

The late-Stage-4c experiments are identified by evidence archive SHA256:

```text
Stage 4c.4u:
c110b6ceca9446283abd405914ede80a553234dcf274f34c484111067bae2dc6

Stage 4c.4v:
346b83766b58b54c5f7ae04c08e46f4ed76492f344867b6aba2ebb376faf82e3

Stage 4c.4w:
d78db90fa9cf0a90cd86a742b6faa90d1e963ce758de59777832745d139236b9

Stage 4c.4x:
1d048eb2eac6509551c054881eebc1756e77fbf209b7db0b872436dce9acdad7
```

Read-only Stage 4d discovery evidence archives:

```text
Stage 4d.0:
6f274fb62cf9be7626c6d17843619205308b3a9532ad3113a89a701042f4311a

Stage 4d.1:
5e5fc3901e33e64dabc7ced580ea3bd6a150dc4794f5f1eb91669e18c0a93477

Stage 4d.2:
5afabb231cb198ffe00fc70c34f7b14725caa873d3596751b435598348920b21
```

The normal isolated state remains:

```text
Mod SHA256:
56efb4928b62b520845ab17d8bb5a2f8be1453e7c73a29c78a0127a4dcf1ed72

Level.sav SHA256:
a0c0464c33763a021727ae345aadda8df61ed6dd72fe7cd0e147fd965e32acf6

Player saves:
19
```

The current production continuity reference for the late accepted runs is:

```text
PID:
82

StartedAt:
2026-08-06T18:30:02.767711895Z
```

Earlier accepted evidence paths remain recorded in their chronological stage
sections.

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

## 21. Stage 4c.4f — UPalItemSlot metadata and aggregate fingerprint

### Goal

Traverse the selected guild aggregate's `ItemSlotArray`, validate every
slot object, map safe reflected slot metadata, and produce read-only
fingerprints without invoking any reflected slot function.

### Candidate identity

```text
Version:
0.1.0-linux-stage4c.4f-slot-fingerprint

Source SHA256:
73674561264974031835d9e3eb26396908d93602629038f7f4932ce723e3bb5d

Artifact SHA256:
37f9b9840ecea03d091e1f35da5fd92b515dd0c070d8e0c4878568662aed0d87

Build ID:
9151a143ad76fbb0de356807b7f8c023040cc65e
```

### Runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c4f-slot-fingerprint-20260806-125800
```

Results:

```text
SLOT_FINGERPRINT PASS:       1
SLOT_FINGERPRINT INCOMPLETE: 0
SLOT_FINGERPRINT EXCEPTION:  0
Registration called:         0
Gate disabled:               1
Invalid thread:              0
Crash markers:               0
```

The selected aggregate exposed:

```text
Slot count:
54

Non-null slots:
54

Accepted-array-class matches:
54

UPalItemSlot matches:
54
```

Every slot object produced the same safe-read shape:

```text
Readable candidate fields:
3

Numeric fields:
1

Fixed-size fields:
2
```

The unique reflected slot fields discovered were:

```text
ContainerId
    kind:   struct
    offset: 284
    size:   16

ItemId
    kind:   struct
    offset: 300
    size:   40

StackCount
    kind:   numeric
    offset: 340
    size:   4
```

`ContainerID` aliases `ContainerId`, and `ItemID` aliases `ItemId`.
These aliases account for the five reported property hits while
representing only three unique fields.

The reflected query metadata was:

```text
GetSlotId
    return: struct
    size:   20

GetItemId
    return: struct
    size:   40

GetStackCount
    return: numeric
    size:   4
```

No reflected slot query was invoked.

Fingerprints:

```text
Structural:
20fe26a215ff09e6

Intra-process content:
64202cdd66241928

Cross-restart stability claimed:
no
```

### Accepted interpretation

The aggregate slot objects are a viable semantic-effect observation
surface.

The current content fingerprint safely includes slot structure,
container identity, and stack count. It does not yet include the 40-byte
`ItemId`, because Stage 4c.4f deliberately refused to hash an unknown
large struct.

The nested layouts of `ContainerId`, `ItemId`, and the 20-byte slot ID
must therefore be mapped before a registration before/after comparison
can claim to observe actual item identity.

The isolated environment was restored exactly and production remained
unchanged.

---

## 22. Stage 4c.4g — nested slot-identity layout

### Goal

Map the known nested structures inside `UPalItemSlot.ContainerId`,
`UPalItemSlot.ItemId`, and `PalItemSlotId` without enumerating unknown
field names or invoking reflected functions.

### Candidate identity

```text
Version:
0.1.0-linux-stage4c.4g-slot-identity-layout

Source SHA256:
1ce2136893630ae25077b1bc2671ab3cd8d49dc3229efdfc103710b38ae02b4a

Artifact SHA256:
2bedd3f51e3f656c9e2ef90a7a0a058120235b907197d05ef269a22970cf1e50

Build ID:
f95a40b439d918018339ba6d4b9de7fddc3484f7
```

### Runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c4g-slot-layout-20260806-131852
```

Results:

```text
SLOT_LAYOUT PASS:       0
SLOT_LAYOUT INCOMPLETE: 1
SLOT_LAYOUT EXCEPTION:  0
Registration called:    0
Gate disabled:          1
Invalid thread:         0
Crash markers:          0
```

The result is accepted as a bounded, read-only diagnostic. `INCOMPLETE`
means only that the known candidate names did not expose a member inside
`PalDynamicItemId`; it does not indicate a crash, unsafe call, invalid
thread, or restoration failure.

### Container identity

The runtime `ContainerId` definition matched
`/Script/Pal.PalContainerId`.

```text
PalContainerId
    total size: 16

    Id
        kind:   struct
        offset: 0
        size:   16
        bounds: valid
```

`ID` is an alias of `Id`.

### Slot identity

The known `/Script/Pal.PalItemSlotId` definition is:

```text
PalItemSlotId
    total size: 20

    ContainerId
        kind:   PalContainerId
        offset: 0
        size:   16
        bounds: valid

    SlotIndex
        kind:   numeric
        offset: 16
        size:   4
        bounds: valid
```

`ContainerID` is an alias of `ContainerId`.

### Item identity

The runtime `ItemId` definition matched `/Script/Pal.PalItemId`.

```text
PalItemId
    total size: 40

    StaticId
        kind:   FName
        offset: 0
        size:   8
        bounds: valid

    DynamicId
        kind:   PalDynamicItemId
        offset: 8
        size:   32
        bounds: valid
```

`StaticID` and `DynamicID` are aliases of the same respective fields.

The known `/Script/Pal.PalDynamicItemId` definition exists and is 32
bytes, but none of the tested names resolved:

```text
Guid
Id
Value
InstanceId
UniqueId
LocalId
Index
Type
```

### Accepted interpretation

The stable top-level slot identity layout is now understood.

The remaining blocker is not the object model or outer layouts; it is
the unnamed ordinal field structure inside:

1. the 16-byte struct at `PalContainerId.Id`; and
2. the 32-byte `PalDynamicItemId`.

The safe next approach is ordinal `TFieldIterator<FProperty>` metadata:
log only ordinal, offset, size, property kind, and nested-struct identity.
Do not call `GetName()` or `FName::ToString`, and do not hash unknown
whole-struct storage.

The isolated environment was restored exactly and production remained
unchanged.

---

## 23. Stage 4c.4h survey — ordinal nested-field API and identity types

### Goal

Confirm that the remaining unknown identity structures can be inspected
by ordinal without converting field names or reading unknown whole
structs.

### Survey status

Inspection-only survey completed on:

```text
def2cf9a754ec40d815b1a68f138089bca0c9fa3
```

No source, staged package, runtime, save, or production state was
modified.

### TFieldIterator result

`TFieldIterator<FProperty>` supports the required name-free traversal:

```text
constructor:
TFieldIterator(
    UStruct*,
    EFieldIterationFlags
)

validity:
explicit operator bool()

advance:
operator++()

dereference:
operator*()
operator->()
```

Using `EFieldIterationFlags::None` restricts traversal to direct fields
without inherited, deprecated, or interface fields.

### Safe metadata surface

Each ordinal property can be inspected through:

```text
GetOffset_Internal()
GetSize()
GetElementSize()
GetArrayDim()
IsInContainer()
CastField<...>()
FStructProperty::GetStruct()
UStruct::GetPropertiesSize()
```

No field name is required.

### Name-conversion boundary

`FField::GetName()` and `FFieldClassVariant::GetName()` internally call
`FName::ToString`.

The runtime probe must therefore avoid:

```text
GetName()
GetFName().ToString()
field-name enumeration
```

### Identity-type evidence

The server binary exposes native struct-ops for:

```text
FGuid
FPalContainerId
FPalItemSlotId
FPalItemId
FPalDynamicItemId
```

The survey did not surface a literal reflected script path for `FGuid`.
The runtime probe may test `/Script/CoreUObject.Guid` as an optional
pointer-identity candidate, but acceptance must not depend on that path
resolving.

### Accepted interpretation

An ordinal-only runtime probe is safe and appropriate.

It should map:

1. the 16-byte structure nested at `PalContainerId.Id`;
2. the 32-byte `PalDynamicItemId`;
3. `PalItemSlotId` as a control layout;
4. `PalContainerId` and `PalItemId` as outer controls.

For each direct field, log only ordinal, offset, size, element size,
array dimension, property kind, nested struct size, and optional
known-type pointer matches.

Do not log names and do not read field values yet.

---

## 24. Stage 4c.4h — ordinal nested-field identity layout

### Goal

Map every remaining nested identity component by ordinal without
converting field names or reading values.

### Candidate identity

```text
Version:
0.1.0-linux-stage4c.4h-ordinal-identity-layout

Source SHA256:
2b7bf6862276d0d38e7f531f0dc92ac7cdd98a77716ee20160ade33f5f30fcaa

Artifact SHA256:
4c7e4d233833e946b80f6084dee450666a5311c21347fd0607d99bd8f731b67c

Build ID:
339ebbfd295f9be5db18bed9dbad739a759df755
```

### Runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c4h-ordinal-layout-20260806-134213
```

```text
ORDINAL_LAYOUT PASS:       1
ORDINAL_LAYOUT INCOMPLETE: 0
ORDINAL_LAYOUT EXCEPTION:  0
Registration called:       0
Gate disabled:             1
Invalid thread:            0
Crash markers:             0
```

### Proven layouts

```text
PalContainerId
└─ FGuid, offset 0, size 16
   ├─ numeric, offset 0,  size 4
   ├─ numeric, offset 4,  size 4
   ├─ numeric, offset 8,  size 4
   └─ numeric, offset 12, size 4

PalItemSlotId
├─ PalContainerId, offset 0,  size 16
└─ numeric index, offset 16, size 4

PalItemId
├─ FName, offset 0, size 8
└─ PalDynamicItemId, offset 8, size 32
   ├─ FGuid, offset 0,  size 16
   └─ FGuid, offset 16, size 16
```

All fields were within bounds and all known structure pointers matched.

### Accepted interpretation

A complete same-process semantic fingerprint can safely hash:

1. slot index;
2. the exact 16-byte `PalContainerId`;
3. the exact 40-byte `PalItemId`; and
4. the exact 4-byte numeric `StackCount`.

These exact structures contain no unknown padding. The embedded
`FName` still means the fingerprint must not be claimed stable across
server restarts.

The isolated environment was restored exactly and production remained
unchanged.

---
## 25. Stage 4c.4i — complete semantic fingerprint repeatability

### Goal

Prove that the complete slot identity-and-stack fingerprint established
by Stage 4c.4h is repeatable across multiple snapshots in one idle server
process.

The probe remained read-only and unarmed. It added no `ProcessEvent`
call, invoked no reflected function, and performed no registration.

### Candidate identity

```text
Version:
0.1.0-linux-stage4c.4i-semantic-repeatability

Source SHA256:
dd643713610ee6df4cc2edc8885b39aea1dd669fd6ace15622e7d1c24e81bc09

Artifact SHA256:
d1a813ae83faaf7b8d57d8ae7179e8c79f288f4c77b6cad9b74b02324fa66024

Build ID:
beb86f657164492a8b0fdd4b191261ad35620a3d
```

### Initial build failure and correction

The first generated source attempted to pass a ternary-selected marker
to:

```cpp
emit_marker(const char (&message)[Size])
```

The conditional expression decayed the two string literals to
`const char*`, so Clang could not bind the result to the literal-only
array-reference overload.

The accepted correction kept `emit_marker` unchanged and replaced the
ternary with explicit `if` and `else` branches, preserving compile-time
character arrays at both call sites.

The failed build automatically restored the committed Stage 4c.4h source
and staged package. Because the Unraid host does not provide `python3`,
the generator correction was applied through the development container.

### Runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c4i-semantic-repeatability-20260806-141404
```

All three snapshots were complete and identical:

| Sample | Valid | Slots | Non-null | Fully read | Exceptions | Fingerprint |
|---:|---:|---:|---:|---:|---:|---|
| 0 | 1 | 54 | 54 | 54 | 0 | `27db2634ac8df4c6` |
| 1 | 1 | 54 | 54 | 54 | 0 | `27db2634ac8df4c6` |
| 2 | 1 | 54 | 54 | 54 | 0 | `27db2634ac8df4c6` |

Exact hashed component coverage per snapshot:

```text
PalContainerId bytes:
864

PalItemId bytes:
2160

StackCount bytes:
216
```

These totals correspond exactly to:

```text
54 × 16-byte PalContainerId
54 × 40-byte PalItemId
54 × 4-byte StackCount
```

Repeatability result:

```text
SEMANTIC_REPEATABILITY PASS:       1
SEMANTIC_REPEATABILITY INCOMPLETE: 0
SEMANTIC_REPEATABILITY EXCEPTION:  0
Semantic samples:                  3
Matching samples:                  3
Registration called:               0
Gate disabled:                     1
Invalid thread:                    0
Crash markers:                     0
Intentional stop exit code:        143
```

### Restoration and production isolation

The isolated environment was restored exactly:

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

### Accepted interpretation

The selected guild aggregate has a stable complete semantic baseline
within one idle server process:

```text
slot_count=54
nonnull_slots=54
fully_read_slots=54
fingerprint=27db2634ac8df4c6
```

The baseline is suitable for controlled before/after comparison around
one registration call.

The embedded `FName` remains process-local. Stage 4c.4i therefore makes
no cross-restart stability or persistence claim.

---
## 26. Stage 4c.4j — controlled semantic before/after registration observation

### Goal

Determine whether the existing validated one-shot registration call changes
the selected guild aggregate's complete slot identity-and-stack state.

The candidate reused the existing `.stage4c3-arm` gate and the sole
`target_storage->ProcessEvent(...)` call site. It added no new
`ProcessEvent` call.

### Candidate identity

```text
Version:
0.1.0-linux-stage4c.4j-semantic-observation

Source SHA256:
45ba3f973c9a55056ac4ba4c259eedda08b38ce54354e8ebcf257fc1a023bb89

Artifact SHA256:
063b010391e7af1d2a62aa0931646eafbdff8cdb809fbc14e501e9530d191982

Build ID:
b9ac1d68efbb3d70fffbeffbcf2621f6e8269347
```

The normal staged package remained unarmed.

### Observation design

The armed isolated run captured:

1. One complete semantic baseline before registration.
2. One immediate snapshot after the call returned.
3. Three delayed snapshots at 5, 10, and 15 seconds.
4. A 180-second post-observation stability window.

Every snapshot covered the exact validated representation of all 54 slots:

```text
54 × 16-byte PalContainerId
54 × 40-byte PalItemId
54 × 4-byte StackCount
```

The probe classified results as `CHANGED`, `UNCHANGED`, `INCOMPLETE`, or
`EXCEPTION`.

### Runtime acceptance

Evidence:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4c4j-semantic-observation-20260806-161619
```

Exactly one ready registration call completed:

```text
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

One earlier pre-readiness scan returned `RESULT=BLOCKED`. It had
`plan=0`, performed no `ProcessEvent` call, and did not consume the later
ready registration.

All five snapshots were complete and identical:

| Phase | Delay | Slots | Non-null | Fully read | Exceptions | Fingerprint |
|---|---:|---:|---:|---:|---:|---|
| Baseline | 0 s | 54 | 54 | 54 | 0 | `ca861a76f7cbc1b4` |
| Immediate | 0 s | 54 | 54 | 54 | 0 | `ca861a76f7cbc1b4` |
| Delayed | 5 s | 54 | 54 | 54 | 0 | `ca861a76f7cbc1b4` |
| Delayed | 10 s | 54 | 54 | 54 | 0 | `ca861a76f7cbc1b4` |
| Delayed | 15 s | 54 | 54 | 54 | 0 | `ca861a76f7cbc1b4` |

Result:

```text
SEMANTIC_OBSERVATION RESULT=UNCHANGED
immediate_changed=0
delayed_changed=0
delayed_consistent=1
retained_change=0
cross_restart_stable=0
```

The server remained stable for 180 seconds after the observation. There were
no invalid-thread markers, registration exceptions, other probe exceptions,
crash markers, or new crash files.

### Restoration and production isolation

The isolated environment was restored exactly:

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

### Accepted interpretation

The controlled registration call produced no observable change to the
selected guild aggregate's slot membership, item identities, or stack counts
during the 15-second semantic observation window.

This does not prove that registration had no effect. The effect may instead
exist in container-manager membership, container-to-group ownership,
`BelongInfo`, routing, visibility, or another state surface outside the 54
aggregate slots.

The pre-restoration `Level.sav` hash changed while the server was running,
but that cannot be attributed specifically to registration. No persistence
claim is made.

Registration idempotency remains blocked.

---
## 27. Stage 4c.4k — exact container-membership observability selection

### Goal

Select one exact, semantic, read-only surface capable of reporting the group
associated with a validated `PalContainerId`.

### Accepted surface

Stage 4c.4k selected:

```text
UPalItemContainerManager.GetGroupIdByItemContainerId
```

Accepted reflected layout:

| Field | Accepted value |
|---|---:|
| `ParmsSize` | 40 bytes |
| Inputs | 2 |
| Returns | 1 |
| Object inputs | 1 |
| Struct inputs | 1 |
| Struct returns | 1 |
| First input | offset 0, size 8 |
| Second input | offset 8, size 16 |
| Return | offset 24, size 16 |

The second input is the exact 16-byte `PalContainerId`. The return is a
16-byte group identifier.

Stage 4c.4k selected this surface but did **not** invoke it.

### Rejected observation routes

`UPalItemContainerManager.ItemContainerMap_InServer` is permanently blocked as
a direct runtime observation path.

Three progressively narrower isolated manager-map probes all produced the same
`FMallocBinned2` allocator-corruption failure followed by signal 11:

1. full live-map iteration;
2. reflected map metadata/layout only;
3. key/value property-type access only.

No direct manager-map probe remains in the accepted source or artifact.

`UPalItemContainer.BelongInfo` was also rejected as the primary exact
membership oracle because it exposed a group identifier without a tested exact
container identifier.

Accepted Stage 4c.4k candidate identity:

```text
Source SHA256:
00bc9061532b67efaf260011e53464d2e757c119fdb16dd7e6f3d5985e14610d

Artifact SHA256:
0a74a9ea8ffb3e5bac6d48b75f0cc6fce17459d9429074ea02c10a818e312654

Build ID:
93e1a5b418478bbba66edd53e08bbf89177d29bb
```

Accepted runtime evidence directory:

```text
/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/
integrated-storage-stage4c4k-query-selection-20260806-172555
```

The server remained stable for 180 seconds, the normal staged package remained
unarmed, the isolated mod/save/player state was restored, and production
remained unchanged.

---

## 28. Stage 4c.4l — physical chest to `PalContainerId` bridge

### Accepted result

Stage 4c.4l established the safe semantic identity bridge:

```text
physical chest
  -> GetItemContainerModule
  -> PalMapObjectItemContainerModule*
  -> GetContainerId
  -> exact nonzero PalContainerId
```

Accepted candidate identity:

```text
Version:
0.1.0-linux-stage4c.4l-module-container-id-accessor

Source SHA256:
b43c0d658d692a31c6063316ca51e8259b0d829c717e34f4169589c51d23e838

Build script SHA256:
1d2283f915e7283491b8110a5d0eeabd944c5c375273c705723be10f46dcc789

Artifact SHA256:
fff28a9da91709d654ea670055addbf8f4b71339576de75ae7d814cfdeff0b4d

Build ID:
5a61dad6ccbfe2b4a4f8892963396822e9a53e8d
```

The chest's `PalContainerId` is reacquired dynamically every runtime. Pointer
identity and a previously observed container ID are not reused across
restarts.

---

## 29. Stage 4c.4m — first selected membership-query observation

Using the accepted module-to-`PalContainerId` bridge, the exact selected query
was invoked for the known unregistered physical chest.

Result:

```text
selected physical chest:
PalContainerId = nonzero

membership return:
00000000000000000000000000000000
```

This was the first semantic evidence that the exact zero Guid represents the
absence of group membership for that container.

A positive registered control was still required before accepting that
interpretation.

---

## 30. Stage 4c.4n — negative/positive membership control

The exact query was tested against both known states:

```text
known unregistered selected physical chest
  -> zero Guid
```

```text
known registered selected-guild storage container
  -> 20f979c33446e7f1f8cea19499aad71a
```

Exact layout:

```text
GetGroupIdByItemContainerId
ParmsSize=40

object input:
offset=0
size=8

PalContainerId input:
offset=8
size=16

Guid return:
offset=24
size=16
```

### Accepted semantic conclusion

The exact zero Guid is an **absence sentinel only**.

It must not be treated as a valid semantic guild ID.

This stage established the safe before/after membership oracle used by the
later controlled callback experiments.

---

## 31. Stage 4c.4o — `OnAvailableConcreteModel_ServerInternal` standalone negative

The historically upstream-looking target-storage callback was tested once
against the selected unregistered chest:

```text
target_storage->OnAvailableConcreteModel_ServerInternal(chest)
```

Precondition:

```text
membership = zero
```

Postcondition:

```text
same PalContainerId
membership = zero
target-storage semantic fingerprint = unchanged
```

The server remained stable.

### Conclusion

```text
OnAvailableConcreteModel_ServerInternal(chest)
is not a standalone guild-association primitive.
```

Do not repeat it blindly.

---

## 32. Stage 4c.4p — bounded exact-name function metadata survey

A bounded metadata-only survey inspected 19 exact candidate names across five
already-known objects.

Important target-storage functions found:

```text
OnAvailableConcreteModel_ServerInternal
  ParmsSize=8
  object input

OnReadyItemContainerGuildChest
  ParmsSize=16
  one input

OnUpdateItemContainerModule
  ParmsSize=8
  object input

OnUpdateItemContainer
  ParmsSize=8
  object input
```

Important manager functions confirmed:

```text
TryGetContainer
GetContainer
GetGroupIdByItemContainerId
```

No broad reflected graph traversal was required.

---

## 33. Stage 4c.4q — exact parameter identities

Stage 4c.4q classified the important candidate parameters exactly.

```text
OnReadyItemContainerGuildChest
ParmsSize=16
input:
  FInterfaceProperty
  exact interface:
  /Script/Pal.PalMapObjectItemContainerAccessInterface
```

```text
OnUpdateItemContainerModule
ParmsSize=8
input:
  exact PalMapObjectItemContainerModule*
```

```text
OnUpdateItemContainer
ParmsSize=8
input:
  exact PalItemContainer*
```

```text
GetContainer
input:
  PalContainerId at offset 0, size 16
return:
  PalItemContainer* at offset 16, size 8
```

```text
TryGetContainer
input:
  PalContainerId at offset 0, size 16
out:
  PalItemContainer* at offset 16
return:
  bool at offset 24
```

This removed generic `UObject*` guessing from the remaining manager and
target-storage experiments.

---

## 34. Stage 4c.4r — manager resolution proves existence != membership

The selected unregistered physical chest was resolved through both exact
manager paths:

```text
GetContainer(PalContainerId)
TryGetContainer(PalContainerId)
```

Both resolved the same existing nonnull `PalItemContainer` while:

```text
GetGroupIdByItemContainerId(...) = zero Guid
```

A registered selected-guild storage control resolved consistently to its
existing container.

### Major architectural conclusion

```text
container existence != guild membership
```

The target chest's `PalItemContainer` already exists.

The unresolved Integrated Storage operation is association of that existing
`PalContainerId` with the guild, not creation of the container itself.

---

## 35. Stage 4c.4s — exact access-interface getter signatures

The selected chest exposes:

```text
GetItemContainerAccess
GetItemChestContainerAccess
```

Both return an exact 16-byte `FInterfaceProperty` of:

```text
/Script/Pal.PalMapObjectItemContainerAccessInterface
```

No getter invocation was required for this stage.

---

## 36. Stage 4c.4t — access-interface getter runtime control

Both access getters were invoked exactly once.

Each returned a coherent nonnull interface pair:

```text
object_nonnull=1
interface_nonnull=1
coherent=1
object_is_chest=0
```

The two getters returned the same backing UObject and the same interface
pointer.

No registration or membership transition occurred.

### Conclusion

The access value is a real game-produced shared access proxy/interface backed
by a UObject that is not the physical chest.

---

## 37. Stage 4c.4u — exact ready-callback parameter assembly

Stage 4c.4u proved the ready callback's exact 16-byte argument can be assembled
without constructing or guessing an interface value.

The probe:

1. invoked `GetItemChestContainerAccess` exactly once;
2. revalidated the exact callback metadata;
3. copied the game-returned 16-byte interface value verbatim into the callback
   argument buffer;
4. did **not** invoke the callback.

Accepted candidate identity:

```text
Version:
0.1.0-linux-stage4c.4u-ready-callback-assembly

Source SHA256:
a088b12b2be80e37dbe457f5597e286a93da22c1a7f28382ca6a01f2dcc2c0ca

Build script SHA256:
a34d1036bfbf7443a42499a4b0dc4656fb055db4da88006a1e7b735481f1d24b

Artifact SHA256:
aa726a014d1401703d9467b5349d22800409492af9156648786649b8cc945b67

Build ID:
e50ee48a221f956c24a530376b060eebfba7485b
```

Evidence archive SHA256:

```text
c110b6ceca9446283abd405914ede80a553234dcf274f34c484111067bae2dc6
```

Result:

```text
getter_calls=1
callback_calls=0
registration_calls=0
argument_assembly=exact
180-second stability=PASS
```

---

## 38. Stage 4c.4v — ready callback standalone negative

Stage 4c.4v invoked exactly once:

```text
OnReadyItemContainerGuildChest(
    exact game-returned PalMapObjectItemContainerAccessInterface
)
```

Accepted candidate identity:

```text
Version:
0.1.0-linux-stage4c.4v-ready-callback-membership-transition

Source SHA256:
a76768f667eceabd0d31c7ea0e3f436e1195054eede2f5e1d6fd2b48fa6b510a

Build script SHA256:
97e8174c84bb667e2af924090f9954e7aaaffb6363f58bed7baa666df5aaf2e5

Artifact SHA256:
3d52d24528021283f3f7f2de1be7c0e152b6a34d69dab430eacbf29181f35148

Build ID:
f6411069d1b72ec755cfa650aaf66c44ba828ebd
```

Evidence archive SHA256:

```text
346b83766b58b54c5f7ae04c08e46f4ed76492f344867b6aba2ebb376faf82e3
```

Controlled result:

```text
PRE:
PalContainerId = nonzero
membership = zero

callback_calls=1

POST:
same PalContainerId=1
membership = zero

outcome=NO_TRANSITION
```

### Conclusion

`OnReadyItemContainerGuildChest` does not establish guild membership by
itself.

---

## 39. Stage 4c.4w — module-update callback standalone negative

Stage 4c.4w invoked exactly once:

```text
OnUpdateItemContainerModule(
    exact game-returned PalMapObjectItemContainerModule*
)
```

Accepted candidate identity:

```text
Version:
0.1.0-linux-stage4c.4w-update-module-membership-transition

Source SHA256:
cf70117e9747b0d90756d779f8b58b7a99b0fd619bb428715ede2149bb8f544f

Build script SHA256:
769e45db1f592311323f911d91c05f7ba5acb467b6927955958867279a0cd658

Artifact SHA256:
fb181a1473fca9b19de9de011ae14aa992c947b012eae147c423697e94cd5405

Build ID:
2153b864f46078a1ef67fef18161ddbc4598bd9a
```

Evidence archive SHA256:

```text
d78db90fa9cf0a90cd86a742b6faa90d1e963ce758de59777832745d139236b9
```

Controlled result:

```text
same module=1
same PalContainerId=1
POST membership=zero

outcome=NO_TRANSITION
```

### Conclusion

`OnUpdateItemContainerModule` does not establish guild membership by itself.

---

## 40. Stage 4c.4x — container-update callback standalone negative

Stage 4c.4x resolved the selected chest's already-existing container through:

```text
PalItemContainerManager.GetContainer(PalContainerId)
```

and invoked exactly once:

```text
OnUpdateItemContainer(PalItemContainer*)
```

Accepted candidate identity:

```text
Version:
0.1.0-linux-stage4c.4x-update-container-membership-transition

Source SHA256:
d6eab74f0ee5ca668dee9ca2ece95b2bdfd853e366cc91f0108b1e8f37f013df

Build script SHA256:
46a001732e6b238e5907001dec957f5fb18d474ea0e49166f0be2a5940513ed7

Artifact SHA256:
2edf7ba33a860f2cbcd6d2b2e348dc0e3c8c5f59cbaa5aba76ea8b9395741196

Build ID:
dc47e6475143c9992ea16ca5729c2f94abed29ca
```

Evidence archive SHA256:

```text
1d048eb2eac6509551c054881eebc1756e77fbf209b7db0b872436dce9acdad7
```

Controlled result:

```text
same_container=1
same PalContainerId=1
POST membership=zero

outcome=NO_TRANSITION
```

The 180-second stability window passed. No nonempty crash evidence was created
and the isolated world restored exactly.

### Conclusion

`OnUpdateItemContainer(PalItemContainer*)` does not establish guild membership
by itself.

---

## 41. Consolidated Stage 4c conclusions

Stage 4c began with a one-shot target-storage call and no semantic membership
oracle.

It ended with an exact chest/container/membership model.

### Proven physical identity chain

```text
physical chest
  -> GetItemContainerModule
  -> PalMapObjectItemContainerModule*
  -> GetContainerId
  -> PalContainerId
  -> PalItemContainerManager.GetContainer
  -> existing PalItemContainer*
```

### Proven membership chain

```text
PalItemContainerManager.GetGroupIdByItemContainerId(
    object,
    PalContainerId
)
```

Known semantics:

```text
unregistered physical chest
  -> zero Guid

registered selected-guild storage
  -> selected guild Guid
```

Therefore:

```text
container existence and guild membership are separate manager layers.
```

The physical chest's container already exists. The unresolved operation is the
guild association.

### Standalone target-storage callback results

```text
OnAvailableConcreteModel_ServerInternal(chest)
  -> no membership transition

OnReadyItemContainerGuildChest(interface)
  -> NO_TRANSITION

OnUpdateItemContainerModule(module*)
  -> NO_TRANSITION

OnUpdateItemContainer(PalItemContainer*)
  -> NO_TRANSITION
```

The latter three used exact reflected parameter types and game-produced
arguments.

These callbacks are no longer valid standalone-registration hypotheses.

They may still be lifecycle/update notifications that expect association to
have been created elsewhere.

### Access-interface result

```text
GetItemContainerAccess
GetItemChestContainerAccess
```

both return the same coherent non-chest backing UObject/interface pair.

The ready-callback argument can be copied verbatim from the game-returned
16-byte interface value.

### Permanent safety exclusions

Do not reopen without a materially different rationale:

```text
direct ItemContainerMap_InServer / FMapProperty inspection
broad reflected graph / TFieldRange traversal
bulk PalItemContainer result processing
16 exhausted fixed property guesses
30 exhausted fixed accessor guesses
standalone ready/module/container update callbacks
```

For the bulk `PalItemContainer` route, the precise finding is:

```text
FindAllOf("PalItemContainer") returned 9,874 objects.
Allocator corruption occurred during subsequent bulk processing before the
first per-container record.
```

The processing strategy is blocked. The initial query alone was not isolated
as the cause.

### Stage 4c final engineering question

The remaining problem is not:

```text
Which target-storage callback should be called?
```

It is:

```text
What operation creates PalContainerId -> GuildId membership?
```

---

## 42. Stage 4d.0 — bounded registration-lifecycle metadata discovery

Stage 4d.0 stopped guessing mutation callbacks.

It resolved the selected unregistered chest through the accepted read-only
module/`PalContainerId`/manager path, then searched twelve binary-derived
lifecycle names across:

```text
physical chest
target storage
item-container module
PalItemContainer
PalItemContainerManager
```

Result:

```text
targets=5
names=12
lookups=60
found=0
parameter_lines=0
candidate_calls=0
```

Evidence archive SHA256:

```text
6f274fb62cf9be7626c6d17843619205308b3a9532ad3113a89a701042f4311a
```

The 180-second stability window passed and the isolated state restored exactly.

---

## 43. Stage 4d.1 — access-owner lifecycle metadata discovery

Stage 4d.1 targeted the coherent backing UObject returned by
`GetItemChestContainerAccess`.

Accepted candidate identity:

```text
Version:
0.1.0-linux-stage4d.1-access-owner-lifecycle-metadata

Source SHA256:
9718e87ed41cc6e4796a42f04c9e8bc860cb0ff04ea64c6a4a05ee2c9123e88c

Build script SHA256:
65c03f728367e8c90be9f7e003eeabbf3972217de50c2b593752be6c84f57aba

Artifact SHA256:
2ad96fc6902eb2a7d5af9b1d1de00c0b309d78fed202a4c975e2927ec020c48e

Build ID:
3c02857ee2acf535a8474117ba175fdeec0b2572
```

Runtime result:

```text
GetItemChestContainerAccess calls=1

object_nonnull=1
interface_nonnull=1
coherent=1
object_is_chest=0

PalMapObjectItemChestModel=0
PalMapObjectItemStorageModel=0
PalMapObjectGuildChestModel=0
PalMapObjectGlobalPalStorageModel=0

classification=OTHER_EXACT_CLASS

lifecycle targets=1
lifecycle names=12
exact lookups=12
found=0
candidate_calls=0
```

Evidence archive SHA256:

```text
5e5fc3901e33e64dabc7ced580ea3bd6a150dc4794f5f1eb91669e18c0a93477
```

No new crash files were created. The isolated mod/save/player state restored
exactly and production remained unchanged.

---

## 44. Stage 4d.2 — access-owner native class identity

### Goal

Identify the exact runtime class of the coherent backing UObject returned by
`GetItemChestContainerAccess` without reopening the known Linux string-lifetime
hazard or any blocked broad-reflection route.

### Candidate identity

```text
Version:
0.1.0-linux-stage4d.2-access-owner-native-class-identity

Repository HEAD:
309d36452f1e9f7df25c78173989d08c37d9e2cd

Source SHA256:
f0c83cfb73711c5fa3d98c4b435cfa46e28f7a15da13f073e1eb4fc847068b19

Build script SHA256:
0c31858af8dcd314cccc85e3f6a8b71310e5fba5892c02ec2155aee75aaf9288

Artifact SHA256:
aeb061c77fea73b055e7a0d88fe7850977894292589d542e75e4284e5e24ed76

Build ID:
ce4320a4141c2cc53dbee0a88122cdd694061bd7
```

### Identification method

At build time, the live PalServer binary's native `UPal*` / `APal*` vtable
symbols were demangled and converted into exact reflected
`/Script/Pal.<Class>` lookups.

Generated candidate set:

```text
native_candidates=2005
vtable_native_class_names=2005
```

Runtime compared only `UClass*` pointers against the access-owner class and its
direct superclass.

No runtime call was made to:

```text
GetName
GetFullName
GetPathName
FName::ToString
```

The accepted ProcessEvent envelope remained:

```text
physical chest: 2
module:         1
manager:        2
target storage: 0
```

Lifecycle lookups and candidate calls remained zero.

### Runtime result

The selected physical chest remained:

```text
ContainerId:
30df4c2d00486b01c5daecae42017e27

membership:
00000000000000000000000000000000
```

The game-returned access interface remained coherent and nonnull.

Native class-resolution result:

```text
native_candidates=2005
lookups=2005
resolved=2003

class_matches=1
class=PalMapObjectItemContainerModule

direct_super_matches=1
direct_super=PalMapObjectConcreteModelModuleBase

identity_resolution=NATIVE_EXACT_CLASS
```

Process-local FName comparison indexes:

```text
access-owner class:
292821

access-owner object:
292821

direct superclass:
287367
```

### Accepted interpretation

`GetItemChestContainerAccess` is backed by an exact native
`PalMapObjectItemContainerModule` UObject.

Its direct native superclass is:

```text
PalMapObjectConcreteModelModuleBase
```

The access owner is therefore not a separate guild-storage proxy class.

Stage 4d.2 does not yet prove that the returned access-owner pointer is
pointer-identical to the module pointer returned by `GetItemContainerModule`;
that becomes the first comparison in Stage 4d.3.

### Stability and restoration

The identity probe completed exactly once.

```text
runtime name conversion=0
lifecycle lookups=0
candidate calls=0
allocator/fatal markers=0
```

The isolated server remained stable for 180 seconds.

One new UE4SS crash filename appeared:

```text
crash_111.log
```

It was zero bytes and contained no fatal marker.

The mature planner remained:

```text
guilds=8
active_guilds=7
chests=157
storages=20
pairs=285
own_camp=157
```

with all duplicate, conflict, null, invalid, missing, zero-guild, and
without-storage counters equal to zero.

The naturally running `Level.sav` changed during the test, then the isolated
state restored exactly:

```text
Restored mod SHA256:
56efb4928b62b520845ab17d8bb5a2f8be1453e7c73a29c78a0127a4dcf1ed72

Restored Level.sav SHA256:
a0c0464c33763a021727ae345aadda8df61ed6dd72fe7cd0e147fd965e32acf6

Restored player saves:
19
```

Production continuity remained:

```text
PID:
83

StartedAt:
2026-08-08T18:30:02.661576192Z
```

Evidence archive SHA256:

```text
5afabb231cb198ffe00fc70c34f7b14725caa873d3596751b435598348920b21
```

---

## 45. Immediate next action

Run Stage 4d.3:

```text
guild-storage anchor comparison
```

Required read-only comparison:

1. ordinary selected unregistered physical chest;
2. one built guild chest;
3. one known registered storage control.

For each object, capture the already accepted semantic chain:

```text
physical/model object
  -> GetItemContainerModule
  -> exact module pointer
  -> GetContainerId
  -> PalContainerId
  -> manager GetContainer
  -> GetGroupIdByItemContainerId
  -> GetItemChestContainerAccess where supported
  -> access-owner pointer and exact native class
```

The first explicit test must compare:

```text
GetItemContainerModule module pointer
vs
GetItemChestContainerAccess backing UObject pointer
```

No mutation is allowed.

Do not reopen:

- direct `ItemContainerMap_InServer` inspection;
- broad graph / `TFieldRange` traversal;
- bulk `PalItemContainer` processing;
- the exhausted fixed property/accessor guesses;
- standalone target-storage lifecycle callbacks.

The purpose is to establish whether the built guild chest exposes the same
module/access-owner architecture but with a native nonzero guild membership,
which would make it a canonical guild-storage association anchor.
