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

### Current accepted engineering state

```text
Latest accepted source/runtime stage:
Stage 4d.8a R2 — read-only transport metadata + bounded foreign-pool probe

Latest accepted functional observation:
Stage 4d.7b — remote-client gate characterization

Latest accepted static/parity stage:
Stage 4d.8 — remote-client transport parity audit
```

Repository parent checkpoint immediately before the Stage 4d.7a source commit:

```text
Branch:
linux/nullprism-dedicated-server

HEAD / origin:
15e5243dbed1b3cc4e8c7cf60d5a1eaa879d7b19

Commit message:
docs: record Stage 4d.6 server parity audit

Working tree before the Stage 4d.7a checkpoint:
src/linux/main.cpp modified only
```

Accepted Stage 4d.7a candidate identity:

```text
Version:
0.1.0-linux-stage4d.7a-arm-gated-full-plan-executor

Source SHA256:
e968bc43d01008808cae58bb7dd9258dc2db2278e5f5ffe017d3fb5349e267b9

Artifact SHA256:
2bbde02085d87d99acce5f0c3f7765e1e95ff6916e875dc25181883cca79c358

Build ID:
8515573a44f3e9acf92a990dbbf67c7b89c35424

Build script SHA256:
0c31858af8dcd314cccc85e3f6a8b71310e5fba5892c02ec2155aee75aaf9288

Windows source SHA256:
de89622f5e6831f8ea24650f1f59e0d97580c05bc36e7efadfaae9c9cbc8107c
```

The exact Git commit created when Stage 4d.7a source + this runsheet are
checkpointed together is emitted by the checkpoint wrapper and becomes the
authoritative repository HEAD/origin after that commit.

The Stage 4d.5b controlled-negative postmortem evidence archive is:

```text
SHA256:
582fadfa947eeae62b874c1d0bf1fb3a44ea1c568309fa74087f99aa72ed0add
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

Current production continuity reference:

```text
PalServer PID:
83

Container StartedAt:
2026-08-08T18:30:02.661576192Z

RestartCount:
0
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

1. Native NullPrism loading and lifecycle operation work on the Linux dedicated
   server. The native mod is a `.so`, exports `start_mod` / `uninstall_mod`, and
   has repeatedly executed inside native Linux PalServer processes.
2. The Linux port has already executed one real reflected call to the same
   server-side registration function used by the upstream Windows implementation:
   `PalBaseCampModuleItemStorage.OnAvailableConcreteModel_ServerInternal(chest)`.
   The call completed without a crash and the isolated environment restored.
3. Dedicated-server role resolution occurs on the Unreal game thread.
4. Populated-world camp, guild, storage, chest, and same-guild foreign-camp
   planning are deterministic.
5. The complete planner contains 157 associated chests, 20 storages, and 285
   deduplicated foreign-camp registration pairs across seven active guilds.
6. A normal physical chest exposes an exact `UPalMapObjectItemContainerModule*`
   through `GetItemContainerModule`.
7. That normal-chest module exposes the chest's exact nonzero 16-byte
   `PalContainerId` through `GetContainerId`.
8. `PalItemContainerManager.GetContainer(PalContainerId)` and
   `TryGetContainer(PalContainerId)` resolve the selected physical chest to an
   already-existing nonnull `PalItemContainer`.
9. `GetGroupIdByItemContainerId(object, PalContainerId)` is an independent
   guild-membership observation surface.
10. The membership query returns the zero Guid for the selected known
    unregistered physical chest and the selected guild Guid for a known
    registered selected-guild storage container.
11. The zero Guid is therefore an absence sentinel for that manager-membership
    layer. It must not be used as a semantic guild ID.
12. Container existence and manager guild membership are separate layers.
    **This does not prove that changing manager guild membership is required for
    Integrated Storage's upstream server-side behaviour.**
13. `GetItemContainerAccess` and `GetItemChestContainerAccess` both return an
    exact 16-byte `PalMapObjectItemContainerAccessInterface`.
14. The two access getters return the same coherent nonnull backing
    UObject/interface pair. The backing UObject is not the physical chest.
15. Stage 4d.2 identified that backing UObject as exact native
    `PalMapObjectItemContainerModule`, with direct native superclass
    `PalMapObjectConcreteModelModuleBase`.
16. `OnAvailableConcreteModel_ServerInternal(chest)` does not, by itself,
    transition the selected chest from zero to nonzero
    `GetGroupIdByItemContainerId` membership. This is a narrow membership result,
    not proof that the upstream registration call has no routing or availability
    effect.
17. `OnReadyItemContainerGuildChest(interface)` does not create the observed
    manager membership by itself.
18. `OnUpdateItemContainerModule(module*)` does not create the observed manager
    membership by itself.
19. `OnUpdateItemContainer(PalItemContainer*)` does not create the observed
    manager membership by itself.
20. Stage 4d.0 found none of twelve binary-derived lifecycle names on the
    physical chest, target storage, item-container module, resolved
    `PalItemContainer`, or `PalItemContainerManager`.
21. Stage 4d.1 found none of those twelve names on the coherent backing UObject
    returned by `GetItemChestContainerAccess`.
22. Stage 4d.3's read-only source/offline-binary survey identified guild-storage
    candidate machinery but did not establish runtime ownership or behaviour.
23. Stage 4d.4's offline ELF symbol-ownership route is rejected/incomplete: the
    PalServer ELF is stripped for the relevant methods and the harness later hit
    the known host-without-Python problem. Its zero method counts are not absence
    proof.
24. Stage 4d.4r resolved five exact reflected owners at runtime without invoking
    any candidate function:
    - `PalMapObjectGuildChestModel:OnUpdateItemContainerInGuildItemStorage`
    - `PalMapObjectItemContainerModule:GetContainerId`
    - `PalBaseCampModuleItemStorage:OnReadyItemContainerGuildChest`
    - `PalItemContainerManager:GetGroupIdByItemContainerId`
    - `PalMapObjectItemContainerAccessInterface:GetItemContainer_ItemContainerAccessInterface`
25. Stage 4d.5 proved the sole object input of
    `PalMapObjectGuildChestModel:OnUpdateItemContainerInGuildItemStorage` is exact
    `PalGuildItemStorage*`.
26. Stage 4d.5/4d.5b discovered 13 exact live
    `PalMapObjectGuildChestModel` objects. All 13 resolve
    `GetItemContainerModule` with the expected reflected layout, but all 13
    return a null module pointer. The normal chest
    `GetItemContainerModule -> GetContainerId` path therefore does not apply to
    live `PalMapObjectGuildChestModel` objects.
27. Stage 4d.5b produced zero model exceptions, zero candidate update/lifecycle
    calls, zero runtime name conversion, zero crash/fatal markers, zero new
    crash directories, exact isolated restoration, and unchanged production.

### Exact reflected layouts now accepted

```text
GetItemContainerModule
ParmsSize=8
return: offset 0, size 8
reflected type: PalMapObjectItemContainerModule*
```

For a normal physical chest the return is nonnull. For the 13 observed live
`PalMapObjectGuildChestModel` instances in Stage 4d.5b the return is null.

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
PalBaseCampModuleItemStorage.OnReadyItemContainerGuildChest
ParmsSize=16
input: exact PalMapObjectItemContainerAccessInterface
```

```text
PalMapObjectGuildChestModel.OnUpdateItemContainerInGuildItemStorage
ParmsSize=8
input: exact PalGuildItemStorage*
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
6. **Standalone `OnAvailableConcreteModel_ServerInternal(chest)` as a manager-
   membership transition test.** It produced no zero-to-guild membership
   transition. Do not reinterpret that narrow negative as proof that upstream
   registration has no Integrated Storage effect.
7. **Standalone `OnReadyItemContainerGuildChest(interface)`.**
   Controlled `NO_TRANSITION` for manager membership; do not repeat standalone.
8. **Standalone `OnUpdateItemContainerModule(module*)`.**
   Controlled `NO_TRANSITION` for manager membership; do not repeat standalone.
9. **Standalone `OnUpdateItemContainer(PalItemContainer*)`.**
   Controlled `NO_TRANSITION` for manager membership; do not repeat standalone.
10. **Offline `nm`/defined-symbol ownership for the stripped PalServer ELF.**
    Stage 4d.4 demonstrated that this is not a viable method-ownership route.
11. The historical no-call `QUERY_ASSEMBLY` incompleteness predates the accepted
    module-to-`PalContainerId` bridge and is not a current blocker.

### Project-health semantic correction

The engineering record had drifted from the upstream behavioural question into
an unproven stronger requirement:

```text
upstream-equivalent Integrated Storage behaviour
    became treated as if it necessarily required
PalContainerId -> GuildId manager-membership transition
```

That equivalence has **not** been proven.

The upstream dedicated-server path recorded in this runsheet registers each
foreign same-guild chest by calling:

```text
storage->OnAvailableConcreteModel_ServerInternal(chest)
```

The Linux port has already executed that call safely once. The later membership
oracle is valuable evidence about one manager layer, but it is not automatically
the acceptance definition for Integrated Storage.

Before any more registration/lifecycle archaeology, the project must return to
upstream server-side semantic parity and define the actual functional effect that
needs to be reproduced.

### What is not yet proven

1. Whether the already-ported upstream
   `OnAvailableConcreteModel_ServerInternal(chest)` call produces the actual
   server-side routing/availability effect needed by Integrated Storage even
   though `GetGroupIdByItemContainerId` remains zero.
2. Whether `PalContainerId -> GuildId` manager membership is required at all for
   upstream-equivalent Integrated Storage behaviour.
3. The exact functional server-side effect of one foreign-camp registration
   pair: e.g. which material/container query begins including the foreign chest.
4. Exact-pair idempotency measured against that functional effect.
5. Full 285-pair registration and reconciliation measured against that effect.
6. Removal/unregistration behaviour.
7. Persistence or reconstruction behaviour across restart.
8. Server-authoritative integrated material routing/consumption.
9. Player-visible integrated-storage behaviour on Linux.

### Next stage

```text
Stage 4d.6 — server-side upstream parity audit
```

Stage 4d.6 is static/read-only. It must compare the dedicated-server path in
`src/dllmain.cpp` against `src/linux/main.cpp` and answer, before another runtime
mutation:

1. What exact upstream server-side discovery, filtering, registration, guard,
   and reconciliation operations exist?
2. Which of those operations are already reproduced in the Linux source?
3. Which upstream server-side dependencies were intentionally client-only and
   therefore correctly omitted?
4. Does the upstream server path ever require or directly create the
   `PalContainerId -> GuildId` membership transition that later experiments used
   as an oracle?
5. What direct functional observable represents successful Integrated Storage
   behaviour on a dedicated server?

No new reflected lifecycle candidate is to be invoked until this parity audit
has defined a concrete missing operation or a functional acceptance test.

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

