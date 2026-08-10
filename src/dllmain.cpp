//! IntegratedStorage (C++). Author: Sarfflow
//
// Cross-camp build/craft on a Palworld guild: build or craft at ANY of your guild's camps using materials
// stored in ANY other same-guild camp. One DLL, role-gated to all three ends (dedicated / host-SP / remote
// client). Native ItemStackInfo is NEVER mutated, so native Quick Stack + the Item Retrieval Device keep
// working. Architecture:
//
//   SERVER (authority)  — a ~8s DISCOVERY RECONCILE enumerates every guild chest from the map-object
//                         manager AND every base camp (incl. EMPTY ones) and cross-registers each guild
//                         chest's container into every same-guild camp's storage module. That lets the
//                         native build/craft flow CONSUME cross-camp (and, on a host/SP authority, the
//                         native collector already reads the merged containers -> correct display for free).
//
//   REMOTE CLIENT       — can't see far-camp containers, so it DISPLAYS the guild total by minting local
//                         item slots and array-swapping them into a spare inventory container ("cont5")
//                         only for the duration of the native material scan (3 AOB-located detours). The
//                         per-item pool comes over a custom TRANSPORT CHANNEL (below), never the ISI.
//
//   TRANSPORT CHANNEL   — demand-driven, event-driven, ISI-free: the client tracks its current camp via the
//                         OnEnterBaseCamp hook (no polling), fires a light trigger RPC from on_update (a safe
//                         top-level tick), the server resolves that client's camp, reads GROUND-TRUTH
//                         container contents for (guild - own), and replies over an engine RPC; the client
//                         parses it into the pool. No FindAllOf in any per-frame path.
//
// All patch sites are located by unique AOB signature at load (survives address-shifting game updates).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <utility>
#include <unordered_map>
#include <unordered_set>

#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Core/Containers/Array.hpp>
#include <Unreal/FMemory.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UFunctionStructs.hpp> // UnrealScriptFunctionCallableContext / GetParams<>
#include <Unreal/World.hpp>            // UWorld (GetWorld for world-change detection)

#pragma push_macro("ensure")
#pragma push_macro("check")
#undef ensure
#undef check
#include <polyhook2/Detour/x64Detour.hpp>
#pragma pop_macro("check")
#pragma pop_macro("ensure")

using namespace RC;
using namespace RC::Unreal;

struct RawTArray { uint8_t* data; int32_t num; int32_t max; };

// ============================================================================
//  Config (config.txt beside the mod; parsed at load — see loadConfig)
// ============================================================================
//! Pre-stable build -> verbose logging DEFAULTS ON so field issues are diagnosable; users can quiet it and
//! tune the reconcile cadence via config.txt without rebuilding. Each key keeps its default if absent.
static bool     g_verbose      = true;     // verbose [ISGATE] diagnostics
static uint64_t g_reconcileMs  = 8000;     // authority: discovery-reconcile cadence (min 500)
static uint64_t g_isiRefreshMs = 1500;     // (reserved; config-compat) remote-client refresh cadence

// ============================================================================
//  Struct offsets (ref/sdk/SDK)
// ============================================================================
static const uintptr_t OFF_INV_MYINFO   = 0x100;  // UPalPlayerInventoryData: FPalPlayerDataInventoryInfo (CommonContainerId @ +0x00)
static const uintptr_t OFF_INV_MULTI    = 0x190;  // UPalPlayerInventoryData: UPalItemContainerMultiHelper*
static const uintptr_t OFF_MULTI_CONTS  = 0x38;   // UPalItemContainerMultiHelper: TArray<UPalItemContainer*>
static const uintptr_t OFF_CONT_ID      = 0x38;   // UPalContainerBase.ID (FPalContainerId, 16 bytes)
static const uintptr_t OFF_CONT_SLOTS   = 0x70;   // UPalItemContainer: TArray<UPalItemSlot*>
static const uintptr_t OFF_CONT_OWNER   = 0xF8;   // UPalItemContainer.OwnerMapObjectInstanceId (FGuid; nonzero => camp-building storage)
static const uintptr_t OFF_SLOT_CONT_ID = 0x11C;  // UPalItemSlot.ContainerId (FPalContainerId, 16 bytes)
static const uintptr_t OFF_SLOT_ITEMID  = 0x12C;  // UPalItemSlot: FPalItemId.StaticId (FName)
static const uintptr_t OFF_SLOT_COUNT   = 0x154;  // UPalItemSlot.StackCount (int32)
static const uintptr_t OFF_SLOT_INDEX   = 0x118;  // UPalItemSlot.SlotIndex (int32)

static bool guidZero(const uint8_t* g) { for (int i = 0; i < 16; ++i) if (g[i]) return false; return true; }
static void hexOf(const uint8_t* g, wchar_t out33[33]) {   // 16-byte FGuid -> 32 lowercase hex chars
    static const wchar_t* H = L"0123456789abcdef";
    for (int b = 0; b < 16; ++b) { out33[b*2] = H[g[b] >> 4]; out33[b*2+1] = H[g[b] & 15]; }
    out33[32] = 0;
}
static bool hexToGuid(const std::wstring& hex, uint8_t out16[16]) {   // 32 hex chars -> 16 bytes; false if malformed
    if (hex.size() < 32) return false;
    auto hv = [](wchar_t c)->int { if (c>=L'0'&&c<=L'9') return c-L'0'; if (c>=L'a'&&c<=L'f') return c-L'a'+10;
                                   if (c>=L'A'&&c<=L'F') return c-L'A'+10; return -1; };
    for (int b = 0; b < 16; ++b) {
        int hi = hv(hex[b*2]), lo = hv(hex[b*2+1]);
        if (hi < 0 || lo < 0) return false;
        out16[b] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

//! The guild pool the client displays = {item id -> count}. Filled from the transport channel (hkChReply),
//! NOT from the ISI. poolGet is used only by diagnostics.
static std::vector<std::pair<FName, int32_t>> g_pool;
static int32_t poolGet(const FName& id) { for (auto& kv : g_pool) if (kv.first == id) return kv.second; return -1; }

// fwd decls (defined further down)
static UObject* findCommonContainer();
static UObject* findDonorContainer();
static void mintPoolSlots();
static void checkWorld(void* anyObj);
static bool clientInCamp();   // PURE-READ local in-camp test (defined near chClientTrigger, needs OFF_PAWN_CAMPCHECK)
static bool chClientTrigger();

// ============================================================================
//  Role (single signal: UPalUtility::IsServer, verified in-game)
// ============================================================================
//! The SAME dll ships to both ends. The client-DISPLAY half (the 3 injection detours) runs ONLY on a pure
//! remote client — an authority (dedicated / host / standalone) reads the server-cross-registered containers
//! natively and needs no display injection. So: DISPLAY <=> !IsServer ; SERVER work <=> IsServer.
//! Pitfall: the TITLE menu is a local STANDALONE world where IsServer=true — so role must be computed from an
//! IN-GAME context (FindFirstOf(PalPlayerCharacter): false on a joined client, true on host/dedicated), never
//! the menu. Role is fixed once connected -> computed once and cached.
static int g_isSrv  = -1;   // -1 unknown, 0 remote client, 1 authority (dedicated/host/standalone)
static int g_isDedi = -1;   // -1 unknown, 0 no, 1 dedicated server (informational: distinguishes dedicated vs host/SP)
static UObject* g_palUtil = nullptr;
static bool callUtilBool(const CharType* fnName, void* wc) {
    if (!wc) return false;
    if (!g_palUtil) g_palUtil = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Pal.Default__PalUtility"));
    if (!g_palUtil) return false;
    UFunction* fn = g_palUtil->GetFunctionByNameInChain(fnName);
    if (!fn) return false;
    struct { UObject* WorldContext; bool Ret; uint8_t pad[7]; } p{}; p.WorldContext = (UObject*)wc;
    g_palUtil->ProcessEvent(fn, &p);
    return p.Ret;
}
static void ensureRole(void* wc) {
    if (g_isSrv >= 0 || !wc) return;
    g_isSrv  = callUtilBool(STR("IsServer"), wc) ? 1 : 0;
    g_isDedi = callUtilBool(STR("IsDedicatedServer"), wc) ? 1 : 0;
    Output::send(STR("[ISGATE] ROLE server={} dedicated={} -> {}\n"), g_isSrv, g_isDedi,
        g_isSrv == 0 ? STR("CLIENT (display)") : (g_isDedi == 1 ? STR("DEDICATED (server)") : STR("HOST/SP (server)")));
}
//! run the client-display half? ONLY on a pure remote client. Role comes from a PalPlayerCharacter (NOT a
//! hook's WorldContext — that resolves to a world reading IsServer=TRUE even on a remote client). Cheap:
//! scans only until the role is cached.
static bool isClient(void* = nullptr) {
    if (g_isSrv < 0) ensureRole(UObjectGlobals::FindFirstOf(STR("PalPlayerCharacter")));
    return g_isSrv == 0;
}

// ============================================================================
//  Client DISPLAY — mint local slots + transient array-swap into cont5
// ============================================================================
//! We show an UNBOUNDED guild total by minting one local UPalItemSlot per pool item via the game's native
//! factory (UPalItemUtility::CreateLocalItemSlot), collecting them into our own buffer, and TRANSIENTLY
//! pointing a spare inventory container's ItemSlotArray at that buffer during the native material scan
//! (restore immediately after). Fresh local slots have no home container, so they can't be double-counted;
//! the swap is a cheap pointer assignment per scan; minting is throttled to reply-time (g_poolDirty).
//! ALLOCATOR: the swapped-in buffer MUST be FMemory-owned (see injectMinted). The native scan is closed-
//! source and, at scale, was observed to Realloc/Free the array it was handed (crash_2026_08_10: a
//! std::vector/CRT-heap buffer here produced "FMallocBinned2 Attempt to realloc an unrecognized block ...
//! canary == 0x0" -> LowLevelFatalError -> client crash, the transient-swap twin of the FindAllOf
//! cross-allocator hazard already hardened against elsewhere in this file).
static void* g_lastWc = nullptr;                 // a WorldContext from the detours (for the slot factory)
static std::vector<UObject*> g_mintedSlots;      // our minted slots (append source)
static RawTArray g_savedDonorArr{};              // cont5's real {data,num,max}, saved across the swap
static UObject* g_swapDonor = nullptr;           // the EXACT donor injectMinted swapped — restore must target THIS one, not a raced g_donorCont
static bool g_swapped2 = false;
static bool g_poolDirty = false;                 // reply arrived -> re-mint on the next detour tick
static bool g_needTrigger = false;               // set by camp-enter / menu-open hooks; on_update fires the network trigger from a SAFE context
//! Channel back-pressure. Triggering used to piggyback on the collector detour (shared with the ammo HUD),
//! so the reliable RPC channel got driven during normal play and saturated (input froze, no disconnect).
//! Now: fire at most one trigger per CH_MIN_INTERVAL_MS, never a second while the previous reply is still
//! outstanding, and only while the player is inside a camp. CH_REPLY_TIMEOUT_MS force-releases the in-flight
//! lock if a reply is ever dropped, so the channel can't wedge shut.
static const uint64_t CH_MIN_INTERVAL_MS  = 3000;
static const uint64_t CH_REPLY_TIMEOUT_MS = 5000;
static bool     g_awaitingReply = false;         // a trigger was sent, reply not yet received (in-flight guard)
static uint64_t g_lastTrigAt    = 0;             // GetTickCount64 of the last trigger we sent
static uint64_t g_myCalls   = 0;                 // triggers WE sent (chClientTrigger) — client-side
static uint64_t g_hookFires = 0;                 // times the dev trigger UFunction fired locally (== g_myCalls iff nothing else calls it)
static UObject* g_common    = nullptr;           // cached local player's Common container
static UObject* g_donorCont = nullptr;           // cached donor container (cont5)
//! NOTE: the local PlayerController / PlayerInventoryData are NOT cached — a cached UObject* can dangle when
//! the game frees/recreates it without a world change, and reading a stale inventory's container array
//! AV'd in findCommonContainer (crash_2026_07_26). These are all resolved live via FindFirstOf at their call
//! sites, which only sit on cold or throttled paths (trigger path, or clientInCamp's 300ms cache).
static UObject* g_itemUtilCdo = nullptr;
static UFunction* g_createSlotFn = nullptr;
static UObject* createLocalSlot(void* wc, const FName& id, int32_t count) {
    if (!g_itemUtilCdo) g_itemUtilCdo = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Pal.Default__PalItemUtility"));
    if (!g_itemUtilCdo) return nullptr;
    if (!g_createSlotFn) g_createSlotFn = g_itemUtilCdo->GetFunctionByNameInChain(STR("CreateLocalItemSlot"));
    if (!g_createSlotFn) return nullptr;
    struct { UObject* WorldContext; FName Id; int32_t Stack; UObject* Ret; } p{};
    p.WorldContext = (UObject*)wc; p.Id = id; p.Stack = count; p.Ret = nullptr;
    g_itemUtilCdo->ProcessEvent(g_createSlotFn, &p);
    return p.Ret;
}
static void mintPoolSlots() {
    //! GC-ROOT the minted slots (fix A): CreateLocalItemSlot returns UObjects referenced ONLY from our
    //! std::vector, which is invisible to UE's GC -> the collector would eventually GC them, and a later scan
    //! that reads the freed slot builds an item-info element with a garbage TSharedPtr -> CTD when the game
    //! destroys that array (0xc0000005 releasing rcx=0xbe.., seen in the UECC dump). SetRootSet() keeps them
    //! alive; unroot the previous batch first so refreshes don't leak.
    for (UObject* s : g_mintedSlots) if (s) s->ClearRootSet();
    g_mintedSlots.clear();
    if (!g_lastWc || g_pool.empty()) return;
    g_mintedSlots.reserve(g_pool.size());
    for (auto& kv : g_pool) {
        if (kv.second <= 0) continue;
        std::wstring nm = kv.first.ToString();                       // fix B: skip empty/None ids the client can't resolve
        if (nm.empty() || nm == STR("None")) continue;
        UObject* s = createLocalSlot(g_lastWc, kv.first, kv.second);  // returns nullptr for an unknown item -> skipped below
        if (s) { s->SetRootSet(); g_mintedSlots.push_back(s); }
    }
}
static int g_injDiag = 0;
//! APPEND (not replace): cont5 is the player's PRECIOUS-ITEMS container (implants / collectibles). The old
//! code swapped cont5's ENTIRE slot array out for the minted buffer; if restoreMinted was ever skipped (a
//! throwing/AV-ing native scan, or a raced g_donorCont), cont5 stayed detached and the player's implants
//! VANISHED until a re-login rebuilt them from the save. Instead we keep every real slot and APPEND the
//! minted ones after them: [real 0..num-1][minted...]. cont5's real items contribute 0 to build recipes
//! (they aren't recipe materials — the reason cont5 was chosen as donor), so the count is unchanged, while
//! the implant/craft UI still sees the real slots. Even a leaked swap can no longer hide or lose them.
static void injectMinted() {
    if (g_swapped2 || g_mintedSlots.empty()) return;
    //! OUT-OF-CAMP GATE: cross-camp items must ONLY appear while the player stands INSIDE a camp. The pool
    //! persists in g_mintedSlots after a reply, and the not-in-camp pool-clear in chClientTrigger only runs
    //! while g_needTrigger is set — so once a pool is fetched, walking out and opening a menu still showed the
    //! cross-camp items. Gate the append here (covers every scan path, not just menu-open): if not in a camp,
    //! drop the stale pool (unroot the minted slots) so the menu shows OWN items only. Re-entering a camp
    //! (hkEnterCamp) re-fetches + re-mints. After this clears, later scans early-return on g_mintedSlots.empty().
    if (!clientInCamp()) {
        for (UObject* s : g_mintedSlots) if (s) s->ClearRootSet();
        g_mintedSlots.clear();
        g_pool.clear();
        if (g_verbose && g_injDiag < 8) { ++g_injDiag; Output::send(STR("[ISGATE] INJ skip: out of camp -> dropped stale pool\n")); }
        return;
    }
    UObject* dc = g_donorCont; if (!dc) return;
    RawTArray* slots = (RawTArray*)((uint8_t*)dc + OFF_CONT_SLOTS);
    if (slots->num < 0 || slots->num > 4096) return;             // sanity: don't touch a garbage array
    g_savedDonorArr = *slots;                                    // save cont5's real {data,num,max}
    //! combined = real slots (kept, so implants stay visible + in-scan) then minted slots. Stamp each minted
    //! slot so the scan accepts it as belonging to cont5: ContainerId (@0x11C) = cont5's ID (@0x38), and give
    //! it a SlotIndex (@0x118) matching its position in the combined array.
    //! Buffer is FMemory-owned (GMalloc), not CRT-heap: the native scan is closed-source and, at scale, was
    //! observed to Realloc/Free the array it's handed. A CRT buffer there crashes FMallocBinned2's bookkeeping
    //! ("Attempt to realloc an unrecognized block ... canary == 0x0"); an FMemory block is a block the engine's
    //! own allocator actually recognizes, so a native Realloc/Free against it is valid either way.
    uint8_t* cid = (uint8_t*)dc + OFF_CONT_ID;
    const int32_t combined = g_savedDonorArr.num + (int32_t)g_mintedSlots.size();
    UObject** buf = (UObject**)FMemory::Malloc((size_t)combined * sizeof(UObject*));
    if (!buf) return;                                            // OOM (a few KB): skip this swap, retry next tick
    for (int i = 0; i < g_savedDonorArr.num; ++i) buf[i] = ((UObject**)g_savedDonorArr.data)[i];
    int32_t widx = g_savedDonorArr.num;
    for (UObject* s : g_mintedSlots) {
        if (!s) continue;
        std::memcpy((uint8_t*)s + OFF_SLOT_CONT_ID, cid, 16);
        *(int32_t*)((uint8_t*)s + OFF_SLOT_INDEX) = widx;
        buf[widx++] = s;
    }
    slots->data = (uint8_t*)buf;
    slots->num  = widx;
    slots->max  = combined;
    g_swapDonor = dc;                                            // pin: restore MUST target this exact donor
    g_swapped2  = true;
    if (g_verbose && g_injDiag < 8) { ++g_injDiag; Output::send(STR("[ISGATE] INJDIAG appended {} minted after {} real slots\n"), (int)g_mintedSlots.size(), g_savedDonorArr.num); }
}
static void restoreMinted() {
    if (!g_swapped2) return;
    if (g_swapDonor) {
        RawTArray* slots = (RawTArray*)((uint8_t*)g_swapDonor + OFF_CONT_SLOTS);
        //! Free whatever data pointer is CURRENTLY there, not the one injectMinted handed in: if the native
        //! scan grew/shrank the array mid-call, slots->data now points at a Realloc'd block — still
        //! FMemory-owned, just not our original pointer. Either way it's ours to release before we drop it.
        if (slots->data) FMemory::Free(slots->data);
        *slots = g_savedDonorArr;
    }
    g_swapDonor = nullptr;
    g_swapped2  = false;
}
static int g_injectDepth = 0;   // re-entrancy guard: only the OUTERMOST detour swaps/restores (they nest)

//! Called at the top of each detour (CHEAP, no scans, no network). ONLY re-mints when the pool just changed.
//! It no longer flags the network trigger: triggering is driven by genuine menu-open / camp-enter hooks. The
//! collector is shared with the ammo HUD, so the old detour-gap heuristic here fired the channel during
//! normal play and saturated the reliable RPC queue. The transient array-swap is done by injectMinted/
//! restoreMinted around the native scan, so g_swapped2 is false in here.
static void clientDetourTick() {
    if (g_poolDirty) { mintPoolSlots(); g_poolDirty = false; }
}

//! Find the LOCAL player's Common container by matching MyInventoryInfo.CommonContainerId against each
//! aggregated container's UPalContainerBase.ID. Client has exactly one inventory.
static UObject* findCommonContainer() {
    //! SEH: the aggregated container array can momentarily hold a dangling entry (a container freed mid-mutation
    //! on the game thread while this runs on the UE4SS thread); reading cont+0x38 then AVs (crash_2026_07_26).
    //! POD-only body -> safe to guard; a fault just yields "no common container this tick".
    __try {
        UObject* inv = UObjectGlobals::FindFirstOf(STR("PalPlayerInventoryData"));   // ALWAYS fresh -> current live object (never cache: a stale ptr dangles)
        if (!inv) return nullptr;
        uint8_t* ip = (uint8_t*)inv;
        uint8_t* commonId = ip + OFF_INV_MYINFO;
        UObject* multi = *(UObject**)(ip + OFF_INV_MULTI);
        if (!multi) return nullptr;
        RawTArray* conts = (RawTArray*)((uint8_t*)multi + OFF_MULTI_CONTS);
        if (!conts->data || conts->num <= 0 || conts->num > 64) return nullptr;
        for (int i = 0; i < conts->num; ++i) {
            UObject* cont = ((UObject**)conts->data)[i];
            if (!cont) continue;
            if (std::memcmp((uint8_t*)cont + OFF_CONT_ID, commonId, 16) == 0) return cont;
        }
        return nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
//! The player's largest non-Common inventory container (= the ~230-slot "cont5"), which is NOT part of the
//! material-count set (proven: injecting it yields CountItemNum 0). SAFE donor: player-owned, never scanned
//! independently, so our transiently-swapped slots can't be double-counted.
static UObject* findDonorContainer() {
    __try {   // same dangling-container guard as findCommonContainer (reads c+0x70)
        UObject* inv = UObjectGlobals::FindFirstOf(STR("PalPlayerInventoryData"));   // ALWAYS fresh -> current live object (never cache)
        if (!inv) return nullptr;
        UObject* multi = *(UObject**)((uint8_t*)inv + OFF_INV_MULTI);
        if (!multi) return nullptr;
        RawTArray* conts = (RawTArray*)((uint8_t*)multi + OFF_MULTI_CONTS);
        if (!conts->data || conts->num <= 0 || conts->num > 64) return nullptr;
        UObject* best = nullptr; int bestN = -1;
        for (int i = 0; i < conts->num; ++i) {
            UObject* c = ((UObject**)conts->data)[i];
            if (!c || c == g_common) continue;
            int nsl = ((RawTArray*)((uint8_t*)c + OFF_CONT_SLOTS))->num;
            if (nsl > bestN) { bestN = nsl; best = c; }
        }
        return best;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// ============================================================================
//  AOB signature scanning (survives address-shifting game updates)
// ============================================================================
//! Locate each site at load by a wildcarded byte signature (relative/RIP operands wildcarded, struct offsets
//! kept). Uniqueness in the module IS the correctness guard: exactly ONE match -> use it; ZERO or MANY ->
//! skip loudly. If a function's BYTES change (not just its address), regenerate its sig from a fresh analysis.
struct Sig { std::vector<uint8_t> b; std::vector<uint8_t> wild; };
static Sig parseSig(const char* s) {
    Sig sig;
    auto hv = [](char c)->int { if (c>='0'&&c<='9') return c-'0'; if (c>='A'&&c<='F') return c-'A'+10;
                                if (c>='a'&&c<='f') return c-'a'+10; return 0; };
    for (const char* p = s; *p; ) {
        if (*p == ' ') { ++p; continue; }
        if (*p == '?') { sig.b.push_back(0); sig.wild.push_back(1); p += (p[1]=='?') ? 2 : 1; }
        else { sig.b.push_back((uint8_t)((hv(p[0])<<4)|hv(p[1]))); sig.wild.push_back(0); p += 2; }
    }
    return sig;
}
struct ExecRange { const uint8_t* start; size_t size; };
static std::vector<ExecRange> g_exec;
static void initExecRanges(uintptr_t base) {
    auto* dos = (IMAGE_DOS_HEADER*)base;
    auto* nt  = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
            g_exec.push_back({ (const uint8_t*)(base + sec[i].VirtualAddress), (size_t)sec[i].Misc.VirtualSize });
}
// scan every executable section; returns the unique match address (0 if none). *count is capped at 2 so
// callers can distinguish not-found (0) from ambiguous (>=2).
static uintptr_t scanSig(const Sig& s, int* count) {
    const size_t n = s.b.size(); *count = 0;
    if (n == 0) return 0;
    const uint8_t b0 = s.b[0]; const bool w0 = s.wild[0] != 0;
    uintptr_t found = 0; int c = 0;
    for (auto& r : g_exec) {
        if (r.size < n) continue;
        const uint8_t* p = r.start; const size_t last = r.size - n;
        for (size_t i = 0; i <= last; ++i) {
            if (!w0 && p[i] != b0) continue;
            size_t j = 1;
            for (; j < n; ++j) if (!s.wild[j] && p[i+j] != s.b[j]) break;
            if (j == n) { if (!found) found = (uintptr_t)(p + i); if (++c >= 2) { *count = c; return found; } }
        }
    }
    *count = c; return found;
}

//! The 3 material-scan functions the remote client detours: collector (craft + build-confirm haves),
//! catalog (build-open availability), placement (per-recipe placement counter). Real injected numbers make
//! the native gates pass on their own (bisection 2026-07-17 removed the old "optimistic gate" overrides), so
//! only these 3 remain. Re-verify unique on every game update.
static const char* SIG_COLLECTOR = "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 48 89 54 24 10 57 41 56 41 57 48 83 EC 60 41 0F B6 E9 4D 8B F0 48 8B FA 48 8B F1 48 8B D1 48 8D 4C 24 48 E8 ?? ?? ?? ?? 48 8D 44 24 38 48 89 44 24 30";
static const char* SIG_CATALOG   = "48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 F0 48 81 EC 10 01 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 00 4D 8B E8 4C 89 44 24 50 48 8B DA 33 FF 48 89 7D B0 48 89 7D B8 48 89 7D D0";
static const char* SIG_PLACEMENT = "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D8 48 81 EC 28 01 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 10 4D 8B F9 4C 89 4C 24 58 4D 8B E0 48 8B DA 4C 89 45 88 33 FF 48 89 7D C0 48 89 7D C8";
struct Target { const CharType* name; const char* sig; uint64_t tramp; PLH::x64Detour* det; bool hooked; uintptr_t addr; };
static Target g_collect = { STR("collector"), SIG_COLLECTOR, 0, nullptr, false, 0 };
static Target g_7d0     = { STR("catalog"),   SIG_CATALOG,   0, nullptr, false, 0 };
static Target g_ac0     = { STR("placement"), SIG_PLACEMENT, 0, nullptr, false, 0 };

//! Each detour (remote client only): tick + transient array-swap cont5's slots -> our minted buffer around
//! the native scan, restore after. Outermost-only guard because these functions nest. On the authority the
//! isClient() guard makes it a pure pass-through (the server reads merged containers natively).
typedef int64_t(__fastcall* tCollect)(void*, void*, void*, uint8_t);   // (ctx, reqIds, OUT, type)
static int64_t __fastcall hkCollect(void* c, void* r, void* o, uint8_t t) {
    if (!isClient(c)) return reinterpret_cast<tCollect>(g_collect.tramp)(c, r, o, t);
    g_lastWc = c; checkWorld(c); clientDetourTick();
    bool outer = (g_injectDepth == 0);
    if (outer) injectMinted();
    ++g_injectDepth;
    int64_t x = 0;
    __try { x = reinterpret_cast<tCollect>(g_collect.tramp)(c, r, o, t); }
    __finally { --g_injectDepth; if (outer) restoreMinted(); }   // ALWAYS restore cont5, even if the scan throws/AVs
    return x;
}
typedef int64_t(__fastcall* t7d0)(void*, void*, void*);   // catalog (ctx, containers, OUT)
static int64_t __fastcall hk7d0(void* a1, void* a2, void* o) {
    if (!isClient(a1)) return reinterpret_cast<t7d0>(g_7d0.tramp)(a1, a2, o);
    g_lastWc = a1; checkWorld(a1); clientDetourTick();
    bool outer = (g_injectDepth == 0);
    if (outer) injectMinted();
    ++g_injectDepth;
    int64_t x = 0;
    __try { x = reinterpret_cast<t7d0>(g_7d0.tramp)(a1, a2, o); }
    __finally { --g_injectDepth; if (outer) restoreMinted(); }   // ALWAYS restore cont5, even if the scan throws/AVs
    return x;
}
typedef int64_t(__fastcall* tAc0)(void*, void*, void*, void*);   // placement (ctx, containers, reqIds, OUT)
static int64_t __fastcall hkAc0(void* a1, void* a2, void* r, void* o) {
    if (!isClient(a1)) return reinterpret_cast<tAc0>(g_ac0.tramp)(a1, a2, r, o);
    g_lastWc = a1; checkWorld(a1); clientDetourTick();
    bool outer = (g_injectDepth == 0);
    if (outer) injectMinted();
    ++g_injectDepth;
    int64_t x = 0;
    __try { x = reinterpret_cast<tAc0>(g_ac0.tramp)(a1, a2, r, o); }
    __finally { --g_injectDepth; if (outer) restoreMinted(); }   // ALWAYS restore cont5, even if the scan throws/AVs
    return x;
}

// ============================================================================
//  SERVER — discovery reconcile + container cross-registration (authority only)
// ============================================================================
static const wchar_t*  SRV_CHEST_CLASS  = L"PalMapObjectItemChestModel";
static const uintptr_t OFF_CAMP_MODULES = 0x180;   // UPalBaseCampModel.ModuleArray (TArray<module*>)
static const uintptr_t OFF_CAMP_GROUPID = 0xE4;    // UPalBaseCampModel.GroupIdBelongTo (FGuid) -> guild key

struct GuildData {
    std::unordered_set<UObject*> storages, models;
    std::unordered_map<UObject*, UObject*> modelCamp;    // chest model  -> its owning camp
    std::unordered_map<UObject*, UObject*> storageCamp;  // storage module -> its owning camp
};
static std::unordered_map<std::wstring, GuildData> g_guilds;
static std::unordered_map<std::wstring, UObject*> g_instToCamp;  // chest map-object instance-id (hex) -> its camp
static bool g_srvInjecting = false;   // re-entrancy guard (our cross-register calls re-fire the storage events)

//! walk the UClass chain for an exact class name
static bool srvClassIs(UObject* o, const wchar_t* name) {
    UStruct* c = (UStruct*)o->GetClassPrivate();
    for (int i = 0; c && i < 24; ++i) { if (c->GetName() == name) return true; c = c->GetSuperStruct(); }
    return false;
}
//! guild key = the camp's GroupIdBelongTo FGuid (16 bytes) read raw as an 8-wchar string key
static std::wstring srvGuildKey(UObject* camp) {
    return std::wstring((const wchar_t*)((uint8_t*)camp + OFF_CAMP_GROUPID), 8);
}
//! call a one-UObject-arg UFunction by name (OnAvailableConcreteModel_ServerInternal)
static void srvCall1(UObject* obj, const CharType* fnName, UObject* model) {
    UFunction* fn = obj->GetFunctionByNameInChain(fnName);
    if (!fn) return;
    struct { UObject* Model; } p{ model };
    obj->ProcessEvent(fn, &p);
}
//! a chest's CURRENT owning camp (survives camp re-association). ProcessEvent GetBaseCampModelBelongTo.
static UObject* srvCampModelOf(UObject* chest) {
    UFunction* fn = chest->GetFunctionByNameInChain(STR("GetBaseCampModelBelongTo"));
    if (!fn) return nullptr;
    struct { UObject* Ret; } p{};
    chest->ProcessEvent(fn, &p);
    return p.Ret;
}
//! a camp's storage module by walking its ModuleArray (no scan)
static UObject* srvStorageOf(UObject* camp) {
    RawTArray* mods = (RawTArray*)((uint8_t*)camp + OFF_CAMP_MODULES);
    if (!mods->data || mods->num <= 0 || mods->num > 64) return nullptr;
    for (int i = 0; i < mods->num; ++i) { UObject* m = ((UObject**)mods->data)[i]; if (m && srvClassIs(m, L"PalBaseCampModuleItemStorage")) return m; }
    return nullptr;
}

//! DISCOVERY RECONCILE (authority; ~8s correctness pass). Rebuilds guild state from GROUND TRUTH and
//! cross-registers every guild chest's model into every same-guild camp's storage module so the native
//! build/craft flow can CONSUME cross-camp. Two enumerations:
//!   (a) every chest concrete model from the UPalMapObjectManager (TMap @0x310, raw sparse-array walk, no
//!       FindAllOf) -> group by CURRENT camp + guild; also record instance-id -> camp for the channel read.
//!   (b) EVERY base camp incl. EMPTY ones (FindAllOf) -> add its storage to its guild bucket so an empty
//!       camp is a cross-registration TARGET. Without (b) an empty camp is never discovered (discovery is
//!       chest-driven) and building there fails — the root of "works at a stocked camp, not an empty one".
//! Re-find the manager each pass (never cache -> no dangling across world change). Idempotent.
static uint64_t g_lastReconcile = 0;
static int g_recLog = 0;
static void srvDiscoverReconcile() {
    if (g_isSrv != 1) return;
    UObject* mgr = UObjectGlobals::FindFirstOf(STR("PalMapObjectManager"));
    if (!mgr) return;
    uint8_t* mm = (uint8_t*)mgr + 0x310;                        // MapObjectConcreteModelMapForServer (TMap)
    uint8_t* elems  = *(uint8_t**)(mm + 0x00);                  // sparse-array element buffer
    int32_t  maxIdx = *(int32_t*)(mm + 0x08);                   // slots incl. holes (== NumBits)
    uint32_t* words = *(uint32_t**)(mm + 0x20); if (!words) words = (uint32_t*)(mm + 0x10);   // allocation bits
    if (!elems || maxIdx <= 0 || maxIdx > 1000000) return;
    std::unordered_map<std::wstring, GuildData> fresh;
    std::unordered_map<std::wstring, UObject*> freshInst;
    int chests = 0;
    for (int32_t i = 0; i < maxIdx; ++i) {
        if (((words[i >> 5] >> (i & 31)) & 1u) == 0) continue;                 // skip free slots
        uint8_t*  keyId = elems + (size_t)i * 0x20 + 0x00;                     // TPair::Key = FGuid instance id
        UObject* model  = *(UObject**)(elems + (size_t)i * 0x20 + 0x10);       // TPair::Value = concrete model
        if (!model || !srvClassIs(model, SRV_CHEST_CLASS)) continue;
        UObject* camp = srvCampModelOf(model); if (!camp) continue;
        GuildData& g = fresh[srvGuildKey(camp)];
        g.models.insert(model); g.modelCamp[model] = camp;
        UObject* st = srvStorageOf(camp); if (st) { g.storages.insert(st); g.storageCamp[st] = camp; }
        wchar_t ih[33]; hexOf(keyId, ih); freshInst[ih] = camp;
        ++chests;
    }
    int campsSeen = 0;   // (b) every camp, incl. empty ones, becomes a cross-registration target
    { std::vector<UObject*> camps; UObjectGlobals::FindAllOf(STR("PalBaseCampModel"), camps);
      for (UObject* camp : camps) { if (!camp) continue;
          GuildData& g = fresh[srvGuildKey(camp)];
          UObject* st = srvStorageOf(camp); if (st) { g.storages.insert(st); g.storageCamp[st] = camp; }
          ++campsSeen; } }
    g_srvInjecting = true;   // CONSUME: register each guild chest into every OTHER same-guild camp's storage
    for (auto& gkv : fresh) { GuildData& g = gkv.second;
        for (UObject* st : g.storages) { UObject* sc = g.storageCamp[st];
            for (UObject* mo : g.models) if (g.modelCamp[mo] != sc) srvCall1(st, STR("OnAvailableConcreteModel_ServerInternal"), mo); } }
    g_srvInjecting = false;
    g_guilds = std::move(fresh);
    g_instToCamp = std::move(freshInst);
    if (g_verbose && g_recLog < 8) { ++g_recLog; Output::send(STR("[ISGATE] SRV discover: chests={} camps={} guilds={} inst={}\n"), chests, campsSeen, (int)g_guilds.size(), (int)g_instToCamp.size()); }
}

// ============================================================================
//  Transport channel — client<->server pool delivery (ISI-free, demand-driven)
// ============================================================================
//! The remote client cannot read far-camp containers, so the server delivers the per-camp guild pool over a
//! custom channel that NEVER touches the native ItemStackInfo aggregate (mutating it broke Quick Stack + the
//! Item Retrieval Device, and it doesn't reliably replicate anyway). PAYLOAD request/response on the
//! PlayerController (client-owned net actor) — two RPCs the shipping game never fires on its own for our data:
//!   client->server request : PalPlayerController:Debug_CheatCommand_ToServer(FString)         (NetServer reliable)
//!   server->client reply   : PalPlayerController:Debug_ReceiveCheatCommand_ToClient(FString)  (NetClient reliable)
//! Flow: the client reads its OWN camp GUID (NowInsideBaseCampID off the pawn's InsideBaseCampCheckComponent)
//! and sends it as the request payload from on_update (safe context). The server looks the camp up BY THAT ID
//! (no connection->player->camp reverse-mapping — that @0x1E0 lookup was replication-timing-fragile and broke
//! across world changes), builds (guild - that camp) from GROUND-TRUTH container contents, and replies on the
//! same controller. The client parses the reply into g_pool. Item ids travel as strings (FName indices are
//! process-local) and are rebuilt with FName(str) — matches the recipe ids exactly.
static const wchar_t*  CH_SENTINEL     = L"IS1|";     // reply   payload tag: IS1|id:cnt,id:cnt,
static const wchar_t*  CH_REQ_SENTINEL = L"ISREQ|";   // request payload tag: ISREQ|<32-hex campGuid>
//! Payload channel on the PlayerController (the canonical client-owned net actor). The client sends its CURRENT
//! camp GUID as the request payload, so the server never reverse-maps connection->player->camp (that @0x1E0
//! lookup was replication-timing-fragile and broke across world changes / rejoin). Carrier = the FString
//! cheat-command RPC pair; its native handler is inert for non-admins, so our sentinel payload has no game side
//! effect (unlike the MapObject dev RPCs, whose native bodies dismantle/rename map objects).
static const CharType* CH_REQ_FN   = STR("Debug_CheatCommand_ToServer");           // client->server request (FString)
static const CharType* CH_REPLY_FN = STR("Debug_ReceiveCheatCommand_ToClient");    // server->client reply   (FString)
static const uintptr_t OFF_CAMP_ID        = 0x58;    // UPalBaseCampModel.ID (FGuid) = the camp's own id (== client NowInsideBaseCampID)
static const uintptr_t OFF_PAWN_CAMPCHECK = 0xC08;   // APalPlayerCharacter.InsideBaseCampCheckComponent
static const uintptr_t OFF_CHK_CAMPID     = 0xC0;    // UPalInsideBaseCampCheckComponent.NowInsideBaseCampID (FGuid)
static int g_chLog = 0;

//! server: find the base camp whose OWN id (@0x58) matches the client-supplied camp GUID. No player/connection
//! reverse-mapping — the requester told us its camp directly.
static UObject* srvCampById(const uint8_t* campGuid16) {
    std::vector<UObject*> camps; UObjectGlobals::FindAllOf(STR("PalBaseCampModel"), camps);
    for (UObject* c : camps)
        if (c && std::memcmp((uint8_t*)c + OFF_CAMP_ID, campGuid16, 16) == 0) return c;
    return nullptr;
}
//! server: build "IS1|id:cnt,..." = the contents of every chest in the requester's GUILD but NOT in the
//! requester's camp (= guild - own; the client shows its own camp natively). GROUND TRUTH: reads real
//! UPalItemContainer ItemSlotArrays. Each camp-storage container carries OwnerMapObjectInstanceId (@0xF8) =
//! its chest's instance id; g_instToCamp (built in the reconcile) maps that id -> the chest's current camp.
static std::wstring srvBuildForCamp(UObject* camp) {
    std::wstring out = CH_SENTINEL;
    if (!camp) return out;
    const std::wstring playerGuild = srvGuildKey(camp);
    std::vector<std::pair<FName, int64_t>> total;
    std::vector<UObject*> conts; UObjectGlobals::FindAllOf(STR("PalItemContainer"), conts);
    int scanned = 0;
    for (UObject* c : conts) {
        if (!c) continue;
        uint8_t* cp = (uint8_t*)c;
        if (guidZero(cp + OFF_CONT_OWNER)) continue;                             // camp-storage containers only
        wchar_t ih[33]; hexOf(cp + OFF_CONT_OWNER, ih);
        auto it = g_instToCamp.find(ih); if (it == g_instToCamp.end()) continue; // not a tracked guild chest
        UObject* ccamp = it->second;
        if (ccamp == camp) continue;                                             // own camp -> exclude
        if (srvGuildKey(ccamp) != playerGuild) continue;                         // different guild -> skip
        RawTArray* slots = (RawTArray*)(cp + OFF_CONT_SLOTS);
        if (!slots->data || slots->num <= 0 || slots->num > 4096) continue;
        ++scanned;
        for (int i = 0; i < slots->num; ++i) {
            UObject* slot = ((UObject**)slots->data)[i]; if (!slot) continue;
            int32_t cnt = *(int32_t*)((uint8_t*)slot + OFF_SLOT_COUNT); if (cnt <= 0) continue;
            FName id = *(FName*)((uint8_t*)slot + OFF_SLOT_ITEMID);
            bool f = false; for (auto& t : total) if (t.first == id) { t.second += cnt; f = true; break; }
            if (!f) total.emplace_back(id, (int64_t)cnt);
        }
    }
    int items = 0;
    for (auto& t : total) {
        int64_t d = t.second; if (d <= 0) continue; if (d > 0x7fffffffLL) d = 0x7fffffffLL;
        out += t.first.ToString() + L":" + std::to_wstring(d) + L","; ++items;
    }
    if (g_verbose && g_chLog < 200) { ++g_chLog; Output::send(STR("[ISGATE] CH build: otherCamp-conts={} items={} len={}\n"), scanned, items, (int)out.size()); }
    return out;
}
//! server handler: hooked on Debug_CheatCommand_ToServer. ctx.Context = the requester's (server-side)
//! PalPlayerController. Read the FString; if it's our request, parse the client-supplied camp GUID, build
//! (guild - that camp), and reply on the SAME controller via Debug_ReceiveCheatCommand_ToClient (routes to
//! that one client). A real cheat command (not our sentinel) is ignored. Client's own local echo -> isClient().
static void hkChRequest(UnrealScriptFunctionCallableContext& ctx, void*) {
    if (isClient()) return;
    UObject* ctrl = ctx.Context; if (!ctrl) return;              // the requester's PalPlayerController (server-side)
    struct P { RawTArray S; };                                   // FString Command == TArray<wchar_t> {data,num,max}
    auto& pr = ctx.GetParams<P>();
    if (!pr.S.data || pr.S.num <= 0) return;
    std::wstring req((const wchar_t*)pr.S.data);
    if (req.rfind(CH_REQ_SENTINEL, 0) != 0) return;              // not ours -> a real cheat command, leave it
    uint8_t guid[16];
    if (!hexToGuid(req.substr(6), guid)) { if (g_verbose && g_chLog < 200) { ++g_chLog; Output::send(STR("[ISGATE] CH req: bad guid\n")); } return; }
    UObject* camp = srvCampById(guid);
    if (!camp) { if (g_verbose && g_chLog < 200) { ++g_chLog; wchar_t gh[33]; hexOf(guid, gh); Output::send(STR("[ISGATE] CH req: camp not found guid={}\n"), gh); } return; }
    std::wstring payload = srvBuildForCamp(camp);
    UFunction* rf = ctrl->GetFunctionByNameInChain(CH_REPLY_FN);
    if (!rf) { if (g_verbose && g_chLog < 200) { ++g_chLog; Output::send(STR("[ISGATE] CH req: no reply fn\n")); } return; }
    struct { FString S; } rp{}; rp.S = FString(payload.c_str());
    ctrl->ProcessEvent(rf, &rp);
    if (g_verbose && g_chLog < 200) { ++g_chLog; Output::send(STR("[ISGATE] CH reply sent len={}\n"), (int)payload.size()); }
}
//! client receiver: hooked on Debug_ReceiveCheatCommand_ToClient. If the string carries our sentinel, parse it
//! into g_pool; otherwise leave it alone. Server's own local echo -> !isClient() guard.
static void hkChReply(UnrealScriptFunctionCallableContext& ctx, void*) {
    if (!isClient()) return;
    struct P { RawTArray S; };                                   // FString Message == TArray<wchar_t> {data,num,max}
    auto& pr = ctx.GetParams<P>();
    if (!pr.S.data || pr.S.num <= 0) return;
    std::wstring str((const wchar_t*)pr.S.data);
    if (str.rfind(CH_SENTINEL, 0) != 0) return;                  // not ours -> a genuine cheat reply, ignore
    g_awaitingReply = false;                                     // our reply arrived -> release the in-flight lock
    std::vector<std::pair<FName, int32_t>> np; int items = 0;
    size_t i = 4;                                                // skip "IS1|"
    while (i < str.size()) {                                     // parse "name:cnt,name:cnt,"
        size_t comma = str.find(L',', i);
        std::wstring tok = str.substr(i, comma == std::wstring::npos ? std::wstring::npos : comma - i);
        i = (comma == std::wstring::npos) ? str.size() : comma + 1;
        if (tok.empty()) continue;
        size_t colon = tok.rfind(L':'); if (colon == std::wstring::npos) continue;
        std::wstring nm = tok.substr(0, colon);
        long cnt = 0; try { cnt = std::stol(tok.substr(colon + 1)); } catch (...) { cnt = 0; }
        if (nm.empty() || cnt <= 0) continue;
        np.emplace_back(FName(nm.c_str()), (int32_t)cnt); ++items;
    }
    g_pool = std::move(np);
    g_poolDirty = true;   // re-mint on the next detour tick (needs g_lastWc, set by the detour)
    if (g_verbose && g_chLog < 200) { ++g_chLog; Output::send(STR("[ISGATE] CH-RECV len={} pool={} Wood={}\n"), (int)str.size(), items, poolGet(FName(STR("Wood")))); }
}
//! PURE-READ local in-camp test (factored from chClientTrigger's gate). Resolves the local pawn via the
//! PlayerController's K2_GetPawn, then reads NowInsideBaseCampID's HIGH 8 bytes off the check component
//! (@0xC08 -> @0xC0). "Inside" == first 8 bytes non-zero (the field isn't zeroed on exit; a real camp guid
//! has a random non-zero high half). NO reflection on the check component (that AV'd out of camp as dev-3.2).
//! THROTTLED: the probe (FindFirstOf(PalPlayerController) = O(all UObjects) + a K2_GetPawn ProcessEvent) is
//! FAR too heavy to run per collect call — in-camp with a pool, injectMinted fires every frame, so an
//! unthrottled probe here tanks the frame rate during the frequent-collect phase. Camp enter/exit is not a
//! per-frame event, so cache the result and only re-probe every ~300ms; every other call is a tick compare.
static bool clientInCamp() {
    static uint64_t s_last = 0;
    static bool     s_cached = false;
    uint64_t now = GetTickCount64();
    if (s_last != 0 && now - s_last < 300) return s_cached;     // serve cache between probes
    s_last = now;
    UObject* ctrl = UObjectGlobals::FindFirstOf(STR("PalPlayerController")); if (!ctrl) return (s_cached = false);
    UFunction* getPawn = ctrl->GetFunctionByNameInChain(STR("K2_GetPawn")); if (!getPawn) return (s_cached = false);
    struct { UObject* Ret; } pp{}; ctrl->ProcessEvent(getPawn, &pp);
    UObject* pawn = pp.Ret; if (!pawn) return (s_cached = false);
    UObject* chk = *(UObject**)((uint8_t*)pawn + OFF_PAWN_CAMPCHECK); if (!chk) return (s_cached = false);
    const uint8_t* campGuid = (const uint8_t*)chk + OFF_CHK_CAMPID;
    uint32_t gHiA = *(const uint32_t*)(campGuid + 0);
    uint32_t gHiB = *(const uint32_t*)(campGuid + 4);
    return (s_cached = !(gHiA == 0 && gHiB == 0));
}
//! client: send a request carrying the LOCAL camp GUID to the server, on the PlayerController (canonical
//! client-owned net actor -> reliable transmit, no transmitter-hop). Returns true ONLY if actually sent (the
//! player is inside a camp). Reads NowInsideBaseCampID off the pawn's InsideBaseCampCheckComponent. Everything
//! resolved live (no cached ptr -> no dangling crash). ONLY call from on_update (top-level tick).
static bool chClientTrigger() {
    UObject* ctrl = UObjectGlobals::FindFirstOf(STR("PalPlayerController")); if (!ctrl) return false;
    UFunction* getPawn = ctrl->GetFunctionByNameInChain(STR("K2_GetPawn")); if (!getPawn) return false;
    struct { UObject* Ret; } pp{}; ctrl->ProcessEvent(getPawn, &pp);
    UObject* pawn = pp.Ret; if (!pawn) return false;
    UObject* chk = *(UObject**)((uint8_t*)pawn + OFF_PAWN_CAMPCHECK);
    if (!chk) { if (g_verbose && g_chLog < 200) { ++g_chLog; Output::send(STR("[ISGATE] CH skip: no InsideBaseCampCheckComponent poolNow={}\n"), (int)g_pool.size()); } return false; }
    uint8_t* campGuid = (uint8_t*)chk + OFF_CHK_CAMPID;
    //! AUTHORITATIVE in-camp gate — PURE READ, no reflection on `chk`. The raw NowInsideBaseCampID field
    //! @0xC0 is NOT zeroed when the player leaves a camp: the pawn's camp-check slot then reads back a stale
    //! PARTIAL guid whose FIRST 8 bytes are zero (e.g. 0000000000000000f010c5bc2902....), so guidZero()
    //! (all-16-zero) alone lets a bogus out-of-camp request through -> the server logs "camp not found".
    //! A real camp FGuid has a random non-zero high half (dwords A/B), so "inside" == first 8 bytes present.
    //! Do NOT call GetFunctionByNameInChain/ProcessEvent on `chk` here: out of camp that slot can point at a
    //! stale/other object — reading its flat bytes is harmless (historically safe), but walking it as a
    //! UObject via reflection follows garbage internal pointers and access-violates (this crashed as dev-3.2:
    //! on_update -> chClientTrigger -> chk->GetFunctionByNameInChain -> UE4SS `mov rcx,[rax]`, rax=0x400000049).
    uint32_t gHiA = *(const uint32_t*)(campGuid + 0);
    uint32_t gHiB = *(const uint32_t*)(campGuid + 4);
    if (gHiA == 0 && gHiB == 0) {                                // first 8 bytes zero => not a live camp id:
        if (guidZero(campGuid)) return false;                    //   all-zero: entering, id not replicated yet -> retry
        g_needTrigger = false;                                   //   partial residue = left the camp -> CONSUME trigger
        if (!g_pool.empty()) {                                   //   drop stale pool so the menu shows OWN items only
            g_pool.clear(); g_poolDirty = true;                  //   re-mint empty on the next detour tick
            if (g_verbose && g_chLog < 200) { ++g_chLog; Output::send(STR("[ISGATE] CH not-in-camp -> cleared stale pool\n")); }
        }
        return false;
    }
    wchar_t hex[33]; hexOf(campGuid, hex);
    UFunction* fn = ctrl->GetFunctionByNameInChain(CH_REQ_FN); if (!fn) return false;
    std::wstring req = std::wstring(CH_REQ_SENTINEL) + hex;
    struct { FString S; } p{}; p.S = FString(req.c_str());
    ++g_myCalls;
    ctrl->ProcessEvent(fn, &p);
    if (g_verbose && g_chLog < 200) { ++g_chLog; Output::send(STR("[ISGATE] CH request sent camp={} myCalls={} hookFires={}\n"), hex, (unsigned)g_myCalls, (unsigned)g_hookFires); }
    return true;
}
//! client: EVENT-DRIVEN camp tracking (no polling). OnEnterBaseCamp fires when the local player enters a
//! camp -> drop the old pool and flag a fresh request (fired from on_update). The reply arrives before a
//! build menu opens, so there's no round-trip latency at collect time. OnExitBaseCamp is intentionally a
//! no-op: it fires spuriously (enter+exit paired, and while standing still), and clearing the pool there
//! dropped the display to 0 mid-build. The pool is refreshed on the next enter + on every menu-open edge.
static void hkEnterCamp(UnrealScriptFunctionCallableContext& ctx, void*) {
    (void)ctx;
    if (!isClient()) return;
    g_pool.clear(); g_poolDirty = true; g_needTrigger = true;
    if (g_verbose && g_chLog < 200) { ++g_chLog; Output::send(STR("[ISGATE] CH enter-camp -> flagged\n")); }
}
static void hkExitCamp(UnrealScriptFunctionCallableContext& ctx, void*) {
    (void)ctx;   // no-op by design (see note above)
}
//! client: most overlay menus open through the central dispatcher UPalUserWidget:Push -> covers build, the
//! ESC/pause menu, and others. We don't care WHICH menu; just flag a fresh pool request. The back-pressure
//! guard (in-camp + CH_MIN_INTERVAL_MS + in-flight lock) keeps this cheap even though Push fires for all
//! overlays. on_update does the actual send from a safe context.
static void hkPush(UnrealScriptFunctionCallableContext& ctx, void*) {
    (void)ctx;
    if (!isClient()) return;
    g_needTrigger = true;
    if (g_verbose && g_chLog < 200) { ++g_chLog; Output::send(STR("[ISGATE] CH menu-open (Push) -> flagged\n")); }
}
//! client: the CRAFT/production menu does NOT route through UPalUserWidget:Push (confirmed live: build + ESC
//! fire Push, craft does not). Its UI model binds via UPalUIConvertItemModel:Initialize -> hook that as a
//! second menu-open edge so opening a workbench/production facility refreshes the pool too. Same flag-only
//! body; we never touch the param.
static void hkCraftOpen(UnrealScriptFunctionCallableContext& ctx, void*) {
    (void)ctx;
    if (!isClient()) return;
    g_needTrigger = true;
    if (g_verbose && g_chLog < 200) { ++g_chLog; Output::send(STR("[ISGATE] CH menu-open (Craft) -> flagged\n")); }
}
static void installChannel() {
    auto noop    = [](UnrealScriptFunctionCallableContext&, void*) {};
    auto trigPre = [](UnrealScriptFunctionCallableContext&, void*) { ++g_hookFires; };   // count EVERY local fire of the request RPC
    //! Core channel (request/reply on the PlayerController + enter/exit): proven paths, one try.
    try {
        UObjectGlobals::RegisterHook(STR("/Script/Pal.PalPlayerController:Debug_CheatCommand_ToServer"),          trigPre, hkChRequest, nullptr);
        UObjectGlobals::RegisterHook(STR("/Script/Pal.PalPlayerController:Debug_ReceiveCheatCommand_ToClient"),   noop,    hkChReply,   nullptr);
        UObjectGlobals::RegisterHook(STR("/Script/Pal.PalBuilderComponent:OnEnterBaseCamp"),                      noop,    hkEnterCamp, nullptr);
        UObjectGlobals::RegisterHook(STR("/Script/Pal.PalBuilderComponent:OnExitBaseCamp"),                       noop,    hkExitCamp,  nullptr);
        Output::send(STR("[ISGATE] core channel hooks OK (request=Debug_CheatCommand reply=Debug_ReceiveCheatCommand + enter/exit)\n"));
    } catch (const std::exception&) { Output::send(STR("[ISGATE] CORE channel hook registration FAILED\n")); }
    //! Single universal menu-open edge: the central overlay dispatcher. Own try so a bad path can't take the
    //! core channel down, and the log confirms it resolved.
    try {
        UObjectGlobals::RegisterHook(STR("/Script/Pal.PalUserWidget:Push"), noop, hkPush, nullptr);
        Output::send(STR("[ISGATE] hook OK: /Script/Pal.PalUserWidget:Push (menu-open: build/esc/etc)\n"));
    } catch (const std::exception&) { Output::send(STR("[ISGATE] hook FAILED: /Script/Pal.PalUserWidget:Push\n")); }
    //! Craft/production menu-open (does NOT go through Push). Own try so a bad path can't take Push down.
    try {
        UObjectGlobals::RegisterHook(STR("/Script/Pal.PalUIConvertItemModel:Initialize"), noop, hkCraftOpen, nullptr);
        Output::send(STR("[ISGATE] hook OK: /Script/Pal.PalUIConvertItemModel:Initialize (craft menu-open)\n"));
    } catch (const std::exception&) { Output::send(STR("[ISGATE] hook FAILED: /Script/Pal.PalUIConvertItemModel:Initialize\n")); }
}

// ============================================================================
//  Lifecycle — drop cached world-object state on a world change
// ============================================================================
//! Every UObject* we cache belongs to the CURRENT world and is freed when it unloads; without this reset,
//! re-entering a save reuses dangling pointers -> crash. CDOs/UClasses (g_palUtil, g_itemUtilCdo,
//! g_createSlotFn) are /Script objects, not world objects -> kept. Role is reset so it recomputes from the
//! new in-game world.
static UObject* g_lastWorld = nullptr;
static void resetState() {
    g_guilds.clear(); g_instToCamp.clear();
    for (UObject* s : g_mintedSlots) if (s) s->ClearRootSet();   // unroot so the old world's minted slots can be GC'd
    g_mintedSlots.clear();
    g_common = nullptr; g_donorCont = nullptr;
    g_pool.clear();
    //! g_swapDonor is a world object: by the time a world change is observed here it may already be dangling
    //! (same reasoning as g_common/g_donorCont below — don't dereference it). injectMinted/restoreMinted always
    //! run as an immediate pair within one detour call, so g_swapped2 == true here should never actually happen;
    //! if it somehow does, the FMemory swap buffer leaks a few KB rather than risk reading a freed UObject.
    g_swapped2 = false; g_swapDonor = nullptr;   // drop the pinned donor of the old world
    g_srvInjecting = false; g_injectDepth = 0;
    g_poolDirty = false; g_needTrigger = false;
    g_awaitingReply = false; g_lastTrigAt = 0;
    g_isSrv = -1; g_isDedi = -1; g_lastWc = nullptr;
    Output::send(STR("[ISGATE] world change -> full state reset\n"));
}
static void checkWorld(void* anyObj) {
    if (!anyObj) return;
    UObject* w = ((UObject*)anyObj)->GetWorld();
    if (!w) return;
    if (w != g_lastWorld) { if (g_lastWorld) resetState(); g_lastWorld = w; }
}

// ============================================================================
//  config.txt loader
// ============================================================================
//! Locate our own DLL (address-of a local fn -> module handle -> path), walk up two dirs
//! (.../Mods/<Name>/dlls/main.dll -> .../Mods/<Name>), parse `key = value` lines (`#`/`;` start a comment).
//! Absent file / unknown keys are fine -> defaults hold. Keys: verbose (bool), reconcile_interval_ms (>=500),
//! isi_refresh_ms (>=200, reserved).
static void loadConfig() {
    HMODULE hm = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)&loadConfig, &hm) || !hm) return;
    wchar_t buf[MAX_PATH]; DWORD n = GetModuleFileNameW(hm, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    std::wstring p(buf, n);
    for (int up = 0; up < 2; ++up) { size_t s = p.find_last_of(L"\\/"); if (s == std::wstring::npos) break; p.resize(s); }
    std::wifstream f(p + L"\\config.txt");
    if (!f) return;
    auto trim = [](std::wstring s) -> std::wstring {
        size_t a = s.find_first_not_of(L" \t\r\n"); if (a == std::wstring::npos) return L"";
        return s.substr(a, s.find_last_not_of(L" \t\r\n") - a + 1);
    };
    auto lower = [](std::wstring s) { for (auto& c : s) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32); return s; };
    std::wstring line;
    while (std::getline(f, line)) {
        size_t h = line.find_first_of(L"#;"); if (h != std::wstring::npos) line.resize(h);
        size_t eq = line.find(L'='); if (eq == std::wstring::npos) continue;
        std::wstring key = lower(trim(line.substr(0, eq)));
        std::wstring val = lower(trim(line.substr(eq + 1)));
        if (key.empty() || val.empty()) continue;
        auto asBool = [&](bool def) { return (val == L"1" || val == L"true" || val == L"yes" || val == L"on") ? true
                                           : (val == L"0" || val == L"false" || val == L"no" || val == L"off") ? false : def; };
        long num = 0; { try { num = std::stol(val); } catch (...) { num = -1; } }
        if      (key == L"verbose")                             g_verbose      = asBool(g_verbose);
        else if (key == L"reconcile_interval_ms" && num >= 500) g_reconcileMs  = (uint64_t)num;
        else if (key == L"isi_refresh_ms"        && num >= 200) g_isiRefreshMs = (uint64_t)num;
    }
}

// ============================================================================
//  Mod entry
// ============================================================================
static constexpr bool EN_COLLECT = true, EN_CATALOG = true, EN_PLACEMENT = true;   // per-detour install toggles (diagnostics)

class ModIntegratedStorageCpp : public CppUserModBase
{
public:
    ModIntegratedStorageCpp() : CppUserModBase()
    {
        ModName = STR("IntegratedStorageCpp"); ModVersion = STR("3.2");   // 3.2 (public): cont5 APPEND (no implant loss) + donor-pin + __finally + out-of-camp gate + role-caching fix + FindFirstOf caching
        ModDescription = STR("Cross-camp build/craft: use any same-guild camp's stored materials at any camp. Server cross-registers guild containers; the remote client displays the guild total via a custom ISI-free transport channel. AOB-signature located (survives game updates).");
        ModAuthors = STR("Sarfflow");
    }
    ~ModIntegratedStorageCpp() override
    {
        for (Target* t : { &g_collect, &g_7d0, &g_ac0 })
            if (t->det) { if (t->hooked) t->det->unHook(); delete t->det; t->det = nullptr; }
        for (UObject* s : g_mintedSlots) if (s) s->ClearRootSet();   // release the rooted minted slots on unload/hot-reload (else they leak in the root set)
        g_mintedSlots.clear();
    }
    auto on_unreal_init() -> void override
    {
        loadConfig();
        const uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        Output::send(STR("[ISGATE] === IntegratedStorage {} loaded, base {:#x} ===\n"), ModVersion, base);
        Output::send(STR("[ISGATE] config: verbose={} reconcile_ms={}\n"), g_verbose, (int)g_reconcileMs);
        initExecRanges(base);
        Output::send(STR("[ISGATE] exec sections={}\n"), (int)g_exec.size());
        auto maybe = [&](bool en, Target& t, uint64_t cb) {
            if (en) install(base, t, cb);
            else Output::send(STR("[ISGATE] {} OFF\n"), t.name);
        };
        //! CLIENT display detours (gated at runtime by isClient(); pure pass-through on the authority).
        maybe(EN_COLLECT,   g_collect, (uint64_t)&hkCollect);
        maybe(EN_CATALOG,   g_7d0,     (uint64_t)&hk7d0);
        maybe(EN_PLACEMENT, g_ac0,     (uint64_t)&hkAc0);
        //! Transport channel hooks (both ends; role-gated inside the handlers). Server consume is the ~8s
        //! reconcile in on_update. Native ISI is never touched.
        installChannel();
    }
    auto on_update() -> void override {
        uint64_t now = GetTickCount64();
        //! ROLE-INDEPENDENT world-change probe (fixes SP->title->dedicated role caching). checkWorld() resets
        //! all cached world state INCLUDING g_isSrv when the UWorld pointer changes — but it was ONLY ever
        //! called from the CLIENT display detours, which short-circuit on the server role (`if(!isClient())
        //! return tramp`). So a process that started in single-player (server role) could never observe the
        //! world change to un-stick itself: it stayed "server" forever and never sent a CH request after
        //! joining a dedicated server. on_update runs for EVERY role, so probe here. FindFirstOf is
        //! O(all UObjects) -> throttle to ~1s (never per-frame); role transitions only happen on map load, so
        //! ~1s latency is invisible. checkWorld(null) at the title screen is a safe no-op.
        static uint64_t g_lastWorldProbe = 0;
        if (now - g_lastWorldProbe > 1000) {
            g_lastWorldProbe = now;
            checkWorld(UObjectGlobals::FindFirstOf(STR("PalPlayerCharacter")));
        }
        //! Resolve role first; do nothing until it's known (title menu has no PalPlayerCharacter).
        if (g_isSrv < 0) { isClient(); return; }
        //! CLIENT: fire a pending channel trigger here — on_update is a top-level tick (safe for a NetServer
        //! RPC). Flagged by the enter hook / menu-open edge; idle => no trigger => zero traffic.
        if (g_isSrv == 0) {
            //! back-pressure: fire only INSIDE a camp, at most one per CH_MIN_INTERVAL_MS, and never while a
            //! reply is still outstanding. The timeout releases a wedged lock if a reply was ever dropped.
            if (g_awaitingReply && (now - g_lastTrigAt) > CH_REPLY_TIMEOUT_MS) g_awaitingReply = false;
            if (g_needTrigger && !g_awaitingReply && (now - g_lastTrigAt) > CH_MIN_INTERVAL_MS) {
                g_lastTrigAt = now;                                  // throttle ATTEMPTS (in-camp check is inside chClientTrigger)
                g_common = findCommonContainer(); g_donorCont = findDonorContainer();
                if (chClientTrigger()) { g_needTrigger = false; g_awaitingReply = true; }   // consume + await only if actually sent
            }
            return;
        }
        //! AUTHORITY: ~8s discovery reconcile (guild state + container cross-registration for consume).
        if (g_lastReconcile == 0 || now - g_lastReconcile >= g_reconcileMs) { g_lastReconcile = now; srvDiscoverReconcile(); }
    }
private:
    auto install(uintptr_t base, Target& t, uint64_t cb) -> void {
        Sig s = parseSig(t.sig); int cnt = 0;
        uintptr_t addr = scanSig(s, &cnt);
        if (cnt != 1) { Output::send(STR("[ISGATE] {} SIG {} — skipped\n"), t.name, cnt == 0 ? STR("NOT FOUND") : STR("AMBIGUOUS")); return; }
        t.addr = addr;
        t.det = new PLH::x64Detour((uint64_t)addr, cb, &t.tramp);
        t.hooked = t.det->hook();
        Output::send(STR("[ISGATE] {} @ {:#x} (rva {:#x}) hooked={}\n"), t.name, addr, addr - base, t.hooked);
    }
};

#define IS_CPP_API __declspec(dllexport)
extern "C"
{
    IS_CPP_API CppUserModBase* start_mod() { return new ModIntegratedStorageCpp(); }
    IS_CPP_API void uninstall_mod(CppUserModBase* mod) { delete mod; }
}
