### New evidence: Rust patternsleuth resolver surface

The accepted `libUE4SS.so` symbol survey exposed:

```text
<patternsleuth::resolvers::unreal::fname::FNamePool>::dyn_resolver
<patternsleuth::resolvers::unreal::fname::FNamePool>::resolver
<patternsleuth::resolvers::unreal::fname::FNamePool as core::str::traits::FromStr>::from_str
```

This is a new static lead.

The Stage 4d.8d analyzer scanned C/C++ source extensions only, so these Rust
resolver symbols were not characterized.

This does not yet prove that a usable FNamePool address is exported to the C++
API or that entry reads are safe. It only proves a resolver implementation is
present in the linked loader.

---

---

## 59. Stage 4d.8e — patternsleuth FNamePool resolver audit

### Classification

```text
STATIC / READ-ONLY — ACCEPTED

STATIC_CLASSIFICATION:
RESOLVER_VISIBLE_RESULT_SEMANTICS_INCOMPLETE

ONE_NAME_RUNTIME_PROBE_ELIGIBLE:
0
```

Evidence archive SHA256:

```text
e9d56d887e938f07a6fa2cbcb99a562140a345e45b90f4b119815bd5e5e6c1eb
```

Accepted state remained:

```text
HEAD / origin:
4144fb98d1578be01ffd60d4cbe85f8f2a8879c6

Linux source SHA256:
4d8247d7beb1fea72df0d91cfd653dfb016b2d43deff299c3e7439baac984000

artifact SHA256:
10c2b8e3c60ba4e618c6709397c097694255ed7b0174bcdbd1d968e09645c594

Build ID:
671730ac4ee16633a317409cd1e9c552b19baca3

runsheet SHA256 before this checkpoint:
ad860c63fac716cc783e1c0f15275f012cd8411019bc6f3821af1933411c01ae
```

### Resolver source is present

The pinned NullPrism tree contains the vendored patternsleuth source:

```text
/workspace/RE-UE4SS-Linux/deps/first/patternsleuth
```

and specifically:

```text
patternsleuth/src/resolvers/unreal/fname.rs
```

The source contains:

```text
pub struct FNamePool(pub u64);
```

at the audited file around line 355.

The surrounding comments explicitly discuss:

```text
TStaticIndirectArrayThreadSafeRead<FNameEntry> / &GNames
FNameEntryAllocator::FNameEntryAllocator
FNameEntry buffer initialization
```

Therefore the Stage 4d.8d exported Rust symbols are backed by real pinned source
and are not merely opaque linker names.

### Generic analyzer limitation

The Stage 4d.8e generic Rust analyzer found the macro-generated:

```text
resolver()
dyn_resolver()
```

wrapper bodies in `resolvers/mod.rs`.

Those bodies establish the general resolver factory and environment override
mechanism, but they do not themselves reveal the concrete `FNamePool` resolver
expression passed into the macro from `fname.rs`.

The resulting matrix was:

```text
resolver_source_found=1
resolver_body_count=2
resolver_body_blocked=0

resolver_result_address_signals=2
resolver_pointer_indirection_signals=0
resolver_function_address_signals=0
resolver_pool_data_signals=0
resolver_pattern_signals=1

resolver_pool_address_semantics_visible=0
```

This is why the automatic classification remained:

```text
RESOLVER_VISIBLE_RESULT_SEMANTICS_INCOMPLETE
```

The result does not mean the resolver semantics are absent. It means the
generic body extractor followed the macro expansion surface instead of
capturing the concrete resolver invocation in `fname.rs`.

### No pinned entry decoder or bridge was proven

The generic audit did not establish a usable Rust entry decoder:

```text
entry_decoder_visible=0
entry_decoder_fixed_copy_signals=0
entry_decoder_layout_signals=0
```

and found no C++ / FFI bridge candidate:

```text
bridge_visible=0
bridge_candidates=0
```

Therefore no runtime access is authorized.

### Linked-library correlation

The accepted loader still exposes:

```text
<patternsleuth::resolvers::unreal::fname::FNamePool>::resolver
<patternsleuth::resolvers::unreal::fname::FNamePool>::dyn_resolver
<FNamePool as FromStr>::from_str
```

The loader string table also contains:

```text
PATTERNSLEUTH_RES_FNamePool
```

and an FNamePool-specific pattern/string corpus.

This confirms that the concrete resolver is compiled into the accepted loader.

It does not yet prove:

```text
what the returned u64 represents
whether one extra dereference is required
whether the result is &GNames / NamePoolData / allocator state
whether a safe entry decoder is exposed
whether C++ can retrieve the resolved value
```

### Accepted conclusion

Stage 4d.8e is an accepted static incomplete, not a blocked result.

Current status:

```text
FName::ToString:
BLOCKED

FName::GetPlainNameString:
BLOCKED

C++ lower-level direct entry chain:
INCOMPLETE

patternsleuth FNamePool resolver:
SOURCE PRESENT
COMPILED INTO LOADER
CONCRETE RETURN SEMANTICS NOT YET CHARACTERIZED

runtime FName probe:
NOT AUTHORIZED
```

Do not infer the meaning of `FNamePool(pub u64)` from the type alone.

---

---

## 60. Stage 4d.8f — focused patternsleuth FNamePool audit

### Classification

```text
R1:
PRE-FLIGHT-REJECTED
NO MUTATION
NO BUILD
NO PALSERVER START

R2:
STATIC / READ-ONLY — ACCEPTED
FOCUSED_SOURCE_CAPTURE_COMPLETE_REVIEW_REQUIRED

ONE_NAME_RUNTIME_PROBE_ELIGIBLE:
0
```

R2 evidence archive SHA256:

```text
7f531bf207e95cc109105c38535c3a2af4a1aad94df2a689a72d3c26e434674f
```

Accepted state remained:

```text
HEAD / origin:
243057d7e095bc5b26c4af2278fad2b4f5edcbc9

Linux source SHA256:
4d8247d7beb1fea72df0d91cfd653dfb016b2d43deff299c3e7439baac984000

artifact SHA256:
10c2b8e3c60ba4e618c6709397c097694255ed7b0174bcdbd1d968e09645c594

Build ID:
671730ac4ee16633a317409cd1e9c552b19baca3

runsheet SHA256 before this checkpoint:
23361be3b080c38ac01b180217a8a2650a2206dcc96bbfba717668c24f218dfb
```

### R1 wrapper failure

R1 incorrectly required a checked-out dependency working tree at:

```text
/workspace/RE-UE4SS-Linux/deps/first/patternsleuth
```

The wrapper aborted before any mutation or runtime.

R2 instead used the NullPrism gitlink and retained submodule Git object database.

### Exact pinned patternsleuth identity

NullPrism gitlink:

```text
deps/first/patternsleuth
-> 23d13d7471c854fb15b586deb2f2678a1b7bc690
```

Pinned patternsleuth commit subject:

```text
fix(linux): resolve console manager references in ELF
```

Exact materialized FName resolver source:

```text
patternsleuth/src/resolvers/unreal/fname.rs
```

SHA256:

```text
b3a7927e14699ea9fa731ae61a88115b997177f105f9da110b9a54d2a314892f
```

### FNamePool resolver semantics — proven

Pinned source explicitly states:

```text
FNamePool: resolved address of the global FName storage.

UE 4.23+:
the FNamePool struct's static address.

pre-4.23:
the address of the static pointer to
TStaticIndirectArrayThreadSafeRead<FNameEntry>
(i.e. &GNames).
```

The type is:

```rust
pub struct FNamePool(pub u64);
```

The post-4.23 resolver searches for the real FNamePool constructor static-init
site:

```text
lea rcx, [pool]
call FNamePool ctor
store initialized flag
```

and converts the captured RIP-relative displacement with:

```rust
ctx.image().memory.rip4(a)
```

before returning the unique value.

Therefore, for the post-4.23 branch:

```text
FNamePool(pub u64)
=
direct static address of the FNamePool object

NOT:
constructor address

NOT:
pointer-to-pool requiring an extra dereference
```

The pre-4.23 fallback separately resolves:

```text
&GNames
```

and requires version-aware interpretation.

### Existing patternsleuth C bridge does not expose FNamePool

The pinned `patternsleuth_bind` collector used by NullPrism defines:

```text
UE4SSResolution
```

with fields for:

```text
GUObjectArray
FNameToString
FNameCtorWchar
GMalloc
StaticConstructObjectInternal
FTextFString
EngineVersion
FUObjectHashTablesGet
GNatives
ConsoleManagerSingleton
UGameEngineTick
```

but **not** `FNamePool`.

NullPrism's corresponding C++ `PsScanResults` mirrors the same field list and
also has no FNamePool member.

Therefore the compiled FNamePool resolver is not available through the current:

```text
ps_scan(PsCtx&, PsScanResults&)
```

ABI consumed by NullPrism.

The generic named-resolver machinery exists in Rust and the CLI, but no pinned
C ABI for requesting arbitrary named resolver `FNamePool` was proven.

Do not call Rust-mangled resolver symbols directly from the mod.

### No pinned FNameEntry decoder is present

The focused `fname.rs` audit found FNameEntry references only in:

```text
resolver comments
constructor fingerprints
pre-4.23 GNames description
```

It did not provide:

```text
FNameEntryHeader layout
FNameEntryAllocator layout
block/cursor decoding
entry length decoder
wide/ANSI decoder
caller-owned name-copy function
```

No `GetUnterminatedName` / fixed-buffer decoder implementation was recovered.

Therefore knowing the FNamePool address alone is insufficient to serialize the
bounded pool's FName keys safely.

### Accepted engineering conclusion

Current FName string-recovery matrix:

```text
FName::ToString:
BLOCKED — runtime allocator fatal

FName::GetPlainNameString:
BLOCKED — delegates to ToString

C++ FNamePool API:
INCOMPLETE

patternsleuth FNamePool resolver semantics:
PROVEN

existing NullPrism/patternsleuth_bind bridge for FNamePool:
ABSENT

pinned FNameEntry decoder:
ABSENT

runtime pool/entry probe:
NOT AUTHORIZED
```

Do not infer FNameEntry offsets from generic Unreal versions.

---

---

## 61. Stage 4d.8g — current PalServer ELF and offline-scanner ABI

### Classification

```text
R1:
WRAPPER-REJECTED / PRE-SCANNER
NO SCANNER INVOCATION
NO PALSERVER START
NO FNAME EXECUTION
NO SOURCE MUTATION

R2:
WRAPPER-REJECTED / PRE-SCANNER
failure = host readelf unavailable (rc=127)
NO SCANNER INVOCATION
NO PALSERVER START
NO FNAME EXECUTION
NO SOURCE MUTATION

R3:
STATIC / READ-ONLY — ACCEPTED

STATIC_CLASSIFICATION:
OFFLINE_SCANNER_ABI_SOURCE_CAPTURED_REVIEW_REQUIRED

OFFLINE_SCANNER_INVOCATION_ELIGIBLE:
0
```

R3 evidence archive SHA256:

```text
ab2998c0fca4aadb0168ec88a80f129e27c473d4ce1cbcf522c0dc632997b33c
```

Accepted repository state remained:

```text
HEAD / origin:
233dccb4356d654a7cef6ebed5f7efea528286cb

Linux source SHA256:
4d8247d7beb1fea72df0d91cfd653dfb016b2d43deff299c3e7439baac984000

artifact SHA256:
10c2b8e3c60ba4e618c6709397c097694255ed7b0174bcdbd1d968e09645c594

Build ID:
671730ac4ee16633a317409cd1e9c552b19baca3

runsheet SHA256 before this checkpoint:
5f3c4cc7e2be337018d72c0d3e670adceed889a3849d049cf2909b6ab3646014
```

### Exact current PalServer ELF identity

Authoritative installed binary:

```text
/mnt/disk1/Servers/Palworld/Pal/Binaries/Linux/PalServer-Linux-Shipping
```

Exact identity:

```text
SHA256:
c508a28b06cebf0752296b38da5244c08a5688da44dad8f816eb2d726d82699e

size:
196297880 bytes

ELF Build ID:
787f7f8c15edb8fb

format:
ELF 64-bit LSB executable, x86-64, dynamically linked, stripped
```

R3 verified the same SHA256 from all three views:

```text
host installed file:
c508a28b06cebf0752296b38da5244c08a5688da44dad8f816eb2d726d82699e

production-container mounted file:
c508a28b06cebf0752296b38da5244c08a5688da44dad8f816eb2d726d82699e

temporary dev-container copy:
c508a28b06cebf0752296b38da5244c08a5688da44dad8f816eb2d726d82699e
```

The temporary dev copy was used only for static ELF tooling and was checked
again during the final immutability audit.

### R1 and R2 wrapper failures

R1 likely terminated in a `readelf | awk ... exit` pipeline under
`set -o pipefail`.

R2 instrumented the identity block and proved the actual environmental problem:

```text
readelf: command not found
IDENTITY_STEP=READELF_NOTES failed rc=127
```

on the Unraid host.

R3 corrected the tool boundary:

```text
host:
SHA256 + stat only

production container:
independent SHA256

palworld-mod-dev:
exact copied ELF SHA verification
readelf
objdump
file
```

No host package installation was performed.

### Offline scanner ABI — proven

Pinned `patternsleuth_bind` gitlink:

```text
ec72ebac946e0237811a8d1a240cf48bde10b590
```

The exact source exposes:

```rust
#[no_mangle]
pub unsafe extern "C" fn ps_scan_file_ue4ss(
    path: *const c_char,
    results: *mut PsFileResolutionResults,
) -> bool
```

The result structure is `#[repr(C)]`:

```rust
pub struct PsEngineVersion {
    major: u16,
    minor: u16,
}

pub struct PsFileResolutionResults {
    engine_version: PsEngineVersion,
    guobject_array: u64,
    fname_tostring: u64,
    fname_ctor_wchar: u64,
    gmalloc: u64,
    static_construct_object_internal: u64,
    ftext_fstring: u64,
    fuobject_hash_tables_get: u64,
    gnatives: u64,
    console_manager_singleton: u64,
    gameengine_tick: u64,
}
```

The pinned repository already contains the matching C++ declaration and
consumer in:

```text
tests/PalworldSignatureTests.cpp
```

with:

```cpp
extern "C" bool ps_scan_file_ue4ss(
    const char* path,
    PsFileResolutionResults* results
);
```

Therefore a future offline invocation does not require inventing a ctypes ABI.

The accepted `libUE4SS.so` exports:

```text
ps_scan
ps_scan_file_ue4ss
```

### Offline scan implementation

The pinned implementation performs:

```rust
let data = std::fs::read(path)?;
let image = Image::read(None, &data, Some(path), false)?;
let resolution = image.resolve(UE4SSResolution::resolver())?;
```

and copies the resolver `u64` values directly into `PsFileResolutionResults`.

The existing Palworld signature test requires:

```text
engine version = 5.1
FNameToString != 0
FNameCtorWchar != 0
```

alongside the other required UE4SS resolvers.

### Address interpretation boundary

Stage 4d.8g intentionally did not invoke the offline scanner, so the exact
numeric `fname_tostring` value for the current PalServer is not yet available.

Do not assume RVA versus ELF virtual address by convention.

The next stage must validate returned-address semantics against the exact ELF:

```text
scanner result
    ->
must fall inside a valid executable PT_LOAD virtual-address range
    ->
objdump --start-address at the same value must resolve real instructions
```

If that validation fails, stop rather than applying guessed base arithmetic.

### Final immutability

R3 final audit confirmed:

```text
HEAD / origin unchanged
working tree clean
accepted Linux source unchanged
accepted runsheet unchanged
accepted artifact unchanged

PalServer SHA256 unchanged:
c508a28b06cebf0752296b38da5244c08a5688da44dad8f816eb2d726d82699e

isolated server:
stopped

production PID:
83

production StartedAt:
2026-08-08T18:30:02.661576192Z

production RestartCount:
0
```

### Accepted engineering conclusion

```text
current PalServer identity:
PROVEN

offline scanner C ABI:
PROVEN

matching pinned C++ consumer:
PROVEN

ps_scan_file_ue4ss export:
PROVEN

offline scan invocation:
NOT YET PERFORMED

current PalServer FNameToString address:
NOT YET RESOLVED

FName execution:
0
```

---

## 62. Immediate next action

Run Stage 4d.8h:

```text
offline PalServer FName resolver + bounded disassembly
```

No PalServer runtime.

Required work:

1. Re-verify the exact PalServer SHA256:
   `c508a28b06cebf0752296b38da5244c08a5688da44dad8f816eb2d726d82699e`.
2. Copy the exact ELF into `palworld-mod-dev` and re-verify its SHA.
3. Use the pinned C++ `PsFileResolutionResults` ABI, preferably by compiling a
   tiny offline helper from the repository's existing
   `PalworldSignatureTests.cpp` contract rather than using ctypes.
4. Link only against the accepted/pinned `libUE4SS.so`.
5. Invoke `ps_scan_file_ue4ss` exactly once on the copied ELF.
6. Record every returned field, including engine version and
   `fname_tostring`.
7. Require engine version `5.1` as the pinned test does.
8. Require `fname_tostring != 0`.
9. Validate `fname_tostring` against the exact ELF executable PT_LOAD ranges.
10. Require direct `objdump` disassembly at the returned address to produce
    instructions.
11. Only then classify the returned value as an ELF virtual address.
12. Disassemble a bounded window around FNameToString.
13. Record direct call targets and RIP-relative global references.
14. Recurse only into bounded directly-called helpers necessary to understand
    name-index decoding.
15. Do not execute PalServer or FNameToString.
16. Do not modify the accepted mod source.
17. If the code path proves block/index/header/length/width/character layout,
    design a separate one-name read-only runtime probe.
18. If any decoder field remains ambiguous, stop as static incomplete.
19. Keep `FName::ToString()` runtime invocation blocked.
20. Keep broad `FindAllOf("PalItemContainer")` blocked.
