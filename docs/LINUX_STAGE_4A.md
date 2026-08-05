# Stage 4a: Read-Only Server Discovery

This stage adds native Linux dedicated-server discovery without
changing game state.

## Included

- `UPalUtility::IsDedicatedServer` as the authoritative role gate
- `UPalUtility::IsServer` retained as a diagnostic signal
- `PalPlayerCharacter` preferred as the role world context
- world-change observation and cache reset
- `PalBaseCampModel` enumeration
- reflected `GroupIdBelongTo` access
- fixed 16-byte guild keys
- reflected `ModuleArray` access
- `PalBaseCampModuleItemStorage` discovery
- periodic diagnostic summaries

## Deliberately excluded

- `PalMapObjectManager` raw `TMap` traversal
- chest discovery
- container cross-registration
- `OnAvailableConcreteModel_ServerInternal`
- material counting or consumption
- client transport
- reflected hooks
- PolyHook
- AOB scanning
- object or container mutation

## Acceptance markers

A populated cloned test save should produce:

- `ROLE RESULT=PASS`
- `DISCOVERY RESULT=PASS`
- one or more `GUILD` summaries

An empty isolated save may produce `DISCOVERY RESULT=EMPTY`; that
proves the discovery pass executed but does not validate populated
camp and storage enumeration.

<!-- stage4a-acceptance:start -->
## Stage 4a acceptance: native Linux dedicated-server discovery

### Support contract

This port is intentionally limited to the native Linux Palworld
dedicated server.

Supported:

- Native Linux PalServer.
- NullPrism RE-UE4SS Linux.
- Server-authoritative, read-only discovery.
- Dedicated-server world, camp, guild and storage-module state.

Not supported:

- Palworld clients.
- Listen servers.
- Single-player sessions.
- Windows DLL injection.
- Client HUD or crafting-interface changes.
- Client RPC detours.
- Minted client storage slots.
- Client-to-server transport from the original Windows implementation.

The runtime `IsDedicatedServer` result is the authoritative role gate.
`IsServer` is retained only as a diagnostic because its result can
depend on the world context used for the reflected call.

This stage does not register storage, traverse buildable chests,
redirect inventory access, transport item requests or mutate any
container.

### How the native mod attaches

The PalServer launcher starts the game through NullPrism's Linux
launcher with `LD_PRELOAD` scoped only to the PalServer process. The
preload is not configured globally.

NullPrism loads:

`Pal/Binaries/Linux/libUE4SS.so`

The native mod package is installed at:

`Pal/Binaries/Linux/Mods/ModIntegratedStorageCpp`

The package contains:

- `enabled.txt`
- `dlls/main.so`
- `BUILD-PROVENANCE.txt`

`main.so` links against `libUE4SS.so` and exports the native lifecycle
functions `start_mod` and `uninstall_mod`.

`start_mod` constructs the `CppUserModBase` implementation.
`uninstall_mod` destroys it when NullPrism unloads the native mod.

The implementation receives NullPrism lifecycle callbacks including:

- `on_program_start`
- `on_cpp_mods_loaded`
- `on_unreal_init`
- `on_update`

No hook, detour, AOB scan or incoming chat-text mutation is used by
this stage.

### Dedicated-server execution flow

The periodic `on_update` path performs the following guarded sequence:

1. Locate an Unreal object that can provide a world context.
2. Detect world acquisition or a world transition.
3. Resolve the reflected `IsDedicatedServer` result.
4. Refuse discovery unless the dedicated-server role passes.
5. Throttle discovery so it does not execute on every NullPrism update.
6. Enumerate loaded `PalBaseCampModel` objects.
7. Read each camp's guild identifier and storage-module array.
8. Group discovered storage modules by guild.
9. Emit read-only diagnostic summaries.

Transient empty discovery results are expected while the populated
world is still loading.

### Base-camp enumeration

NullPrism's reflected `FindAllOf` API enumerates:

`PalBaseCampModel`

The returned camp pointers are held in one reusable process-lifetime
`std::vector<UObject*>`.

This lifetime is intentional. `FindAllOf` grows the vector from inside
`libUE4SS.so`. Destroying that vector from `main.so` crossed the
separate C++ runtime and allocator boundary and triggered Unreal's
`MallocBinned2` protection.

The vector is therefore created once, cleared without releasing its
capacity, and reused for later discovery cycles.

### Guild discovery

Each camp's `GroupIdBelongTo` property is read through reflected
property lookup.

The guild identifier is copied as an opaque 16-byte binary value into
`GuildKey`. This avoids assumptions about platform `wchar_t` width and
preserves the native in-memory identifier exactly.

The key is used only for read-only grouping and diagnostics.

### Storage-module discovery

Each camp's `ModuleArray` property is treated as the validated
read-only `RawTArray` representation.

The code rejects arrays with:

- A null data pointer.
- A non-positive element count.
- More than 64 entries.
- A maximum count smaller than the active count.

Each non-null module object is tested against the reflected class:

`/Script/Pal.PalBaseCampModuleItemStorage`

The class is resolved through `StaticFindObject<UClass*>`.

Class matching uses `UStruct*` pointer identity while walking
`GetSuperStruct`. It does not convert an `FName` into a C++ string.

This removes the cross-module temporary-string destruction that caused
the first populated-world allocator crash.

### In-module aggregation

Discovered data is grouped using an in-module
`std::unordered_map<GuildKey, GuildDiscovery>`.

Each `GuildDiscovery` contains an in-module
`std::unordered_set<UObject*>` so duplicate storage-module pointers are
not counted twice.

These containers are created, populated and destroyed entirely inside
`main.so`; they are not passed across the NullPrism shared-library
boundary.

The current diagnostics report:

- Number of camp objects.
- Number of valid camps.
- Number of guilds.
- Number of unique storage modules.
- Null camp entries.
- Missing guild properties.
- Zero guild identifiers.
- Camps without a storage module.
- Per-guild camp and storage counts.

### Linux ABI hardening

Two allocator-boundary failures were identified and removed during
populated-world validation:

1. Class-name comparison caused `FName::ToString` storage to be
   destroyed through the wrong C++ runtime.
2. A local `FindAllOf` vector destroyed storage allocated while
   `libUE4SS.so` populated that vector.

The accepted implementation now uses:

- Reflected `UClass` pointer identity instead of class-name strings.
- A process-lifetime reusable vector for `FindAllOf` output.
- Binary 16-byte guild keys instead of platform-wide strings.
- No client-side hooks, detours or Windows ABI dependencies.

### Populated-world validation

Validated source SHA256:

`ef58b4c4d1333301a148662ce0a755f883883b8156c3ed1a27cb58855938e38f`

Validated Linux `main.so` SHA256:

`92f38aebcabf4d0f93809a2d14e5bdc0dbcc1154c969ab3b298c900426d5acf6`

The isolated populated-world test observed:

- 20 valid camps.
- 8 guilds.
- 20 storage modules.
- 0 null camps.
- 0 missing guild properties.
- 0 zero guild identifiers.
- 0 camps without storage.

The first populated discovery pass occurred approximately 32 seconds
after startup.

The server then survived a 180-second post-pass stability window and
completed 25 discovery cycles with the same 20-camp, 8-guild and
20-storage result.

No nonempty Unreal or NullPrism crash record was produced.

The isolated save changed through normal PalServer runtime saving and
was then restored to the exact baseline hash. The production container
remained running on the same PID with unchanged PalServer and
`libUE4SS.so` hashes.

Validation evidence:

`/mnt/disk1/Development/palworld-linux-mods/runtime-test/evidence/integrated-storage-stage4a2-populated-20260806-054338`

Stage 4a is accepted for its dedicated-server-only, read-only scope.

### Deferred work

The following remain deferred to later stages:

- Read-only buildable chest discovery.
- Safe chest ownership and camp association.
- Cross-camp storage registration.
- Reconciliation after camp or storage changes.
- Server-authoritative item routing.
- Any container or inventory mutation.
- Configuration beyond diagnostic discovery timing.
<!-- stage4a-acceptance:end -->
