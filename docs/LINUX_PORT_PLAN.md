# NullPrism Linux Dedicated-Server Port

## Goal

Build Integrated Storage as a native Linux UE4SS C++ mod for the
Palworld dedicated server running under NullPrism RE-UE4SS-Linux.

The Linux build is dedicated-server only. Windows clients continue
using the upstream Windows DLL.

## Initial Linux scope

Retain:

- Dedicated-server role detection
- Base-camp discovery
- Storage-container discovery
- Guild ownership filtering
- Same-guild container cross-registration
- Periodic reconciliation
- Server-authoritative material consumption
- Existing reflected request/reply transport
- Configuration and diagnostic logging

Exclude from the Linux server build:

- Windows client material-display detours
- PolyHook client trampolines
- Windows x86-64 client AOB signatures
- Client-side temporary inventory injection
- Windows-only module-path and timer APIs

## Compatibility changes

- Windows module discovery -> dladdr
- GetTickCount64 -> std::chrono::steady_clock
- Windows wchar_t assumptions -> RC::CharType / UTF-16-safe access
- SEH cleanup -> RAII
- main.dll -> main.so
- __declspec(dllexport) -> default ELF symbol visibility
- Client-only code compiled out for Linux dedicated builds

## Validation stages

1. Build and load a minimal native NullPrism probe.
2. Build a dedicated-only Integrated Storage skeleton.
3. Confirm role detection and clean lifecycle.
4. Enable read-only camp, guild and storage discovery.
5. Enable cross-registration.
6. Test same-guild isolation.
7. Test crafting and building from remote storage.
8. Verify correct material consumption.
9. Verify new bases and containers.
10. Verify restart and shutdown stability.

## Release principles

- Preserve the upstream MIT licence and attribution.
- Keep upstream main available as the merge base.
- Publish reproducible build instructions.
- Record compatible Palworld and NullPrism versions.
- Do not bundle proprietary game files or generated SDK dumps.
