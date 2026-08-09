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

