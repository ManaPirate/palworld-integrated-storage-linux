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
- `be86a58db4df15a756affbc4d3cab9cbb039e939` — feat(linux): identify item-container access owner
- `8c072462bb16740c6449ff0ab43072a6a2c57471` — Stage 4d.4r: map guild storage reflection owners
- `bd19c4e4adcde9e37df262027eefac6d02b7ac57` — Stage 4d.5b: characterize GuildChest null module route

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

