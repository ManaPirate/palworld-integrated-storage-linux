# Linux Port Evidence Index

This file is the compact lookup index for major engineering checkpoints and
evidence archives.

For the current state, see [`linux-port-status.md`](linux-port-status.md).
For complete chronological detail, see
[`linux-port-history.md`](linux-port-history.md).

## Authority rules

Evidence should be interpreted in this order:

1. exact evidence archive and its SHA256;
2. exact Git commit and file hashes;
3. detailed chronological history;
4. this compact index;
5. current status summary.

A stage can be accepted as static evidence without being accepted as runtime
proof. A compile success is not runtime acceptance. A planner observation is
not executor proof. A client-side refusal is not a server executor failure.

## Current identities

```text
Engineering checkpoint entering the documentation split:
b5720213f65a8190baba6284f7fb0dcca5e47f9a

Accepted Linux source:
4d8247d7beb1fea72df0d91cfd653dfb016b2d43deff299c3e7439baac984000

Accepted artifact:
10c2b8e3c60ba4e618c6709397c097694255ed7b0174bcdbd1d968e09645c594

Accepted artifact Build ID:
671730ac4ee16633a317409cd1e9c552b19baca3

Windows source:
de89622f5e6831f8ea24650f1f59e0d97580c05bc36e7efadfaae9c9cbc8107c

Build script:
0c31858af8dcd314cccc85e3f6a8b71310e5fba5892c02ec2155aee75aaf9288

Current PalServer:
c508a28b06cebf0752296b38da5244c08a5688da44dad8f816eb2d726d82699e

Current PalServer ELF Build ID:
787f7f8c15edb8fb
```

## Major stage index

| Stage | Classification | Git checkpoint / identity | Evidence / important identity |
|---|---|---|---|
| 4d.5b | Controlled negative | See chronological history | Postmortem evidence `582fadfa947eeae62b874c1d0bf1fb3a44ea1c568309fa74087f99aa72ed0add` |
| 4d.6 | Accepted static parity | See chronological history | Evidence `00b381468e6acf6efccafb352585480752204da7101145fad08171660e0277f4` |
| 4d.7a | Accepted runtime executor | `7761f2507ce08adb1c3635e224132de1c3fa388a` | Postmortem/stability evidence `ba93cf299fccb1c0dce41690b86f56a4dd20a7fa11c69554be5f24f7553a9298` |
| 4d.7b | Client-blocked functional gate | See chronological history | Evidence `1ab94e147e650f02cb98fc1a6416355d9755eb902f2805df3693221f2c665560` |
| 4d.7b screenshots | Supporting observation | — | `cdb1f654795f64f5e4b94478ec9b6c6178e51cd3ef80aae13c60556ad8995e4b` |
| 4d.8 | Accepted static transport parity | See chronological history | Evidence `ba53de5e9e8f05ab7b29c2c4b9f5518cf5c6562e93c68d2d953f5bb8ffb041` |
| 4d.8a R1 | Compile-rejected | Never runtime accepted | Do not rerun; compile-only rejection |
| 4d.8a R2 | Accepted runtime metadata/pool | Source `4d8247d7beb1fea72df0d91cfd653dfb016b2d43deff299c3e7439baac984000` | Artifact `10c2b8e3c60ba4e618c6709397c097694255ed7b0174bcdbd1d968e09645c594`, Build ID `671730ac4ee16633a317409cd1e9c552b19baca3` |
| 4d.8b R1 | Compile-rejected | Never runtime accepted | wchar/char16_t mismatch; do not rerun |
| 4d.8b R2 | Runtime-crashed / rejected | Candidate source `aaa802c6d0891a7a375d93ffe5e25571ebd1e2542e6818a3cfdd5514935d0b3f` | Failure evidence `5c78c00970fbedcf1ff76263fa885e0567eb4242473ba7319a065d8b9e23f834` |
| 4d.8b postmortem | Accepted failure analysis | Recovered later to accepted 4d.8a source/artifact | Evidence `2510093646e775f75cc6ec1eaa80738b8a441c0ec3e2c04d08b904e0a146ba40` |
| 4d.8b recovery | Accepted docs/recovery | `2506c70318b5c838da3b85ea5289a70dd71e96c0` | Accepted 4d.8a source/artifact restored |
| 4d.8c | Accepted static negative | `d1cdebfff2a58011ea64c10e8fd32c82cd501564` | Evidence `f411fce07ab231d43421840a45f17720cc6d1d1b5e19b30d5ace297f1bc13c2b` |
| 4d.8d | Accepted static incomplete | `4144fb98d1578be01ffd60d4cbe85f8f2a8879c6` | Evidence `93cb769883a719361d7f535f130e8cef415d8ab6116b125991f10bc345a26eeb` |
| 4d.8e | Accepted static incomplete | `243057d7e095bc5b26c4af2278fad2b4f5edcbc9` | Evidence `e9d56d887e938f07a6fa2cbcb99a562140a345e45b90f4b119815bd5e5e6c1eb` |
| 4d.8f R1 | Wrapper/pre-flight rejected | No mutation/runtime | Missing dependency working-tree assumption |
| 4d.8f R2 | Accepted static characterization | 4d.8f docs checkpoint parent `243057d7e095bc5b26c4af2278fad2b4f5edcbc9`; subsequent 4d.8f checkpoint `233dccb4356d654a7cef6ebed5f7efea528286cb` | Evidence `7f531bf207e95cc109105c38535c3a2af4a1aad94df2a689a72d3c26e434674f` |
| 4d.8g R1 | Wrapper-rejected / pre-scanner | No scanner/runtime/mutation | Silent host inspection wrapper failure |
| 4d.8g R2 | Wrapper-rejected / pre-scanner | No scanner/runtime/mutation | Host `readelf` unavailable, rc `127` |
| 4d.8g R3 | Accepted static ABI + ELF provenance | `b5720213f65a8190baba6284f7fb0dcca5e47f9a` after its docs checkpoint | Evidence `ab2998c0fca4aadb0168ec88a80f129e27c473d4ce1cbcf522c0dc632997b33c` |

## Critical accepted runtime results

### Stage 4d.7a — full registration executor

```text
planned=285
attempted=285
completed=285
blocked=0
exceptions=0
function_mismatches=0
guild_mismatches=0
camp_mismatches=0
storage_class_mismatches=0
game_thread=1
dedicated=1
metadata=1
RESULT=PASS
```

### Stage 4d.7b — dynamic topology observation

```text
initial:
20 storages / 285 pairs

after new same-guild camp created after startup:
21 storages / 307 pairs
```

The mature planner discovered the change. The one-shot executor did not execute
the expanded 307-pair plan.

### Stage 4d.8a — bounded transport pool

```text
foreign_chests=22
containers=22
slot_arrays=22
slot_objects=850
positive_slots=288
fully_read_slots=288
layout_failures=0
exceptions=0
unique_items=272
total_quantity=69227
passed=1
```

## Critical negative evidence

### Broad container enumeration

```text
FindAllOf("PalItemContainer")
```

is blocked after allocator corruption on the pinned Linux runtime.

### FName::ToString

Stage 4d.8b R2:

```text
LowLevelFatalError
MallocBinned2.cpp
FMallocBinned2 Attempt to realloc an unrecognized block
canary mismatch
Signal 11
Segmentation fault
```

Direct runtime `FName::ToString()` is blocked.

### FName::GetPlainNameString

Stage 4d.8c proved the pinned implementation calls:

```cpp
auto String = FName(Entry).ToString();
```

Therefore it is also blocked.

## Current FName recovery evidence

Stage 4d.8f R2 pinned patternsleuth:

```text
commit:
23d13d7471c854fb15b586deb2f2678a1b7bc690

fname.rs SHA256:
b3a7927e14699ea9fa731ae61a88115b997177f105f9da110b9a54d2a314892f
```

Proven semantics:

```text
UE 4.23+:
FNamePool(pub u64) = direct static address of FNamePool

pre-4.23:
FNamePool(pub u64) = &GNames
```

Current NullPrism/patternsleuth C bridge does not expose FNamePool and the pinned
resolver module does not provide a complete FNameEntry decoder.

## Current offline-scanner evidence

Stage 4d.8g R3 pinned:

```text
PalServer SHA256:
c508a28b06cebf0752296b38da5244c08a5688da44dad8f816eb2d726d82699e

PalServer Build ID:
787f7f8c15edb8fb

patternsleuth_bind gitlink:
ec72ebac946e0237811a8d1a240cf48bde10b590

4d.8g R3 evidence:
ab2998c0fca4aadb0168ec88a80f129e27c473d4ce1cbcf522c0dc632997b33c
```

Pinned exported ABI:

```cpp
extern "C" bool ps_scan_file_ue4ss(
    const char* path,
    PsFileResolutionResults* results
);
```

The scanner has not yet been invoked against the current ELF.

## Evidence discipline

When adding a new stage:

- record classification exactly;
- record evidence archive SHA256;
- record source/artifact/Build ID if source changed;
- record commit ID after checkpoint;
- distinguish static evidence from runtime proof;
- record rejected candidates instead of silently replacing them;
- preserve restoration/recovery evidence;
- never claim push success without origin verification.
