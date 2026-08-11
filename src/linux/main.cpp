// Integrated Storage native Linux dedicated-server port.
//
// Stage 4a implements read-only discovery only:
//
// - Dedicated-server role detection
// - World-change detection and state reset
// - Base-camp enumeration
// - Guild grouping using copied 16-byte GUID keys
// - Base-camp storage-module discovery
// - Periodic diagnostic reporting
//
// Deliberately excluded:
//
// - Chest-map traversal
// - Container cross-registration
// - Material pooling or consumption
// - RPC transport
// - Hooks, detours and AOB scanning
// - Any mutation of Unreal objects or containers
//
// The upstream Windows implementation remains in ../dllmain.cpp and
// is intentionally unchanged.

#include <Unreal/UnrealInitializer.hpp>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <unistd.h>

#include <Helpers/String.hpp>
#include <Mod/CppUserModBase.hpp>

#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/World.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <algorithm>
#include <atomic>
#include <dlfcn.h>
#include <memory>
#include <string>
#include <thread>

namespace ModIntegratedStorageModulePin
{
    // Intentionally retained until process exit.
    //
    // NullPrism's CppMod wrapper owns the original dlopen handle
    // and can close it after uninstall_mod. This additional loader
    // reference keeps callback code mapped until PalServer exits.
    void* g_process_lifetime_pin{};

    auto pin_for_process_lifetime() noexcept -> bool
    {
        if (g_process_lifetime_pin != nullptr)
        {
            return true;
        }

        Dl_info module_info{};

        if (
            dladdr(
                static_cast<const void*>(
                    &g_process_lifetime_pin
                ),
                &module_info
            ) == 0 ||
            module_info.dli_fname == nullptr ||
            module_info.dli_fname[0] == '\0'
        )
        {
            return false;
        }

        g_process_lifetime_pin =
            dlopen(
                module_info.dli_fname,
                RTLD_NOW | RTLD_LOCAL
            );

        return g_process_lifetime_pin != nullptr;
    }
}


namespace
{
    using Clock = std::chrono::steady_clock;
    using GuildKey = std::array<std::uint8_t, 16>;

    constexpr auto WorldProbeInterval = std::chrono::seconds{1};
    constexpr auto DiscoveryInterval = std::chrono::seconds{8};

    struct RawTArray
    {
        std::uint8_t* data{};
        std::int32_t num{};
        std::int32_t max{};
    };

    struct GuildKeyHash
    {
        auto operator()(const GuildKey& key) const noexcept
            -> std::size_t
        {
            std::size_t hash =
                static_cast<std::size_t>(1469598103934665603ULL);

            for (const auto byte : key)
            {
                hash ^= static_cast<std::size_t>(byte);
                hash *= static_cast<std::size_t>(1099511628211ULL);
            }

            return hash;
        }
    };

    struct GuildDiscovery
    {
        std::size_t camp_count{};
        std::unordered_set<RC::Unreal::UObject*> storage_modules{};
    };


    struct RegistrationPlanGuild
    {
        std::unordered_map<
            RC::Unreal::UObject*,
            RC::Unreal::UObject*
        > chest_camps{};

        std::unordered_map<
            RC::Unreal::UObject*,
            RC::Unreal::UObject*
        > storage_camps{};
    };

    struct RegistrationPlanPair
    {
        RC::Unreal::UObject* chest{};
        RC::Unreal::UObject* storage{};

        auto operator==(
            const RegistrationPlanPair& other
        ) const noexcept -> bool
        {
            return
                chest == other.chest &&
                storage == other.storage;
        }
    };


    struct RegistrationExecutionPair
    {
        RC::Unreal::UObject* chest{};
        RC::Unreal::UObject* chest_camp{};
        RC::Unreal::UObject* storage{};
        RC::Unreal::UObject* storage_camp{};
        GuildKey guild{};
    };


    struct RegistrationCallMetadata
    {
        RC::Unreal::UFunction* function{};
        std::size_t parameter_bytes{};
        std::int32_t parameter_offset{-1};
        std::int32_t property_size{-1};
        bool passed{};
    };


    using TransportItemNameKey =
        std::array<std::uint8_t, 8>;

    struct TransportItemNameKeyHash
    {
        auto operator()(
            const TransportItemNameKey& key
        ) const noexcept -> std::size_t
        {
            std::size_t hash =
                static_cast<std::size_t>(
                    1469598103934665603ULL
                );

            for (const auto byte : key)
            {
                hash ^=
                    static_cast<std::size_t>(
                        byte
                    );

                hash *=
                    static_cast<std::size_t>(
                        1099511628211ULL
                    );
            }

            return hash;
        }
    };

    auto transport_name_key_to_hex(
        const TransportItemNameKey& key
    ) noexcept -> std::array<char, 17>
    {
        constexpr char HexDigits[] =
            "0123456789abcdef";

        std::array<char, 17> result{};

        for (
            std::size_t index{};
            index < key.size();
            ++index
        )
        {
            result[index * 2] =
                HexDigits[
                    (key[index] >> 4) & 0x0f
                ];

            result[index * 2 + 1] =
                HexDigits[
                    key[index] & 0x0f
                ];
        }

        result[16] = '\0';
        return result;
    }

    auto mix_plan_value(std::uint64_t value) noexcept
        -> std::uint64_t
    {
        value += 0x9e3779b97f4a7c15ULL;
        value =
            (value ^ (value >> 30)) *
            0xbf58476d1ce4e5b9ULL;
        value =
            (value ^ (value >> 27)) *
            0x94d049bb133111ebULL;

        return value ^ (value >> 31);
    }

    auto guild_plan_value(
        const GuildKey& guild_key
    ) noexcept -> std::uint64_t
    {
        std::uint64_t value =
            0xcbf29ce484222325ULL;

        for (const auto byte : guild_key)
        {
            value ^= byte;
            value *= 0x100000001b3ULL;
        }

        return mix_plan_value(value);
    }

    auto object_plan_value(
        RC::Unreal::UObject* object
    ) noexcept -> std::uint64_t
    {
        return mix_plan_value(
            static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(
                    object
                )
            )
        );
    }

    struct RegistrationPlanPairHash
    {
        auto operator()(
            const RegistrationPlanPair& pair
        ) const noexcept -> std::size_t
        {
            const auto chest =
                object_plan_value(pair.chest);

            const auto storage =
                object_plan_value(pair.storage);

            return static_cast<std::size_t>(
                chest ^
                (
                    storage +
                    0x9e3779b97f4a7c15ULL +
                    (chest << 6) +
                    (chest >> 2)
                )
            );
        }
    };

    auto registration_pair_fingerprint(
        const GuildKey& guild_key,
        const RegistrationPlanPair& pair
    ) noexcept -> std::uint64_t
    {
        const auto storage =
            object_plan_value(pair.storage);

        const auto rotated_storage =
            (storage << 1) |
            (storage >> 63);

        return mix_plan_value(
            guild_plan_value(guild_key) ^
            object_plan_value(pair.chest) ^
            rotated_storage
        );
    }

    RC::Unreal::UObject* g_pal_utility{};
    RC::Unreal::UObject* g_last_world{};

    std::atomic_int g_is_server{-1};
    std::atomic_int g_is_dedicated{-1};

    Clock::time_point g_last_world_probe{};
    Clock::time_point g_last_discovery{};

    std::uint64_t g_discovery_runs{};

    // Transport wire protocol (ISREQ/IS1, docs/linux-port-status.md §4).
    //
    // The request hook (registered in on_unreal_init) only parses the
    // incoming FString and enqueues a lightweight record — it never
    // touches reflection, FindAllOf or ProcessEvent itself, since RPC
    // dispatch is a new, previously-untested call context for this mod.
    // Actual pool-building and the reply happen on the next engine tick
    // (proven-safe context, same one run_read_only_chest_association
    // already uses hundreds of times per discovery pass), using a
    // snapshot of the registration plan cached at the end of the most
    // recent discovery pass.
    struct PendingTransportRequest
    {
        RC::Unreal::UObject* controller{};
        GuildKey requester_camp_id{};
    };

    constexpr std::size_t MaxPendingTransportRequests = 64;

    std::vector<PendingTransportRequest>
        g_pending_transport_requests{};

    std::atomic_bool g_transport_hook_registered{false};
    std::atomic_bool g_transport_mod_shutting_down{false};
    std::atomic_uint64_t g_transport_request_hook_fires{0};

    bool g_cached_registration_plan_valid{false};

    std::unordered_map<
        GuildKey,
        RegistrationPlanGuild,
        GuildKeyHash
    > g_cached_registration_plan{};

    std::unordered_map<
        GuildKey,
        RC::Unreal::UObject*,
        GuildKeyHash
    > g_cached_camp_id_to_camp{};

    std::unordered_map<
        RC::Unreal::UObject*,
        GuildKey
    > g_cached_camp_to_guild{};

    // Cross-camp material reconciliation (Stage 4F, deferred/periodic).
    //
    // The native build-material gate (UPalBuilderComponent::
    // IsExistsMaterialForBuildObject) only ever sees a camp's REAL local
    // storage - it has no notion of this mod's guild-wide pool. Mirroring
    // this file's existing rule for the transport hook (never do
    // reflection/FindAllOf/ProcessEvent work inside a hook callback,
    // always defer to the next engine tick), the hooks below only record
    // {camp -> currently-needed materials}; the actual container walk and
    // any top-up happen in process_pending_material_reconciliation() on
    // the next tick. A build attempt can therefore fail once and succeed
    // moments later on retry, once the pool has leveled that camp's stock.
    //
    // All property offsets used at hook-fire time are resolved exactly
    // once, up front, in resolve_build_gate_layout() (called from
    // on_unreal_init - an already-proven-safe, non-hook context) so the
    // hooks themselves never call GetPropertyByNameInChain or any other
    // reflection API.

    struct BuildMaterialLayout
    {
        std::int32_t id_offset{-1};
        std::int32_t count_offset{-1};
    };

    struct BuildGateLayout
    {
        std::int32_t enter_base_camp_model_offset{-1};
        std::array<BuildMaterialLayout, 4> materials{};
        bool ok{};

        // Diagnostic-only probe (not yet load-bearing): confirms whether
        // RequestBuild_ToServer itself fires server-side, since Stage
        // 4F.1's PalBuilderComponent hooks turned out to never fire on a
        // dedicated server at all (fire counts stayed at zero through a
        // real test session) - meaning those functions are client-
        // predicted/local-only, not server-authoritative. Resolved the
        // same way: verified once via reflection at init, never guessed.
        std::int32_t request_build_id_offset{-1};
        bool request_build_probe_ok{};
    };

    BuildGateLayout g_build_gate_layout{};
    bool g_build_gate_layout_resolved{};

    std::unordered_map<
        RC::Unreal::UObject*,
        RC::Unreal::UObject*
    > g_player_current_camp{};

    struct PendingMaterialCheck
    {
        struct Material
        {
            RC::Unreal::FName id{};
            std::int32_t needed{};
        };

        RC::Unreal::UObject* camp{};
        std::array<Material, 4> materials{};
    };

    constexpr std::size_t MaxPendingMaterialChecks = 64;

    std::vector<PendingMaterialCheck>
        g_pending_material_checks{};

    std::atomic_bool g_build_gate_hooks_registered{false};

    // Unconditional fire counters, bumped in the PRE hooks before any of
    // this mod's own gating/lookups run. Diagnostic-only: answers whether
    // these UFunctions are actually invoked on a dedicated server at all
    // (as opposed to being purely local/client-predicted, in which case
    // no amount of correct offset/lookup logic here would ever run).
    std::atomic_uint64_t g_enter_base_camp_hook_fires{0};
    std::atomic_uint64_t g_exit_base_camp_hook_fires{0};
    std::atomic_uint64_t g_material_check_hook_fires{0};
    std::atomic_uint64_t g_request_build_hook_fires{0};
    std::atomic_bool g_request_build_hook_registered{false};

    template <std::size_t Size>
    auto emit_marker(const char (&message)[Size]) noexcept -> void
    {
        static_assert(Size > 1);

        const auto ignored_message_result =
            ::write(STDERR_FILENO, message, Size - 1);

        const auto ignored_newline_result =
            ::write(STDERR_FILENO, "\n", 1);

        static_cast<void>(ignored_message_result);
        static_cast<void>(ignored_newline_result);
    }

    auto emit_format(const char* format, ...) noexcept -> void
    {
        std::va_list arguments;
        va_start(arguments, format);

        std::vfprintf(stderr, format, arguments);

        va_end(arguments);

        std::fputc('\n', stderr);
        std::fflush(stderr);
    }

    auto timepoint_is_empty(const Clock::time_point& value) noexcept
        -> bool
    {
        return value.time_since_epoch().count() == 0;
    }

    auto read_process_rss_kb() noexcept -> std::uint64_t
    {
        std::FILE* const status_file =
            std::fopen("/proc/self/status", "r");

        if (status_file == nullptr)
        {
            return 0;
        }

        unsigned long rss_kb{0};
        char line[256];

        while (
            std::fgets(
                line,
                sizeof(line),
                status_file
            ) != nullptr
        )
        {
            if (std::strncmp(line, "VmRSS:", 6) == 0)
            {
                std::sscanf(line + 6, "%lu", &rss_kb);
                break;
            }
        }

        std::fclose(status_file);

        return static_cast<std::uint64_t>(rss_kb);
    }

    auto guid_is_zero(const GuildKey& key) noexcept -> bool
    {
        for (const auto byte : key)
        {
            if (byte != 0)
            {
                return false;
            }
        }

        return true;
    }

    auto guid_to_hex(const GuildKey& key) noexcept
        -> std::array<char, 33>
    {
        constexpr char HexDigits[] = "0123456789abcdef";

        std::array<char, 33> result{};

        for (std::size_t index = 0; index < key.size(); ++index)
        {
            result[index * 2] =
                HexDigits[(key[index] >> 4) & 0x0F];

            result[index * 2 + 1] =
                HexDigits[key[index] & 0x0F];
        }

        result[32] = '\0';

        return result;
    }

    auto class_is(
        RC::Unreal::UObject* object,
        RC::Unreal::UClass* expected_class
    ) -> bool
    {
        if (object == nullptr || expected_class == nullptr)
        {
            return false;
        }

        RC::Unreal::UStruct* current = object->GetClassPrivate();
        RC::Unreal::UStruct* expected = expected_class;

        for (
            std::size_t depth = 0;
            current != nullptr && depth < 24;
            ++depth
        )
        {
            if (current == expected)
            {
                return true;
            }

            current = current->GetSuperStruct();
        }

        return false;
    }

    auto get_storage_module_class()
        -> RC::Unreal::UClass*
    {
        static RC::Unreal::UClass* storage_module_class{};

        if (storage_module_class == nullptr)
        {
            storage_module_class = RC::Unreal::UObjectGlobals::StaticFindObject<
                RC::Unreal::UClass*>(
                nullptr,
                nullptr,
                STR("/Script/Pal.PalBaseCampModuleItemStorage")
            );
        }

        return storage_module_class;
    }

    // Resolves every property offset the material-reconciliation hooks
    // need, exactly once, via the PalBuilderComponent CDO. Called from
    // on_unreal_init - never from inside a hook callback. Every offset is
    // verified (correct property kind + exact expected size) before
    // g_build_gate_layout.ok is set; the hooks below refuse to run at all
    // if this failed, rather than trusting an unverified guess.
    auto resolve_build_gate_layout() -> void
    {
        if (g_build_gate_layout_resolved)
        {
            return;
        }

        g_build_gate_layout_resolved = true;

        auto* builder_cdo =
            RC::Unreal::UObjectGlobals::StaticFindObject<
                RC::Unreal::UObject*
            >(
                nullptr,
                nullptr,
                STR("/Script/Pal.Default__PalBuilderComponent")
            );

        if (builder_cdo == nullptr)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "BUILD_GATE_LAYOUT RESULT=NO_CDO"
            );

            return;
        }

        auto* enter_function =
            builder_cdo->GetFunctionByNameInChain(
                STR("OnEnterBaseCamp")
            );

        if (enter_function != nullptr)
        {
            auto* model_property =
                RC::Unreal::CastField<
                    RC::Unreal::FObjectPropertyBase
                >(
                    enter_function->
                        GetPropertyByNameInChain(
                            STR("BaseCampModel")
                        )
                );

            if (
                model_property != nullptr &&
                model_property->GetSize() == 8
            )
            {
                g_build_gate_layout.
                    enter_base_camp_model_offset =
                        model_property->
                            GetOffset_Internal();
            }
        }

        auto* check_function =
            builder_cdo->GetFunctionByNameInChain(
                STR("IsExistsMaterialForBuildObject")
            );

        bool materials_ok = check_function != nullptr;

        if (materials_ok)
        {
            auto* struct_property =
                RC::Unreal::CastField<
                    RC::Unreal::FStructProperty
                >(
                    check_function->
                        GetPropertyByNameInChain(
                            STR("BuildObjectData")
                        )
                );

            auto* build_object_data_struct =
                struct_property != nullptr
                    ? struct_property->GetStruct().Get()
                    : nullptr;

            materials_ok =
                struct_property != nullptr &&
                struct_property->GetSize() == 0x98 &&
                build_object_data_struct != nullptr;

            if (materials_ok)
            {
                const std::int32_t data_offset =
                    struct_property->
                        GetOffset_Internal();

                const std::array<
                    std::pair<
                        const RC::CharType*,
                        const RC::CharType*
                    >,
                    4
                > field_names{{
                    {STR("Material1_Id"), STR("Material1_Count")},
                    {STR("Material2_Id"), STR("Material2_Count")},
                    {STR("Material3_Id"), STR("Material3_Count")},
                    {STR("Material4_Id"), STR("Material4_Count")},
                }};

                for (
                    std::size_t index{};
                    index < field_names.size() &&
                        materials_ok;
                    ++index
                )
                {
                    auto* id_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FNameProperty
                        >(
                            build_object_data_struct->
                                GetPropertyByNameInChain(
                                    field_names[index]
                                        .first
                                )
                        );

                    auto* count_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FNumericProperty
                        >(
                            build_object_data_struct->
                                GetPropertyByNameInChain(
                                    field_names[index]
                                        .second
                                )
                        );

                    materials_ok =
                        id_property != nullptr &&
                        count_property != nullptr &&
                        id_property->GetSize() == 8 &&
                        count_property->GetSize() == 4;

                    if (materials_ok)
                    {
                        g_build_gate_layout.
                            materials[index].id_offset =
                                data_offset +
                                id_property->
                                    GetOffset_Internal();

                        g_build_gate_layout.
                            materials[index].
                                count_offset =
                                data_offset +
                                count_property->
                                    GetOffset_Internal();
                    }
                }
            }
        }

        g_build_gate_layout.ok =
            g_build_gate_layout.
                enter_base_camp_model_offset >= 0 &&
            materials_ok;

        auto* network_player_cdo =
            RC::Unreal::UObjectGlobals::StaticFindObject<
                RC::Unreal::UObject*
            >(
                nullptr,
                nullptr,
                STR(
                    "/Script/Pal."
                    "Default__PalNetworkPlayerComponent"
                )
            );

        auto* request_build_function =
            network_player_cdo != nullptr
                ? network_player_cdo->
                    GetFunctionByNameInChain(
                        STR("RequestBuild_ToServer")
                    )
                : nullptr;

        if (request_build_function != nullptr)
        {
            auto* build_id_property =
                RC::Unreal::CastField<
                    RC::Unreal::FNameProperty
                >(
                    request_build_function->
                        GetPropertyByNameInChain(
                            STR("BuildObjectId")
                        )
                );

            if (
                build_id_property != nullptr &&
                build_id_property->GetSize() == 8
            )
            {
                g_build_gate_layout.
                    request_build_id_offset =
                        build_id_property->
                            GetOffset_Internal();

                g_build_gate_layout.
                    request_build_probe_ok = true;
            }
        }

        emit_format(
            "[ModIntegratedStorageCpp] "
            "BUILD_GATE_LAYOUT ok=%d "
            "enter_offset=%d "
            "request_build_probe_ok=%d "
            "request_build_id_offset=%d",
            g_build_gate_layout.ok ? 1 : 0,
            g_build_gate_layout.
                enter_base_camp_model_offset,
            g_build_gate_layout.
                request_build_probe_ok ? 1 : 0,
            g_build_gate_layout.
                request_build_id_offset
        );
    }

    auto copy_guild_key(
        RC::Unreal::UObject* camp,
        GuildKey& output
    ) -> bool
    {
        if (camp == nullptr)
        {
            return false;
        }

        const auto* property_value =
            camp->GetValuePtrByPropertyNameInChain(
                STR("GroupIdBelongTo")
            );

        if (property_value == nullptr)
        {
            return false;
        }

        std::memcpy(
            output.data(),
            property_value,
            output.size()
        );

        return true;
    }

    // Mirrors copy_guild_key exactly, reading a camp's own ID (FGuid)
    // instead of its GroupIdBelongTo. Same lightweight accessor already
    // trusted by production code (copy_guild_key feeds the accepted
    // registration executor, not just diagnostics).
    auto copy_camp_id(
        RC::Unreal::UObject* camp,
        GuildKey& output
    ) -> bool
    {
        if (camp == nullptr)
        {
            return false;
        }

        const auto* property_value =
            camp->GetValuePtrByPropertyNameInChain(
                STR("ID")
            );

        if (property_value == nullptr)
        {
            return false;
        }

        std::memcpy(
            output.data(),
            property_value,
            output.size()
        );

        return true;
    }

    constexpr char TransportRequestSentinel[] = "ISREQ|";

    constexpr std::size_t TransportRequestSentinelLength =
        sizeof(TransportRequestSentinel) - 1;

    constexpr std::size_t TransportGuidHexLength = 32;

    // Inverse of guid_to_hex: same byte order (byte i -> hex chars at
    // 2*i, 2*i+1), so this round-trips exactly with what the client
    // read from its own camp GUID and hex-encoded.
    auto parse_transport_request_guid(
        const std::string& text,
        GuildKey& output
    ) noexcept -> bool
    {
        if (
            text.size() !=
                TransportRequestSentinelLength +
                    TransportGuidHexLength ||
            text.compare(
                0,
                TransportRequestSentinelLength,
                TransportRequestSentinel
            ) != 0
        )
        {
            return false;
        }

        const auto hex_value = [](char c) noexcept -> int
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };

        for (
            std::size_t index = 0;
            index < output.size();
            ++index
        )
        {
            const auto high = hex_value(
                text[
                    TransportRequestSentinelLength +
                        index * 2
                ]
            );

            const auto low = hex_value(
                text[
                    TransportRequestSentinelLength +
                        index * 2 + 1
                ]
            );

            if (high < 0 || low < 0)
            {
                return false;
            }

            output[index] =
                static_cast<std::uint8_t>(
                    (high << 4) | low
                );
        }

        return true;
    }

    // Reads an incoming FString's raw TArray<TCHAR> as plain ASCII. Pure
    // read of memory the engine already owns (same class of access as
    // every other reflected property read in this file) — no Unreal
    // object is constructed or destructed here, so none of the §5
    // FMallocBinned2 destructor risk applies.
    auto ascii_narrow(const RawTArray& array) -> std::string
    {
        if (array.data == nullptr || array.num <= 0)
        {
            return {};
        }

        const auto* chars =
            reinterpret_cast<const RC::Unreal::TCHAR*>(
                array.data
            );

        std::string result{};
        result.reserve(static_cast<std::size_t>(array.num));

        for (
            std::int32_t index{};
            index < array.num;
            ++index
        )
        {
            const auto code_point =
                static_cast<std::uint32_t>(chars[index]);

            if (code_point == 0)
            {
                break;
            }

            if (code_point > 0x7f)
            {
                // Our wire payloads are pure ASCII by construction;
                // anything else can't be our sentinel.
                return {};
            }

            result.push_back(
                static_cast<char>(code_point)
            );
        }

        return result;
    }

    // Builds a raw TCHAR buffer + RawTArray for an outbound FString
    // parameter, deliberately leaked (never freed) so no destructor for
    // it ever runs inside main.so — the same leak-and-never-destruct
    // shape Stage 4D.9d-4D.9g proved safe for FName::ToString() results,
    // applied here to the one other place this mod hands an
    // Unreal-visible string-shaped buffer back to the engine. The
    // buffer itself is plain heap memory from the mod's own libstdc++
    // allocator (new[]), never an Unreal FString — its construction path
    // never touches FMemory, only its consumption (ProcessEvent copying
    // out of it) does.
    auto ascii_widen_leaked(const std::string& text) -> RawTArray
    {
        const auto length = text.size();

        auto* buffer = new RC::Unreal::TCHAR[length + 1];

        for (std::size_t index{}; index < length; ++index)
        {
            buffer[index] =
                static_cast<RC::Unreal::TCHAR>(
                    static_cast<unsigned char>(text[index])
                );
        }

        buffer[length] = static_cast<RC::Unreal::TCHAR>(0);

        RawTArray result{};

        result.data =
            reinterpret_cast<std::uint8_t*>(buffer);

        result.num =
            static_cast<std::int32_t>(length + 1);

        result.max = result.num;

        return result;
    }

    auto find_storage_module(RC::Unreal::UObject* camp)
        -> RC::Unreal::UObject*
    {
        if (camp == nullptr)
        {
            return nullptr;
        }

        auto* raw_modules =
            camp->GetValuePtrByPropertyNameInChain(
                STR("ModuleArray")
            );

        if (raw_modules == nullptr)
        {
            return nullptr;
        }

        const auto* modules =
            static_cast<const RawTArray*>(raw_modules);

        if (
            modules->data == nullptr ||
            modules->num <= 0 ||
            modules->num > 64 ||
            modules->max < modules->num
        )
        {
            return nullptr;
        }

        auto** module_objects =
            reinterpret_cast<RC::Unreal::UObject**>(
                modules->data
            );

        for (
            std::int32_t index = 0;
            index < modules->num;
            ++index
        )
        {
            auto* module = module_objects[index];

            if (
                class_is(
                    module,
                    get_storage_module_class()
                )
            )
            {
                return module;
            }
        }

        return nullptr;
    }


    template <typename Visitor>
    auto for_each_storage_module(
        RC::Unreal::UObject* camp,
        Visitor&& visitor
    ) -> std::size_t
    {
        if (camp == nullptr)
        {
            return 0;
        }

        auto* raw_modules =
            camp->GetValuePtrByPropertyNameInChain(
                STR("ModuleArray")
            );

        if (raw_modules == nullptr)
        {
            return 0;
        }

        const auto* modules =
            static_cast<const RawTArray*>(
                raw_modules
            );

        if (
            modules->data == nullptr ||
            modules->num <= 0 ||
            modules->num > 64 ||
            modules->max < modules->num
        )
        {
            return 0;
        }

        auto** module_objects =
            reinterpret_cast<
                RC::Unreal::UObject**
            >(modules->data);

        std::size_t count{};

        for (
            std::int32_t index = 0;
            index < modules->num;
            ++index
        )
        {
            auto* module =
                module_objects[index];

            if (
                !class_is(
                    module,
                    get_storage_module_class()
                )
            )
            {
                continue;
            }

            visitor(module);
            ++count;
        }

        return count;
    }

    auto call_pal_utility_bool(
        const RC::CharType* function_name,
        RC::Unreal::UObject* world_context
    ) -> int
    {
        if (
            function_name == nullptr ||
            world_context == nullptr
        )
        {
            return -1;
        }

        if (g_pal_utility == nullptr)
        {
            g_pal_utility =
                RC::Unreal::UObjectGlobals::StaticFindObject<
                    RC::Unreal::UObject*>(
                    nullptr,
                    nullptr,
                    STR("/Script/Pal.Default__PalUtility")
                );
        }

        if (g_pal_utility == nullptr)
        {
            return -1;
        }

        auto* function =
            g_pal_utility->GetFunctionByNameInChain(
                function_name
            );

        if (function == nullptr)
        {
            return -1;
        }

        struct Parameters
        {
            RC::Unreal::UObject* WorldContextObject{};
            bool ReturnValue{};
            std::uint8_t Padding[7]{};
        };

        Parameters parameters{};
        parameters.WorldContextObject = world_context;

        g_pal_utility->ProcessEvent(
            function,
            &parameters
        );

        return parameters.ReturnValue ? 1 : 0;
    }

    std::atomic_bool g_role_probe_requested{false};
    std::atomic_bool g_chest_association_requested{false};
    std::atomic_bool g_chest_association_running{false};
    std::atomic_bool g_chest_association_enabled{false};

    std::atomic_bool g_full_plan_registration_attempted{false};
    std::atomic_bool g_full_plan_registration_completed{false};
    std::atomic_bool g_full_plan_registration_gate_reported{false};
    std::atomic_bool g_full_plan_registration_blocked_reported{false};

    std::atomic_bool g_observability_metadata_reported{false};
    std::atomic_bool g_item_storage_linkage_reported{false};
    std::atomic_bool g_container_query_metadata_reported{false};
    std::atomic_bool g_container_query_assembly_reported{false};
    std::atomic_bool g_deep_layout_metadata_reported{false};
    std::atomic_bool g_slot_fingerprint_reported{false};
    std::atomic_bool g_slot_identity_layout_reported{false};
    std::atomic_bool g_ordinal_identity_layout_reported{false};
    std::atomic_bool g_access_owner_class_identity_reported{false};
    std::atomic_bool g_semantic_repeatability_complete{false};
    std::atomic_bool g_transport_metadata_reported{false};

    std::atomic_uint32_t g_engine_tick_entries{0};
    std::atomic_uint64_t g_chest_association_runs{0};

    RC::Unreal::Hook::GlobalCallbackId
        g_engine_tick_callback_id{
            RC::Unreal::Hook::ERROR_ID
        };

    class EngineTickEntryGuard final
    {
      public:
        EngineTickEntryGuard() noexcept
        {
            g_engine_tick_entries.fetch_add(
                1,
                std::memory_order_acq_rel
            );
        }

        ~EngineTickEntryGuard()
        {
            g_engine_tick_entries.fetch_sub(
                1,
                std::memory_order_acq_rel
            );
        }

        EngineTickEntryGuard(
            const EngineTickEntryGuard&
        ) = delete;

        auto operator=(
            const EngineTickEntryGuard&
        ) -> EngineTickEntryGuard& = delete;
    };

    class AssociationRunningGuard final
    {
      public:
        AssociationRunningGuard() = default;

        ~AssociationRunningGuard()
        {
            g_chest_association_running.store(
                false,
                std::memory_order_release
            );
        }

        AssociationRunningGuard(
            const AssociationRunningGuard&
        ) = delete;

        auto operator=(
            const AssociationRunningGuard&
        ) -> AssociationRunningGuard& = delete;
    };

    auto reset_world_state() noexcept -> void
    {
        g_is_server.store(
            -1,
            std::memory_order_release
        );

        g_is_dedicated.store(
            -1,
            std::memory_order_release
        );

        g_role_probe_requested.store(
            false,
            std::memory_order_release
        );

        g_last_discovery = Clock::time_point{};
        g_discovery_runs = 0;

        g_chest_association_requested.store(
            false,
            std::memory_order_release
        );

        g_pending_transport_requests.clear();
        g_cached_registration_plan.clear();
        g_cached_camp_id_to_camp.clear();
        g_cached_camp_to_guild.clear();
        g_cached_registration_plan_valid = false;

        g_player_current_camp.clear();
        g_pending_material_checks.clear();

        emit_marker(
            "[ModIntegratedStorageCpp] WORLD state reset"
        );
    }

    auto observe_world(RC::Unreal::UObject* context) -> bool
    {
        if (context == nullptr)
        {
            return false;
        }

        auto* world = context->GetWorld();

        if (world == nullptr)
        {
            return false;
        }

        if (g_last_world == nullptr)
        {
            g_last_world = world;

            emit_marker(
                "[ModIntegratedStorageCpp] WORLD acquired"
            );

            return true;
        }

        if (world != g_last_world)
        {
            reset_world_state();
            g_last_world = world;

            emit_marker(
                "[ModIntegratedStorageCpp] WORLD changed"
            );
        }

        return true;
    }

    auto resolve_dedicated_role(
        RC::Unreal::UObject* context
    ) -> bool
    {
        const int cached_dedicated =
            g_is_dedicated.load(
                std::memory_order_acquire
            );

        if (cached_dedicated >= 0)
        {
            return cached_dedicated == 1;
        }

        const int dedicated_result =
            call_pal_utility_bool(
                STR("IsDedicatedServer"),
                context
            );

        if (dedicated_result < 0)
        {
            return false;
        }

        const int server_result =
            call_pal_utility_bool(
                STR("IsServer"),
                context
            );

        g_is_dedicated.store(
            dedicated_result,
            std::memory_order_release
        );

        g_is_server.store(
            server_result,
            std::memory_order_release
        );

        emit_format(
            "[ModIntegratedStorageCpp] "
            "ROLE server=%d dedicated=%d",
            server_result,
            dedicated_result
        );

        if (dedicated_result == 1)
        {
            if (server_result != 1)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "ROLE IsServer context mismatch; "
                    "IsDedicatedServer is authoritative"
                );
            }

            emit_marker(
                "[ModIntegratedStorageCpp] "
                "ROLE RESULT=PASS"
            );

            return true;
        }

        emit_marker(
            "[ModIntegratedStorageCpp] "
            "ROLE RESULT=NOT_DEDICATED"
        );

        return false;
    }

    auto get_camp_discovery_buffer()
        -> std::vector<RC::Unreal::UObject*>&
    {
        // FindAllOf populates this vector inside libUE4SS.so.
        //
        // NullPrism and native mods currently carry separate static
        // C++ runtimes. Destroying the vector in this module would
        // therefore free storage allocated by the loader's runtime.
        //
        // Retain one process-lifetime buffer and reuse its capacity.
        // clear() removes pointer elements without releasing storage.
        static auto* buffer =
            new std::vector<RC::Unreal::UObject*>{};

        return *buffer;
    }

    auto get_chest_discovery_buffer()
        -> std::vector<RC::Unreal::UObject*>&
    {
        // FindAllOf populates this vector inside libUE4SS.so.
        //
        // As with the Stage-4a camp buffer, retain it for process
        // lifetime so main.so never destroys storage allocated while
        // libUE4SS.so was operating on the vector.
        static auto* buffer =
            new std::vector<RC::Unreal::UObject*>{};

        return *buffer;
    }

    auto find_discovery_context()
        -> RC::Unreal::UObject*
    {
        auto* manager =
            RC::Unreal::UObjectGlobals::FindFirstOf(
                STR("PalMapObjectManager")
            );

        if (manager != nullptr)
        {
            return manager;
        }

        auto& camps = get_camp_discovery_buffer();
        camps.clear();

        RC::Unreal::UObjectGlobals::FindAllOf(
            STR("PalBaseCampModel"),
            camps
        );

        for (auto* camp : camps)
        {
            if (camp != nullptr)
            {
                return camp;
            }
        }

        return nullptr;
    }

    auto find_role_context()
        -> RC::Unreal::UObject*
    {
        auto* player_character =
            RC::Unreal::UObjectGlobals::FindFirstOf(
                STR("PalPlayerCharacter")
            );

        if (player_character != nullptr)
        {
            return player_character;
        }

        return find_discovery_context();
    }


    auto get_role_probe_camp_buffer()
        -> std::vector<RC::Unreal::UObject*>&
    {
        // Dedicated process-lifetime buffer prevents concurrent use of
        // the worker-thread discovery vector and avoids cross-DSO release.
        static auto* buffer =
            new std::vector<RC::Unreal::UObject*>{};

        return *buffer;
    }

    auto find_game_thread_role_context()
        -> RC::Unreal::UObject*
    {
        auto* player_character =
            RC::Unreal::UObjectGlobals::FindFirstOf(
                STR("PalPlayerCharacter")
            );

        if (player_character != nullptr)
        {
            return player_character;
        }

        auto& camps = get_role_probe_camp_buffer();
        camps.clear();

        RC::Unreal::UObjectGlobals::FindAllOf(
            STR("PalBaseCampModel"),
            camps
        );

        for (auto* camp : camps)
        {
            if (camp != nullptr)
            {
                return camp;
            }
        }

        return nullptr;
    }


    auto get_registration_probe_camp_buffer()
        -> std::vector<RC::Unreal::UObject*>&
    {
        // Process-lifetime allocation avoids releasing vector storage
        // across the native-mod and NullPrism runtime boundary.
        static auto* buffer =
            new std::vector<RC::Unreal::UObject*>{};

        return *buffer;
    }

    auto run_read_only_discovery() -> void
    {
        auto& camps = get_camp_discovery_buffer();
        camps.clear();

        RC::Unreal::UObjectGlobals::FindAllOf(
            STR("PalBaseCampModel"),
            camps
        );

        std::unordered_map<
            GuildKey,
            GuildDiscovery,
            GuildKeyHash
        > guilds{};

        std::size_t valid_camps{};
        std::size_t null_camps{};
        std::size_t missing_guild_properties{};
        std::size_t zero_guild_keys{};
        std::size_t storage_count{};
        std::size_t camps_without_storage{};

        for (auto* camp : camps)
        {
            if (camp == nullptr)
            {
                ++null_camps;
                continue;
            }

            GuildKey guild_key{};

            if (!copy_guild_key(camp, guild_key))
            {
                ++missing_guild_properties;
                continue;
            }

            if (guid_is_zero(guild_key))
            {
                ++zero_guild_keys;
                continue;
            }

            ++valid_camps;

            auto& guild = guilds[guild_key];
            ++guild.camp_count;

            auto* storage = find_storage_module(camp);

            if (storage == nullptr)
            {
                ++camps_without_storage;
                continue;
            }

            const auto [ignored_iterator, inserted] =
                guild.storage_modules.insert(storage);

            static_cast<void>(ignored_iterator);

            if (inserted)
            {
                ++storage_count;
            }
        }

        ++g_discovery_runs;

        emit_format(
            "[ModIntegratedStorageCpp] DISCOVERY run=%llu "
            "objects=%zu valid_camps=%zu guilds=%zu storages=%zu "
            "null_camps=%zu missing_guild=%zu zero_guild=%zu "
            "without_storage=%zu",
            static_cast<unsigned long long>(g_discovery_runs),
            camps.size(),
            valid_camps,
            guilds.size(),
            storage_count,
            null_camps,
            missing_guild_properties,
            zero_guild_keys,
            camps_without_storage
        );

        for (const auto& [guild_key, guild] : guilds)
        {
            const auto hex = guid_to_hex(guild_key);

            emit_format(
                "[ModIntegratedStorageCpp] GUILD id=%s "
                "camps=%zu storages=%zu",
                hex.data(),
                guild.camp_count,
                guild.storage_modules.size()
            );
        }

        if (
            valid_camps > 0 &&
            !guilds.empty() &&
            storage_count > 0
        )
        {
            emit_marker(
                "[ModIntegratedStorageCpp] DISCOVERY RESULT=PASS"
            );
        }
        else if (camps.empty())
        {
            emit_marker(
                "[ModIntegratedStorageCpp] DISCOVERY RESULT=EMPTY"
            );
        }
        else
        {
            emit_marker(
                "[ModIntegratedStorageCpp] DISCOVERY RESULT=INCOMPLETE"
            );
        }
    }

    auto association_class_is(
        RC::Unreal::UObject* object,
        RC::Unreal::UClass* expected
    ) -> bool
    {
        if (
            object == nullptr ||
            expected == nullptr
        )
        {
            return false;
        }

        RC::Unreal::UStruct* current =
            object->GetClassPrivate();

        while (current != nullptr)
        {
            if (current == expected)
            {
                return true;
            }

            current = current->GetSuperStruct();
        }

        return false;
    }

    auto get_base_camp_class()
        -> RC::Unreal::UClass*
    {
        static auto* base_camp_class =
            RC::Unreal::UObjectGlobals::StaticFindObject<
                RC::Unreal::UClass*>(
                nullptr,
                nullptr,
                STR("/Script/Pal.PalBaseCampModel")
            );

        return base_camp_class;
    }


    auto stage4d7a_arm_file_present() noexcept -> bool
    {
        Dl_info module_info{};

        if (
            dladdr(
                static_cast<const void*>(
                    &ModIntegratedStorageModulePin::
                        g_process_lifetime_pin
                ),
                &module_info
            ) == 0 ||
            module_info.dli_fname == nullptr ||
            module_info.dli_fname[0] == '\0'
        )
        {
            return false;
        }

        try
        {
            std::string arm_path{
                module_info.dli_fname
            };

            arm_path += ".stage4d7a-arm";

            return
                ::access(
                    arm_path.c_str(),
                    F_OK
                ) == 0;
        }
        catch (...)
        {
            return false;
        }
    }

    auto request_read_only_chest_association() noexcept
        -> void
    {
        if (
            !g_chest_association_enabled.load(
                std::memory_order_acquire
            )
        )
        {
            return;
        }

        g_chest_association_requested.store(
            true,
            std::memory_order_release
        );
    }


    // Production FName -> string resolver, following the leak-and-cache
    // design proven safe by Stages 4D.9d-4D.9f (docs/linux-port-status.md
    // §5): FName::ToString() itself, and reading its character data via
    // RC::to_string(), are both safe on this runtime. What corrupts
    // FMallocBinned2's allocator bookkeeping is letting the ToString()
    // result's destructor run inside main.so -- not the call, and not
    // reading its content. So the ToString() result is deliberately
    // leaked (heap-allocated, never deleted) exactly once per unique
    // FName, and only the converted std::string -- built with the mod's
    // own libstdc++ allocator, safely destructible -- is cached and
    // returned. Stage 4D.9f proved this is safe at sustained per-tick
    // frequency (~121,000 leaks over 2+ continuous production hours,
    // ~1KB/leak, fully absorbed by the existing daily restart) with zero
    // crashes. References into the cache stay valid for the life of the
    // process: std::unordered_map never invalidates existing elements on
    // insertion.
    auto resolve_transport_item_name(
        const TransportItemNameKey& key
    ) noexcept -> const std::string&
    {
        static std::unordered_map<
            TransportItemNameKey,
            std::string,
            TransportItemNameKeyHash
        > cache{};

        static const std::string empty_name{};

        const auto found = cache.find(key);

        if (found != cache.end())
        {
            return found->second;
        }

        if (sizeof(RC::Unreal::FName) != key.size())
        {
            return empty_name;
        }

        try
        {
            RC::Unreal::FName reconstructed_fname{};

            std::memcpy(
                &reconstructed_fname,
                key.data(),
                key.size()
            );

            using ToStringResultType =
                decltype(reconstructed_fname.ToString());

            // Deliberately leaked: never deleted, matching the exact
            // shape stages 4D.9d-4D.9f proved safe.
            auto* const leaked_result =
                new ToStringResultType(
                    reconstructed_fname.ToString()
                );

            auto [inserted, did_insert] =
                cache.emplace(
                    key,
                    RC::to_string(*leaked_result)
                );

            static_cast<void>(did_insert);

            return inserted->second;
        }
        catch (...)
        {
            return empty_name;
        }
    }


    // Transport wire protocol pool builder. Deliberately a standalone
    // function rather than a refactor of the accepted diagnostic probe
    // below (run_read_only_transport_metadata_probe) — same bounded
    // planner-selected-chest walk, same accepted §3 shape, but kept
    // separate so this new, previously-untested call path can never
    // change the already-proven-in-production probe's behavior.
    struct TransportItemIdLayout
    {
        RC::Unreal::UScriptStruct* known_item_id{};
        RC::Unreal::FProperty* static_id_property{};
        bool ok{};
    };

    auto get_transport_item_id_layout()
        -> const TransportItemIdLayout&
    {
        static TransportItemIdLayout layout{};
        static bool resolved{};

        if (!resolved)
        {
            resolved = true;

            layout.known_item_id =
                RC::Unreal::UObjectGlobals::StaticFindObject<
                    RC::Unreal::UScriptStruct*
                >(
                    nullptr,
                    nullptr,
                    STR("/Script/Pal.PalItemId")
                );

            layout.static_id_property =
                layout.known_item_id != nullptr
                    ? layout.known_item_id->
                        GetPropertyByNameInChain(
                            STR("StaticId")
                        )
                    : nullptr;

            auto* static_id_name_property =
                RC::Unreal::CastField<
                    RC::Unreal::FNameProperty
                >(layout.static_id_property);

            layout.ok =
                layout.known_item_id != nullptr &&
                layout.known_item_id->
                    GetPropertiesSize() == 40 &&
                static_id_name_property != nullptr &&
                layout.static_id_property->
                    GetOffset_Internal() == 0 &&
                layout.static_id_property->
                    GetSize() == 8;
        }

        return layout;
    }

    struct TransportContainerManager
    {
        RC::Unreal::UObject* manager{};
        RC::Unreal::UFunction* get_container_function{};
        bool ok{};
    };

    auto get_transport_container_manager()
        -> TransportContainerManager
    {
        // Process-lifetime, never-destroyed vector: FindAllOf grows this
        // storage using libUE4SS.so's allocator. A local vector here would
        // free that storage from main.so on return, crossing the same
        // allocator boundary the Stage 4a hardening notes already
        // identified as a crash cause (see the identical pattern used by
        // the diagnostic probe's own `managers` vector).
        static auto* managers =
            new std::vector<RC::Unreal::UObject*>();

        managers->clear();

        RC::Unreal::UObjectGlobals::FindAllOf(
            STR("PalItemContainerManager"),
            *managers
        );

        TransportContainerManager result{};
        std::size_t nonnull_managers{};

        for (auto* candidate : *managers)
        {
            if (candidate == nullptr)
            {
                continue;
            }

            ++nonnull_managers;

            if (result.manager == nullptr)
            {
                result.manager = candidate;
            }
        }

        result.get_container_function =
            result.manager != nullptr
                ? result.manager->
                    GetFunctionByNameInChain(
                        STR("GetContainer")
                    )
                : nullptr;

        result.ok =
            managers->size() == 1 &&
            nonnull_managers == 1 &&
            result.manager != nullptr &&
            result.get_container_function !=
                nullptr &&
            result.get_container_function->
                GetParmsSize() == 24;

        return result;
    }

    // Bounded planner-selected chest walk: identical shape to §3's
    // accepted transport pool (every foreign same-guild chest ->
    // module -> container-id -> container-manager -> reflected slot
    // array), just parameterized by an arbitrary requester camp instead
    // of the diagnostic probe's fixed "first candidate" selection.
    // Never touches ItemContainerMap_InServer, never bulk-enumerates
    // PalItemContainer — both remain blocked (§6 items 1 and 3).
    auto build_transport_pool_for_request(
        const RegistrationPlanGuild& guild_plan,
        RC::Unreal::UObject* requester_camp
    ) -> std::vector<
        std::pair<TransportItemNameKey, std::int64_t>
    >
    {
        std::vector<
            std::pair<TransportItemNameKey, std::int64_t>
        > ordered_pool{};

        const auto& item_layout =
            get_transport_item_id_layout();

        if (!item_layout.ok)
        {
            return ordered_pool;
        }

        const auto manager =
            get_transport_container_manager();

        if (!manager.ok)
        {
            return ordered_pool;
        }

        std::unordered_map<
            TransportItemNameKey,
            std::int64_t,
            TransportItemNameKeyHash
        > pool{};

        for (
            const auto& [chest, chest_camp] :
                guild_plan.chest_camps
        )
        {
            if (
                chest == nullptr ||
                chest_camp == nullptr ||
                chest_camp == requester_camp
            )
            {
                continue;
            }

            try
            {
                auto* module_function =
                    chest->GetFunctionByNameInChain(
                        STR("GetItemContainerModule")
                    );

                if (
                    module_function == nullptr ||
                    module_function->
                        GetParmsSize() != 8
                )
                {
                    continue;
                }

                std::array<std::byte, 8>
                    module_buffer{};

                chest->ProcessEvent(
                    module_function,
                    module_buffer.data()
                );

                RC::Unreal::UObject* module{};

                std::memcpy(
                    &module,
                    module_buffer.data(),
                    sizeof(module)
                );

                if (module == nullptr)
                {
                    continue;
                }

                auto* id_function =
                    module->GetFunctionByNameInChain(
                        STR("GetContainerId")
                    );

                if (
                    id_function == nullptr ||
                    id_function->
                        GetParmsSize() != 16
                )
                {
                    continue;
                }

                std::array<std::byte, 16> id_buffer{};

                module->ProcessEvent(
                    id_function,
                    id_buffer.data()
                );

                GuildKey container_id{};

                std::memcpy(
                    container_id.data(),
                    id_buffer.data(),
                    container_id.size()
                );

                if (guid_is_zero(container_id))
                {
                    continue;
                }

                std::array<std::byte, 24> get_buffer{};

                std::memcpy(
                    get_buffer.data(),
                    container_id.data(),
                    container_id.size()
                );

                manager.manager->ProcessEvent(
                    manager.get_container_function,
                    get_buffer.data()
                );

                RC::Unreal::UObject* container{};

                std::memcpy(
                    &container,
                    get_buffer.data() + 16,
                    sizeof(container)
                );

                if (container == nullptr)
                {
                    continue;
                }

                auto* slot_property =
                    container->
                        GetPropertyByNameInChain(
                            STR("ItemSlotArray")
                        );

                auto* array_property =
                    RC::Unreal::CastField<
                        RC::Unreal::FArrayProperty
                    >(slot_property);

                auto* object_property =
                    array_property != nullptr
                        ? RC::Unreal::CastField<
                            RC::Unreal::
                                FObjectPropertyBase
                        >(
                            array_property->
                                GetInner()
                        )
                        : nullptr;

                if (
                    array_property == nullptr ||
                    object_property == nullptr
                )
                {
                    continue;
                }

                RC::Unreal::
                    FScriptArrayHelper_InContainer
                        helper(
                            array_property,
                            container
                        );

                for (
                    std::int32_t slot_index{};
                    slot_index < helper.Num();
                    ++slot_index
                )
                {
                    auto* slot =
                        object_property->
                            GetObjectPropertyValue(
                                helper.GetRawPtr(
                                    slot_index
                                )
                            );

                    if (slot == nullptr)
                    {
                        continue;
                    }

                    auto* item_property =
                        RC::Unreal::CastField<
                            RC::Unreal::
                                FStructProperty
                        >(
                            slot->
                                GetPropertyByNameInChain(
                                    STR("ItemId")
                                )
                        );

                    auto* stack_property =
                        RC::Unreal::CastField<
                            RC::Unreal::
                                FNumericProperty
                        >(
                            slot->
                                GetPropertyByNameInChain(
                                    STR("StackCount")
                                )
                        );

                    if (
                        item_property == nullptr ||
                        stack_property == nullptr ||
                        item_property->
                            GetStruct().Get() !=
                            item_layout.
                                known_item_id ||
                        item_property->
                            GetSize() != 40 ||
                        stack_property->
                            GetSize() != 4
                    )
                    {
                        continue;
                    }

                    auto* item_data =
                        item_property->
                            ContainerPtrToValuePtr<
                                void
                            >(slot);

                    auto* stack_data =
                        stack_property->
                            ContainerPtrToValuePtr<
                                void
                            >(slot);

                    if (
                        item_data == nullptr ||
                        stack_data == nullptr
                    )
                    {
                        continue;
                    }

                    std::int32_t stack_count{};

                    std::memcpy(
                        &stack_count,
                        stack_data,
                        sizeof(stack_count)
                    );

                    if (stack_count <= 0)
                    {
                        continue;
                    }

                    TransportItemNameKey
                        static_name{};

                    std::memcpy(
                        static_name.data(),
                        static_cast<
                            const std::byte*
                        >(item_data) +
                            item_layout.
                                static_id_property->
                                GetOffset_Internal(),
                        static_name.size()
                    );

                    pool[static_name] +=
                        static_cast<std::int64_t>(
                            stack_count
                        );
                }
            }
            catch (...)
            {
                continue;
            }
        }

        ordered_pool.reserve(pool.size());

        for (const auto& [key, quantity] : pool)
        {
            ordered_pool.emplace_back(key, quantity);
        }

        return ordered_pool;
    }

    auto build_transport_reply_payload(
        const std::vector<
            std::pair<TransportItemNameKey, std::int64_t>
        >& ordered_pool
    ) -> std::string
    {
        std::string payload = "IS1|";

        for (const auto& [key, quantity] : ordered_pool)
        {
            if (quantity <= 0)
            {
                continue;
            }

            const auto& name =
                resolve_transport_item_name(key);

            if (name.empty())
            {
                continue;
            }

            payload += name;
            payload += ':';
            payload += std::to_string(quantity);
            payload += ',';
        }

        return payload;
    }

    auto send_transport_reply(
        RC::Unreal::UObject* controller,
        const std::string& payload
    ) -> bool
    {
        if (controller == nullptr)
        {
            return false;
        }

        auto* reply_function =
            controller->GetFunctionByNameInChain(
                STR(
                    "Debug_ReceiveCheatCommand_"
                    "ToClient"
                )
            );

        if (reply_function == nullptr)
        {
            return false;
        }

        struct ReplyParams
        {
            RawTArray Message{};
        };

        ReplyParams params{};
        params.Message =
            ascii_widen_leaked(payload);

        controller->ProcessEvent(
            reply_function,
            &params
        );

        return true;
    }

    auto process_pending_transport_requests() -> void
    {
        if (g_pending_transport_requests.empty())
        {
            return;
        }

        auto requests =
            std::move(g_pending_transport_requests);

        g_pending_transport_requests.clear();

        if (!g_cached_registration_plan_valid)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "TRANSPORT_REQUEST RESULT=NO_PLAN"
            );

            return;
        }

        for (const auto& request : requests)
        {
            try
            {
                const auto camp_iterator =
                    g_cached_camp_id_to_camp.find(
                        request.requester_camp_id
                    );

                if (
                    camp_iterator ==
                        g_cached_camp_id_to_camp.end()
                )
                {
                    emit_marker(
                        "[ModIntegratedStorageCpp] "
                        "TRANSPORT_REQUEST "
                        "RESULT=CAMP_NOT_FOUND"
                    );

                    continue;
                }

                auto* requester_camp =
                    camp_iterator->second;

                const auto guild_iterator =
                    g_cached_camp_to_guild.find(
                        requester_camp
                    );

                if (
                    guild_iterator ==
                        g_cached_camp_to_guild.end()
                )
                {
                    emit_marker(
                        "[ModIntegratedStorageCpp] "
                        "TRANSPORT_REQUEST "
                        "RESULT=NO_GUILD"
                    );

                    continue;
                }

                const auto plan_iterator =
                    g_cached_registration_plan.find(
                        guild_iterator->second
                    );

                if (
                    plan_iterator ==
                        g_cached_registration_plan.
                            end()
                )
                {
                    emit_marker(
                        "[ModIntegratedStorageCpp] "
                        "TRANSPORT_REQUEST "
                        "RESULT=NO_PLAN_GUILD"
                    );

                    continue;
                }

                const auto ordered_pool =
                    build_transport_pool_for_request(
                        plan_iterator->second,
                        requester_camp
                    );

                const auto payload =
                    build_transport_reply_payload(
                        ordered_pool
                    );

                const bool sent =
                    send_transport_reply(
                        request.controller,
                        payload
                    );

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "TRANSPORT_REQUEST RESULT=%s "
                    "items=%zu len=%zu",
                    sent ? "SENT" : "SEND_FAILED",
                    ordered_pool.size(),
                    payload.size()
                );
            }
            catch (...)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "TRANSPORT_REQUEST "
                    "RESULT=EXCEPTION"
                );
            }
        }
    }

    // Resolves the real UPalItemContainer objects for every chest whose
    // CURRENT camp satisfies `camp_matches`. Deliberately duplicates
    // build_transport_pool_for_request's own chest -> module -> container
    // resolution rather than sharing it, so this newer, less-proven path
    // can never change that already-verified one's behavior.
    template <typename CampPredicate>
    auto resolve_camp_containers(
        const RegistrationPlanGuild& guild_plan,
        CampPredicate camp_matches
    ) -> std::vector<RC::Unreal::UObject*>
    {
        std::vector<RC::Unreal::UObject*> containers{};

        const auto manager = get_transport_container_manager();

        if (!manager.ok)
        {
            return containers;
        }

        for (const auto& [chest, chest_camp] : guild_plan.chest_camps)
        {
            if (
                chest == nullptr ||
                chest_camp == nullptr ||
                !camp_matches(chest_camp)
            )
            {
                continue;
            }

            try
            {
                auto* module_function =
                    chest->GetFunctionByNameInChain(
                        STR("GetItemContainerModule")
                    );

                if (
                    module_function == nullptr ||
                    module_function->GetParmsSize() != 8
                )
                {
                    continue;
                }

                std::array<std::byte, 8> module_buffer{};

                chest->ProcessEvent(
                    module_function,
                    module_buffer.data()
                );

                RC::Unreal::UObject* module{};

                std::memcpy(
                    &module,
                    module_buffer.data(),
                    sizeof(module)
                );

                if (module == nullptr)
                {
                    continue;
                }

                auto* id_function =
                    module->GetFunctionByNameInChain(
                        STR("GetContainerId")
                    );

                if (
                    id_function == nullptr ||
                    id_function->GetParmsSize() != 16
                )
                {
                    continue;
                }

                std::array<std::byte, 16> id_buffer{};

                module->ProcessEvent(
                    id_function,
                    id_buffer.data()
                );

                GuildKey container_id{};

                std::memcpy(
                    container_id.data(),
                    id_buffer.data(),
                    container_id.size()
                );

                if (guid_is_zero(container_id))
                {
                    continue;
                }

                std::array<std::byte, 24> get_buffer{};

                std::memcpy(
                    get_buffer.data(),
                    container_id.data(),
                    container_id.size()
                );

                manager.manager->ProcessEvent(
                    manager.get_container_function,
                    get_buffer.data()
                );

                RC::Unreal::UObject* container{};

                std::memcpy(
                    &container,
                    get_buffer.data() + 16,
                    sizeof(container)
                );

                if (container != nullptr)
                {
                    containers.push_back(container);
                }
            }
            catch (...)
            {
                continue;
            }
        }

        return containers;
    }

    struct ItemSlotMatch
    {
        void* value_ptr{};
        std::int32_t stack_count{};
    };

    // Walks ItemSlotArray on every container looking for slots whose
    // ItemId.StaticId matches `target_id`. When `require_positive` is
    // true (source/donor search), empty slots are skipped; when false
    // (destination search), the first matching slot is returned
    // regardless of its current count, since an existing-but-empty slot
    // is still a safe place to deposit into.
    auto find_item_slots(
        const std::vector<RC::Unreal::UObject*>& containers,
        const TransportItemIdLayout& item_layout,
        const RC::Unreal::FName& target_id,
        bool require_positive
    ) -> std::vector<ItemSlotMatch>
    {
        std::vector<ItemSlotMatch> matches{};

        for (auto* container : containers)
        {
            if (container == nullptr)
            {
                continue;
            }

            try
            {
                auto* slot_property =
                    container->GetPropertyByNameInChain(
                        STR("ItemSlotArray")
                    );

                auto* array_property =
                    RC::Unreal::CastField<
                        RC::Unreal::FArrayProperty
                    >(slot_property);

                auto* object_property =
                    array_property != nullptr
                        ? RC::Unreal::CastField<
                            RC::Unreal::FObjectPropertyBase
                        >(array_property->GetInner())
                        : nullptr;

                if (
                    array_property == nullptr ||
                    object_property == nullptr
                )
                {
                    continue;
                }

                RC::Unreal::FScriptArrayHelper_InContainer
                    helper(array_property, container);

                for (
                    std::int32_t slot_index{};
                    slot_index < helper.Num();
                    ++slot_index
                )
                {
                    auto* slot =
                        object_property->
                            GetObjectPropertyValue(
                                helper.GetRawPtr(slot_index)
                            );

                    if (slot == nullptr)
                    {
                        continue;
                    }

                    auto* item_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FStructProperty
                        >(
                            slot->GetPropertyByNameInChain(
                                STR("ItemId")
                            )
                        );

                    auto* stack_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FNumericProperty
                        >(
                            slot->GetPropertyByNameInChain(
                                STR("StackCount")
                            )
                        );

                    if (
                        item_property == nullptr ||
                        stack_property == nullptr ||
                        item_property->GetStruct().Get() !=
                            item_layout.known_item_id ||
                        item_property->GetSize() != 40 ||
                        stack_property->GetSize() != 4
                    )
                    {
                        continue;
                    }

                    auto* item_data =
                        item_property->
                            ContainerPtrToValuePtr<void>(
                                slot
                            );

                    auto* stack_data =
                        stack_property->
                            ContainerPtrToValuePtr<void>(
                                slot
                            );

                    if (
                        item_data == nullptr ||
                        stack_data == nullptr
                    )
                    {
                        continue;
                    }

                    RC::Unreal::FName slot_id{};

                    std::memcpy(
                        &slot_id,
                        static_cast<const std::byte*>(
                            item_data
                        ) +
                            item_layout.static_id_property->
                                GetOffset_Internal(),
                        sizeof(slot_id)
                    );

                    if (
                        std::memcmp(
                            &slot_id,
                            &target_id,
                            sizeof(slot_id)
                        ) != 0
                    )
                    {
                        continue;
                    }

                    std::int32_t stack_count{};

                    std::memcpy(
                        &stack_count,
                        stack_data,
                        sizeof(stack_count)
                    );

                    if (
                        require_positive &&
                        stack_count <= 0
                    )
                    {
                        continue;
                    }

                    matches.push_back(
                        ItemSlotMatch{
                            stack_data,
                            stack_count
                        }
                    );
                }
            }
            catch (...)
            {
                continue;
            }
        }

        return matches;
    }

    // Drains the queue the material-check hook fills and, for any camp
    // that's short on a material the guild has surplus of elsewhere,
    // physically moves just the shortfall into an EXISTING local slot for
    // that item. If no local slot for the item exists at all, this logs
    // and skips rather than creating one (a new UPalItemSlot would need
    // its own safe creation/registration path, not yet implemented).
    // Runs entirely in the same proven-safe tick context as
    // run_read_only_chest_association - never inside a hook callback.
    auto process_pending_material_reconciliation() -> void
    {
        if (g_pending_material_checks.empty())
        {
            return;
        }

        auto checks = std::move(g_pending_material_checks);
        g_pending_material_checks.clear();

        if (!g_cached_registration_plan_valid)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "MATERIAL_RECONCILE RESULT=NO_PLAN"
            );

            return;
        }

        const auto& item_layout = get_transport_item_id_layout();

        if (!item_layout.ok)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "MATERIAL_RECONCILE RESULT=NO_ITEM_LAYOUT"
            );

            return;
        }

        const RC::Unreal::FName none_name{};

        for (const auto& check : checks)
        {
            try
            {
                if (check.camp == nullptr)
                {
                    continue;
                }

                const auto guild_iterator =
                    g_cached_camp_to_guild.find(check.camp);

                if (
                    guild_iterator ==
                        g_cached_camp_to_guild.end()
                )
                {
                    emit_marker(
                        "[ModIntegratedStorageCpp] "
                        "MATERIAL_RECONCILE RESULT=NO_GUILD"
                    );

                    continue;
                }

                const auto plan_iterator =
                    g_cached_registration_plan.find(
                        guild_iterator->second
                    );

                if (
                    plan_iterator ==
                        g_cached_registration_plan.end()
                )
                {
                    emit_marker(
                        "[ModIntegratedStorageCpp] "
                        "MATERIAL_RECONCILE "
                        "RESULT=NO_PLAN_GUILD"
                    );

                    continue;
                }

                const auto& guild_plan = plan_iterator->second;
                auto* target_camp = check.camp;

                const auto local_containers =
                    resolve_camp_containers(
                        guild_plan,
                        [target_camp](
                            RC::Unreal::UObject* camp
                        ) {
                            return camp == target_camp;
                        }
                    );

                const auto foreign_containers =
                    resolve_camp_containers(
                        guild_plan,
                        [target_camp](
                            RC::Unreal::UObject* camp
                        ) {
                            return camp != target_camp;
                        }
                    );

                for (const auto& material : check.materials)
                {
                    if (
                        material.needed <= 0 ||
                        std::memcmp(
                            &material.id,
                            &none_name,
                            sizeof(none_name)
                        ) == 0
                    )
                    {
                        continue;
                    }

                    const auto local_matches =
                        find_item_slots(
                            local_containers,
                            item_layout,
                            material.id,
                            true
                        );

                    std::int64_t local_total{};

                    for (const auto& match : local_matches)
                    {
                        local_total += match.stack_count;
                    }

                    const std::int64_t shortfall =
                        static_cast<std::int64_t>(
                            material.needed
                        ) - local_total;

                    if (shortfall <= 0)
                    {
                        continue;
                    }

                    const auto destination =
                        find_item_slots(
                            local_containers,
                            item_layout,
                            material.id,
                            false
                        );

                    if (destination.empty())
                    {
                        emit_format(
                            "[ModIntegratedStorageCpp] "
                            "MATERIAL_RECONCILE "
                            "RESULT=NO_LOCAL_SLOT camp=%p "
                            "needed=%d local=%lld",
                            static_cast<void*>(target_camp),
                            material.needed,
                            static_cast<long long>(
                                local_total
                            )
                        );

                        continue;
                    }

                    auto* dest_value_ptr =
                        destination.front().value_ptr;

                    std::int32_t dest_count =
                        destination.front().stack_count;

                    const auto foreign_matches =
                        find_item_slots(
                            foreign_containers,
                            item_layout,
                            material.id,
                            true
                        );

                    std::int64_t remaining = shortfall;
                    std::int64_t moved{};

                    for (
                        const auto& source : foreign_matches
                    )
                    {
                        if (remaining <= 0)
                        {
                            break;
                        }

                        const std::int64_t take =
                            std::min<std::int64_t>(
                                remaining,
                                source.stack_count
                            );

                        if (take <= 0)
                        {
                            continue;
                        }

                        const std::int32_t new_source_count =
                            source.stack_count -
                                static_cast<std::int32_t>(
                                    take
                                );

                        std::memcpy(
                            source.value_ptr,
                            &new_source_count,
                            sizeof(new_source_count)
                        );

                        dest_count +=
                            static_cast<std::int32_t>(take);

                        remaining -= take;
                        moved += take;
                    }

                    if (moved > 0)
                    {
                        std::memcpy(
                            dest_value_ptr,
                            &dest_count,
                            sizeof(dest_count)
                        );
                    }

                    emit_format(
                        "[ModIntegratedStorageCpp] "
                        "MATERIAL_RECONCILE RESULT=%s "
                        "camp=%p needed=%d local=%lld "
                        "moved=%lld",
                        moved >= shortfall
                            ? "TOPPED_UP"
                            : (moved > 0
                                ? "PARTIAL"
                                : "NO_SOURCE"),
                        static_cast<void*>(target_camp),
                        material.needed,
                        static_cast<long long>(local_total),
                        static_cast<long long>(moved)
                    );
                }
            }
            catch (...)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "MATERIAL_RECONCILE RESULT=EXCEPTION"
                );
            }
        }
    }

    auto on_enter_base_camp_hook_pre(
        RC::Unreal::UnrealScriptFunctionCallableContext&,
        void*
    ) -> void
    {
        g_enter_base_camp_hook_fires.fetch_add(
            1,
            std::memory_order_relaxed
        );
    }

    auto on_enter_base_camp_hook_post(
        RC::Unreal::UnrealScriptFunctionCallableContext&
            context,
        void*
    ) -> void
    {
        if (
            !g_build_gate_layout.ok ||
            !RC::Unreal::IsInGameThreadRaw() ||
            context.Context == nullptr
        )
        {
            return;
        }

        struct RawParams
        {
            std::array<std::byte, 16> bytes{};
        };

        auto& params = context.GetParams<RawParams>();

        RC::Unreal::UObject* camp{};

        std::memcpy(
            &camp,
            params.bytes.data() +
                g_build_gate_layout.
                    enter_base_camp_model_offset,
            sizeof(camp)
        );

        if (camp != nullptr)
        {
            g_player_current_camp[context.Context] = camp;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "BUILD_CAMP_ENTER owner=%p camp=%p "
                "tracked=%zu",
                static_cast<void*>(context.Context),
                static_cast<void*>(camp),
                g_player_current_camp.size()
            );
        }
    }

    auto on_exit_base_camp_hook_pre(
        RC::Unreal::UnrealScriptFunctionCallableContext&,
        void*
    ) -> void
    {
        g_exit_base_camp_hook_fires.fetch_add(
            1,
            std::memory_order_relaxed
        );
    }

    auto on_exit_base_camp_hook_post(
        RC::Unreal::UnrealScriptFunctionCallableContext&
            context,
        void*
    ) -> void
    {
        if (
            !RC::Unreal::IsInGameThreadRaw() ||
            context.Context == nullptr
        )
        {
            return;
        }

        g_player_current_camp.erase(context.Context);

        emit_format(
            "[ModIntegratedStorageCpp] "
            "BUILD_CAMP_EXIT owner=%p tracked=%zu",
            static_cast<void*>(context.Context),
            g_player_current_camp.size()
        );
    }

    auto on_build_material_check_hook_pre(
        RC::Unreal::UnrealScriptFunctionCallableContext&,
        void*
    ) -> void
    {
        g_material_check_hook_fires.fetch_add(
            1,
            std::memory_order_relaxed
        );
    }

    auto on_build_material_check_hook_post(
        RC::Unreal::UnrealScriptFunctionCallableContext&
            context,
        void*
    ) -> void
    {
        if (
            g_transport_mod_shutting_down.load(
                std::memory_order_acquire
            ) ||
            !g_build_gate_layout.ok ||
            !RC::Unreal::IsInGameThreadRaw() ||
            context.Context == nullptr
        )
        {
            return;
        }

        const auto camp_iterator =
            g_player_current_camp.find(context.Context);

        if (camp_iterator == g_player_current_camp.end())
        {
            // Logged every fire deliberately, for now: this is the
            // exact branch that would silently explain zero
            // MATERIAL_RECONCILE activity if OnEnterBaseCamp never
            // populated the map for this player/camp combination.
            emit_format(
                "[ModIntegratedStorageCpp] "
                "BUILD_MATERIAL_CHECK owner=%p "
                "RESULT=CAMP_NOT_TRACKED tracked=%zu",
                static_cast<void*>(context.Context),
                g_player_current_camp.size()
            );

            return;
        }

        if (
            g_pending_material_checks.size() >=
                MaxPendingMaterialChecks
        )
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "BUILD_MATERIAL_CHECK RESULT=QUEUE_FULL"
            );

            return;
        }

        struct RawParams
        {
            std::array<std::byte, 0xA0> bytes{};
        };

        auto& params = context.GetParams<RawParams>();

        PendingMaterialCheck check{};
        check.camp = camp_iterator->second;

        for (
            std::size_t index{};
            index < check.materials.size();
            ++index
        )
        {
            std::memcpy(
                &check.materials[index].id,
                params.bytes.data() +
                    g_build_gate_layout.
                        materials[index].id_offset,
                sizeof(check.materials[index].id)
            );

            std::memcpy(
                &check.materials[index].needed,
                params.bytes.data() +
                    g_build_gate_layout.
                        materials[index].count_offset,
                sizeof(check.materials[index].needed)
            );
        }

        g_pending_material_checks.push_back(check);

        emit_format(
            "[ModIntegratedStorageCpp] "
            "BUILD_MATERIAL_CHECK RESULT=QUEUED camp=%p "
            "queued=%zu",
            static_cast<void*>(check.camp),
            g_pending_material_checks.size()
        );
    }

    // Diagnostic-only probe for RequestBuild_ToServer (see the comment
    // on BuildGateLayout::request_build_id_offset). Does not feed the
    // reconciliation queue yet - only confirms whether this RPC fires
    // server-side at all, and whether BuildObjectId reads back sane.
    auto on_request_build_probe_hook_pre(
        RC::Unreal::UnrealScriptFunctionCallableContext&,
        void*
    ) -> void
    {
        g_request_build_hook_fires.fetch_add(
            1,
            std::memory_order_relaxed
        );
    }

    auto on_request_build_probe_hook_post(
        RC::Unreal::UnrealScriptFunctionCallableContext&
            context,
        void*
    ) -> void
    {
        if (
            !g_build_gate_layout.request_build_probe_ok ||
            !RC::Unreal::IsInGameThreadRaw()
        )
        {
            emit_format(
                "[ModIntegratedStorageCpp] "
                "BUILD_REQUEST_PROBE RESULT=FIRED "
                "probe_ok=%d",
                g_build_gate_layout.
                    request_build_probe_ok ? 1 : 0
            );

            return;
        }

        struct RawParams
        {
            std::array<std::byte, 16> bytes{};
        };

        auto& params = context.GetParams<RawParams>();

        TransportItemNameKey build_id{};

        std::memcpy(
            build_id.data(),
            params.bytes.data() +
                g_build_gate_layout.
                    request_build_id_offset,
            build_id.size()
        );

        // Raw hex only here (pure byte formatting, zero engine calls) -
        // FName::ToString() (what resolve_transport_item_name() would
        // call) was only ever proven safe from the tick context, not
        // from inside a hook callback; resolving the human-readable name
        // is deferred to the tick just like everything else this file
        // does from a hook.
        const auto hex = transport_name_key_to_hex(build_id);

        emit_format(
            "[ModIntegratedStorageCpp] "
            "BUILD_REQUEST_PROBE RESULT=FIRED owner=%p "
            "build_id_hex=%s",
            static_cast<void*>(context.Context),
            hex.data()
        );
    }

    auto on_transport_request_hook_pre(
        RC::Unreal::UnrealScriptFunctionCallableContext&,
        void*
    ) -> void
    {
        g_transport_request_hook_fires.fetch_add(
            1,
            std::memory_order_relaxed
        );
    }

    auto on_transport_request_hook_post(
        RC::Unreal::UnrealScriptFunctionCallableContext&
            context,
        void*
    ) -> void
    {
        if (
            g_transport_mod_shutting_down.load(
                std::memory_order_acquire
            )
        )
        {
            return;
        }

        if (!RC::Unreal::IsInGameThreadRaw())
        {
            return;
        }

        auto* controller = context.Context;

        if (controller == nullptr)
        {
            return;
        }

        struct RequestParams
        {
            RawTArray Command{};
        };

        auto& params =
            context.GetParams<RequestParams>();

        const auto text =
            ascii_narrow(params.Command);

        GuildKey requester_camp_id{};

        if (
            !parse_transport_request_guid(
                text,
                requester_camp_id
            )
        )
        {
            // Not our sentinel -> a genuine cheat command. Its native
            // handler is inert for non-admins on a dedicated server;
            // leave it alone.
            return;
        }

        if (
            g_pending_transport_requests.size() >=
                MaxPendingTransportRequests
        )
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "TRANSPORT_REQUEST RESULT=QUEUE_FULL"
            );

            return;
        }

        g_pending_transport_requests.push_back(
            PendingTransportRequest{
                controller,
                requester_camp_id
            }
        );

        emit_format(
            "[ModIntegratedStorageCpp] "
            "TRANSPORT_REQUEST queued=%zu",
            g_pending_transport_requests.size()
        );
    }


    auto run_read_only_transport_metadata_probe(
        const std::unordered_map<
            GuildKey,
            RegistrationPlanGuild,
            GuildKeyHash
        >& registration_plan,
        bool plan_complete,
        std::uint64_t planned_run
    ) noexcept -> void
    {
        if (!plan_complete)
        {
            return;
        }

        bool expected_reported{false};

        if (
            !g_transport_metadata_reported.
                compare_exchange_strong(
                    expected_reported,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                )
        )
        {
            return;
        }

        try
        {
            auto* player_controller_cdo =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UObject*
                    >(
                        nullptr,
                        nullptr,
                        STR(
                            "/Script/Pal."
                            "Default__PalPlayerController"
                        )
                    );

            auto describe_rpc =
                [](
                    RC::Unreal::UObject* cdo,
                    const RC::Unreal::TCHAR* name,
                    const char* label
                ) -> bool
                {
                    auto* function =
                        cdo != nullptr
                            ? cdo->
                                GetFunctionByNameInChain(
                                    name
                                )
                            : nullptr;

                    std::uint64_t parameters{};
                    std::uint64_t inputs{};
                    std::uint64_t returns{};
                    std::uint64_t string_inputs{};

                    std::int32_t input_offset{-1};
                    std::int32_t input_size{-1};
                    std::int32_t input_class_index{-1};
                    std::uint64_t input_flags{};

                    static const auto
                        str_property_name =
                            RC::Unreal::FName(
                                STR("StrProperty"),
                                RC::Unreal::FNAME_Find
                            );

                    const auto str_property_index =
                        str_property_name.
                            GetComparisonIndex();

                    if (function != nullptr)
                    {
                        for (
                            auto* property :
                                function->
                                    ForEachProperty()
                        )
                        {
                            if (
                                property == nullptr ||
                                !property->
                                    HasAnyPropertyFlags(
                                        RC::Unreal::
                                            CPF_Parm
                                    )
                            )
                            {
                                continue;
                            }

                            ++parameters;

                            if (
                                property->
                                    HasAnyPropertyFlags(
                                        RC::Unreal::
                                            CPF_ReturnParm
                                    )
                            )
                            {
                                ++returns;
                                continue;
                            }

                            ++inputs;

                            const auto
                                property_class_index =
                                    property->
                                        GetClass().
                                        GetFName().
                                        GetComparisonIndex();

                            const bool string_property =
                                property_class_index ==
                                    str_property_index;

                            if (string_property)
                            {
                                ++string_inputs;

                                if (input_offset < 0)
                                {
                                    input_offset =
                                        property->
                                            GetOffset_Internal();

                                    input_size =
                                        property->
                                            GetSize();

                                    input_class_index =
                                        property_class_index;

                                    input_flags =
                                        static_cast<
                                            std::uint64_t
                                        >(
                                            property->
                                                GetPropertyFlags()
                                        );
                                }
                            }
                        }
                    }

                    const auto parms =
                        function != nullptr
                            ? function->GetParmsSize()
                            : 0;

                    const bool passed =
                        function != nullptr &&
                        parms == 16 &&
                        parameters == 1 &&
                        inputs == 1 &&
                        returns == 0 &&
                        string_inputs == 1 &&
                        input_offset == 0 &&
                        input_size == 16;

                    emit_format(
                        "[ModIntegratedStorageCpp] "
                        "TRANSPORT_RPC label=%s "
                        "function=%d parms=%d "
                        "parameters=%llu inputs=%llu "
                        "returns=%llu str_inputs=%llu "
                        "offset=%d size=%d class_index=%d "
                        "expected_str_class_index=%d "
                        "flags=%llu passed=%d",
                        label,
                        function != nullptr ? 1 : 0,
                        static_cast<int>(parms),
                        static_cast<
                            unsigned long long
                        >(parameters),
                        static_cast<
                            unsigned long long
                        >(inputs),
                        static_cast<
                            unsigned long long
                        >(returns),
                        static_cast<
                            unsigned long long
                        >(string_inputs),
                        input_offset,
                        input_size,
                        input_class_index,
                        str_property_index,
                        static_cast<
                            unsigned long long
                        >(input_flags),
                        passed ? 1 : 0
                    );

                    return passed;
                };

            const bool request_rpc_ok =
                describe_rpc(
                    player_controller_cdo,
                    STR(
                        "Debug_CheatCommand_ToServer"
                    ),
                    "request"
                );

            const bool reply_rpc_ok =
                describe_rpc(
                    player_controller_cdo,
                    STR(
                        "Debug_ReceiveCheatCommand_"
                        "ToClient"
                    ),
                    "reply"
                );

            auto* known_guid =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR(
                            "/Script/CoreUObject.Guid"
                        )
                    );

            auto* known_item_id =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/Pal.PalItemId")
                    );

            auto* static_id_property =
                known_item_id != nullptr
                    ? known_item_id->
                        GetPropertyByNameInChain(
                            STR("StaticId")
                        )
                    : nullptr;

            auto* static_id_name_property =
                RC::Unreal::CastField<
                    RC::Unreal::FNameProperty
                >(static_id_property);

            const bool static_id_layout_ok =
                known_item_id != nullptr &&
                known_item_id->
                    GetPropertiesSize() == 40 &&
                static_id_name_property != nullptr &&
                static_id_property->
                    GetOffset_Internal() == 0 &&
                static_id_property->
                    GetSize() == 8;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "TRANSPORT_ITEM_ID_LAYOUT "
                "item_struct=%d struct_size=%d "
                "static_id=%d static_name=%d "
                "static_offset=%d static_size=%d "
                "passed=%d",
                known_item_id != nullptr ? 1 : 0,
                known_item_id != nullptr
                    ? known_item_id->
                        GetPropertiesSize()
                    : -1,
                static_id_property != nullptr
                    ? 1
                    : 0,
                static_id_name_property != nullptr
                    ? 1
                    : 0,
                static_id_property != nullptr
                    ? static_id_property->
                        GetOffset_Internal()
                    : -1,
                static_id_property != nullptr
                    ? static_id_property->
                        GetSize()
                    : -1,
                static_id_layout_ok ? 1 : 0
            );

            std::unordered_set<
                RC::Unreal::UObject*
            > unique_camps{};

            for (
                const auto& [
                    ignored_guild_key,
                    guild
                ] : registration_plan
            )
            {
                static_cast<void>(
                    ignored_guild_key
                );

                for (
                    const auto& [
                        ignored_storage,
                        camp
                    ] : guild.storage_camps
                )
                {
                    static_cast<void>(
                        ignored_storage
                    );

                    if (camp != nullptr)
                    {
                        unique_camps.insert(camp);
                    }
                }

                for (
                    const auto& [
                        ignored_chest,
                        camp
                    ] : guild.chest_camps
                )
                {
                    static_cast<void>(
                        ignored_chest
                    );

                    if (camp != nullptr)
                    {
                        unique_camps.insert(camp);
                    }
                }
            }

            std::unordered_set<
                GuildKey,
                GuildKeyHash
            > camp_ids{};

            std::uint64_t camp_id_properties{};
            std::uint64_t camp_id_structs{};
            std::uint64_t camp_id_guid_types{};
            std::uint64_t camp_id_size_16{};
            std::uint64_t camp_id_nonzero{};
            std::uint64_t camp_id_duplicates{};
            std::uint64_t camp_id_exceptions{};

            for (auto* camp : unique_camps)
            {
                try
                {
                    auto* id_property =
                        camp->
                            GetPropertyByNameInChain(
                                STR("ID")
                            );

                    auto* id_struct_property =
                        RC::Unreal::CastField<
                            RC::Unreal::
                                FStructProperty
                        >(id_property);

                    if (id_property != nullptr)
                    {
                        ++camp_id_properties;
                    }

                    if (id_struct_property != nullptr)
                    {
                        ++camp_id_structs;
                    }

                    const bool guid_type =
                        id_struct_property != nullptr &&
                        known_guid != nullptr &&
                        id_struct_property->
                            GetStruct().Get() ==
                            known_guid;

                    if (guid_type)
                    {
                        ++camp_id_guid_types;
                    }

                    const bool size_16 =
                        id_property != nullptr &&
                        id_property->GetSize() == 16;

                    if (size_16)
                    {
                        ++camp_id_size_16;
                    }

                    GuildKey camp_id{};

                    if (
                        id_property == nullptr ||
                        id_struct_property == nullptr ||
                        !guid_type ||
                        !size_16
                    )
                    {
                        continue;
                    }

                    auto* value =
                        id_property->
                            ContainerPtrToValuePtr<
                                void
                            >(camp);

                    if (value == nullptr)
                    {
                        continue;
                    }

                    std::memcpy(
                        camp_id.data(),
                        value,
                        camp_id.size()
                    );

                    const bool nonzero =
                        !guid_is_zero(camp_id);

                    if (nonzero)
                    {
                        ++camp_id_nonzero;

                        const auto [
                            ignored_id,
                            inserted
                        ] =
                            camp_ids.insert(
                                camp_id
                            );

                        static_cast<void>(
                            ignored_id
                        );

                        if (!inserted)
                        {
                            ++camp_id_duplicates;
                        }
                    }

                    GuildKey guild{};
                    const bool guild_ok =
                        copy_guild_key(
                            camp,
                            guild
                        );

                    const auto id_hex =
                        guid_to_hex(camp_id);

                    const auto guild_hex =
                        guid_to_hex(guild);

                    emit_format(
                        "[ModIntegratedStorageCpp] "
                        "TRANSPORT_CAMP_ID "
                        "id=%s guild=%s guild_ok=%d "
                        "offset=%d size=%d guid_type=%d "
                        "nonzero=%d",
                        id_hex.data(),
                        guild_hex.data(),
                        guild_ok ? 1 : 0,
                        id_property->
                            GetOffset_Internal(),
                        id_property->GetSize(),
                        guid_type ? 1 : 0,
                        nonzero ? 1 : 0
                    );
                }
                catch (...)
                {
                    ++camp_id_exceptions;
                }
            }

            const bool camp_id_ok =
                !unique_camps.empty() &&
                camp_id_properties ==
                    unique_camps.size() &&
                camp_id_structs ==
                    unique_camps.size() &&
                camp_id_guid_types ==
                    unique_camps.size() &&
                camp_id_size_16 ==
                    unique_camps.size() &&
                camp_id_nonzero ==
                    unique_camps.size() &&
                camp_ids.size() ==
                    unique_camps.size() &&
                camp_id_duplicates == 0 &&
                camp_id_exceptions == 0;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "TRANSPORT_CAMP_ID_SUMMARY "
                "camps=%zu properties=%llu structs=%llu "
                "guid_types=%llu size16=%llu "
                "nonzero=%llu unique=%zu duplicates=%llu "
                "exceptions=%llu passed=%d",
                unique_camps.size(),
                static_cast<
                    unsigned long long
                >(camp_id_properties),
                static_cast<
                    unsigned long long
                >(camp_id_structs),
                static_cast<
                    unsigned long long
                >(camp_id_guid_types),
                static_cast<
                    unsigned long long
                >(camp_id_size_16),
                static_cast<
                    unsigned long long
                >(camp_id_nonzero),
                camp_ids.size(),
                static_cast<
                    unsigned long long
                >(camp_id_duplicates),
                static_cast<
                    unsigned long long
                >(camp_id_exceptions),
                camp_id_ok ? 1 : 0
            );

            std::vector<GuildKey> guild_order{};
            guild_order.reserve(
                registration_plan.size()
            );

            for (
                const auto& [
                    guild_key,
                    ignored_guild
                ] : registration_plan
            )
            {
                static_cast<void>(
                    ignored_guild
                );
                guild_order.push_back(
                    guild_key
                );
            }

            std::sort(
                guild_order.begin(),
                guild_order.end()
            );

            const RegistrationPlanGuild*
                selected_guild_plan{};

            GuildKey selected_guild{};

            RC::Unreal::UObject*
                selected_requester_camp{};

            GuildKey selected_requester_id{};

            for (const auto& guild_key : guild_order)
            {
                const auto iterator =
                    registration_plan.find(
                        guild_key
                    );

                if (
                    iterator ==
                        registration_plan.end()
                )
                {
                    continue;
                }

                const auto& guild =
                    iterator->second;

                std::unordered_set<
                    RC::Unreal::UObject*
                > guild_camps{};

                for (
                    const auto& [
                        ignored_storage,
                        camp
                    ] : guild.storage_camps
                )
                {
                    static_cast<void>(
                        ignored_storage
                    );

                    if (camp != nullptr)
                    {
                        guild_camps.insert(camp);
                    }
                }

                if (
                    guild_camps.size() < 2 ||
                    guild.chest_camps.empty()
                )
                {
                    continue;
                }

                std::vector<
                    std::pair<
                        GuildKey,
                        RC::Unreal::UObject*
                    >
                > ordered_camps{};

                for (auto* camp : guild_camps)
                {
                    auto* id_property =
                        camp->
                            GetPropertyByNameInChain(
                                STR("ID")
                            );

                    auto* id_struct_property =
                        RC::Unreal::CastField<
                            RC::Unreal::
                                FStructProperty
                        >(id_property);

                    if (
                        id_property == nullptr ||
                        id_struct_property == nullptr ||
                        known_guid == nullptr ||
                        id_struct_property->
                            GetStruct().Get() !=
                            known_guid ||
                        id_property->GetSize() != 16
                    )
                    {
                        continue;
                    }

                    auto* value =
                        id_property->
                            ContainerPtrToValuePtr<
                                void
                            >(camp);

                    if (value == nullptr)
                    {
                        continue;
                    }

                    GuildKey id{};
                    std::memcpy(
                        id.data(),
                        value,
                        id.size()
                    );

                    if (guid_is_zero(id))
                    {
                        continue;
                    }

                    ordered_camps.emplace_back(
                        id,
                        camp
                    );
                }

                if (ordered_camps.size() < 2)
                {
                    continue;
                }

                std::sort(
                    ordered_camps.begin(),
                    ordered_camps.end(),
                    [](
                        const auto& left,
                        const auto& right
                    )
                    {
                        return left.first <
                            right.first;
                    }
                );

                auto* candidate_requester =
                    ordered_camps.front().second;

                std::size_t candidate_foreign_chests{};

                for (
                    const auto& [
                        ignored_chest,
                        chest_camp
                    ] : guild.chest_camps
                )
                {
                    static_cast<void>(
                        ignored_chest
                    );

                    if (
                        chest_camp != nullptr &&
                        chest_camp !=
                            candidate_requester
                    )
                    {
                        ++candidate_foreign_chests;
                    }
                }

                if (candidate_foreign_chests == 0)
                {
                    continue;
                }

                selected_guild_plan =
                    &guild;

                selected_guild =
                    guild_key;

                selected_requester_camp =
                    candidate_requester;

                selected_requester_id =
                    ordered_camps.front().first;

                break;
            }

            static auto* managers =
                new std::vector<
                    RC::Unreal::UObject*
                >();

            managers->clear();

            RC::Unreal::UObjectGlobals::
                FindAllOf(
                    STR("PalItemContainerManager"),
                    *managers
                );

            RC::Unreal::UObject*
                item_container_manager{};

            std::size_t nonnull_managers{};

            for (auto* candidate : *managers)
            {
                if (candidate == nullptr)
                {
                    continue;
                }

                ++nonnull_managers;

                if (
                    item_container_manager ==
                        nullptr
                )
                {
                    item_container_manager =
                        candidate;
                }
            }

            auto* get_container_function =
                item_container_manager != nullptr
                    ? item_container_manager->
                        GetFunctionByNameInChain(
                            STR("GetContainer")
                        )
                    : nullptr;

            const bool manager_ok =
                managers->size() == 1 &&
                nonnull_managers == 1 &&
                item_container_manager !=
                    nullptr &&
                get_container_function !=
                    nullptr &&
                get_container_function->
                    GetParmsSize() == 24;

            std::unordered_map<
                TransportItemNameKey,
                std::int64_t,
                TransportItemNameKeyHash
            > pool{};

            std::uint64_t foreign_chests{};
            std::uint64_t module_functions{};
            std::uint64_t modules{};
            std::uint64_t id_functions{};
            std::uint64_t ids{};
            std::uint64_t resolved_containers{};
            std::uint64_t slot_arrays{};
            std::uint64_t slot_objects{};
            std::uint64_t positive_slots{};
            std::uint64_t fully_read_slots{};
            std::uint64_t slot_layout_failures{};
            std::uint64_t pool_exceptions{};
            std::int64_t total_quantity{};

            if (
                selected_guild_plan != nullptr &&
                selected_requester_camp !=
                    nullptr &&
                manager_ok &&
                static_id_layout_ok
            )
            {
                for (
                    const auto& [
                        chest,
                        chest_camp
                    ] :
                        selected_guild_plan->
                            chest_camps
                )
                {
                    if (
                        chest == nullptr ||
                        chest_camp == nullptr ||
                        chest_camp ==
                            selected_requester_camp
                    )
                    {
                        continue;
                    }

                    ++foreign_chests;

                    try
                    {
                        auto* module_function =
                            chest->
                                GetFunctionByNameInChain(
                                    STR(
                                        "GetItemContainerModule"
                                    )
                                );

                        if (
                            module_function == nullptr ||
                            module_function->
                                GetParmsSize() != 8
                        )
                        {
                            continue;
                        }

                        ++module_functions;

                        std::array<std::byte, 8>
                            module_buffer{};

                        chest->ProcessEvent(
                            module_function,
                            module_buffer.data()
                        );

                        RC::Unreal::UObject* module{};

                        std::memcpy(
                            &module,
                            module_buffer.data(),
                            sizeof(module)
                        );

                        if (module == nullptr)
                        {
                            continue;
                        }

                        ++modules;

                        auto* id_function =
                            module->
                                GetFunctionByNameInChain(
                                    STR("GetContainerId")
                                );

                        if (
                            id_function == nullptr ||
                            id_function->
                                GetParmsSize() != 16
                        )
                        {
                            continue;
                        }

                        ++id_functions;

                        std::array<std::byte, 16>
                            id_buffer{};

                        module->ProcessEvent(
                            id_function,
                            id_buffer.data()
                        );

                        GuildKey container_id{};

                        std::memcpy(
                            container_id.data(),
                            id_buffer.data(),
                            container_id.size()
                        );

                        if (guid_is_zero(container_id))
                        {
                            continue;
                        }

                        ++ids;

                        std::array<std::byte, 24>
                            get_buffer{};

                        std::memcpy(
                            get_buffer.data(),
                            container_id.data(),
                            container_id.size()
                        );

                        item_container_manager->
                            ProcessEvent(
                                get_container_function,
                                get_buffer.data()
                            );

                        RC::Unreal::UObject* container{};

                        std::memcpy(
                            &container,
                            get_buffer.data() + 16,
                            sizeof(container)
                        );

                        if (container == nullptr)
                        {
                            continue;
                        }

                        ++resolved_containers;

                        auto* slot_property =
                            container->
                                GetPropertyByNameInChain(
                                    STR("ItemSlotArray")
                                );

                        auto* array_property =
                            RC::Unreal::CastField<
                                RC::Unreal::
                                    FArrayProperty
                            >(slot_property);

                        auto* object_property =
                            array_property != nullptr
                                ? RC::Unreal::
                                    CastField<
                                        RC::Unreal::
                                            FObjectPropertyBase
                                    >(
                                        array_property->
                                            GetInner()
                                    )
                                : nullptr;

                        if (
                            array_property == nullptr ||
                            object_property == nullptr
                        )
                        {
                            ++slot_layout_failures;
                            continue;
                        }

                        ++slot_arrays;

                        RC::Unreal::
                            FScriptArrayHelper_InContainer
                                helper(
                                    array_property,
                                    container
                                );

                        for (
                            std::int32_t slot_index{};
                            slot_index <
                                helper.Num();
                            ++slot_index
                        )
                        {
                            auto* slot =
                                object_property->
                                    GetObjectPropertyValue(
                                        helper.GetRawPtr(
                                            slot_index
                                        )
                                    );

                            if (slot == nullptr)
                            {
                                continue;
                            }

                            ++slot_objects;

                            auto* item_property =
                                RC::Unreal::CastField<
                                    RC::Unreal::
                                        FStructProperty
                                >(
                                    slot->
                                        GetPropertyByNameInChain(
                                            STR("ItemId")
                                        )
                                );

                            auto* stack_property =
                                RC::Unreal::CastField<
                                    RC::Unreal::
                                        FNumericProperty
                                >(
                                    slot->
                                        GetPropertyByNameInChain(
                                            STR("StackCount")
                                        )
                                );

                            if (
                                item_property == nullptr ||
                                stack_property == nullptr ||
                                known_item_id == nullptr ||
                                item_property->
                                    GetStruct().Get() !=
                                    known_item_id ||
                                item_property->GetSize() !=
                                    40 ||
                                stack_property->GetSize() !=
                                    4
                            )
                            {
                                ++slot_layout_failures;
                                continue;
                            }

                            auto* item_data =
                                item_property->
                                    ContainerPtrToValuePtr<
                                        void
                                    >(slot);

                            auto* stack_data =
                                stack_property->
                                    ContainerPtrToValuePtr<
                                        void
                                    >(slot);

                            if (
                                item_data == nullptr ||
                                stack_data == nullptr
                            )
                            {
                                ++slot_layout_failures;
                                continue;
                            }

                            std::int32_t stack_count{};

                            std::memcpy(
                                &stack_count,
                                stack_data,
                                sizeof(stack_count)
                            );

                            if (stack_count <= 0)
                            {
                                continue;
                            }

                            ++positive_slots;

                            TransportItemNameKey
                                static_name{};

                            std::memcpy(
                                static_name.data(),
                                static_cast<
                                    const std::byte*
                                >(item_data) +
                                    static_id_property->
                                        GetOffset_Internal(),
                                static_name.size()
                            );

                            pool[static_name] +=
                                static_cast<
                                    std::int64_t
                                >(stack_count);

                            total_quantity +=
                                static_cast<
                                    std::int64_t
                                >(stack_count);

                            ++fully_read_slots;
                        }
                    }
                    catch (...)
                    {
                        ++pool_exceptions;
                    }
                }
            }

            std::vector<
                std::pair<
                    TransportItemNameKey,
                    std::int64_t
                >
            > ordered_pool{};

            ordered_pool.reserve(pool.size());

            for (
                const auto& [key, quantity] :
                    pool
            )
            {
                ordered_pool.emplace_back(
                    key,
                    quantity
                );
            }

            std::sort(
                ordered_pool.begin(),
                ordered_pool.end(),
                [](
                    const auto& left,
                    const auto& right
                )
                {
                    return left.first <
                        right.first;
                }
            );

            constexpr std::size_t
                k_max_pool_report_items = 16;

            for (
                std::size_t index{};
                index < ordered_pool.size() &&
                index < k_max_pool_report_items;
                ++index
            )
            {
                const auto key_hex =
                    transport_name_key_to_hex(
                        ordered_pool[index].first
                    );

                const auto& item_name =
                    resolve_transport_item_name(
                        ordered_pool[index].first
                    );

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "TRANSPORT_POOL_ITEM "
                    "index=%zu name_key=%s name=%s quantity=%lld",
                    index,
                    key_hex.data(),
                    item_name.c_str(),
                    static_cast<long long>(
                        ordered_pool[index].second
                    )
                );
            }

            const auto guild_hex =
                guid_to_hex(selected_guild);

            const auto requester_hex =
                guid_to_hex(
                    selected_requester_id
                );

            const bool pool_ok =
                selected_guild_plan != nullptr &&
                selected_requester_camp !=
                    nullptr &&
                !guid_is_zero(selected_guild) &&
                !guid_is_zero(
                    selected_requester_id
                ) &&
                manager_ok &&
                foreign_chests > 0 &&
                module_functions ==
                    foreign_chests &&
                modules ==
                    foreign_chests &&
                id_functions ==
                    foreign_chests &&
                ids ==
                    foreign_chests &&
                resolved_containers ==
                    foreign_chests &&
                slot_arrays ==
                    foreign_chests &&
                positive_slots > 0 &&
                fully_read_slots ==
                    positive_slots &&
                slot_layout_failures == 0 &&
                pool_exceptions == 0 &&
                !pool.empty() &&
                total_quantity > 0;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "TRANSPORT_POOL "
                "run=%llu guild=%s requester=%s "
                "manager_objects=%zu manager_nonnull=%zu "
                "manager_ok=%d foreign_chests=%llu "
                "module_functions=%llu modules=%llu "
                "id_functions=%llu ids=%llu "
                "containers=%llu slot_arrays=%llu "
                "slot_objects=%llu positive_slots=%llu "
                "fully_read_slots=%llu "
                "layout_failures=%llu exceptions=%llu "
                "unique_items=%zu total_quantity=%lld "
                "passed=%d",
                static_cast<
                    unsigned long long
                >(planned_run),
                guild_hex.data(),
                requester_hex.data(),
                managers->size(),
                nonnull_managers,
                manager_ok ? 1 : 0,
                static_cast<
                    unsigned long long
                >(foreign_chests),
                static_cast<
                    unsigned long long
                >(module_functions),
                static_cast<
                    unsigned long long
                >(modules),
                static_cast<
                    unsigned long long
                >(id_functions),
                static_cast<
                    unsigned long long
                >(ids),
                static_cast<
                    unsigned long long
                >(resolved_containers),
                static_cast<
                    unsigned long long
                >(slot_arrays),
                static_cast<
                    unsigned long long
                >(slot_objects),
                static_cast<
                    unsigned long long
                >(positive_slots),
                static_cast<
                    unsigned long long
                >(fully_read_slots),
                static_cast<
                    unsigned long long
                >(slot_layout_failures),
                static_cast<
                    unsigned long long
                >(pool_exceptions),
                pool.size(),
                static_cast<long long>(
                    total_quantity
                ),
                pool_ok ? 1 : 0
            );

            const bool passed =
                request_rpc_ok &&
                reply_rpc_ok &&
                camp_id_ok &&
                static_id_layout_ok &&
                pool_ok;

            if (passed)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "TRANSPORT_METADATA RESULT=PASS"
                );
            }
            else
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "TRANSPORT_METADATA RESULT=INCOMPLETE"
                );
            }
        }
        catch (...)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "TRANSPORT_METADATA RESULT=EXCEPTION"
            );
        }
    }


    auto run_read_only_registration_metadata_probe(
        RC::Unreal::UObject* chest,
        RC::Unreal::UObject* target_storage
    ) -> RegistrationCallMetadata
    {
        const auto run =
            g_chest_association_runs.load(
                std::memory_order_acquire
            ) + 1;

        RC::Unreal::UFunction* function{};

        if (target_storage != nullptr)
        {
            function =
                target_storage->
                    GetFunctionByNameInChain(
                        STR(
                            "OnAvailableConcreteModel_"
                            "ServerInternal"
                        )
                    );
        }

        std::size_t parameter_bytes{};
        std::size_t input_parameters{};
        std::size_t object_parameters{};

        RC::Unreal::FObjectProperty*
            object_parameter{};

        if (function != nullptr)
        {
            parameter_bytes =
                static_cast<std::size_t>(
                    function->GetParmsSize()
                );

            for (
                auto* property :
                    function->ForEachProperty()
            )
            {
                if (
                    property == nullptr ||
                    !property->HasAnyPropertyFlags(
                        RC::Unreal::CPF_Parm
                    ) ||
                    property->HasAnyPropertyFlags(
                        RC::Unreal::CPF_ReturnParm
                    )
                )
                {
                    continue;
                }

                ++input_parameters;

                auto* object_property =
                    RC::Unreal::CastField<
                        RC::Unreal::FObjectProperty
                    >(property);

                if (object_property != nullptr)
                {
                    ++object_parameters;

                    if (object_parameter == nullptr)
                    {
                        object_parameter =
                            object_property;
                    }
                }
            }
        }

        std::int32_t parameter_offset{-1};
        std::int32_t property_size{-1};
        std::uint64_t property_flags{};

        RC::Unreal::UClass* property_class{};

        if (object_parameter != nullptr)
        {
            parameter_offset =
                object_parameter->
                    GetOffset_Internal();

            property_size =
                object_parameter->GetSize();

            property_flags =
                static_cast<std::uint64_t>(
                    object_parameter->
                        GetPropertyFlags()
                );

            property_class =
                object_parameter->
                    GetPropertyClass();
        }

        const bool bounds_valid =
            parameter_offset >= 0 &&
            property_size ==
                static_cast<std::int32_t>(
                    sizeof(
                        RC::Unreal::UObject*
                    )
                ) &&
            parameter_bytes >=
                sizeof(
                    RC::Unreal::UObject*
                ) &&
            static_cast<std::size_t>(
                parameter_offset
            ) <=
                parameter_bytes -
                    sizeof(
                        RC::Unreal::UObject*
                    );

        const bool chest_compatible =
            class_is(
                chest,
                property_class
            );

        const bool passed =
            chest != nullptr &&
            target_storage != nullptr &&
            function != nullptr &&
            input_parameters == 1 &&
            object_parameters == 1 &&
            object_parameter != nullptr &&
            property_class != nullptr &&
            bounds_valid &&
            chest_compatible;

        emit_format(
            "[ModIntegratedStorageCpp] REG_META "
            "run=%llu candidate=%d target=%d "
            "function=%d parms=%zu inputs=%zu "
            "object_inputs=%zu offset=%d size=%d "
            "flags=0x%llx property_class=%d "
            "compatible=%d",
            static_cast<unsigned long long>(
                run
            ),
            chest != nullptr ? 1 : 0,
            target_storage != nullptr ? 1 : 0,
            function != nullptr ? 1 : 0,
            parameter_bytes,
            input_parameters,
            object_parameters,
            parameter_offset,
            property_size,
            static_cast<unsigned long long>(
                property_flags
            ),
            property_class != nullptr ? 1 : 0,
            chest_compatible ? 1 : 0
        );

        if (passed)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "REG_META RESULT=PASS"
            );
        }
        else
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "REG_META RESULT=INCOMPLETE"
            );
        }
        return RegistrationCallMetadata{
            function,
            parameter_bytes,
            parameter_offset,
            property_size,
            passed
        };
    }



    struct ObservabilityMetadataCounters
    {
        std::uint64_t properties_found{};
        std::uint64_t functions_found{};
        std::uint64_t property_exceptions{};
        std::uint64_t function_exceptions{};
    };

    auto emit_observability_property_metadata(
        RC::Unreal::UObject* object,
        const char* object_label,
        const RC::Unreal::TCHAR* candidate_name,
        const char* candidate_label,
        ObservabilityMetadataCounters& counters
    ) noexcept -> void
    {
        if (object == nullptr)
        {
            emit_format(
                "[ModIntegratedStorageCpp] "
                "OBS_PROPERTY object=%s candidate=%s "
                "exists=0 reason=null_object",
                object_label,
                candidate_label
            );

            return;
        }

        try
        {
            auto* property =
                object->GetPropertyByNameInChain(
                    candidate_name
                );

            if (property == nullptr)
            {
                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "OBS_PROPERTY object=%s candidate=%s "
                    "exists=0",
                    object_label,
                    candidate_label
                );

                return;
            }

            ++counters.properties_found;

            auto* object_property =
                RC::Unreal::CastField<
                    RC::Unreal::FObjectPropertyBase
                >(property);

            auto* weak_object_property =
                RC::Unreal::CastField<
                    RC::Unreal::FWeakObjectProperty
                >(property);

            auto* array_property =
                RC::Unreal::CastField<
                    RC::Unreal::FArrayProperty
                >(property);

            auto* set_property =
                RC::Unreal::CastField<
                    RC::Unreal::FSetProperty
                >(property);

            auto* map_property =
                RC::Unreal::CastField<
                    RC::Unreal::FMapProperty
                >(property);

            auto* struct_property =
                RC::Unreal::CastField<
                    RC::Unreal::FStructProperty
                >(property);

            const char* kind = "other";

            if (object_property != nullptr)
            {
                kind = "object";
            }
            else if (weak_object_property != nullptr)
            {
                kind = "weak_object";
            }
            else if (array_property != nullptr)
            {
                kind = "array";
            }
            else if (set_property != nullptr)
            {
                kind = "set";
            }
            else if (map_property != nullptr)
            {
                kind = "map";
            }
            else if (struct_property != nullptr)
            {
                kind = "struct";
            }

            bool accepted_class{};

            if (object_property != nullptr)
            {
                accepted_class =
                    object_property->GetPropertyClass() !=
                    nullptr;
            }

            bool inner_object{};

            if (array_property != nullptr)
            {
                auto* inner =
                    array_property->GetInner();

                inner_object =
                    RC::Unreal::CastField<
                        RC::Unreal::FObjectPropertyBase
                    >(inner) != nullptr ||
                    RC::Unreal::CastField<
                        RC::Unreal::FWeakObjectProperty
                    >(inner) != nullptr;
            }

            bool set_element_object{};

            if (set_property != nullptr)
            {
                auto* element =
                    set_property->GetElementProp();

                set_element_object =
                    RC::Unreal::CastField<
                        RC::Unreal::FObjectPropertyBase
                    >(element) != nullptr ||
                    RC::Unreal::CastField<
                        RC::Unreal::FWeakObjectProperty
                    >(element) != nullptr;
            }

            emit_format(
                "[ModIntegratedStorageCpp] "
                "OBS_PROPERTY object=%s candidate=%s "
                "exists=1 kind=%s offset=%d size=%d "
                "element_size=%d accepted_class=%d "
                "inner_object=%d set_element_object=%d",
                object_label,
                candidate_label,
                kind,
                property->GetOffset_Internal(),
                property->GetSize(),
                property->GetElementSize(),
                accepted_class ? 1 : 0,
                inner_object ? 1 : 0,
                set_element_object ? 1 : 0
            );
        }
        catch (...)
        {
            ++counters.property_exceptions;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "OBS_PROPERTY object=%s candidate=%s "
                "exists=0 reason=exception",
                object_label,
                candidate_label
            );
        }
    }

    auto emit_observability_function_metadata(
        RC::Unreal::UObject* object,
        const char* object_label,
        const RC::Unreal::TCHAR* candidate_name,
        const char* candidate_label,
        ObservabilityMetadataCounters& counters
    ) noexcept -> void
    {
        if (object == nullptr)
        {
            emit_format(
                "[ModIntegratedStorageCpp] "
                "OBS_FUNCTION object=%s candidate=%s "
                "exists=0 reason=null_object",
                object_label,
                candidate_label
            );

            return;
        }

        try
        {
            auto* function =
                object->GetFunctionByNameInChain(
                    candidate_name
                );

            if (function == nullptr)
            {
                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "OBS_FUNCTION object=%s candidate=%s "
                    "exists=0",
                    object_label,
                    candidate_label
                );

                return;
            }

            ++counters.functions_found;

            std::uint64_t inputs{};
            std::uint64_t returns{};
            std::uint64_t object_inputs{};
            std::uint64_t object_returns{};
            std::uint64_t array_inputs{};
            std::uint64_t array_returns{};
            std::uint64_t set_inputs{};
            std::uint64_t set_returns{};
            std::uint64_t map_inputs{};
            std::uint64_t map_returns{};

            for (
                auto* property :
                function->ForEachProperty()
            )
            {
                if (
                    property == nullptr ||
                    !property->HasAnyPropertyFlags(
                        RC::Unreal::EPropertyFlags::
                            CPF_Parm
                    )
                )
                {
                    continue;
                }

                const bool is_return =
                    property->HasAnyPropertyFlags(
                        RC::Unreal::EPropertyFlags::
                            CPF_ReturnParm
                    );

                if (is_return)
                {
                    ++returns;
                }
                else
                {
                    ++inputs;
                }

                const bool is_object =
                    RC::Unreal::CastField<
                        RC::Unreal::FObjectPropertyBase
                    >(property) != nullptr ||
                    RC::Unreal::CastField<
                        RC::Unreal::FWeakObjectProperty
                    >(property) != nullptr;

                const bool is_array =
                    RC::Unreal::CastField<
                        RC::Unreal::FArrayProperty
                    >(property) != nullptr;

                const bool is_set =
                    RC::Unreal::CastField<
                        RC::Unreal::FSetProperty
                    >(property) != nullptr;

                const bool is_map =
                    RC::Unreal::CastField<
                        RC::Unreal::FMapProperty
                    >(property) != nullptr;

                if (is_return)
                {
                    object_returns +=
                        is_object ? 1U : 0U;

                    array_returns +=
                        is_array ? 1U : 0U;

                    set_returns +=
                        is_set ? 1U : 0U;

                    map_returns +=
                        is_map ? 1U : 0U;
                }
                else
                {
                    object_inputs +=
                        is_object ? 1U : 0U;

                    array_inputs +=
                        is_array ? 1U : 0U;

                    set_inputs +=
                        is_set ? 1U : 0U;

                    map_inputs +=
                        is_map ? 1U : 0U;
                }
            }

            emit_format(
                "[ModIntegratedStorageCpp] "
                "OBS_FUNCTION object=%s candidate=%s "
                "exists=1 parms=%zu inputs=%llu "
                "returns=%llu object_inputs=%llu "
                "object_returns=%llu array_inputs=%llu "
                "array_returns=%llu set_inputs=%llu "
                "set_returns=%llu map_inputs=%llu "
                "map_returns=%llu",
                object_label,
                candidate_label,
                function->GetParmsSize(),
                static_cast<unsigned long long>(
                    inputs
                ),
                static_cast<unsigned long long>(
                    returns
                ),
                static_cast<unsigned long long>(
                    object_inputs
                ),
                static_cast<unsigned long long>(
                    object_returns
                ),
                static_cast<unsigned long long>(
                    array_inputs
                ),
                static_cast<unsigned long long>(
                    array_returns
                ),
                static_cast<unsigned long long>(
                    set_inputs
                ),
                static_cast<unsigned long long>(
                    set_returns
                ),
                static_cast<unsigned long long>(
                    map_inputs
                ),
                static_cast<unsigned long long>(
                    map_returns
                )
            );
        }
        catch (...)
        {
            ++counters.function_exceptions;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "OBS_FUNCTION object=%s candidate=%s "
                "exists=0 reason=exception",
                object_label,
                candidate_label
            );
        }
    }

    auto run_read_only_observability_metadata_probe(
        RC::Unreal::UObject* chest,
        RC::Unreal::UObject* target_storage,
        bool plan_complete
    ) noexcept -> void
    {
        if (
            !plan_complete ||
            chest == nullptr ||
            target_storage == nullptr
        )
        {
            return;
        }

        bool expected_reported{false};

        if (
            !g_observability_metadata_reported.
                compare_exchange_strong(
                    expected_reported,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                )
        )
        {
            return;
        }

        ObservabilityMetadataCounters counters{};

        try
        {
            static auto* guild_storages =
                new std::vector<
                    RC::Unreal::UObject*
                >();

            guild_storages->clear();

            RC::Unreal::UObjectGlobals::FindAllOf(
                STR("PalGuildItemStorage"),
                *guild_storages
            );

            RC::Unreal::UObject* guild_storage{};

            for (auto* candidate : *guild_storages)
            {
                if (candidate != nullptr)
                {
                    guild_storage = candidate;
                    break;
                }
            }

            emit_format(
                "[ModIntegratedStorageCpp] "
                "OBS_META guild_storage_objects=%zu "
                "guild_storage_candidate=%d",
                guild_storages->size(),
                guild_storage != nullptr ? 1 : 0
            );

            emit_observability_property_metadata(
                target_storage,
                "storage",
                STR("GuildItemStorage"),
                "GuildItemStorage",
                counters
            );

            emit_observability_property_metadata(
                target_storage,
                "storage",
                STR("ItemContainer"),
                "ItemContainer",
                counters
            );

            emit_observability_property_metadata(
                target_storage,
                "storage",
                STR("ItemStorage"),
                "ItemStorage",
                counters
            );

            emit_observability_property_metadata(
                target_storage,
                "storage",
                STR("ConcreteModel"),
                "ConcreteModel",
                counters
            );

            emit_observability_property_metadata(
                target_storage,
                "storage",
                STR("CachedConcreteModel"),
                "CachedConcreteModel",
                counters
            );

            emit_observability_property_metadata(
                target_storage,
                "storage",
                STR("OwnerConcreteModel"),
                "OwnerConcreteModel",
                counters
            );

            emit_observability_function_metadata(
                target_storage,
                "storage",
                STR(
                    "OnAvailableConcreteModel_"
                    "ServerInternal"
                ),
                "OnAvailableConcreteModel_ServerInternal",
                counters
            );

            emit_observability_function_metadata(
                target_storage,
                "storage",
                STR(
                    "OnNotAvailableConcreteModel_"
                    "ServerInternal"
                ),
                "OnNotAvailableConcreteModel_ServerInternal",
                counters
            );

            emit_observability_function_metadata(
                target_storage,
                "storage",
                STR(
                    "OnUpdateItemContainerIn"
                    "GuildItemStorage"
                ),
                "OnUpdateItemContainerInGuildItemStorage",
                counters
            );

            emit_observability_function_metadata(
                target_storage,
                "storage",
                STR("GetItemContainer"),
                "GetItemContainer",
                counters
            );

            emit_observability_function_metadata(
                target_storage,
                "storage",
                STR(
                    "GetItemContainer_"
                    "ItemContainerAccessInterface"
                ),
                "GetItemContainer_ItemContainerAccessInterface",
                counters
            );

            emit_observability_property_metadata(
                chest,
                "chest",
                STR("ConcreteModel"),
                "ConcreteModel",
                counters
            );

            emit_observability_property_metadata(
                chest,
                "chest",
                STR("CachedConcreteModel"),
                "CachedConcreteModel",
                counters
            );

            emit_observability_property_metadata(
                chest,
                "chest",
                STR("OwnerConcreteModel"),
                "OwnerConcreteModel",
                counters
            );

            emit_observability_property_metadata(
                chest,
                "chest",
                STR("ItemContainer"),
                "ItemContainer",
                counters
            );

            emit_observability_property_metadata(
                chest,
                "chest",
                STR("GuildItemStorage"),
                "GuildItemStorage",
                counters
            );

            emit_observability_function_metadata(
                chest,
                "chest",
                STR("GetItemContainer"),
                "GetItemContainer",
                counters
            );

            emit_observability_function_metadata(
                chest,
                "chest",
                STR(
                    "GetItemContainer_"
                    "ItemContainerAccessInterface"
                ),
                "GetItemContainer_ItemContainerAccessInterface",
                counters
            );

            emit_observability_function_metadata(
                chest,
                "chest",
                STR("GetConcreteModel"),
                "GetConcreteModel",
                counters
            );

            emit_observability_function_metadata(
                chest,
                "chest",
                STR("GetOwnerMapObjectConcreteModel"),
                "GetOwnerMapObjectConcreteModel",
                counters
            );

            emit_observability_property_metadata(
                guild_storage,
                "guild_storage",
                STR("ItemContainer"),
                "ItemContainer",
                counters
            );

            emit_observability_property_metadata(
                guild_storage,
                "guild_storage",
                STR("ItemContainers"),
                "ItemContainers",
                counters
            );

            emit_observability_property_metadata(
                guild_storage,
                "guild_storage",
                STR("ItemContainerArray"),
                "ItemContainerArray",
                counters
            );

            emit_observability_property_metadata(
                guild_storage,
                "guild_storage",
                STR("ContainerArray"),
                "ContainerArray",
                counters
            );

            emit_observability_property_metadata(
                guild_storage,
                "guild_storage",
                STR("ConcreteModel"),
                "ConcreteModel",
                counters
            );

            emit_observability_property_metadata(
                guild_storage,
                "guild_storage",
                STR("ConcreteModels"),
                "ConcreteModels",
                counters
            );

            emit_observability_property_metadata(
                guild_storage,
                "guild_storage",
                STR("RegisteredConcreteModels"),
                "RegisteredConcreteModels",
                counters
            );

            emit_observability_property_metadata(
                guild_storage,
                "guild_storage",
                STR("GroupId"),
                "GroupId",
                counters
            );

            emit_observability_property_metadata(
                guild_storage,
                "guild_storage",
                STR("GroupIdBelongTo"),
                "GroupIdBelongTo",
                counters
            );

            emit_observability_property_metadata(
                guild_storage,
                "guild_storage",
                STR("GuildId"),
                "GuildId",
                counters
            );

            emit_observability_property_metadata(
                guild_storage,
                "guild_storage",
                STR("GuildID"),
                "GuildID",
                counters
            );

            emit_observability_function_metadata(
                guild_storage,
                "guild_storage",
                STR("GetItemContainer"),
                "GetItemContainer",
                counters
            );

            emit_observability_function_metadata(
                guild_storage,
                "guild_storage",
                STR("FindItemContainer"),
                "FindItemContainer",
                counters
            );

            emit_observability_function_metadata(
                guild_storage,
                "guild_storage",
                STR("TryGetItemContainer"),
                "TryGetItemContainer",
                counters
            );

            emit_observability_function_metadata(
                guild_storage,
                "guild_storage",
                STR("ContainsItemContainer"),
                "ContainsItemContainer",
                counters
            );

            emit_format(
                "[ModIntegratedStorageCpp] "
                "OBS_META properties_found=%llu "
                "functions_found=%llu "
                "property_exceptions=%llu "
                "function_exceptions=%llu",
                static_cast<unsigned long long>(
                    counters.properties_found
                ),
                static_cast<unsigned long long>(
                    counters.functions_found
                ),
                static_cast<unsigned long long>(
                    counters.property_exceptions
                ),
                static_cast<unsigned long long>(
                    counters.function_exceptions
                )
            );

            if (
                counters.property_exceptions == 0 &&
                counters.function_exceptions == 0
            )
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "OBS_META RESULT=PASS"
                );
            }
            else
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "OBS_META RESULT=INCOMPLETE"
                );
            }
        }
        catch (...)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "OBS_META RESULT=EXCEPTION"
            );
        }
    }


    struct ReadObjectPropertyResult
    {
        bool exists{};
        bool object_property{};
        RC::Unreal::UObject* value{};
        std::int32_t offset{};
        std::int32_t size{};
    };

    auto read_object_property_candidate(
        RC::Unreal::UObject* object,
        const RC::Unreal::TCHAR* candidate_name
    ) noexcept -> ReadObjectPropertyResult
    {
        ReadObjectPropertyResult result{};

        if (object == nullptr)
        {
            return result;
        }

        try
        {
            auto* property =
                object->GetPropertyByNameInChain(
                    candidate_name
                );

            if (property == nullptr)
            {
                return result;
            }

            result.exists = true;
            result.offset =
                property->GetOffset_Internal();
            result.size =
                property->GetSize();

            auto* object_property =
                RC::Unreal::CastField<
                    RC::Unreal::FObjectPropertyBase
                >(property);

            if (object_property == nullptr)
            {
                return result;
            }

            result.object_property = true;

            auto* address =
                property->ContainerPtrToValuePtr<
                    void
                >(object);

            if (address == nullptr)
            {
                return result;
            }

            result.value =
                object_property->
                    GetObjectPropertyValue(
                        address
                    );

            return result;
        }
        catch (...)
        {
            return result;
        }
    }

    auto object_pointer_in_vector(
        RC::Unreal::UObject* value,
        const std::vector<
            RC::Unreal::UObject*
        >& values
    ) noexcept -> bool
    {
        if (value == nullptr)
        {
            return false;
        }

        for (auto* candidate : values)
        {
            if (candidate == value)
            {
                return true;
            }
        }

        return false;
    }

    auto run_read_only_item_storage_linkage_probe(
        RC::Unreal::UObject* chest,
        RC::Unreal::UObject* target_storage,
        bool plan_complete
    ) noexcept -> void
    {
        if (
            !plan_complete ||
            chest == nullptr ||
            target_storage == nullptr
        )
        {
            return;
        }

        bool expected_reported{false};

        if (
            !g_item_storage_linkage_reported.
                compare_exchange_strong(
                    expected_reported,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                )
        )
        {
            return;
        }

        try
        {
            static auto* item_storage_models =
                new std::vector<
                    RC::Unreal::UObject*
                >();

            static auto* guild_storages =
                new std::vector<
                    RC::Unreal::UObject*
                >();

            static auto* guild_item_containers =
                new std::vector<
                    RC::Unreal::UObject*
                >();

            item_storage_models->clear();
            guild_storages->clear();
            guild_item_containers->clear();

            RC::Unreal::UObjectGlobals::FindAllOf(
                STR("PalMapObjectItemStorageModel"),
                *item_storage_models
            );

            RC::Unreal::UObjectGlobals::FindAllOf(
                STR("PalGuildItemStorage"),
                *guild_storages
            );

            std::uint64_t guild_storage_valid{};
            std::uint64_t guild_item_container_properties{};
            std::uint64_t guild_item_container_nonnull{};

            for (auto* guild_storage : *guild_storages)
            {
                if (guild_storage == nullptr)
                {
                    continue;
                }

                ++guild_storage_valid;

                const auto item_container =
                    read_object_property_candidate(
                        guild_storage,
                        STR("ItemContainer")
                    );

                if (
                    item_container.exists &&
                    item_container.object_property
                )
                {
                    ++guild_item_container_properties;

                    if (item_container.value != nullptr)
                    {
                        ++guild_item_container_nonnull;

                        if (
                            !object_pointer_in_vector(
                                item_container.value,
                                *guild_item_containers
                            )
                        )
                        {
                            guild_item_containers->
                                push_back(
                                    item_container.value
                                );
                        }
                    }
                }
            }

            std::uint64_t valid_models{};
            std::uint64_t direct_matches{};
            std::uint64_t linked_models{};
            std::uint64_t conflicting_links{};
            std::uint64_t item_container_properties{};
            std::uint64_t item_container_nonnull{};
            std::uint64_t guild_storage_properties{};
            std::uint64_t guild_storage_nonnull{};

            RC::Unreal::UObject* linked_model{};
            RC::Unreal::UObject* linked_item_container{};
            RC::Unreal::UObject* linked_guild_storage{};

            const RC::Unreal::TCHAR*
                owner_candidate_names[] = {
                    STR("OwnerConcreteModel"),
                    STR("ConcreteModel"),
                    STR("CachedConcreteModel"),
                    STR("MapObjectConcreteModel"),
                    STR("OwnerMapObjectConcreteModel"),
                    STR("OwnerMapObject")
                };

            for (auto* model : *item_storage_models)
            {
                if (model == nullptr)
                {
                    continue;
                }

                ++valid_models;

                if (model == chest)
                {
                    ++direct_matches;
                }

                bool matches_selected_chest{};

                for (
                    const auto* candidate_name :
                    owner_candidate_names
                )
                {
                    const auto owner =
                        read_object_property_candidate(
                            model,
                            candidate_name
                        );

                    if (
                        owner.exists &&
                        owner.object_property &&
                        owner.value == chest
                    )
                    {
                        matches_selected_chest = true;
                    }
                }

                const auto item_container =
                    read_object_property_candidate(
                        model,
                        STR("ItemContainer")
                    );

                if (
                    item_container.exists &&
                    item_container.object_property
                )
                {
                    ++item_container_properties;

                    if (item_container.value != nullptr)
                    {
                        ++item_container_nonnull;
                    }
                }

                const auto guild_storage =
                    read_object_property_candidate(
                        model,
                        STR("GuildItemStorage")
                    );

                if (
                    guild_storage.exists &&
                    guild_storage.object_property
                )
                {
                    ++guild_storage_properties;

                    if (guild_storage.value != nullptr)
                    {
                        ++guild_storage_nonnull;
                    }
                }

                if (!matches_selected_chest)
                {
                    continue;
                }

                ++linked_models;

                if (linked_model == nullptr)
                {
                    linked_model = model;
                    linked_item_container =
                        item_container.value;
                    linked_guild_storage =
                        guild_storage.value;
                }
                else if (linked_model != model)
                {
                    ++conflicting_links;
                }
            }

            RC::Unreal::UObject* first_model{};

            for (auto* candidate : *item_storage_models)
            {
                if (candidate != nullptr)
                {
                    first_model = candidate;
                    break;
                }
            }

            ObservabilityMetadataCounters
                metadata_counters{};

            emit_observability_property_metadata(
                first_model,
                "item_storage_model",
                STR("ItemContainer"),
                "ItemContainer",
                metadata_counters
            );

            emit_observability_property_metadata(
                first_model,
                "item_storage_model",
                STR("GuildItemStorage"),
                "GuildItemStorage",
                metadata_counters
            );

            emit_observability_property_metadata(
                first_model,
                "item_storage_model",
                STR("OwnerConcreteModel"),
                "OwnerConcreteModel",
                metadata_counters
            );

            emit_observability_property_metadata(
                first_model,
                "item_storage_model",
                STR("ConcreteModel"),
                "ConcreteModel",
                metadata_counters
            );

            emit_observability_property_metadata(
                first_model,
                "item_storage_model",
                STR("CachedConcreteModel"),
                "CachedConcreteModel",
                metadata_counters
            );

            emit_observability_property_metadata(
                first_model,
                "item_storage_model",
                STR("MapObjectConcreteModel"),
                "MapObjectConcreteModel",
                metadata_counters
            );

            emit_observability_property_metadata(
                first_model,
                "item_storage_model",
                STR("OwnerMapObjectConcreteModel"),
                "OwnerMapObjectConcreteModel",
                metadata_counters
            );

            emit_observability_function_metadata(
                first_model,
                "item_storage_model",
                STR("GetItemContainer"),
                "GetItemContainer",
                metadata_counters
            );

            emit_observability_function_metadata(
                first_model,
                "item_storage_model",
                STR(
                    "GetItemContainer_"
                    "ItemContainerAccessInterface"
                ),
                "GetItemContainer_ItemContainerAccessInterface",
                metadata_counters
            );

            emit_observability_function_metadata(
                first_model,
                "item_storage_model",
                STR(
                    "OnUpdateItemContainerIn"
                    "GuildItemStorage"
                ),
                "OnUpdateItemContainerInGuildItemStorage",
                metadata_counters
            );

            emit_format(
                "[ModIntegratedStorageCpp] "
                "ITEM_LINK models=%zu valid=%llu "
                "direct_matches=%llu linked_models=%llu "
                "conflicting_links=%llu "
                "item_container_properties=%llu "
                "item_container_nonnull=%llu "
                "guild_storage_properties=%llu "
                "guild_storage_nonnull=%llu "
                "guild_storage_objects=%zu "
                "guild_storage_valid=%llu "
                "guild_item_container_properties=%llu "
                "guild_item_container_nonnull=%llu "
                "distinct_guild_item_containers=%zu "
                "linked_model=%d linked_item_container=%d "
                "linked_guild_storage=%d "
                "linked_guild_object_match=%d "
                "linked_container_matches_guild_container=%d "
                "metadata_property_exceptions=%llu "
                "metadata_function_exceptions=%llu",
                item_storage_models->size(),
                static_cast<unsigned long long>(
                    valid_models
                ),
                static_cast<unsigned long long>(
                    direct_matches
                ),
                static_cast<unsigned long long>(
                    linked_models
                ),
                static_cast<unsigned long long>(
                    conflicting_links
                ),
                static_cast<unsigned long long>(
                    item_container_properties
                ),
                static_cast<unsigned long long>(
                    item_container_nonnull
                ),
                static_cast<unsigned long long>(
                    guild_storage_properties
                ),
                static_cast<unsigned long long>(
                    guild_storage_nonnull
                ),
                guild_storages->size(),
                static_cast<unsigned long long>(
                    guild_storage_valid
                ),
                static_cast<unsigned long long>(
                    guild_item_container_properties
                ),
                static_cast<unsigned long long>(
                    guild_item_container_nonnull
                ),
                guild_item_containers->size(),
                linked_model != nullptr ? 1 : 0,
                linked_item_container != nullptr ? 1 : 0,
                linked_guild_storage != nullptr ? 1 : 0,
                object_pointer_in_vector(
                    linked_guild_storage,
                    *guild_storages
                ) ? 1 : 0,
                object_pointer_in_vector(
                    linked_item_container,
                    *guild_item_containers
                ) ? 1 : 0,
                static_cast<unsigned long long>(
                    metadata_counters.
                        property_exceptions
                ),
                static_cast<unsigned long long>(
                    metadata_counters.
                        function_exceptions
                )
            );

            if (
                conflicting_links == 0 &&
                metadata_counters.
                    property_exceptions == 0 &&
                metadata_counters.
                    function_exceptions == 0
            )
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "ITEM_LINK RESULT=PASS"
                );
            }
            else
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "ITEM_LINK RESULT=INCOMPLETE"
                );
            }
        }
        catch (...)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "ITEM_LINK RESULT=EXCEPTION"
            );
        }
    }


    auto emit_observability_function_layout(
        RC::Unreal::UObject* object,
        const char* object_label,
        const RC::Unreal::TCHAR* candidate_name,
        const char* candidate_label,
        ObservabilityMetadataCounters& counters
    ) noexcept -> void
    {
        if (object == nullptr)
        {
            emit_format(
                "[ModIntegratedStorageCpp] "
                "QUERY_FUNCTION object=%s candidate=%s "
                "exists=0 reason=null_object",
                object_label,
                candidate_label
            );

            return;
        }

        try
        {
            auto* function =
                object->GetFunctionByNameInChain(
                    candidate_name
                );

            if (function == nullptr)
            {
                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "QUERY_FUNCTION object=%s candidate=%s "
                    "exists=0",
                    object_label,
                    candidate_label
                );

                return;
            }

            ++counters.functions_found;

            std::uint64_t inputs{};
            std::uint64_t returns{};
            std::uint64_t object_inputs{};
            std::uint64_t object_returns{};
            std::uint64_t struct_inputs{};
            std::uint64_t struct_returns{};
            std::uint64_t array_inputs{};
            std::uint64_t array_returns{};
            std::uint64_t set_inputs{};
            std::uint64_t set_returns{};
            std::uint64_t map_inputs{};
            std::uint64_t map_returns{};

            std::int32_t first_input_offset{-1};
            std::int32_t first_input_size{-1};
            std::int32_t first_return_offset{-1};
            std::int32_t first_return_size{-1};

            for (
                auto* property :
                function->ForEachProperty()
            )
            {
                if (
                    property == nullptr ||
                    !property->HasAnyPropertyFlags(
                        RC::Unreal::EPropertyFlags::
                            CPF_Parm
                    )
                )
                {
                    continue;
                }

                const bool is_return =
                    property->HasAnyPropertyFlags(
                        RC::Unreal::EPropertyFlags::
                            CPF_ReturnParm
                    );

                const bool is_object =
                    RC::Unreal::CastField<
                        RC::Unreal::FObjectPropertyBase
                    >(property) != nullptr ||
                    RC::Unreal::CastField<
                        RC::Unreal::FWeakObjectProperty
                    >(property) != nullptr;

                const bool is_struct =
                    RC::Unreal::CastField<
                        RC::Unreal::FStructProperty
                    >(property) != nullptr;

                const bool is_array =
                    RC::Unreal::CastField<
                        RC::Unreal::FArrayProperty
                    >(property) != nullptr;

                const bool is_set =
                    RC::Unreal::CastField<
                        RC::Unreal::FSetProperty
                    >(property) != nullptr;

                const bool is_map =
                    RC::Unreal::CastField<
                        RC::Unreal::FMapProperty
                    >(property) != nullptr;

                if (is_return)
                {
                    ++returns;

                    if (first_return_offset < 0)
                    {
                        first_return_offset =
                            property->
                                GetOffset_Internal();

                        first_return_size =
                            property->GetSize();
                    }

                    object_returns +=
                        is_object ? 1U : 0U;

                    struct_returns +=
                        is_struct ? 1U : 0U;

                    array_returns +=
                        is_array ? 1U : 0U;

                    set_returns +=
                        is_set ? 1U : 0U;

                    map_returns +=
                        is_map ? 1U : 0U;
                }
                else
                {
                    ++inputs;

                    if (first_input_offset < 0)
                    {
                        first_input_offset =
                            property->
                                GetOffset_Internal();

                        first_input_size =
                            property->GetSize();
                    }

                    object_inputs +=
                        is_object ? 1U : 0U;

                    struct_inputs +=
                        is_struct ? 1U : 0U;

                    array_inputs +=
                        is_array ? 1U : 0U;

                    set_inputs +=
                        is_set ? 1U : 0U;

                    map_inputs +=
                        is_map ? 1U : 0U;
                }
            }

            emit_format(
                "[ModIntegratedStorageCpp] "
                "QUERY_FUNCTION object=%s candidate=%s "
                "exists=1 parms=%zu inputs=%llu "
                "returns=%llu object_inputs=%llu "
                "object_returns=%llu struct_inputs=%llu "
                "struct_returns=%llu array_inputs=%llu "
                "array_returns=%llu set_inputs=%llu "
                "set_returns=%llu map_inputs=%llu "
                "map_returns=%llu first_input_offset=%d "
                "first_input_size=%d first_return_offset=%d "
                "first_return_size=%d",
                object_label,
                candidate_label,
                function->GetParmsSize(),
                static_cast<unsigned long long>(
                    inputs
                ),
                static_cast<unsigned long long>(
                    returns
                ),
                static_cast<unsigned long long>(
                    object_inputs
                ),
                static_cast<unsigned long long>(
                    object_returns
                ),
                static_cast<unsigned long long>(
                    struct_inputs
                ),
                static_cast<unsigned long long>(
                    struct_returns
                ),
                static_cast<unsigned long long>(
                    array_inputs
                ),
                static_cast<unsigned long long>(
                    array_returns
                ),
                static_cast<unsigned long long>(
                    set_inputs
                ),
                static_cast<unsigned long long>(
                    set_returns
                ),
                static_cast<unsigned long long>(
                    map_inputs
                ),
                static_cast<unsigned long long>(
                    map_returns
                ),
                first_input_offset,
                first_input_size,
                first_return_offset,
                first_return_size
            );
        }
        catch (...)
        {
            ++counters.function_exceptions;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "QUERY_FUNCTION object=%s candidate=%s "
                "exists=0 reason=exception",
                object_label,
                candidate_label
            );
        }
    }

    auto run_read_only_container_query_metadata_probe(
        bool plan_complete
    ) noexcept -> void
    {
        if (!plan_complete)
        {
            return;
        }

        bool expected_reported{false};

        if (
            !g_container_query_metadata_reported.
                compare_exchange_strong(
                    expected_reported,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                )
        )
        {
            return;
        }

        try
        {
            static auto* guild_storages =
                new std::vector<
                    RC::Unreal::UObject*
                >();

            static auto* item_container_managers =
                new std::vector<
                    RC::Unreal::UObject*
                >();

            guild_storages->clear();
            item_container_managers->clear();

            RC::Unreal::UObjectGlobals::FindAllOf(
                STR("PalGuildItemStorage"),
                *guild_storages
            );

            RC::Unreal::UObjectGlobals::FindAllOf(
                STR("PalItemContainerManager"),
                *item_container_managers
            );

            RC::Unreal::UObject* guild_storage{};
            RC::Unreal::UObject* guild_item_container{};
            RC::Unreal::UObject* item_container_manager{};

            for (auto* candidate : *guild_storages)
            {
                if (candidate == nullptr)
                {
                    continue;
                }

                const auto item_container =
                    read_object_property_candidate(
                        candidate,
                        STR("ItemContainer")
                    );

                if (
                    item_container.exists &&
                    item_container.object_property &&
                    item_container.value != nullptr
                )
                {
                    guild_storage = candidate;
                    guild_item_container =
                        item_container.value;
                    break;
                }
            }

            for (
                auto* candidate :
                *item_container_managers
            )
            {
                if (candidate != nullptr)
                {
                    item_container_manager =
                        candidate;
                    break;
                }
            }

            ObservabilityMetadataCounters counters{};

            const RC::Unreal::TCHAR*
                container_property_names[] = {
                    STR("ItemContainerId"),
                    STR("ItemContainerID"),
                    STR("ContainerId"),
                    STR("ContainerID"),
                    STR("ItemContainerBelongInfo"),
                    STR("BelongInfo"),
                    STR("ContainerList"),
                    STR("ItemSlotArray"),
                    STR("SlotArray"),
                    STR("Slots"),
                    STR("ItemSlots"),
                    STR("ItemArray"),
                    STR("Items")
                };

            const char* container_property_labels[] = {
                    "ItemContainerId",
                    "ItemContainerID",
                    "ContainerId",
                    "ContainerID",
                    "ItemContainerBelongInfo",
                    "BelongInfo",
                    "ContainerList",
                    "ItemSlotArray",
                    "SlotArray",
                    "Slots",
                    "ItemSlots",
                    "ItemArray",
                    "Items"
                };

            for (
                std::size_t index{};
                index <
                    sizeof(container_property_names) /
                    sizeof(container_property_names[0]);
                ++index
            )
            {
                emit_observability_property_metadata(
                    guild_item_container,
                    "guild_item_container",
                    container_property_names[index],
                    container_property_labels[index],
                    counters
                );
            }

            const RC::Unreal::TCHAR*
                container_function_names[] = {
                    STR("GetContainerId"),
                    STR("GetItemContainerId"),
                    STR("GetSlotCount"),
                    STR("GetItemSlotNum"),
                    STR("GetSlots"),
                    STR("GetSlot"),
                    STR("GetSlotBySlotIndex"),
                    STR("GetItemSlot")
                };

            const char* container_function_labels[] = {
                    "GetContainerId",
                    "GetItemContainerId",
                    "GetSlotCount",
                    "GetItemSlotNum",
                    "GetSlots",
                    "GetSlot",
                    "GetSlotBySlotIndex",
                    "GetItemSlot"
                };

            for (
                std::size_t index{};
                index <
                    sizeof(container_function_names) /
                    sizeof(container_function_names[0]);
                ++index
            )
            {
                emit_observability_function_layout(
                    guild_item_container,
                    "guild_item_container",
                    container_function_names[index],
                    container_function_labels[index],
                    counters
                );
            }

            const RC::Unreal::TCHAR*
                manager_property_names[] = {
                    STR("ItemContainerMap_InServer"),
                    STR("ContainerMap_InServer"),
                    STR("ItemContainerMap"),
                    STR("ContainerMap")
                };

            const char* manager_property_labels[] = {
                    "ItemContainerMap_InServer",
                    "ContainerMap_InServer",
                    "ItemContainerMap",
                    "ContainerMap"
                };

            for (
                std::size_t index{};
                index <
                    sizeof(manager_property_names) /
                    sizeof(manager_property_names[0]);
                ++index
            )
            {
                emit_observability_property_metadata(
                    item_container_manager,
                    "item_container_manager",
                    manager_property_names[index],
                    manager_property_labels[index],
                    counters
                );
            }

            const RC::Unreal::TCHAR*
                manager_function_names[] = {
                    STR("GetGroupIdByItemContainerId"),
                    STR("GetGroupIdByItemSlotId"),
                    STR("GetContainer"),
                    STR("TryGetContainer"),
                    STR("GetItemContainer")
                };

            const char* manager_function_labels[] = {
                    "GetGroupIdByItemContainerId",
                    "GetGroupIdByItemSlotId",
                    "GetContainer",
                    "TryGetContainer",
                    "GetItemContainer"
                };

            for (
                std::size_t index{};
                index <
                    sizeof(manager_function_names) /
                    sizeof(manager_function_names[0]);
                ++index
            )
            {
                emit_observability_function_layout(
                    item_container_manager,
                    "item_container_manager",
                    manager_function_names[index],
                    manager_function_labels[index],
                    counters
                );
            }

            emit_format(
                "[ModIntegratedStorageCpp] "
                "QUERY_META guild_storage_objects=%zu "
                "guild_storage=%d guild_item_container=%d "
                "item_container_manager_objects=%zu "
                "item_container_manager=%d "
                "properties_found=%llu "
                "functions_found=%llu "
                "property_exceptions=%llu "
                "function_exceptions=%llu",
                guild_storages->size(),
                guild_storage != nullptr ? 1 : 0,
                guild_item_container != nullptr ? 1 : 0,
                item_container_managers->size(),
                item_container_manager != nullptr ? 1 : 0,
                static_cast<unsigned long long>(
                    counters.properties_found
                ),
                static_cast<unsigned long long>(
                    counters.functions_found
                ),
                static_cast<unsigned long long>(
                    counters.property_exceptions
                ),
                static_cast<unsigned long long>(
                    counters.function_exceptions
                )
            );

            if (
                guild_item_container != nullptr &&
                item_container_manager != nullptr &&
                counters.property_exceptions == 0 &&
                counters.function_exceptions == 0
            )
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "QUERY_META RESULT=PASS"
                );
            }
            else
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "QUERY_META RESULT=INCOMPLETE"
                );
            }
        }
        catch (...)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "QUERY_META RESULT=EXCEPTION"
            );
        }
    }


    struct StructCandidateRead
    {
        bool exists{};
        bool bounds_ok{};
        bool size_is_16{};
        std::int32_t offset{-1};
        std::int32_t size{-1};
        GuildKey value{};
    };

    auto guild_key_is_zero(
        const GuildKey& value
    ) noexcept -> bool
    {
        for (const auto byte : value)
        {
            if (byte != 0)
            {
                return false;
            }
        }

        return true;
    }

    auto guild_key_hex(
        const GuildKey& value
    ) noexcept -> std::array<char, 33>
    {
        static constexpr char digits[] =
            "0123456789abcdef";

        std::array<char, 33> result{};

        for (
            std::size_t index{};
            index < value.size();
            ++index
        )
        {
            result[index * 2] =
                digits[
                    (value[index] >> 4) &
                    0x0f
                ];

            result[index * 2 + 1] =
                digits[
                    value[index] &
                    0x0f
                ];
        }

        result[32] = '\0';
        return result;
    }

    auto read_nested_struct_candidate(
        RC::Unreal::UStruct* definition,
        void* data,
        std::int32_t container_size,
        const RC::Unreal::TCHAR* candidate_name
    ) noexcept -> StructCandidateRead
    {
        StructCandidateRead result{};

        if (
            definition == nullptr ||
            data == nullptr ||
            container_size <= 0
        )
        {
            return result;
        }

        try
        {
            auto* property =
                definition->GetPropertyByNameInChain(
                    candidate_name
                );

            if (property == nullptr)
            {
                return result;
            }

            result.exists = true;
            result.offset =
                property->GetOffset_Internal();
            result.size =
                property->GetSize();

            if (
                result.offset < 0 ||
                result.size <= 0 ||
                result.offset > container_size ||
                result.size >
                    container_size - result.offset
            )
            {
                return result;
            }

            result.bounds_ok = true;

            if (
                result.size !=
                static_cast<std::int32_t>(
                    sizeof(GuildKey)
                )
            )
            {
                return result;
            }

            auto* value_address =
                property->ContainerPtrToValuePtr<
                    void
                >(data);

            if (value_address == nullptr)
            {
                return result;
            }

            result.size_is_16 = true;

            std::memcpy(
                result.value.data(),
                value_address,
                sizeof(GuildKey)
            );

            return result;
        }
        catch (...)
        {
            return result;
        }
    }

    auto emit_detailed_function_parameters(
        RC::Unreal::UObject* object,
        const RC::Unreal::TCHAR* candidate_name,
        const char* candidate_label,
        std::uint64_t& functions_found,
        std::uint64_t& parameter_lines,
        std::uint64_t& exceptions
    ) noexcept -> void
    {
        if (object == nullptr)
        {
            emit_format(
                "[ModIntegratedStorageCpp] "
                "PARAM_FUNCTION candidate=%s exists=0 "
                "reason=null_object",
                candidate_label
            );

            return;
        }

        try
        {
            auto* function =
                object->GetFunctionByNameInChain(
                    candidate_name
                );

            if (function == nullptr)
            {
                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "PARAM_FUNCTION candidate=%s exists=0",
                    candidate_label
                );

                return;
            }

            ++functions_found;

            std::uint64_t ordinal{};

            for (
                auto* property :
                function->ForEachProperty()
            )
            {
                if (
                    property == nullptr ||
                    !property->HasAnyPropertyFlags(
                        RC::Unreal::EPropertyFlags::
                            CPF_Parm
                    )
                )
                {
                    continue;
                }

                const bool is_return =
                    property->HasAnyPropertyFlags(
                        RC::Unreal::EPropertyFlags::
                            CPF_ReturnParm
                    );

                const bool is_object =
                    RC::Unreal::CastField<
                        RC::Unreal::FObjectPropertyBase
                    >(property) != nullptr ||
                    RC::Unreal::CastField<
                        RC::Unreal::FWeakObjectProperty
                    >(property) != nullptr;

                const bool is_struct =
                    RC::Unreal::CastField<
                        RC::Unreal::FStructProperty
                    >(property) != nullptr;

                const bool is_array =
                    RC::Unreal::CastField<
                        RC::Unreal::FArrayProperty
                    >(property) != nullptr;

                const bool is_set =
                    RC::Unreal::CastField<
                        RC::Unreal::FSetProperty
                    >(property) != nullptr;

                const bool is_map =
                    RC::Unreal::CastField<
                        RC::Unreal::FMapProperty
                    >(property) != nullptr;

                const bool is_bool =
                    RC::Unreal::CastField<
                        RC::Unreal::FBoolProperty
                    >(property) != nullptr;

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "PARAM_META function=%s ordinal=%llu "
                    "return=%d offset=%d size=%d "
                    "element_size=%d object=%d struct=%d "
                    "array=%d set=%d map=%d bool=%d",
                    candidate_label,
                    static_cast<unsigned long long>(
                        ordinal
                    ),
                    is_return ? 1 : 0,
                    property->GetOffset_Internal(),
                    property->GetSize(),
                    property->GetElementSize(),
                    is_object ? 1 : 0,
                    is_struct ? 1 : 0,
                    is_array ? 1 : 0,
                    is_set ? 1 : 0,
                    is_map ? 1 : 0,
                    is_bool ? 1 : 0
                );

                ++ordinal;
                ++parameter_lines;
            }

            emit_format(
                "[ModIntegratedStorageCpp] "
                "PARAM_FUNCTION candidate=%s exists=1 "
                "parms=%zu parameter_lines=%llu",
                candidate_label,
                function->GetParmsSize(),
                static_cast<unsigned long long>(
                    ordinal
                )
            );
        }
        catch (...)
        {
            ++exceptions;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "PARAM_FUNCTION candidate=%s exists=0 "
                "reason=exception",
                candidate_label
            );
        }
    }





    auto run_read_only_container_query_assembly_probe(
        RC::Unreal::UObject* chest,
        const GuildKey& selected_guild,
        bool plan_complete
    ) noexcept -> void
    {
        if (
            !plan_complete ||
            chest == nullptr ||
            guid_is_zero(selected_guild)
        )
        {
            return;
        }

        bool expected_reported{false};

        if (
            !g_container_query_assembly_reported.
                compare_exchange_strong(
                    expected_reported,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                )
        )
        {
            return;
        }

        try
        {
            static auto* item_container_managers =
                new std::vector<
                    RC::Unreal::UObject*
                >();

            item_container_managers->clear();

            RC::Unreal::UObjectGlobals::FindAllOf(
                STR("PalItemContainerManager"),
                *item_container_managers
            );

            RC::Unreal::UObject*
                item_container_manager{};

            for (
                auto* candidate :
                *item_container_managers
            )
            {
                if (candidate != nullptr)
                {
                    item_container_manager =
                        candidate;
                    break;
                }
            }

            auto* function =
                item_container_manager != nullptr
                    ? item_container_manager->
                        GetFunctionByNameInChain(
                            STR(
                                "GetGroupIdBy"
                                "ItemContainerId"
                            )
                        )
                    : nullptr;

            auto* known_object_class =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UClass*
                    >(
                        nullptr,
                        nullptr,
                        STR(
                            "/Script/CoreUObject.Object"
                        )
                    );

            auto* known_container_id =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR(
                            "/Script/Pal.PalContainerId"
                        )
                    );

            auto* known_guid =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR(
                            "/Script/CoreUObject.Guid"
                        )
                    );

            RC::Unreal::FObjectPropertyBase*
                object_input{};

            RC::Unreal::FStructProperty*
                container_input{};

            RC::Unreal::FStructProperty*
                group_return{};

            std::uint64_t parameter_count{};
            std::uint64_t input_count{};
            std::uint64_t return_count{};
            std::uint64_t object_input_count{};
            std::uint64_t struct_input_count{};
            std::uint64_t struct_return_count{};

            if (function != nullptr)
            {
                for (
                    auto* property :
                    function->ForEachProperty()
                )
                {
                    if (
                        property == nullptr ||
                        !property->HasAnyPropertyFlags(
                            RC::Unreal::CPF_Parm
                        )
                    )
                    {
                        continue;
                    }

                    const bool is_return =
                        property->HasAnyPropertyFlags(
                            RC::Unreal::CPF_ReturnParm
                        );

                    ++parameter_count;

                    if (is_return)
                    {
                        ++return_count;

                        auto* struct_property =
                            RC::Unreal::CastField<
                                RC::Unreal::
                                    FStructProperty
                            >(property);

                        if (struct_property != nullptr)
                        {
                            ++struct_return_count;

                            if (group_return == nullptr)
                            {
                                group_return =
                                    struct_property;
                            }
                        }
                    }
                    else
                    {
                        ++input_count;

                        auto* object_property =
                            RC::Unreal::CastField<
                                RC::Unreal::
                                    FObjectPropertyBase
                            >(property);

                        if (object_property != nullptr)
                        {
                            ++object_input_count;

                            if (object_input == nullptr)
                            {
                                object_input =
                                    object_property;
                            }
                        }

                        auto* struct_property =
                            RC::Unreal::CastField<
                                RC::Unreal::
                                    FStructProperty
                            >(property);

                        if (struct_property != nullptr)
                        {
                            ++struct_input_count;

                            if (container_input == nullptr)
                            {
                                container_input =
                                    struct_property;
                            }
                        }
                    }
                }
            }

            auto* named_world_context =
                function != nullptr
                    ? function->
                        GetPropertyByNameInChain(
                            STR("WorldContextObject")
                        )
                    : nullptr;

            auto* named_context =
                function != nullptr
                    ? function->
                        GetPropertyByNameInChain(
                            STR("Context")
                        )
                    : nullptr;

            auto* named_item_container_id =
                function != nullptr
                    ? function->
                        GetPropertyByNameInChain(
                            STR("ItemContainerId")
                        )
                    : nullptr;

            auto* named_item_container_id_upper =
                function != nullptr
                    ? function->
                        GetPropertyByNameInChain(
                            STR("ItemContainerID")
                        )
                    : nullptr;

            auto* named_container_id =
                function != nullptr
                    ? function->
                        GetPropertyByNameInChain(
                            STR("ContainerId")
                        )
                    : nullptr;

            auto* named_return_value =
                function != nullptr
                    ? function->
                        GetPropertyByNameInChain(
                            STR("ReturnValue")
                        )
                    : nullptr;

            RC::Unreal::UClass*
                object_input_class{};

            if (object_input != nullptr)
            {
                object_input_class =
                    object_input->
                        GetPropertyClass();
            }

            auto* container_input_definition =
                container_input != nullptr
                    ? container_input->
                        GetStruct().Get()
                    : nullptr;

            auto* group_return_definition =
                group_return != nullptr
                    ? group_return->
                        GetStruct().Get()
                    : nullptr;

            const bool object_name_world_context =
                object_input != nullptr &&
                object_input ==
                    named_world_context;

            const bool object_name_context =
                object_input != nullptr &&
                object_input ==
                    named_context;

            const bool object_class_is_object =
                object_input_class != nullptr &&
                known_object_class != nullptr &&
                object_input_class ==
                    known_object_class;

            const bool chest_context_compatible =
                class_is(
                    chest,
                    object_input_class
                );

            const bool container_name_item_id =
                container_input != nullptr &&
                (
                    container_input ==
                        named_item_container_id ||
                    container_input ==
                        named_item_container_id_upper
                );

            const bool container_name_container_id =
                container_input != nullptr &&
                container_input ==
                    named_container_id;

            const bool container_type_match =
                container_input_definition != nullptr &&
                known_container_id != nullptr &&
                container_input_definition ==
                    known_container_id;

            const bool return_name_match =
                group_return != nullptr &&
                group_return ==
                    named_return_value;

            const bool return_type_guid =
                group_return_definition != nullptr &&
                known_guid != nullptr &&
                group_return_definition ==
                    known_guid;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "QUERY_PARAM_SEMANTIC ordinal=0 "
                "exists=%d return=0 offset=%d size=%d "
                "name_world_context=%d name_context=%d "
                "object=1 class_object=%d "
                "context_source=selected_chest "
                "context_compatible=%d flags=0x%llx",
                object_input != nullptr ? 1 : 0,
                object_input != nullptr
                    ? object_input->
                        GetOffset_Internal()
                    : -1,
                object_input != nullptr
                    ? object_input->GetSize()
                    : -1,
                object_name_world_context ? 1 : 0,
                object_name_context ? 1 : 0,
                object_class_is_object ? 1 : 0,
                chest_context_compatible ? 1 : 0,
                static_cast<unsigned long long>(
                    object_input != nullptr
                        ? object_input->
                            GetPropertyFlags()
                        : 0
                )
            );

            emit_format(
                "[ModIntegratedStorageCpp] "
                "QUERY_PARAM_SEMANTIC ordinal=1 "
                "exists=%d return=0 offset=%d size=%d "
                "name_item_container_id=%d "
                "name_container_id=%d struct=1 "
                "type_pal_container_id=%d "
                "zero_constructor=%d "
                "const_parameter=%d "
                "reference_parameter=%d flags=0x%llx",
                container_input != nullptr ? 1 : 0,
                container_input != nullptr
                    ? container_input->
                        GetOffset_Internal()
                    : -1,
                container_input != nullptr
                    ? container_input->GetSize()
                    : -1,
                container_name_item_id ? 1 : 0,
                container_name_container_id ? 1 : 0,
                container_type_match ? 1 : 0,
                container_input != nullptr &&
                container_input->
                    HasAnyPropertyFlags(
                        RC::Unreal::
                            CPF_ZeroConstructor
                    ) ? 1 : 0,
                container_input != nullptr &&
                container_input->
                    HasAnyPropertyFlags(
                        RC::Unreal::CPF_ConstParm
                    ) ? 1 : 0,
                container_input != nullptr &&
                container_input->
                    HasAnyPropertyFlags(
                        RC::Unreal::
                            CPF_ReferenceParm
                    ) ? 1 : 0,
                static_cast<unsigned long long>(
                    container_input != nullptr
                        ? container_input->
                            GetPropertyFlags()
                        : 0
                )
            );

            emit_format(
                "[ModIntegratedStorageCpp] "
                "QUERY_PARAM_SEMANTIC ordinal=2 "
                "exists=%d return=1 offset=%d size=%d "
                "name_return_value=%d struct=1 "
                "type_guid=%d zero_constructor=%d "
                "out_parameter=%d flags=0x%llx",
                group_return != nullptr ? 1 : 0,
                group_return != nullptr
                    ? group_return->
                        GetOffset_Internal()
                    : -1,
                group_return != nullptr
                    ? group_return->GetSize()
                    : -1,
                return_name_match ? 1 : 0,
                return_type_guid ? 1 : 0,
                group_return != nullptr &&
                group_return->
                    HasAnyPropertyFlags(
                        RC::Unreal::
                            CPF_ZeroConstructor
                    ) ? 1 : 0,
                group_return != nullptr &&
                group_return->
                    HasAnyPropertyFlags(
                        RC::Unreal::CPF_OutParm
                    ) ? 1 : 0,
                static_cast<unsigned long long>(
                    group_return != nullptr
                        ? group_return->
                            GetPropertyFlags()
                        : 0
                )
            );

            const RC::Unreal::TCHAR*
                direct_candidate_names[] = {
                    STR("ContainerId"),
                    STR("ContainerID"),
                    STR("ItemContainerId"),
                    STR("ItemContainerID")
                };

            const char*
                direct_candidate_labels[] = {
                    "ContainerId",
                    "ContainerID",
                    "ItemContainerId",
                    "ItemContainerID"
                };

            GuildKey direct_container_id{};
            bool direct_container_id_found{};

            std::uint64_t direct_properties_found{};
            std::uint64_t direct_type_matches{};
            std::uint64_t direct_nonzero_values{};

            for (
                std::size_t candidate_index{};
                candidate_index <
                    sizeof(direct_candidate_names) /
                    sizeof(direct_candidate_names[0]);
                ++candidate_index
            )
            {
                auto* property =
                    chest->
                        GetPropertyByNameInChain(
                            direct_candidate_names[
                                candidate_index
                            ]
                        );

                auto* struct_property =
                    RC::Unreal::CastField<
                        RC::Unreal::FStructProperty
                    >(property);

                const bool type_match =
                    struct_property != nullptr &&
                    known_container_id != nullptr &&
                    struct_property->
                        GetStruct().Get() ==
                        known_container_id;

                GuildKey value{};
                bool value_address_valid{};
                bool nonzero{};

                if (
                    property != nullptr &&
                    struct_property != nullptr &&
                    type_match &&
                    property->GetSize() ==
                        static_cast<std::int32_t>(
                            sizeof(GuildKey)
                        )
                )
                {
                    auto* value_address =
                        property->
                            ContainerPtrToValuePtr<
                                void
                            >(chest);

                    if (value_address != nullptr)
                    {
                        value_address_valid = true;

                        std::memcpy(
                            value.data(),
                            value_address,
                            value.size()
                        );

                        nonzero =
                            !guid_is_zero(value);
                    }
                }

                direct_properties_found +=
                    property != nullptr ? 1U : 0U;

                direct_type_matches +=
                    type_match ? 1U : 0U;

                direct_nonzero_values +=
                    nonzero ? 1U : 0U;

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "QUERY_ID_PROPERTY candidate=%s "
                    "exists=%d struct=%d offset=%d "
                    "size=%d type_pal_container_id=%d "
                    "value_address=%d nonzero=%d",
                    direct_candidate_labels[
                        candidate_index
                    ],
                    property != nullptr ? 1 : 0,
                    struct_property != nullptr ? 1 : 0,
                    property != nullptr
                        ? property->
                            GetOffset_Internal()
                        : -1,
                    property != nullptr
                        ? property->GetSize()
                        : -1,
                    type_match ? 1 : 0,
                    value_address_valid ? 1 : 0,
                    nonzero ? 1 : 0
                );

                if (
                    nonzero &&
                    !direct_container_id_found
                )
                {
                    direct_container_id = value;
                    direct_container_id_found = true;
                }
            }

            const RC::Unreal::TCHAR*
                array_candidate_names[] = {
                    STR("ItemSlotArray"),
                    STR("SlotArray"),
                    STR("ItemSlots"),
                    STR("Slots")
                };

            const char*
                array_candidate_labels[] = {
                    "ItemSlotArray",
                    "SlotArray",
                    "ItemSlots",
                    "Slots"
                };

            GuildKey slot_container_id{};
            bool slot_container_id_found{};

            std::uint64_t slot_arrays_found{};
            std::uint64_t slot_arrays_object_inner{};
            std::uint64_t slot_objects{};
            std::uint64_t slot_container_properties{};
            std::uint64_t slot_container_type_matches{};
            std::uint64_t slot_container_nonzero{};
            std::uint64_t slot_container_mismatches{};
            std::uint64_t slot_exceptions{};

            for (
                std::size_t candidate_index{};
                candidate_index <
                    sizeof(array_candidate_names) /
                    sizeof(array_candidate_names[0]);
                ++candidate_index
            )
            {
                auto* property =
                    chest->
                        GetPropertyByNameInChain(
                            array_candidate_names[
                                candidate_index
                            ]
                        );

                auto* array_property =
                    RC::Unreal::CastField<
                        RC::Unreal::FArrayProperty
                    >(property);

                auto* slot_object_property =
                    array_property != nullptr
                        ? RC::Unreal::CastField<
                            RC::Unreal::
                                FObjectPropertyBase
                        >(
                            array_property->
                                GetInner()
                        )
                        : nullptr;

                const bool object_inner =
                    slot_object_property != nullptr;

                std::int32_t slot_count{-1};

                if (array_property != nullptr)
                {
                    ++slot_arrays_found;
                }

                if (object_inner)
                {
                    ++slot_arrays_object_inner;
                }

                if (
                    array_property != nullptr &&
                    slot_object_property != nullptr
                )
                {
                    RC::Unreal::
                        FScriptArrayHelper_InContainer
                            helper(
                                array_property,
                                chest
                            );

                    slot_count = helper.Num();

                    for (
                        std::int32_t slot_index{};
                        slot_index < slot_count;
                        ++slot_index
                    )
                    {
                        try
                        {
                            auto* slot =
                                slot_object_property->
                                    GetObjectPropertyValue(
                                        helper.GetRawPtr(
                                            slot_index
                                        )
                                    );

                            if (slot == nullptr)
                            {
                                continue;
                            }

                            ++slot_objects;

                            auto* slot_container_property =
                                slot->
                                    GetPropertyByNameInChain(
                                        STR("ContainerId")
                                    );

                            if (
                                slot_container_property ==
                                nullptr
                            )
                            {
                                slot_container_property =
                                    slot->
                                        GetPropertyByNameInChain(
                                            STR(
                                                "ContainerID"
                                            )
                                        );
                            }

                            auto*
                                slot_container_struct =
                                    RC::Unreal::CastField<
                                        RC::Unreal::
                                            FStructProperty
                                    >(
                                        slot_container_property
                                    );

                            if (
                                slot_container_property !=
                                nullptr
                            )
                            {
                                ++slot_container_properties;
                            }

                            const bool type_match =
                                slot_container_struct !=
                                    nullptr &&
                                known_container_id !=
                                    nullptr &&
                                slot_container_struct->
                                    GetStruct().Get() ==
                                    known_container_id &&
                                slot_container_property->
                                    GetSize() ==
                                    static_cast<
                                        std::int32_t
                                    >(
                                        sizeof(GuildKey)
                                    );

                            if (!type_match)
                            {
                                continue;
                            }

                            ++slot_container_type_matches;

                            auto* value_address =
                                slot_container_property->
                                    ContainerPtrToValuePtr<
                                        void
                                    >(slot);

                            if (value_address == nullptr)
                            {
                                continue;
                            }

                            GuildKey value{};

                            std::memcpy(
                                value.data(),
                                value_address,
                                value.size()
                            );

                            if (guid_is_zero(value))
                            {
                                continue;
                            }

                            ++slot_container_nonzero;

                            if (!slot_container_id_found)
                            {
                                slot_container_id =
                                    value;

                                slot_container_id_found =
                                    true;
                            }
                            else if (
                                slot_container_id !=
                                value
                            )
                            {
                                ++slot_container_mismatches;
                            }
                        }
                        catch (...)
                        {
                            ++slot_exceptions;
                        }
                    }
                }

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "QUERY_ID_ARRAY candidate=%s "
                    "exists=%d array=%d "
                    "inner_object=%d slot_count=%d",
                    array_candidate_labels[
                        candidate_index
                    ],
                    property != nullptr ? 1 : 0,
                    array_property != nullptr ? 1 : 0,
                    object_inner ? 1 : 0,
                    slot_count
                );
            }

            const bool source_consistent =
                !direct_container_id_found ||
                !slot_container_id_found ||
                direct_container_id ==
                    slot_container_id;

            GuildKey selected_container_id{};

            const char* source = "none";

            if (direct_container_id_found)
            {
                selected_container_id =
                    direct_container_id;
                source = "direct_property";
            }
            else if (
                slot_container_id_found &&
                slot_container_mismatches == 0
            )
            {
                selected_container_id =
                    slot_container_id;
                source = "slot_consensus";
            }

            const bool selected_container_id_found =
                !guid_is_zero(
                    selected_container_id
                );

            const auto container_hex =
                guid_to_hex(
                    selected_container_id
                );

            const auto guild_hex =
                guid_to_hex(
                    selected_guild
                );

            emit_format(
                "[ModIntegratedStorageCpp] "
                "QUERY_ID_SOURCE source=%s "
                "direct_properties=%llu "
                "direct_type_matches=%llu "
                "direct_nonzero=%llu "
                "slot_arrays=%llu "
                "slot_arrays_object_inner=%llu "
                "slot_objects=%llu "
                "slot_container_properties=%llu "
                "slot_container_type_matches=%llu "
                "slot_container_nonzero=%llu "
                "slot_container_mismatches=%llu "
                "slot_exceptions=%llu "
                "source_consistent=%d found=%d "
                "container_id=%s expected_group=%s",
                source,
                static_cast<unsigned long long>(
                    direct_properties_found
                ),
                static_cast<unsigned long long>(
                    direct_type_matches
                ),
                static_cast<unsigned long long>(
                    direct_nonzero_values
                ),
                static_cast<unsigned long long>(
                    slot_arrays_found
                ),
                static_cast<unsigned long long>(
                    slot_arrays_object_inner
                ),
                static_cast<unsigned long long>(
                    slot_objects
                ),
                static_cast<unsigned long long>(
                    slot_container_properties
                ),
                static_cast<unsigned long long>(
                    slot_container_type_matches
                ),
                static_cast<unsigned long long>(
                    slot_container_nonzero
                ),
                static_cast<unsigned long long>(
                    slot_container_mismatches
                ),
                static_cast<unsigned long long>(
                    slot_exceptions
                ),
                source_consistent ? 1 : 0,
                selected_container_id_found ? 1 : 0,
                container_hex.data(),
                guild_hex.data()
            );

            const std::size_t parameter_bytes =
                function != nullptr
                    ? static_cast<std::size_t>(
                        function->GetParmsSize()
                    )
                    : 0;

            const bool layout_valid =
                function != nullptr &&
                parameter_bytes == 40 &&
                parameter_count == 3 &&
                input_count == 2 &&
                return_count == 1 &&
                object_input_count == 1 &&
                struct_input_count == 1 &&
                struct_return_count == 1 &&
                object_input != nullptr &&
                object_input->
                    GetOffset_Internal() == 0 &&
                object_input->GetSize() ==
                    static_cast<std::int32_t>(
                        sizeof(
                            RC::Unreal::UObject*
                        )
                    ) &&
                container_input != nullptr &&
                container_input->
                    GetOffset_Internal() == 8 &&
                container_input->GetSize() == 16 &&
                group_return != nullptr &&
                group_return->
                    GetOffset_Internal() == 24 &&
                group_return->GetSize() == 16;

            std::array<std::byte, 40>
                parameter_buffer{};

            bool buffer_assembled{};

            if (
                layout_valid &&
                object_name_world_context &&
                object_class_is_object &&
                chest_context_compatible &&
                (
                    container_name_item_id ||
                    container_name_container_id
                ) &&
                container_type_match &&
                return_name_match &&
                return_type_guid &&
                selected_container_id_found &&
                source_consistent &&
                slot_container_mismatches == 0 &&
                slot_exceptions == 0
            )
            {
                std::memcpy(
                    parameter_buffer.data() +
                        static_cast<std::size_t>(
                            object_input->
                                GetOffset_Internal()
                        ),
                    &chest,
                    sizeof(chest)
                );

                std::memcpy(
                    parameter_buffer.data() +
                        static_cast<std::size_t>(
                            container_input->
                                GetOffset_Internal()
                        ),
                    selected_container_id.data(),
                    selected_container_id.size()
                );

                buffer_assembled = true;
            }

            RC::Unreal::UObject*
                recovered_context{};

            GuildKey
                recovered_container_id{};

            if (buffer_assembled)
            {
                std::memcpy(
                    &recovered_context,
                    parameter_buffer.data(),
                    sizeof(recovered_context)
                );

                std::memcpy(
                    recovered_container_id.data(),
                    parameter_buffer.data() + 8,
                    recovered_container_id.size()
                );
            }

            const bool context_roundtrip =
                buffer_assembled &&
                recovered_context == chest;

            const bool container_roundtrip =
                buffer_assembled &&
                recovered_container_id ==
                    selected_container_id;

            bool return_zero{true};

            for (
                std::size_t index{24};
                index < parameter_buffer.size();
                ++index
            )
            {
                if (
                    parameter_buffer[index] !=
                    std::byte{}
                )
                {
                    return_zero = false;
                    break;
                }
            }

            const bool passed =
                item_container_managers->size() == 1 &&
                item_container_manager != nullptr &&
                known_object_class != nullptr &&
                known_container_id != nullptr &&
                known_guid != nullptr &&
                layout_valid &&
                object_name_world_context &&
                object_class_is_object &&
                chest_context_compatible &&
                (
                    container_name_item_id ||
                    container_name_container_id
                ) &&
                container_type_match &&
                return_name_match &&
                return_type_guid &&
                selected_container_id_found &&
                source_consistent &&
                slot_container_mismatches == 0 &&
                slot_exceptions == 0 &&
                buffer_assembled &&
                context_roundtrip &&
                container_roundtrip &&
                return_zero;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "QUERY_ASSEMBLY manager_objects=%zu "
                "manager=%d function=%d parms=%zu "
                "parameters=%llu inputs=%llu "
                "returns=%llu object_inputs=%llu "
                "struct_inputs=%llu struct_returns=%llu "
                "layout=%d object_name=%d "
                "object_class=%d context_compatible=%d "
                "container_name=%d container_type=%d "
                "return_name=%d return_type=%d "
                "id_found=%d source_consistent=%d "
                "buffer_assembled=%d "
                "context_roundtrip=%d "
                "container_roundtrip=%d "
                "return_zero=%d query_called=0",
                item_container_managers->size(),
                item_container_manager != nullptr
                    ? 1
                    : 0,
                function != nullptr ? 1 : 0,
                parameter_bytes,
                static_cast<unsigned long long>(
                    parameter_count
                ),
                static_cast<unsigned long long>(
                    input_count
                ),
                static_cast<unsigned long long>(
                    return_count
                ),
                static_cast<unsigned long long>(
                    object_input_count
                ),
                static_cast<unsigned long long>(
                    struct_input_count
                ),
                static_cast<unsigned long long>(
                    struct_return_count
                ),
                layout_valid ? 1 : 0,
                object_name_world_context ? 1 : 0,
                object_class_is_object ? 1 : 0,
                chest_context_compatible ? 1 : 0,
                (
                    container_name_item_id ||
                    container_name_container_id
                ) ? 1 : 0,
                container_type_match ? 1 : 0,
                return_name_match ? 1 : 0,
                return_type_guid ? 1 : 0,
                selected_container_id_found ? 1 : 0,
                source_consistent ? 1 : 0,
                buffer_assembled ? 1 : 0,
                context_roundtrip ? 1 : 0,
                container_roundtrip ? 1 : 0,
                return_zero ? 1 : 0
            );

            if (passed)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "QUERY_ASSEMBLY RESULT=PASS"
                );
            }
            else
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "QUERY_ASSEMBLY RESULT=INCOMPLETE"
                );
            }
        }
        catch (...)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "QUERY_ASSEMBLY RESULT=EXCEPTION"
            );
        }
    }

    auto run_read_only_deep_layout_metadata_probe(
        const GuildKey& selected_guild,
        bool plan_complete
    ) noexcept -> void
    {
        if (!plan_complete)
        {
            return;
        }

        bool expected_reported{false};

        if (
            !g_deep_layout_metadata_reported.
                compare_exchange_strong(
                    expected_reported,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                )
        )
        {
            return;
        }

        try
        {
            static auto* guild_storages =
                new std::vector<
                    RC::Unreal::UObject*
                >();

            static auto* item_container_managers =
                new std::vector<
                    RC::Unreal::UObject*
                >();

            guild_storages->clear();
            item_container_managers->clear();

            RC::Unreal::UObjectGlobals::FindAllOf(
                STR("PalGuildItemStorage"),
                *guild_storages
            );

            RC::Unreal::UObjectGlobals::FindAllOf(
                STR("PalItemContainerManager"),
                *item_container_managers
            );

            const RC::Unreal::TCHAR*
                group_candidate_names[] = {
                    STR("GroupId"),
                    STR("GroupID"),
                    STR("GuildId"),
                    STR("GuildID"),
                    STR("BelongGroupId")
                };

            const char* group_candidate_labels[] = {
                    "GroupId",
                    "GroupID",
                    "GuildId",
                    "GuildID",
                    "BelongGroupId"
                };

            const RC::Unreal::TCHAR*
                container_candidate_names[] = {
                    STR("ContainerId"),
                    STR("ContainerID"),
                    STR("ItemContainerId"),
                    STR("ItemContainerID")
                };

            const char* container_candidate_labels[] = {
                    "ContainerId",
                    "ContainerID",
                    "ItemContainerId",
                    "ItemContainerID"
                };

            std::uint64_t valid_storages{};
            std::uint64_t belong_properties{};
            std::uint64_t belong_structs{};
            std::uint64_t group_candidates_found{};
            std::uint64_t group_candidates_16{};
            std::uint64_t selected_guild_matches{};
            std::uint64_t selected_storage_matches{};
            std::uint64_t container_candidates_found{};
            std::uint64_t container_candidates_16{};
            std::uint64_t nonzero_container_ids{};
            std::uint64_t item_slot_arrays{};
            std::uint64_t item_slot_array_objects{};
            std::uint64_t total_slots{};
            std::uint64_t min_slots{};
            std::uint64_t max_slots{};

            bool have_slot_count{};

            RC::Unreal::UObject*
                selected_guild_storage{};

            RC::Unreal::UObject*
                selected_guild_item_container{};

            GuildKey selected_container_id{};
            bool selected_container_id_found{};

            std::uint64_t storage_index{};

            for (auto* storage : *guild_storages)
            {
                if (storage == nullptr)
                {
                    continue;
                }

                ++valid_storages;

                const auto item_container =
                    read_object_property_candidate(
                        storage,
                        STR("ItemContainer")
                    );

                auto* container =
                    item_container.value;

                bool storage_matches_selected{};
                bool storage_has_container_id{};
                GuildKey storage_container_id{};

                auto* belong_property =
                    container != nullptr
                        ? container->
                            GetPropertyByNameInChain(
                                STR("BelongInfo")
                            )
                        : nullptr;

                if (belong_property != nullptr)
                {
                    ++belong_properties;
                }

                auto* belong_struct_property =
                    RC::Unreal::CastField<
                        RC::Unreal::FStructProperty
                    >(belong_property);

                RC::Unreal::UStruct* belong_definition{};
                void* belong_data{};

                if (belong_struct_property != nullptr)
                {
                    ++belong_structs;

                    belong_definition =
                        belong_struct_property->
                            GetStruct().Get();

                    belong_data =
                        belong_property->
                            ContainerPtrToValuePtr<
                                void
                            >(container);
                }

                for (
                    std::size_t candidate_index{};
                    candidate_index <
                        sizeof(group_candidate_names) /
                        sizeof(group_candidate_names[0]);
                    ++candidate_index
                )
                {
                    const auto result =
                        read_nested_struct_candidate(
                            belong_definition,
                            belong_data,
                            belong_property != nullptr
                                ? belong_property->
                                    GetSize()
                                : 0,
                            group_candidate_names[
                                candidate_index
                            ]
                        );

                    if (result.exists)
                    {
                        ++group_candidates_found;
                    }

                    if (result.size_is_16)
                    {
                        ++group_candidates_16;

                        const bool matches =
                            result.value ==
                                selected_guild;

                        selected_guild_matches +=
                            matches ? 1U : 0U;

                        storage_matches_selected =
                            storage_matches_selected ||
                            matches;

                        const auto hex =
                            guild_key_hex(
                                result.value
                            );

                        emit_format(
                            "[ModIntegratedStorageCpp] "
                            "BELONG_MEMBER storage=%llu "
                            "candidate=%s exists=1 "
                            "offset=%d size=%d zero=%d "
                            "selected_guild_match=%d "
                            "value=%s",
                            static_cast<
                                unsigned long long
                            >(storage_index),
                            group_candidate_labels[
                                candidate_index
                            ],
                            result.offset,
                            result.size,
                            guild_key_is_zero(
                                result.value
                            ) ? 1 : 0,
                            matches ? 1 : 0,
                            hex.data()
                        );
                    }
                    else
                    {
                        emit_format(
                            "[ModIntegratedStorageCpp] "
                            "BELONG_MEMBER storage=%llu "
                            "candidate=%s exists=%d "
                            "bounds=%d offset=%d size=%d "
                            "size16=0",
                            static_cast<
                                unsigned long long
                            >(storage_index),
                            group_candidate_labels[
                                candidate_index
                            ],
                            result.exists ? 1 : 0,
                            result.bounds_ok ? 1 : 0,
                            result.offset,
                            result.size
                        );
                    }
                }

                for (
                    std::size_t candidate_index{};
                    candidate_index <
                        sizeof(container_candidate_names) /
                        sizeof(container_candidate_names[0]);
                    ++candidate_index
                )
                {
                    const auto result =
                        read_nested_struct_candidate(
                            belong_definition,
                            belong_data,
                            belong_property != nullptr
                                ? belong_property->
                                    GetSize()
                                : 0,
                            container_candidate_names[
                                candidate_index
                            ]
                        );

                    if (result.exists)
                    {
                        ++container_candidates_found;
                    }

                    if (result.size_is_16)
                    {
                        ++container_candidates_16;

                        const bool nonzero =
                            !guild_key_is_zero(
                                result.value
                            );

                        nonzero_container_ids +=
                            nonzero ? 1U : 0U;

                        if (
                            nonzero &&
                            !storage_has_container_id
                        )
                        {
                            storage_has_container_id =
                                true;

                            storage_container_id =
                                result.value;
                        }

                        const auto hex =
                            guild_key_hex(
                                result.value
                            );

                        emit_format(
                            "[ModIntegratedStorageCpp] "
                            "BELONG_MEMBER storage=%llu "
                            "candidate=%s exists=1 "
                            "offset=%d size=%d zero=%d "
                            "value=%s",
                            static_cast<
                                unsigned long long
                            >(storage_index),
                            container_candidate_labels[
                                candidate_index
                            ],
                            result.offset,
                            result.size,
                            nonzero ? 0 : 1,
                            hex.data()
                        );
                    }
                    else
                    {
                        emit_format(
                            "[ModIntegratedStorageCpp] "
                            "BELONG_MEMBER storage=%llu "
                            "candidate=%s exists=%d "
                            "bounds=%d offset=%d size=%d "
                            "size16=0",
                            static_cast<
                                unsigned long long
                            >(storage_index),
                            container_candidate_labels[
                                candidate_index
                            ],
                            result.exists ? 1 : 0,
                            result.bounds_ok ? 1 : 0,
                            result.offset,
                            result.size
                        );
                    }
                }

                auto* slot_property =
                    container != nullptr
                        ? container->
                            GetPropertyByNameInChain(
                                STR("ItemSlotArray")
                            )
                        : nullptr;

                auto* slot_array_property =
                    RC::Unreal::CastField<
                        RC::Unreal::FArrayProperty
                    >(slot_property);

                std::int32_t slot_count{-1};
                bool inner_object{};

                if (slot_array_property != nullptr)
                {
                    ++item_slot_arrays;

                    auto* inner =
                        slot_array_property->GetInner();

                    inner_object =
                        RC::Unreal::CastField<
                            RC::Unreal::
                                FObjectPropertyBase
                        >(inner) != nullptr ||
                        RC::Unreal::CastField<
                            RC::Unreal::
                                FWeakObjectProperty
                        >(inner) != nullptr;

                    item_slot_array_objects +=
                        inner_object ? 1U : 0U;

                    RC::Unreal::
                        FScriptArrayHelper_InContainer
                            helper(
                                slot_array_property,
                                container
                            );

                    slot_count = helper.Num();

                    if (slot_count >= 0)
                    {
                        total_slots +=
                            static_cast<
                                std::uint64_t
                            >(slot_count);

                        if (!have_slot_count)
                        {
                            min_slots =
                                static_cast<
                                    std::uint64_t
                                >(slot_count);

                            max_slots = min_slots;
                            have_slot_count = true;
                        }
                        else
                        {
                            const auto count =
                                static_cast<
                                    std::uint64_t
                                >(slot_count);

                            if (count < min_slots)
                            {
                                min_slots = count;
                            }

                            if (count > max_slots)
                            {
                                max_slots = count;
                            }
                        }
                    }
                }

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "BELONG_STORAGE storage=%llu "
                    "container=%d belong_property=%d "
                    "belong_struct=%d selected_match=%d "
                    "container_id=%d slot_array=%d "
                    "slot_inner_object=%d slot_count=%d",
                    static_cast<
                        unsigned long long
                    >(storage_index),
                    container != nullptr ? 1 : 0,
                    belong_property != nullptr ? 1 : 0,
                    belong_struct_property != nullptr
                        ? 1
                        : 0,
                    storage_matches_selected ? 1 : 0,
                    storage_has_container_id ? 1 : 0,
                    slot_array_property != nullptr ? 1 : 0,
                    inner_object ? 1 : 0,
                    slot_count
                );

                if (storage_matches_selected)
                {
                    ++selected_storage_matches;

                    if (
                        selected_guild_storage ==
                        nullptr
                    )
                    {
                        selected_guild_storage =
                            storage;

                        selected_guild_item_container =
                            container;

                        if (storage_has_container_id)
                        {
                            selected_container_id =
                                storage_container_id;

                            selected_container_id_found =
                                true;
                        }
                    }
                }

                ++storage_index;
            }

            RC::Unreal::UObject*
                item_container_manager{};

            for (
                auto* candidate :
                *item_container_managers
            )
            {
                if (candidate != nullptr)
                {
                    item_container_manager =
                        candidate;
                    break;
                }
            }

            std::uint64_t functions_found{};
            std::uint64_t parameter_lines{};
            std::uint64_t parameter_exceptions{};

            emit_detailed_function_parameters(
                item_container_manager,
                STR("GetGroupIdByItemContainerId"),
                "GetGroupIdByItemContainerId",
                functions_found,
                parameter_lines,
                parameter_exceptions
            );

            emit_detailed_function_parameters(
                item_container_manager,
                STR("GetGroupIdByItemSlotId"),
                "GetGroupIdByItemSlotId",
                functions_found,
                parameter_lines,
                parameter_exceptions
            );

            emit_detailed_function_parameters(
                item_container_manager,
                STR("GetContainer"),
                "GetContainer",
                functions_found,
                parameter_lines,
                parameter_exceptions
            );

            emit_detailed_function_parameters(
                item_container_manager,
                STR("TryGetContainer"),
                "TryGetContainer",
                functions_found,
                parameter_lines,
                parameter_exceptions
            );

            emit_format(
                "[ModIntegratedStorageCpp] "
                "DEEP_LAYOUT guild_storage_objects=%zu "
                "valid_storages=%llu "
                "belong_properties=%llu "
                "belong_structs=%llu "
                "group_candidates_found=%llu "
                "group_candidates_16=%llu "
                "selected_guild_matches=%llu "
                "selected_storage_matches=%llu "
                "selected_storage=%d "
                "selected_item_container=%d "
                "selected_container_id=%d "
                "container_candidates_found=%llu "
                "container_candidates_16=%llu "
                "nonzero_container_ids=%llu "
                "item_slot_arrays=%llu "
                "item_slot_array_objects=%llu "
                "total_slots=%llu min_slots=%llu "
                "max_slots=%llu manager_objects=%zu "
                "manager=%d functions_found=%llu "
                "parameter_lines=%llu "
                "parameter_exceptions=%llu",
                guild_storages->size(),
                static_cast<unsigned long long>(
                    valid_storages
                ),
                static_cast<unsigned long long>(
                    belong_properties
                ),
                static_cast<unsigned long long>(
                    belong_structs
                ),
                static_cast<unsigned long long>(
                    group_candidates_found
                ),
                static_cast<unsigned long long>(
                    group_candidates_16
                ),
                static_cast<unsigned long long>(
                    selected_guild_matches
                ),
                static_cast<unsigned long long>(
                    selected_storage_matches
                ),
                selected_guild_storage != nullptr
                    ? 1
                    : 0,
                selected_guild_item_container != nullptr
                    ? 1
                    : 0,
                selected_container_id_found ? 1 : 0,
                static_cast<unsigned long long>(
                    container_candidates_found
                ),
                static_cast<unsigned long long>(
                    container_candidates_16
                ),
                static_cast<unsigned long long>(
                    nonzero_container_ids
                ),
                static_cast<unsigned long long>(
                    item_slot_arrays
                ),
                static_cast<unsigned long long>(
                    item_slot_array_objects
                ),
                static_cast<unsigned long long>(
                    total_slots
                ),
                static_cast<unsigned long long>(
                    min_slots
                ),
                static_cast<unsigned long long>(
                    max_slots
                ),
                item_container_managers->size(),
                item_container_manager != nullptr
                    ? 1
                    : 0,
                static_cast<unsigned long long>(
                    functions_found
                ),
                static_cast<unsigned long long>(
                    parameter_lines
                ),
                static_cast<unsigned long long>(
                    parameter_exceptions
                )
            );

            if (
                valid_storages > 0 &&
                belong_structs == valid_storages &&
                item_slot_arrays == valid_storages &&
                item_container_manager != nullptr &&
                parameter_exceptions == 0
            )
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "DEEP_LAYOUT RESULT=PASS"
                );
            }
            else
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "DEEP_LAYOUT RESULT=INCOMPLETE"
                );
            }
        }
        catch (...)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "DEEP_LAYOUT RESULT=EXCEPTION"
            );
        }
    }


    constexpr std::uint64_t k_slot_fnv_offset{
        1469598103934665603ULL
    };

    constexpr std::uint64_t k_slot_fnv_prime{
        1099511628211ULL
    };

    auto slot_hash_byte(
        std::uint64_t& state,
        std::uint8_t value
    ) noexcept -> void
    {
        state ^= value;
        state *= k_slot_fnv_prime;
    }

    auto slot_hash_bytes(
        std::uint64_t& state,
        const void* data,
        std::size_t size
    ) noexcept -> void
    {
        if (data == nullptr)
        {
            slot_hash_byte(state, 0xff);
            return;
        }

        const auto* bytes =
            static_cast<const std::uint8_t*>(
                data
            );

        for (std::size_t index{}; index < size; ++index)
        {
            slot_hash_byte(state, bytes[index]);
        }
    }

    auto slot_hash_u64(
        std::uint64_t& state,
        std::uint64_t value
    ) noexcept -> void
    {
        for (std::size_t index{}; index < 8; ++index)
        {
            slot_hash_byte(
                state,
                static_cast<std::uint8_t>(
                    (value >> (index * 8)) & 0xff
                )
            );
        }
    }

    auto slot_property_kind(
        RC::Unreal::FProperty* property
    ) noexcept -> const char*
    {
        if (property == nullptr)
        {
            return "missing";
        }

        if (
            RC::Unreal::CastField<
                RC::Unreal::FNumericProperty
            >(property) != nullptr
        )
        {
            return "numeric";
        }

        if (
            RC::Unreal::CastField<
                RC::Unreal::FBoolProperty
            >(property) != nullptr
        )
        {
            return "bool";
        }

        if (
            RC::Unreal::CastField<
                RC::Unreal::FNameProperty
            >(property) != nullptr
        )
        {
            return "name";
        }

        if (
            RC::Unreal::CastField<
                RC::Unreal::FStructProperty
            >(property) != nullptr
        )
        {
            return "struct";
        }

        if (
            RC::Unreal::CastField<
                RC::Unreal::FObjectPropertyBase
            >(property) != nullptr
        )
        {
            return "object";
        }

        return "other";
    }

    auto run_read_only_slot_fingerprint_probe(
        const GuildKey& selected_guild,
        bool plan_complete
    ) noexcept -> void
    {
        if (!plan_complete)
        {
            return;
        }

        bool expected_reported{false};

        if (
            !g_slot_fingerprint_reported.
                compare_exchange_strong(
                    expected_reported,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                )
        )
        {
            return;
        }

        try
        {
            static auto* guild_storages =
                new std::vector<
                    RC::Unreal::UObject*
                >();

            guild_storages->clear();

            RC::Unreal::UObjectGlobals::FindAllOf(
                STR("PalGuildItemStorage"),
                *guild_storages
            );

            auto* pal_item_slot_class =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UClass*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/Pal.PalItemSlot")
                    );

            RC::Unreal::UObject*
                selected_item_container{};

            std::uint64_t valid_storages{};
            std::uint64_t selected_storage_matches{};
            std::uint64_t selection_exceptions{};

            for (auto* storage : *guild_storages)
            {
                if (storage == nullptr)
                {
                    continue;
                }

                ++valid_storages;

                try
                {
                    const auto item_container =
                        read_object_property_candidate(
                            storage,
                            STR("ItemContainer")
                        );

                    auto* container =
                        item_container.value;

                    if (container == nullptr)
                    {
                        continue;
                    }

                    auto* belong_property =
                        container->
                            GetPropertyByNameInChain(
                                STR("BelongInfo")
                            );

                    auto* belong_struct_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FStructProperty
                        >(belong_property);

                    if (
                        belong_property == nullptr ||
                        belong_struct_property == nullptr
                    )
                    {
                        continue;
                    }

                    auto* belong_definition =
                        belong_struct_property->
                            GetStruct().Get();

                    auto* belong_data =
                        belong_property->
                            ContainerPtrToValuePtr<
                                void
                            >(container);

                    const auto group_id =
                        read_nested_struct_candidate(
                            belong_definition,
                            belong_data,
                            belong_property->GetSize(),
                            STR("GroupId")
                        );

                    if (
                        group_id.size_is_16 &&
                        group_id.value ==
                            selected_guild
                    )
                    {
                        ++selected_storage_matches;

                        if (
                            selected_item_container ==
                            nullptr
                        )
                        {
                            selected_item_container =
                                container;
                        }
                    }
                }
                catch (...)
                {
                    ++selection_exceptions;
                }
            }

            auto* slot_property =
                selected_item_container != nullptr
                    ? selected_item_container->
                        GetPropertyByNameInChain(
                            STR("ItemSlotArray")
                        )
                    : nullptr;

            auto* slot_array_property =
                RC::Unreal::CastField<
                    RC::Unreal::FArrayProperty
                >(slot_property);

            auto* slot_inner_property =
                slot_array_property != nullptr
                    ? slot_array_property->GetInner()
                    : nullptr;

            auto* slot_object_property =
                RC::Unreal::CastField<
                    RC::Unreal::FObjectPropertyBase
                >(slot_inner_property);

            auto* accepted_slot_class =
                slot_object_property != nullptr
                    ? slot_object_property->
                        GetPropertyClass().Get()
                    : nullptr;

            const RC::Unreal::TCHAR*
                candidate_names[] = {
                    STR("ItemSlotId"),
                    STR("SlotId"),
                    STR("SlotID"),
                    STR("ContainerId"),
                    STR("ContainerID"),
                    STR("ItemContainerId"),
                    STR("ItemContainerID"),
                    STR("ItemId"),
                    STR("ItemID"),
                    STR("StaticItemId"),
                    STR("StaticItemID"),
                    STR("ItemNum"),
                    STR("ItemCount"),
                    STR("StackCount"),
                    STR("Count"),
                    STR("Amount"),
                    STR("ItemData")
                };

            const char* candidate_labels[] = {
                    "ItemSlotId",
                    "SlotId",
                    "SlotID",
                    "ContainerId",
                    "ContainerID",
                    "ItemContainerId",
                    "ItemContainerID",
                    "ItemId",
                    "ItemID",
                    "StaticItemId",
                    "StaticItemID",
                    "ItemNum",
                    "ItemCount",
                    "StackCount",
                    "Count",
                    "Amount",
                    "ItemData"
                };

            constexpr std::size_t candidate_count =
                sizeof(candidate_names) /
                sizeof(candidate_names[0]);

            std::uint64_t candidate_exists[
                candidate_count
            ]{};

            std::uint64_t candidate_readable[
                candidate_count
            ]{};

            std::int32_t slot_count{-1};
            std::uint64_t nonnull_slots{};
            std::uint64_t accepted_class_matches{};
            std::uint64_t pal_slot_class_matches{};
            std::uint64_t slot_exceptions{};
            std::uint64_t property_exceptions{};
            std::uint64_t properties_found{};
            std::uint64_t functions_found{};
            std::uint64_t function_parameter_lines{};
            std::uint64_t function_exceptions{};

            std::uint64_t structural_fingerprint{
                k_slot_fnv_offset
            };

            std::uint64_t content_fingerprint{
                k_slot_fnv_offset
            };

            slot_hash_bytes(
                structural_fingerprint,
                selected_guild.data(),
                selected_guild.size()
            );

            slot_hash_bytes(
                content_fingerprint,
                selected_guild.data(),
                selected_guild.size()
            );

            RC::Unreal::UObject*
                first_valid_slot{};

            if (
                slot_array_property != nullptr &&
                slot_object_property != nullptr &&
                selected_item_container != nullptr
            )
            {
                RC::Unreal::
                    FScriptArrayHelper_InContainer
                        helper(
                            slot_array_property,
                            selected_item_container
                        );

                slot_count = helper.Num();

                slot_hash_u64(
                    structural_fingerprint,
                    static_cast<std::uint64_t>(
                        slot_count > 0
                            ? slot_count
                            : 0
                    )
                );

                slot_hash_u64(
                    content_fingerprint,
                    static_cast<std::uint64_t>(
                        slot_count > 0
                            ? slot_count
                            : 0
                    )
                );

                for (
                    std::int32_t slot_index{};
                    slot_index < slot_count;
                    ++slot_index
                )
                {
                    try
                    {
                        auto* element_address =
                            helper.GetRawPtr(
                                slot_index
                            );

                        auto* slot =
                            slot_object_property->
                                GetObjectPropertyValue(
                                    element_address
                                );

                        const bool nonnull =
                            slot != nullptr;

                        const bool accepted_match =
                            nonnull &&
                            accepted_slot_class !=
                                nullptr &&
                            slot->IsA(
                                accepted_slot_class
                            );

                        const bool pal_slot_match =
                            nonnull &&
                            pal_item_slot_class !=
                                nullptr &&
                            slot->IsA(
                                pal_item_slot_class
                            );

                        nonnull_slots +=
                            nonnull ? 1U : 0U;

                        accepted_class_matches +=
                            accepted_match ? 1U : 0U;

                        pal_slot_class_matches +=
                            pal_slot_match ? 1U : 0U;

                        slot_hash_u64(
                            structural_fingerprint,
                            static_cast<
                                std::uint64_t
                            >(slot_index)
                        );

                        slot_hash_byte(
                            structural_fingerprint,
                            nonnull ? 1 : 0
                        );

                        slot_hash_byte(
                            structural_fingerprint,
                            accepted_match ? 1 : 0
                        );

                        slot_hash_byte(
                            structural_fingerprint,
                            pal_slot_match ? 1 : 0
                        );

                        slot_hash_u64(
                            content_fingerprint,
                            static_cast<
                                std::uint64_t
                            >(slot_index)
                        );

                        slot_hash_byte(
                            content_fingerprint,
                            nonnull ? 1 : 0
                        );

                        slot_hash_byte(
                            content_fingerprint,
                            accepted_match ? 1 : 0
                        );

                        slot_hash_byte(
                            content_fingerprint,
                            pal_slot_match ? 1 : 0
                        );

                        std::uint64_t fields_read{};
                        std::uint64_t numeric_fields{};
                        std::uint64_t fixed_fields{};
                        std::uint64_t nonnull_object_fields{};

                        if (
                            slot != nullptr &&
                            first_valid_slot == nullptr
                        )
                        {
                            first_valid_slot = slot;
                        }

                        if (slot != nullptr)
                        {
                            for (
                                std::size_t
                                    candidate_index{};
                                candidate_index <
                                    candidate_count;
                                ++candidate_index
                            )
                            {
                                auto* property =
                                    slot->
                                        GetPropertyByNameInChain(
                                            candidate_names[
                                                candidate_index
                                            ]
                                        );

                                if (property == nullptr)
                                {
                                    continue;
                                }

                                ++candidate_exists[
                                    candidate_index
                                ];

                                auto* value_address =
                                    property->
                                        ContainerPtrToValuePtr<
                                            void
                                        >(slot);

                                const auto property_size =
                                    property->GetSize();

                                slot_hash_u64(
                                    structural_fingerprint,
                                    candidate_index
                                );

                                slot_hash_u64(
                                    structural_fingerprint,
                                    static_cast<
                                        std::uint64_t
                                    >(
                                        property_size > 0
                                            ? property_size
                                            : 0
                                    )
                                );

                                auto* numeric_property =
                                    RC::Unreal::CastField<
                                        RC::Unreal::
                                            FNumericProperty
                                    >(property);

                                auto* bool_property =
                                    RC::Unreal::CastField<
                                        RC::Unreal::
                                            FBoolProperty
                                    >(property);

                                auto* name_property =
                                    RC::Unreal::CastField<
                                        RC::Unreal::
                                            FNameProperty
                                    >(property);

                                auto* struct_property =
                                    RC::Unreal::CastField<
                                        RC::Unreal::
                                            FStructProperty
                                    >(property);

                                auto* object_property =
                                    RC::Unreal::CastField<
                                        RC::Unreal::
                                            FObjectPropertyBase
                                    >(property);

                                if (
                                    numeric_property !=
                                        nullptr &&
                                    value_address !=
                                        nullptr &&
                                    property_size > 0 &&
                                    property_size <= 8
                                )
                                {
                                    ++candidate_readable[
                                        candidate_index
                                    ];

                                    ++fields_read;
                                    ++numeric_fields;

                                    slot_hash_u64(
                                        content_fingerprint,
                                        candidate_index
                                    );

                                    slot_hash_bytes(
                                        content_fingerprint,
                                        value_address,
                                        static_cast<
                                            std::size_t
                                        >(property_size)
                                    );
                                }
                                else if (
                                    bool_property !=
                                        nullptr &&
                                    value_address !=
                                        nullptr
                                )
                                {
                                    ++candidate_readable[
                                        candidate_index
                                    ];

                                    ++fields_read;

                                    const bool value =
                                        bool_property->
                                            GetPropertyValue(
                                                value_address
                                            );

                                    slot_hash_u64(
                                        content_fingerprint,
                                        candidate_index
                                    );

                                    slot_hash_byte(
                                        content_fingerprint,
                                        value ? 1 : 0
                                    );
                                }
                                else if (
                                    (
                                        name_property !=
                                            nullptr ||
                                        struct_property !=
                                            nullptr
                                    ) &&
                                    value_address !=
                                        nullptr &&
                                    property_size > 0 &&
                                    property_size <= 32
                                )
                                {
                                    ++candidate_readable[
                                        candidate_index
                                    ];

                                    ++fields_read;
                                    ++fixed_fields;

                                    slot_hash_u64(
                                        content_fingerprint,
                                        candidate_index
                                    );

                                    slot_hash_bytes(
                                        content_fingerprint,
                                        value_address,
                                        static_cast<
                                            std::size_t
                                        >(property_size)
                                    );
                                }
                                else if (
                                    object_property !=
                                        nullptr &&
                                    value_address !=
                                        nullptr
                                )
                                {
                                    ++candidate_readable[
                                        candidate_index
                                    ];

                                    ++fields_read;

                                    auto* nested_object =
                                        object_property->
                                            GetObjectPropertyValue(
                                                value_address
                                            );

                                    const bool
                                        nested_nonnull =
                                            nested_object !=
                                            nullptr;

                                    nonnull_object_fields +=
                                        nested_nonnull
                                            ? 1U
                                            : 0U;

                                    slot_hash_u64(
                                        content_fingerprint,
                                        candidate_index
                                    );

                                    slot_hash_byte(
                                        content_fingerprint,
                                        nested_nonnull
                                            ? 1
                                            : 0
                                    );
                                }
                            }
                        }

                        emit_format(
                            "[ModIntegratedStorageCpp] "
                            "SLOT_OBJECT index=%d "
                            "nonnull=%d accepted_class=%d "
                            "pal_item_slot=%d "
                            "fields_read=%llu "
                            "numeric_fields=%llu "
                            "fixed_fields=%llu "
                            "nonnull_object_fields=%llu",
                            slot_index,
                            nonnull ? 1 : 0,
                            accepted_match ? 1 : 0,
                            pal_slot_match ? 1 : 0,
                            static_cast<
                                unsigned long long
                            >(fields_read),
                            static_cast<
                                unsigned long long
                            >(numeric_fields),
                            static_cast<
                                unsigned long long
                            >(fixed_fields),
                            static_cast<
                                unsigned long long
                            >(nonnull_object_fields)
                        );
                    }
                    catch (...)
                    {
                        ++slot_exceptions;

                        emit_format(
                            "[ModIntegratedStorageCpp] "
                            "SLOT_OBJECT index=%d "
                            "reason=exception",
                            slot_index
                        );
                    }
                }
            }

            if (first_valid_slot != nullptr)
            {
                for (
                    std::size_t candidate_index{};
                    candidate_index <
                        candidate_count;
                    ++candidate_index
                )
                {
                    try
                    {
                        auto* property =
                            first_valid_slot->
                                GetPropertyByNameInChain(
                                    candidate_names[
                                        candidate_index
                                    ]
                                );

                        if (property == nullptr)
                        {
                            emit_format(
                                "[ModIntegratedStorageCpp] "
                                "SLOT_PROPERTY candidate=%s "
                                "exists=0",
                                candidate_labels[
                                    candidate_index
                                ]
                            );

                            continue;
                        }

                        ++properties_found;

                        emit_format(
                            "[ModIntegratedStorageCpp] "
                            "SLOT_PROPERTY candidate=%s "
                            "exists=1 kind=%s offset=%d "
                            "size=%d element_size=%d",
                            candidate_labels[
                                candidate_index
                            ],
                            slot_property_kind(
                                property
                            ),
                            property->
                                GetOffset_Internal(),
                            property->GetSize(),
                            property->GetElementSize()
                        );
                    }
                    catch (...)
                    {
                        ++property_exceptions;

                        emit_format(
                            "[ModIntegratedStorageCpp] "
                            "SLOT_PROPERTY candidate=%s "
                            "exists=0 reason=exception",
                            candidate_labels[
                                candidate_index
                            ]
                        );
                    }
                }
            }

            emit_detailed_function_parameters(
                first_valid_slot,
                STR("GetSlotId"),
                "GetSlotId",
                functions_found,
                function_parameter_lines,
                function_exceptions
            );

            emit_detailed_function_parameters(
                first_valid_slot,
                STR("GetContainerId"),
                "GetContainerId",
                functions_found,
                function_parameter_lines,
                function_exceptions
            );

            emit_detailed_function_parameters(
                first_valid_slot,
                STR("GetItemId"),
                "GetItemId",
                functions_found,
                function_parameter_lines,
                function_exceptions
            );

            emit_detailed_function_parameters(
                first_valid_slot,
                STR("GetItemStackCount"),
                "GetItemStackCount",
                functions_found,
                function_parameter_lines,
                function_exceptions
            );

            emit_detailed_function_parameters(
                first_valid_slot,
                STR("GetStackCount"),
                "GetStackCount",
                functions_found,
                function_parameter_lines,
                function_exceptions
            );

            emit_detailed_function_parameters(
                first_valid_slot,
                STR("GetStaticItemData"),
                "GetStaticItemData",
                functions_found,
                function_parameter_lines,
                function_exceptions
            );

            for (
                std::size_t candidate_index{};
                candidate_index < candidate_count;
                ++candidate_index
            )
            {
                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "SLOT_CANDIDATE candidate=%s "
                    "exists=%llu readable=%llu",
                    candidate_labels[
                        candidate_index
                    ],
                    static_cast<
                        unsigned long long
                    >(
                        candidate_exists[
                            candidate_index
                        ]
                    ),
                    static_cast<
                        unsigned long long
                    >(
                        candidate_readable[
                            candidate_index
                        ]
                    )
                );
            }

            emit_format(
                "[ModIntegratedStorageCpp] "
                "SLOT_FINGERPRINT "
                "guild_storage_objects=%zu "
                "valid_storages=%llu "
                "selected_storage_matches=%llu "
                "selected_item_container=%d "
                "slot_array=%d slot_inner_object=%d "
                "accepted_slot_class=%d "
                "pal_item_slot_class=%d "
                "slot_count=%d nonnull_slots=%llu "
                "accepted_class_matches=%llu "
                "pal_slot_class_matches=%llu "
                "properties_found=%llu "
                "property_exceptions=%llu "
                "functions_found=%llu "
                "function_parameter_lines=%llu "
                "function_exceptions=%llu "
                "selection_exceptions=%llu "
                "slot_exceptions=%llu "
                "structural=%016llx "
                "content=%016llx "
                "content_restart_stable=0",
                guild_storages->size(),
                static_cast<unsigned long long>(
                    valid_storages
                ),
                static_cast<unsigned long long>(
                    selected_storage_matches
                ),
                selected_item_container != nullptr
                    ? 1
                    : 0,
                slot_array_property != nullptr ? 1 : 0,
                slot_object_property != nullptr ? 1 : 0,
                accepted_slot_class != nullptr ? 1 : 0,
                pal_item_slot_class != nullptr ? 1 : 0,
                slot_count,
                static_cast<unsigned long long>(
                    nonnull_slots
                ),
                static_cast<unsigned long long>(
                    accepted_class_matches
                ),
                static_cast<unsigned long long>(
                    pal_slot_class_matches
                ),
                static_cast<unsigned long long>(
                    properties_found
                ),
                static_cast<unsigned long long>(
                    property_exceptions
                ),
                static_cast<unsigned long long>(
                    functions_found
                ),
                static_cast<unsigned long long>(
                    function_parameter_lines
                ),
                static_cast<unsigned long long>(
                    function_exceptions
                ),
                static_cast<unsigned long long>(
                    selection_exceptions
                ),
                static_cast<unsigned long long>(
                    slot_exceptions
                ),
                static_cast<unsigned long long>(
                    structural_fingerprint
                ),
                static_cast<unsigned long long>(
                    content_fingerprint
                )
            );

            if (
                selected_storage_matches == 1 &&
                selected_item_container != nullptr &&
                slot_array_property != nullptr &&
                slot_object_property != nullptr &&
                accepted_slot_class != nullptr &&
                pal_item_slot_class != nullptr &&
                slot_count > 0 &&
                nonnull_slots ==
                    static_cast<std::uint64_t>(
                        slot_count
                    ) &&
                accepted_class_matches ==
                    nonnull_slots &&
                pal_slot_class_matches ==
                    nonnull_slots &&
                properties_found > 0 &&
                property_exceptions == 0 &&
                function_exceptions == 0 &&
                selection_exceptions == 0 &&
                slot_exceptions == 0
            )
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "SLOT_FINGERPRINT RESULT=PASS"
                );
            }
            else
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "SLOT_FINGERPRINT RESULT=INCOMPLETE"
                );
            }
        }
        catch (...)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "SLOT_FINGERPRINT RESULT=EXCEPTION"
            );
        }
    }


    struct SlotLayoutProbeCounts
    {
        std::uint64_t found{};
        std::uint64_t valid_bounds{};
        std::uint64_t numeric{};
        std::uint64_t boolean{};
        std::uint64_t name{};
        std::uint64_t structure{};
        std::uint64_t object{};
        std::uint64_t other{};
        std::uint64_t exceptions{};
    };

    auto inspect_known_struct_candidates(
        RC::Unreal::UStruct* definition,
        const void* value_data,
        std::int32_t outer_size,
        const char* structure_label,
        const RC::Unreal::TCHAR* const* candidate_names,
        const char* const* candidate_labels,
        std::size_t candidate_count,
        RC::Unreal::UStruct* expected_nested_definition,
        SlotLayoutProbeCounts& counts
    ) noexcept -> void
    {
        if (definition == nullptr)
        {
            emit_format(
                "[ModIntegratedStorageCpp] "
                "SLOT_LAYOUT_STRUCT structure=%s "
                "definition=0 outer_size=%d",
                structure_label,
                outer_size
            );

            return;
        }

        emit_format(
            "[ModIntegratedStorageCpp] "
            "SLOT_LAYOUT_STRUCT structure=%s "
            "definition=1 outer_size=%d "
            "value_data=%d",
            structure_label,
            outer_size,
            value_data != nullptr ? 1 : 0
        );

        for (
            std::size_t candidate_index{};
            candidate_index < candidate_count;
            ++candidate_index
        )
        {
            try
            {
                auto* property =
                    definition->
                        GetPropertyByNameInChain(
                            candidate_names[
                                candidate_index
                            ]
                        );

                if (property == nullptr)
                {
                    emit_format(
                        "[ModIntegratedStorageCpp] "
                        "SLOT_LAYOUT_MEMBER structure=%s "
                        "candidate=%s exists=0",
                        structure_label,
                        candidate_labels[
                            candidate_index
                        ]
                    );

                    continue;
                }

                ++counts.found;

                const auto offset =
                    property->GetOffset_Internal();

                const auto size =
                    property->GetSize();

                const bool valid_bounds =
                    offset >= 0 &&
                    size > 0 &&
                    outer_size > 0 &&
                    offset <= outer_size &&
                    size <= outer_size - offset;

                counts.valid_bounds +=
                    valid_bounds ? 1U : 0U;

                auto* numeric_property =
                    RC::Unreal::CastField<
                        RC::Unreal::FNumericProperty
                    >(property);

                auto* bool_property =
                    RC::Unreal::CastField<
                        RC::Unreal::FBoolProperty
                    >(property);

                auto* name_property =
                    RC::Unreal::CastField<
                        RC::Unreal::FNameProperty
                    >(property);

                auto* struct_property =
                    RC::Unreal::CastField<
                        RC::Unreal::FStructProperty
                    >(property);

                auto* object_property =
                    RC::Unreal::CastField<
                        RC::Unreal::FObjectPropertyBase
                    >(property);

                const char* kind = "other";

                if (numeric_property != nullptr)
                {
                    kind = "numeric";
                    ++counts.numeric;
                }
                else if (bool_property != nullptr)
                {
                    kind = "bool";
                    ++counts.boolean;
                }
                else if (name_property != nullptr)
                {
                    kind = "name";
                    ++counts.name;
                }
                else if (struct_property != nullptr)
                {
                    kind = "struct";
                    ++counts.structure;
                }
                else if (object_property != nullptr)
                {
                    kind = "object";
                    ++counts.object;
                }
                else
                {
                    ++counts.other;
                }

                auto* nested_definition =
                    struct_property != nullptr
                        ? struct_property->
                            GetStruct().Get()
                        : nullptr;

                const bool expected_nested_match =
                    expected_nested_definition !=
                        nullptr &&
                    nested_definition ==
                        expected_nested_definition;

                const bool value_address_available =
                    valid_bounds &&
                    value_data != nullptr &&
                    property->
                        ContainerPtrToValuePtr<
                            void
                        >(
                            const_cast<void*>(
                                value_data
                            )
                        ) != nullptr;

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "SLOT_LAYOUT_MEMBER structure=%s "
                    "candidate=%s exists=1 kind=%s "
                    "offset=%d size=%d element_size=%d "
                    "bounds=%d nested_definition=%d "
                    "expected_nested_match=%d "
                    "value_address=%d",
                    structure_label,
                    candidate_labels[
                        candidate_index
                    ],
                    kind,
                    offset,
                    size,
                    property->GetElementSize(),
                    valid_bounds ? 1 : 0,
                    nested_definition != nullptr
                        ? 1
                        : 0,
                    expected_nested_match
                        ? 1
                        : 0,
                    value_address_available
                        ? 1
                        : 0
                );
            }
            catch (...)
            {
                ++counts.exceptions;

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "SLOT_LAYOUT_MEMBER structure=%s "
                    "candidate=%s exists=0 "
                    "reason=exception",
                    structure_label,
                    candidate_labels[
                        candidate_index
                    ]
                );
            }
        }
    }

    auto run_read_only_slot_identity_layout_probe(
        const GuildKey& selected_guild,
        bool plan_complete
    ) noexcept -> void
    {
        if (!plan_complete)
        {
            return;
        }

        bool expected_reported{false};

        if (
            !g_slot_identity_layout_reported.
                compare_exchange_strong(
                    expected_reported,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                )
        )
        {
            return;
        }

        try
        {
            static auto* guild_storages =
                new std::vector<
                    RC::Unreal::UObject*
                >();

            guild_storages->clear();

            RC::Unreal::UObjectGlobals::FindAllOf(
                STR("PalGuildItemStorage"),
                *guild_storages
            );

            auto* known_container_id =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/Pal.PalContainerId")
                    );

            auto* known_item_id =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/Pal.PalItemId")
                    );

            auto* known_slot_id =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/Pal.PalItemSlotId")
                    );

            auto* known_dynamic_item_id =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/Pal.PalDynamicItemId")
                    );

            RC::Unreal::UObject*
                selected_item_container{};

            std::uint64_t valid_storages{};
            std::uint64_t selected_storage_matches{};
            std::uint64_t selection_exceptions{};

            for (auto* storage : *guild_storages)
            {
                if (storage == nullptr)
                {
                    continue;
                }

                ++valid_storages;

                try
                {
                    const auto item_container =
                        read_object_property_candidate(
                            storage,
                            STR("ItemContainer")
                        );

                    auto* container =
                        item_container.value;

                    if (container == nullptr)
                    {
                        continue;
                    }

                    auto* belong_property =
                        container->
                            GetPropertyByNameInChain(
                                STR("BelongInfo")
                            );

                    auto* belong_struct_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FStructProperty
                        >(belong_property);

                    if (
                        belong_property == nullptr ||
                        belong_struct_property == nullptr
                    )
                    {
                        continue;
                    }

                    auto* belong_definition =
                        belong_struct_property->
                            GetStruct().Get();

                    auto* belong_data =
                        belong_property->
                            ContainerPtrToValuePtr<
                                void
                            >(container);

                    const auto group_id =
                        read_nested_struct_candidate(
                            belong_definition,
                            belong_data,
                            belong_property->GetSize(),
                            STR("GroupId")
                        );

                    if (
                        group_id.size_is_16 &&
                        group_id.value ==
                            selected_guild
                    )
                    {
                        ++selected_storage_matches;

                        if (
                            selected_item_container ==
                            nullptr
                        )
                        {
                            selected_item_container =
                                container;
                        }
                    }
                }
                catch (...)
                {
                    ++selection_exceptions;
                }
            }

            auto* slot_array_property =
                selected_item_container != nullptr
                    ? RC::Unreal::CastField<
                        RC::Unreal::FArrayProperty
                    >(
                        selected_item_container->
                            GetPropertyByNameInChain(
                                STR("ItemSlotArray")
                            )
                    )
                    : nullptr;

            auto* slot_object_property =
                slot_array_property != nullptr
                    ? RC::Unreal::CastField<
                        RC::Unreal::FObjectPropertyBase
                    >(
                        slot_array_property->
                            GetInner()
                    )
                    : nullptr;

            RC::Unreal::UObject*
                first_valid_slot{};

            std::int32_t slot_count{-1};
            std::uint64_t slot_exceptions{};

            if (
                selected_item_container != nullptr &&
                slot_array_property != nullptr &&
                slot_object_property != nullptr
            )
            {
                RC::Unreal::
                    FScriptArrayHelper_InContainer
                        helper(
                            slot_array_property,
                            selected_item_container
                        );

                slot_count = helper.Num();

                for (
                    std::int32_t slot_index{};
                    slot_index < slot_count;
                    ++slot_index
                )
                {
                    try
                    {
                        auto* slot =
                            slot_object_property->
                                GetObjectPropertyValue(
                                    helper.GetRawPtr(
                                        slot_index
                                    )
                                );

                        if (slot != nullptr)
                        {
                            first_valid_slot = slot;
                            break;
                        }
                    }
                    catch (...)
                    {
                        ++slot_exceptions;
                    }
                }
            }

            auto* container_property =
                first_valid_slot != nullptr
                    ? RC::Unreal::CastField<
                        RC::Unreal::FStructProperty
                    >(
                        first_valid_slot->
                            GetPropertyByNameInChain(
                                STR("ContainerId")
                            )
                    )
                    : nullptr;

            auto* item_property =
                first_valid_slot != nullptr
                    ? RC::Unreal::CastField<
                        RC::Unreal::FStructProperty
                    >(
                        first_valid_slot->
                            GetPropertyByNameInChain(
                                STR("ItemId")
                            )
                    )
                    : nullptr;

            auto* container_definition =
                container_property != nullptr
                    ? container_property->
                        GetStruct().Get()
                    : nullptr;

            auto* item_definition =
                item_property != nullptr
                    ? item_property->
                        GetStruct().Get()
                    : nullptr;

            auto* container_data =
                container_property != nullptr &&
                first_valid_slot != nullptr
                    ? container_property->
                        ContainerPtrToValuePtr<
                            void
                        >(first_valid_slot)
                    : nullptr;

            auto* item_data =
                item_property != nullptr &&
                first_valid_slot != nullptr
                    ? item_property->
                        ContainerPtrToValuePtr<
                            void
                        >(first_valid_slot)
                    : nullptr;

            const RC::Unreal::TCHAR*
                container_candidates[] = {
                    STR("Guid"),
                    STR("GUID"),
                    STR("Id"),
                    STR("ID"),
                    STR("Value"),
                    STR("InstanceId"),
                    STR("InstanceID"),
                    STR("UniqueId"),
                    STR("UniqueID"),
                    STR("LocalId"),
                    STR("LocalID"),
                    STR("Index"),
                    STR("Type"),
                    STR("GroupId")
                };

            const char*
                container_labels[] = {
                    "Guid",
                    "GUID",
                    "Id",
                    "ID",
                    "Value",
                    "InstanceId",
                    "InstanceID",
                    "UniqueId",
                    "UniqueID",
                    "LocalId",
                    "LocalID",
                    "Index",
                    "Type",
                    "GroupId"
                };

            const RC::Unreal::TCHAR*
                slot_candidates[] = {
                    STR("ContainerId"),
                    STR("ContainerID"),
                    STR("SlotIndex"),
                    STR("Index"),
                    STR("SlotId"),
                    STR("SlotID"),
                    STR("ItemSlotId"),
                    STR("ItemSlotID"),
                    STR("Id"),
                    STR("ID"),
                    STR("Value"),
                    STR("Num")
                };

            const char*
                slot_labels[] = {
                    "ContainerId",
                    "ContainerID",
                    "SlotIndex",
                    "Index",
                    "SlotId",
                    "SlotID",
                    "ItemSlotId",
                    "ItemSlotID",
                    "Id",
                    "ID",
                    "Value",
                    "Num"
                };

            const RC::Unreal::TCHAR*
                item_candidates[] = {
                    STR("StaticId"),
                    STR("StaticID"),
                    STR("StaticItemId"),
                    STR("StaticItemID"),
                    STR("DynamicId"),
                    STR("DynamicID"),
                    STR("DynamicItemId"),
                    STR("DynamicItemID"),
                    STR("InstanceId"),
                    STR("InstanceID"),
                    STR("UniqueId"),
                    STR("UniqueID"),
                    STR("ItemId"),
                    STR("ItemID"),
                    STR("Guid"),
                    STR("GUID"),
                    STR("Type"),
                    STR("Category"),
                    STR("Value"),
                    STR("Name"),
                    STR("ItemName")
                };

            const char*
                item_labels[] = {
                    "StaticId",
                    "StaticID",
                    "StaticItemId",
                    "StaticItemID",
                    "DynamicId",
                    "DynamicID",
                    "DynamicItemId",
                    "DynamicItemID",
                    "InstanceId",
                    "InstanceID",
                    "UniqueId",
                    "UniqueID",
                    "ItemId",
                    "ItemID",
                    "Guid",
                    "GUID",
                    "Type",
                    "Category",
                    "Value",
                    "Name",
                    "ItemName"
                };

            const RC::Unreal::TCHAR*
                dynamic_candidates[] = {
                    STR("Guid"),
                    STR("GUID"),
                    STR("Id"),
                    STR("ID"),
                    STR("Value"),
                    STR("InstanceId"),
                    STR("InstanceID"),
                    STR("UniqueId"),
                    STR("UniqueID"),
                    STR("LocalId"),
                    STR("LocalID"),
                    STR("Index"),
                    STR("Type")
                };

            const char*
                dynamic_labels[] = {
                    "Guid",
                    "GUID",
                    "Id",
                    "ID",
                    "Value",
                    "InstanceId",
                    "InstanceID",
                    "UniqueId",
                    "UniqueID",
                    "LocalId",
                    "LocalID",
                    "Index",
                    "Type"
                };

            SlotLayoutProbeCounts
                container_counts{};

            SlotLayoutProbeCounts
                slot_counts{};

            SlotLayoutProbeCounts
                item_counts{};

            SlotLayoutProbeCounts
                dynamic_counts{};

            inspect_known_struct_candidates(
                container_definition,
                container_data,
                container_property != nullptr
                    ? container_property->GetSize()
                    : 0,
                "ContainerId",
                container_candidates,
                container_labels,
                sizeof(container_candidates) /
                    sizeof(container_candidates[0]),
                nullptr,
                container_counts
            );

            inspect_known_struct_candidates(
                known_slot_id,
                nullptr,
                20,
                "SlotId",
                slot_candidates,
                slot_labels,
                sizeof(slot_candidates) /
                    sizeof(slot_candidates[0]),
                known_container_id,
                slot_counts
            );

            inspect_known_struct_candidates(
                item_definition,
                item_data,
                item_property != nullptr
                    ? item_property->GetSize()
                    : 0,
                "ItemId",
                item_candidates,
                item_labels,
                sizeof(item_candidates) /
                    sizeof(item_candidates[0]),
                known_dynamic_item_id,
                item_counts
            );

            inspect_known_struct_candidates(
                known_dynamic_item_id,
                nullptr,
                known_dynamic_item_id != nullptr
                    ? 32
                    : 0,
                "DynamicItemId",
                dynamic_candidates,
                dynamic_labels,
                sizeof(dynamic_candidates) /
                    sizeof(dynamic_candidates[0]),
                nullptr,
                dynamic_counts
            );

            const bool container_type_match =
                container_definition != nullptr &&
                known_container_id != nullptr &&
                container_definition ==
                    known_container_id;

            const bool item_type_match =
                item_definition != nullptr &&
                known_item_id != nullptr &&
                item_definition ==
                    known_item_id;

            const std::uint64_t total_exceptions =
                selection_exceptions +
                slot_exceptions +
                container_counts.exceptions +
                slot_counts.exceptions +
                item_counts.exceptions +
                dynamic_counts.exceptions;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "SLOT_LAYOUT "
                "guild_storage_objects=%zu "
                "valid_storages=%llu "
                "selected_storage_matches=%llu "
                "selected_item_container=%d "
                "slot_count=%d first_valid_slot=%d "
                "container_property=%d "
                "container_size=%d "
                "container_definition=%d "
                "known_container_definition=%d "
                "container_type_match=%d "
                "container_members=%llu "
                "container_bounds=%llu "
                "item_property=%d item_size=%d "
                "item_definition=%d "
                "known_item_definition=%d "
                "item_type_match=%d "
                "item_members=%llu "
                "item_bounds=%llu "
                "known_slot_definition=%d "
                "slot_members=%llu "
                "slot_bounds=%llu "
                "known_dynamic_definition=%d "
                "dynamic_members=%llu "
                "dynamic_bounds=%llu "
                "exceptions=%llu",
                guild_storages->size(),
                static_cast<unsigned long long>(
                    valid_storages
                ),
                static_cast<unsigned long long>(
                    selected_storage_matches
                ),
                selected_item_container != nullptr
                    ? 1
                    : 0,
                slot_count,
                first_valid_slot != nullptr
                    ? 1
                    : 0,
                container_property != nullptr
                    ? 1
                    : 0,
                container_property != nullptr
                    ? container_property->GetSize()
                    : 0,
                container_definition != nullptr
                    ? 1
                    : 0,
                known_container_id != nullptr
                    ? 1
                    : 0,
                container_type_match ? 1 : 0,
                static_cast<unsigned long long>(
                    container_counts.found
                ),
                static_cast<unsigned long long>(
                    container_counts.valid_bounds
                ),
                item_property != nullptr
                    ? 1
                    : 0,
                item_property != nullptr
                    ? item_property->GetSize()
                    : 0,
                item_definition != nullptr
                    ? 1
                    : 0,
                known_item_id != nullptr
                    ? 1
                    : 0,
                item_type_match ? 1 : 0,
                static_cast<unsigned long long>(
                    item_counts.found
                ),
                static_cast<unsigned long long>(
                    item_counts.valid_bounds
                ),
                known_slot_id != nullptr
                    ? 1
                    : 0,
                static_cast<unsigned long long>(
                    slot_counts.found
                ),
                static_cast<unsigned long long>(
                    slot_counts.valid_bounds
                ),
                known_dynamic_item_id != nullptr
                    ? 1
                    : 0,
                static_cast<unsigned long long>(
                    dynamic_counts.found
                ),
                static_cast<unsigned long long>(
                    dynamic_counts.valid_bounds
                ),
                static_cast<unsigned long long>(
                    total_exceptions
                )
            );

            if (
                selected_storage_matches == 1 &&
                selected_item_container != nullptr &&
                first_valid_slot != nullptr &&
                container_property != nullptr &&
                container_property->GetSize() == 16 &&
                container_type_match &&
                item_property != nullptr &&
                item_property->GetSize() == 40 &&
                item_type_match &&
                known_slot_id != nullptr &&
                known_dynamic_item_id != nullptr &&
                container_counts.found > 0 &&
                container_counts.found ==
                    container_counts.valid_bounds &&
                slot_counts.found > 0 &&
                slot_counts.found ==
                    slot_counts.valid_bounds &&
                item_counts.found > 0 &&
                item_counts.found ==
                    item_counts.valid_bounds &&
                dynamic_counts.found > 0 &&
                dynamic_counts.found ==
                    dynamic_counts.valid_bounds &&
                total_exceptions == 0
            )
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "SLOT_LAYOUT RESULT=PASS"
                );
            }
            else
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "SLOT_LAYOUT RESULT=INCOMPLETE"
                );
            }
        }
        catch (...)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "SLOT_LAYOUT RESULT=EXCEPTION"
            );
        }
    }


    struct OrdinalLayoutCounts
    {
        std::uint64_t fields{};
        std::uint64_t valid_bounds{};
        std::uint64_t numeric{};
        std::uint64_t boolean{};
        std::uint64_t name{};
        std::uint64_t structure{};
        std::uint64_t object{};
        std::uint64_t array{};
        std::uint64_t set{};
        std::uint64_t map{};
        std::uint64_t other{};
        std::uint64_t nested_guid_matches{};
        std::uint64_t nested_container_matches{};
        std::uint64_t nested_dynamic_matches{};
        std::uint64_t exceptions{};
    };

    auto inspect_ordinal_struct_layout(
        RC::Unreal::UStruct* definition,
        std::int32_t expected_size,
        const char* structure_label,
        RC::Unreal::UStruct* known_guid,
        RC::Unreal::UStruct* known_container,
        RC::Unreal::UStruct* known_dynamic,
        OrdinalLayoutCounts& counts
    ) noexcept -> void
    {
        if (definition == nullptr)
        {
            emit_format(
                "[ModIntegratedStorageCpp] "
                "ORDINAL_STRUCT structure=%s "
                "definition=0 expected_size=%d",
                structure_label,
                expected_size
            );

            return;
        }

        emit_format(
            "[ModIntegratedStorageCpp] "
            "ORDINAL_STRUCT structure=%s "
            "definition=1 expected_size=%d "
            "properties_size=%d",
            structure_label,
            expected_size,
            definition->GetPropertiesSize()
        );

        try
        {
            RC::Unreal::TFieldIterator<
                RC::Unreal::FProperty
            > iterator(
                definition,
                RC::Unreal::EFieldIterationFlags::None
            );

            std::uint64_t ordinal{};

            while (iterator)
            {
                auto* property = *iterator;

                if (property == nullptr)
                {
                    ++counts.exceptions;
                    ++iterator;
                    ++ordinal;
                    continue;
                }

                try
                {
                    ++counts.fields;

                    const auto offset =
                        property->GetOffset_Internal();

                    const auto size =
                        property->GetSize();

                    const auto element_size =
                        property->GetElementSize();

                    const auto array_dim =
                        property->GetArrayDim();

                    const bool valid_bounds =
                        offset >= 0 &&
                        size > 0 &&
                        expected_size > 0 &&
                        offset <= expected_size &&
                        size <= expected_size - offset;

                    counts.valid_bounds +=
                        valid_bounds ? 1U : 0U;

                    auto* numeric_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FNumericProperty
                        >(property);

                    auto* bool_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FBoolProperty
                        >(property);

                    auto* name_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FNameProperty
                        >(property);

                    auto* struct_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FStructProperty
                        >(property);

                    auto* object_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FObjectPropertyBase
                        >(property);

                    auto* array_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FArrayProperty
                        >(property);

                    auto* set_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FSetProperty
                        >(property);

                    auto* map_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FMapProperty
                        >(property);

                    const char* kind = "other";

                    if (numeric_property != nullptr)
                    {
                        kind = "numeric";
                        ++counts.numeric;
                    }
                    else if (bool_property != nullptr)
                    {
                        kind = "bool";
                        ++counts.boolean;
                    }
                    else if (name_property != nullptr)
                    {
                        kind = "name";
                        ++counts.name;
                    }
                    else if (struct_property != nullptr)
                    {
                        kind = "struct";
                        ++counts.structure;
                    }
                    else if (object_property != nullptr)
                    {
                        kind = "object";
                        ++counts.object;
                    }
                    else if (array_property != nullptr)
                    {
                        kind = "array";
                        ++counts.array;
                    }
                    else if (set_property != nullptr)
                    {
                        kind = "set";
                        ++counts.set;
                    }
                    else if (map_property != nullptr)
                    {
                        kind = "map";
                        ++counts.map;
                    }
                    else
                    {
                        ++counts.other;
                    }

                    auto* nested_definition =
                        struct_property != nullptr
                            ? struct_property->
                                GetStruct().Get()
                            : nullptr;

                    const auto nested_size =
                        nested_definition != nullptr
                            ? nested_definition->
                                GetPropertiesSize()
                            : 0;

                    const bool guid_match =
                        nested_definition != nullptr &&
                        known_guid != nullptr &&
                        nested_definition == known_guid;

                    const bool container_match =
                        nested_definition != nullptr &&
                        known_container != nullptr &&
                        nested_definition ==
                            known_container;

                    const bool dynamic_match =
                        nested_definition != nullptr &&
                        known_dynamic != nullptr &&
                        nested_definition ==
                            known_dynamic;

                    counts.nested_guid_matches +=
                        guid_match ? 1U : 0U;

                    counts.nested_container_matches +=
                        container_match ? 1U : 0U;

                    counts.nested_dynamic_matches +=
                        dynamic_match ? 1U : 0U;

                    emit_format(
                        "[ModIntegratedStorageCpp] "
                        "ORDINAL_FIELD structure=%s "
                        "ordinal=%llu kind=%s "
                        "offset=%d size=%d "
                        "element_size=%d array_dim=%d "
                        "bounds=%d nested_definition=%d "
                        "nested_size=%d guid_match=%d "
                        "container_match=%d "
                        "dynamic_match=%d",
                        structure_label,
                        static_cast<
                            unsigned long long
                        >(ordinal),
                        kind,
                        offset,
                        size,
                        element_size,
                        array_dim,
                        valid_bounds ? 1 : 0,
                        nested_definition != nullptr
                            ? 1
                            : 0,
                        nested_size,
                        guid_match ? 1 : 0,
                        container_match ? 1 : 0,
                        dynamic_match ? 1 : 0
                    );
                }
                catch (...)
                {
                    ++counts.exceptions;

                    emit_format(
                        "[ModIntegratedStorageCpp] "
                        "ORDINAL_FIELD structure=%s "
                        "ordinal=%llu reason=exception",
                        structure_label,
                        static_cast<
                            unsigned long long
                        >(ordinal)
                    );
                }

                ++iterator;
                ++ordinal;
            }
        }
        catch (...)
        {
            ++counts.exceptions;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "ORDINAL_STRUCT structure=%s "
                "reason=iterator_exception",
                structure_label
            );
        }
    }

    auto run_read_only_ordinal_identity_probe(
        const GuildKey& selected_guild,
        bool plan_complete
    ) noexcept -> void
    {
        if (!plan_complete)
        {
            return;
        }

        bool expected_reported{false};

        if (
            !g_ordinal_identity_layout_reported.
                compare_exchange_strong(
                    expected_reported,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                )
        )
        {
            return;
        }

        try
        {
            static auto* guild_storages =
                new std::vector<
                    RC::Unreal::UObject*
                >();

            guild_storages->clear();

            RC::Unreal::UObjectGlobals::FindAllOf(
                STR("PalGuildItemStorage"),
                *guild_storages
            );

            auto* known_container_id =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/Pal.PalContainerId")
                    );

            auto* known_item_id =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/Pal.PalItemId")
                    );

            auto* known_slot_id =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/Pal.PalItemSlotId")
                    );

            auto* known_dynamic_id =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/Pal.PalDynamicItemId")
                    );

            auto* known_guid =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/CoreUObject.Guid")
                    );

            RC::Unreal::UObject*
                selected_item_container{};

            std::uint64_t valid_storages{};
            std::uint64_t selected_storage_matches{};
            std::uint64_t selection_exceptions{};

            for (auto* storage : *guild_storages)
            {
                if (storage == nullptr)
                {
                    continue;
                }

                ++valid_storages;

                try
                {
                    const auto item_container =
                        read_object_property_candidate(
                            storage,
                            STR("ItemContainer")
                        );

                    auto* container =
                        item_container.value;

                    if (container == nullptr)
                    {
                        continue;
                    }

                    auto* belong_property =
                        container->
                            GetPropertyByNameInChain(
                                STR("BelongInfo")
                            );

                    auto* belong_struct_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FStructProperty
                        >(belong_property);

                    if (
                        belong_property == nullptr ||
                        belong_struct_property == nullptr
                    )
                    {
                        continue;
                    }

                    auto* belong_definition =
                        belong_struct_property->
                            GetStruct().Get();

                    auto* belong_data =
                        belong_property->
                            ContainerPtrToValuePtr<
                                void
                            >(container);

                    const auto group_id =
                        read_nested_struct_candidate(
                            belong_definition,
                            belong_data,
                            belong_property->GetSize(),
                            STR("GroupId")
                        );

                    if (
                        group_id.size_is_16 &&
                        group_id.value ==
                            selected_guild
                    )
                    {
                        ++selected_storage_matches;

                        if (
                            selected_item_container ==
                            nullptr
                        )
                        {
                            selected_item_container =
                                container;
                        }
                    }
                }
                catch (...)
                {
                    ++selection_exceptions;
                }
            }

            auto* slot_array_property =
                selected_item_container != nullptr
                    ? RC::Unreal::CastField<
                        RC::Unreal::FArrayProperty
                    >(
                        selected_item_container->
                            GetPropertyByNameInChain(
                                STR("ItemSlotArray")
                            )
                    )
                    : nullptr;

            auto* slot_object_property =
                slot_array_property != nullptr
                    ? RC::Unreal::CastField<
                        RC::Unreal::FObjectPropertyBase
                    >(
                        slot_array_property->
                            GetInner()
                    )
                    : nullptr;

            RC::Unreal::UObject*
                first_valid_slot{};

            std::int32_t slot_count{-1};
            std::uint64_t slot_exceptions{};

            if (
                selected_item_container != nullptr &&
                slot_array_property != nullptr &&
                slot_object_property != nullptr
            )
            {
                RC::Unreal::
                    FScriptArrayHelper_InContainer
                        helper(
                            slot_array_property,
                            selected_item_container
                        );

                slot_count = helper.Num();

                for (
                    std::int32_t slot_index{};
                    slot_index < slot_count;
                    ++slot_index
                )
                {
                    try
                    {
                        auto* slot =
                            slot_object_property->
                                GetObjectPropertyValue(
                                    helper.GetRawPtr(
                                        slot_index
                                    )
                                );

                        if (slot != nullptr)
                        {
                            first_valid_slot = slot;
                            break;
                        }
                    }
                    catch (...)
                    {
                        ++slot_exceptions;
                    }
                }
            }

            auto* container_property =
                first_valid_slot != nullptr
                    ? RC::Unreal::CastField<
                        RC::Unreal::FStructProperty
                    >(
                        first_valid_slot->
                            GetPropertyByNameInChain(
                                STR("ContainerId")
                            )
                    )
                    : nullptr;

            auto* item_property =
                first_valid_slot != nullptr
                    ? RC::Unreal::CastField<
                        RC::Unreal::FStructProperty
                    >(
                        first_valid_slot->
                            GetPropertyByNameInChain(
                                STR("ItemId")
                            )
                    )
                    : nullptr;

            auto* container_definition =
                container_property != nullptr
                    ? container_property->
                        GetStruct().Get()
                    : nullptr;

            auto* item_definition =
                item_property != nullptr
                    ? item_property->
                        GetStruct().Get()
                    : nullptr;

            auto* container_id_member =
                container_definition != nullptr
                    ? RC::Unreal::CastField<
                        RC::Unreal::FStructProperty
                    >(
                        container_definition->
                            GetPropertyByNameInChain(
                                STR("Id")
                            )
                    )
                    : nullptr;

            auto* dynamic_id_member =
                item_definition != nullptr
                    ? RC::Unreal::CastField<
                        RC::Unreal::FStructProperty
                    >(
                        item_definition->
                            GetPropertyByNameInChain(
                                STR("DynamicId")
                            )
                    )
                    : nullptr;

            auto* container_id_definition =
                container_id_member != nullptr
                    ? container_id_member->
                        GetStruct().Get()
                    : nullptr;

            auto* dynamic_id_definition =
                dynamic_id_member != nullptr
                    ? dynamic_id_member->
                        GetStruct().Get()
                    : nullptr;

            OrdinalLayoutCounts
                container_outer_counts{};

            OrdinalLayoutCounts
                container_id_counts{};

            OrdinalLayoutCounts
                slot_id_counts{};

            OrdinalLayoutCounts
                item_id_counts{};

            OrdinalLayoutCounts
                dynamic_id_counts{};

            inspect_ordinal_struct_layout(
                container_definition,
                16,
                "ContainerId",
                known_guid,
                known_container_id,
                known_dynamic_id,
                container_outer_counts
            );

            inspect_ordinal_struct_layout(
                container_id_definition,
                16,
                "ContainerIdId",
                known_guid,
                known_container_id,
                known_dynamic_id,
                container_id_counts
            );

            inspect_ordinal_struct_layout(
                known_slot_id,
                20,
                "SlotId",
                known_guid,
                known_container_id,
                known_dynamic_id,
                slot_id_counts
            );

            inspect_ordinal_struct_layout(
                item_definition,
                40,
                "ItemId",
                known_guid,
                known_container_id,
                known_dynamic_id,
                item_id_counts
            );

            inspect_ordinal_struct_layout(
                dynamic_id_definition,
                32,
                "DynamicItemId",
                known_guid,
                known_container_id,
                known_dynamic_id,
                dynamic_id_counts
            );

            const bool container_type_match =
                container_definition != nullptr &&
                known_container_id != nullptr &&
                container_definition ==
                    known_container_id;

            const bool item_type_match =
                item_definition != nullptr &&
                known_item_id != nullptr &&
                item_definition ==
                    known_item_id;

            const bool dynamic_type_match =
                dynamic_id_definition != nullptr &&
                known_dynamic_id != nullptr &&
                dynamic_id_definition ==
                    known_dynamic_id;

            const std::uint64_t total_exceptions =
                selection_exceptions +
                slot_exceptions +
                container_outer_counts.exceptions +
                container_id_counts.exceptions +
                slot_id_counts.exceptions +
                item_id_counts.exceptions +
                dynamic_id_counts.exceptions;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "ORDINAL_LAYOUT "
                "guild_storage_objects=%zu "
                "valid_storages=%llu "
                "selected_storage_matches=%llu "
                "selected_item_container=%d "
                "slot_count=%d first_valid_slot=%d "
                "known_guid=%d "
                "container_type_match=%d "
                "item_type_match=%d "
                "dynamic_type_match=%d "
                "container_fields=%llu "
                "container_bounds=%llu "
                "container_id_fields=%llu "
                "container_id_bounds=%llu "
                "slot_id_fields=%llu "
                "slot_id_bounds=%llu "
                "item_id_fields=%llu "
                "item_id_bounds=%llu "
                "dynamic_id_fields=%llu "
                "dynamic_id_bounds=%llu "
                "container_id_guid_matches=%llu "
                "dynamic_guid_matches=%llu "
                "exceptions=%llu",
                guild_storages->size(),
                static_cast<unsigned long long>(
                    valid_storages
                ),
                static_cast<unsigned long long>(
                    selected_storage_matches
                ),
                selected_item_container != nullptr
                    ? 1
                    : 0,
                slot_count,
                first_valid_slot != nullptr
                    ? 1
                    : 0,
                known_guid != nullptr ? 1 : 0,
                container_type_match ? 1 : 0,
                item_type_match ? 1 : 0,
                dynamic_type_match ? 1 : 0,
                static_cast<unsigned long long>(
                    container_outer_counts.fields
                ),
                static_cast<unsigned long long>(
                    container_outer_counts.valid_bounds
                ),
                static_cast<unsigned long long>(
                    container_id_counts.fields
                ),
                static_cast<unsigned long long>(
                    container_id_counts.valid_bounds
                ),
                static_cast<unsigned long long>(
                    slot_id_counts.fields
                ),
                static_cast<unsigned long long>(
                    slot_id_counts.valid_bounds
                ),
                static_cast<unsigned long long>(
                    item_id_counts.fields
                ),
                static_cast<unsigned long long>(
                    item_id_counts.valid_bounds
                ),
                static_cast<unsigned long long>(
                    dynamic_id_counts.fields
                ),
                static_cast<unsigned long long>(
                    dynamic_id_counts.valid_bounds
                ),
                static_cast<unsigned long long>(
                    container_id_counts.
                        nested_guid_matches
                ),
                static_cast<unsigned long long>(
                    dynamic_id_counts.
                        nested_guid_matches
                ),
                static_cast<unsigned long long>(
                    total_exceptions
                )
            );

            if (
                selected_storage_matches == 1 &&
                selected_item_container != nullptr &&
                first_valid_slot != nullptr &&
                container_type_match &&
                item_type_match &&
                dynamic_type_match &&
                container_id_definition != nullptr &&
                known_slot_id != nullptr &&
                container_outer_counts.fields > 0 &&
                container_outer_counts.fields ==
                    container_outer_counts.valid_bounds &&
                container_id_counts.fields > 0 &&
                container_id_counts.fields ==
                    container_id_counts.valid_bounds &&
                slot_id_counts.fields > 0 &&
                slot_id_counts.fields ==
                    slot_id_counts.valid_bounds &&
                item_id_counts.fields > 0 &&
                item_id_counts.fields ==
                    item_id_counts.valid_bounds &&
                dynamic_id_counts.fields > 0 &&
                dynamic_id_counts.fields ==
                    dynamic_id_counts.valid_bounds &&
                total_exceptions == 0
            )
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "ORDINAL_LAYOUT RESULT=PASS"
                );
            }
            else
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "ORDINAL_LAYOUT RESULT=INCOMPLETE"
                );
            }
        }
        catch (...)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "ORDINAL_LAYOUT RESULT=EXCEPTION"
            );
        }
    }


    constexpr std::uint64_t k_semantic_offset{
        1469598103934665603ULL
    };

    constexpr std::uint64_t k_semantic_prime{
        1099511628211ULL
    };

    auto semantic_mix_bytes(
        std::uint64_t& state,
        const void* data,
        std::size_t size
    ) noexcept -> void
    {
        if (data == nullptr)
        {
            state ^= 0xff;
            state *= k_semantic_prime;
            return;
        }

        const auto* bytes =
            static_cast<const std::uint8_t*>(data);

        for (std::size_t index{}; index < size; ++index)
        {
            state ^= bytes[index];
            state *= k_semantic_prime;
        }
    }

    auto semantic_mix_u64(
        std::uint64_t& state,
        std::uint64_t value
    ) noexcept -> void
    {
        semantic_mix_bytes(
            state,
            &value,
            sizeof(value)
        );
    }

    struct SemanticSnapshot
    {
        bool valid{};
        std::uint64_t fingerprint{k_semantic_offset};
        std::int32_t slot_count{-1};
        std::uint64_t nonnull_slots{};
        std::uint64_t fully_read_slots{};
        std::uint64_t container_bytes{};
        std::uint64_t item_bytes{};
        std::uint64_t stack_bytes{};
        std::uint64_t exceptions{};
    };

    auto build_semantic_snapshot(
        const GuildKey& selected_guild
    ) noexcept -> SemanticSnapshot
    {
        SemanticSnapshot snapshot{};

        try
        {
            static auto* guild_storages =
                new std::vector<RC::Unreal::UObject*>();

            guild_storages->clear();

            RC::Unreal::UObjectGlobals::FindAllOf(
                STR("PalGuildItemStorage"),
                *guild_storages
            );

            auto* known_container =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/Pal.PalContainerId")
                    );

            auto* known_item =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/Pal.PalItemId")
                    );

            RC::Unreal::UObject* selected_container{};
            std::uint64_t selected_matches{};

            for (auto* storage : *guild_storages)
            {
                if (storage == nullptr)
                {
                    continue;
                }

                const auto item_container =
                    read_object_property_candidate(
                        storage,
                        STR("ItemContainer")
                    );

                auto* container = item_container.value;
                if (container == nullptr)
                {
                    continue;
                }

                auto* belong_property =
                    container->GetPropertyByNameInChain(
                        STR("BelongInfo")
                    );

                auto* belong_struct =
                    RC::Unreal::CastField<
                        RC::Unreal::FStructProperty
                    >(belong_property);

                if (
                    belong_property == nullptr ||
                    belong_struct == nullptr
                )
                {
                    continue;
                }

                const auto group_id =
                    read_nested_struct_candidate(
                        belong_struct->GetStruct().Get(),
                        belong_property->
                            ContainerPtrToValuePtr<void>(
                                container
                            ),
                        belong_property->GetSize(),
                        STR("GroupId")
                    );

                if (
                    group_id.size_is_16 &&
                    group_id.value == selected_guild
                )
                {
                    ++selected_matches;
                    selected_container = container;
                }
            }

            if (
                selected_matches != 1 ||
                selected_container == nullptr ||
                known_container == nullptr ||
                known_item == nullptr
            )
            {
                return snapshot;
            }

            auto* array_property =
                RC::Unreal::CastField<
                    RC::Unreal::FArrayProperty
                >(
                    selected_container->
                        GetPropertyByNameInChain(
                            STR("ItemSlotArray")
                        )
                );

            auto* object_property =
                array_property != nullptr
                    ? RC::Unreal::CastField<
                        RC::Unreal::FObjectPropertyBase
                    >(array_property->GetInner())
                    : nullptr;

            if (
                array_property == nullptr ||
                object_property == nullptr
            )
            {
                return snapshot;
            }

            RC::Unreal::FScriptArrayHelper_InContainer
                helper(
                    array_property,
                    selected_container
                );

            snapshot.slot_count = helper.Num();

            semantic_mix_bytes(
                snapshot.fingerprint,
                selected_guild.data(),
                selected_guild.size()
            );

            semantic_mix_u64(
                snapshot.fingerprint,
                static_cast<std::uint64_t>(
                    snapshot.slot_count
                )
            );

            for (
                std::int32_t slot_index{};
                slot_index < snapshot.slot_count;
                ++slot_index
            )
            {
                try
                {
                    auto* slot =
                        object_property->
                            GetObjectPropertyValue(
                                helper.GetRawPtr(slot_index)
                            );

                    if (slot == nullptr)
                    {
                        continue;
                    }

                    ++snapshot.nonnull_slots;

                    auto* container_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FStructProperty
                        >(
                            slot->GetPropertyByNameInChain(
                                STR("ContainerId")
                            )
                        );

                    auto* item_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FStructProperty
                        >(
                            slot->GetPropertyByNameInChain(
                                STR("ItemId")
                            )
                        );

                    auto* stack_property =
                        RC::Unreal::CastField<
                            RC::Unreal::FNumericProperty
                        >(
                            slot->GetPropertyByNameInChain(
                                STR("StackCount")
                            )
                        );

                    if (
                        container_property == nullptr ||
                        item_property == nullptr ||
                        stack_property == nullptr ||
                        container_property->GetSize() != 16 ||
                        item_property->GetSize() != 40 ||
                        stack_property->GetSize() != 4 ||
                        container_property->GetStruct().Get() !=
                            known_container ||
                        item_property->GetStruct().Get() !=
                            known_item
                    )
                    {
                        continue;
                    }

                    auto* container_data =
                        container_property->
                            ContainerPtrToValuePtr<void>(
                                slot
                            );

                    auto* item_data =
                        item_property->
                            ContainerPtrToValuePtr<void>(
                                slot
                            );

                    auto* stack_data =
                        stack_property->
                            ContainerPtrToValuePtr<void>(
                                slot
                            );

                    if (
                        container_data == nullptr ||
                        item_data == nullptr ||
                        stack_data == nullptr
                    )
                    {
                        continue;
                    }

                    semantic_mix_u64(
                        snapshot.fingerprint,
                        static_cast<std::uint64_t>(
                            slot_index
                        )
                    );

                    semantic_mix_bytes(
                        snapshot.fingerprint,
                        container_data,
                        16
                    );

                    semantic_mix_bytes(
                        snapshot.fingerprint,
                        item_data,
                        40
                    );

                    semantic_mix_bytes(
                        snapshot.fingerprint,
                        stack_data,
                        4
                    );

                    snapshot.container_bytes += 16;
                    snapshot.item_bytes += 40;
                    snapshot.stack_bytes += 4;
                    ++snapshot.fully_read_slots;
                }
                catch (...)
                {
                    ++snapshot.exceptions;
                }
            }

            snapshot.valid =
                snapshot.slot_count > 0 &&
                snapshot.nonnull_slots ==
                    static_cast<std::uint64_t>(
                        snapshot.slot_count
                    ) &&
                snapshot.fully_read_slots ==
                    snapshot.nonnull_slots &&
                snapshot.container_bytes ==
                    snapshot.fully_read_slots * 16 &&
                snapshot.item_bytes ==
                    snapshot.fully_read_slots * 40 &&
                snapshot.stack_bytes ==
                    snapshot.fully_read_slots * 4 &&
                snapshot.exceptions == 0;

            return snapshot;
        }
        catch (...)
        {
            ++snapshot.exceptions;
            return snapshot;
        }
    }


    auto semantic_snapshots_equal(
        const SemanticSnapshot& left,
        const SemanticSnapshot& right
    ) noexcept -> bool
    {
        return
            left.valid &&
            right.valid &&
            left.fingerprint == right.fingerprint &&
            left.slot_count == right.slot_count &&
            left.nonnull_slots == right.nonnull_slots &&
            left.fully_read_slots == right.fully_read_slots &&
            left.container_bytes == right.container_bytes &&
            left.item_bytes == right.item_bytes &&
            left.stack_bytes == right.stack_bytes &&
            left.exceptions == right.exceptions;
    }

    auto emit_semantic_observation_snapshot(
        const char* phase,
        std::uint64_t sample,
        std::uint64_t delay_seconds,
        const SemanticSnapshot& snapshot
    ) noexcept -> void
    {
        emit_format(
            "[ModIntegratedStorageCpp] "
            "SEMANTIC_OBSERVATION phase=%s sample=%llu "
            "delay_seconds=%llu valid=%d "
            "slot_count=%d nonnull_slots=%llu "
            "fully_read_slots=%llu "
            "container_bytes=%llu item_bytes=%llu "
            "stack_bytes=%llu exceptions=%llu "
            "fingerprint=%016llx "
            "cross_restart_stable=0",
            phase,
            static_cast<unsigned long long>(sample),
            static_cast<unsigned long long>(
                delay_seconds
            ),
            snapshot.valid ? 1 : 0,
            snapshot.slot_count,
            static_cast<unsigned long long>(
                snapshot.nonnull_slots
            ),
            static_cast<unsigned long long>(
                snapshot.fully_read_slots
            ),
            static_cast<unsigned long long>(
                snapshot.container_bytes
            ),
            static_cast<unsigned long long>(
                snapshot.item_bytes
            ),
            static_cast<unsigned long long>(
                snapshot.stack_bytes
            ),
            static_cast<unsigned long long>(
                snapshot.exceptions
            ),
            static_cast<unsigned long long>(
                snapshot.fingerprint
            )
        );
    }

    auto run_controlled_semantic_observation(
        const GuildKey& selected_guild,
        bool plan_complete,
        bool post_registration
    ) noexcept -> void
    {
        if (
            !plan_complete ||
            g_semantic_repeatability_complete.load(
                std::memory_order_acquire
            )
        )
        {
            return;
        }

        using Clock = std::chrono::steady_clock;

        static bool baseline_captured{};
        static bool immediate_captured{};
        static GuildKey baseline_guild{};
        static SemanticSnapshot baseline{};
        static SemanticSnapshot immediate{};
        static SemanticSnapshot first_delayed{};
        static Clock::time_point next_delayed{};
        static std::uint64_t delayed_samples{};
        static std::uint64_t delayed_matches_baseline{};
        static std::uint64_t delayed_matches_immediate{};
        static std::uint64_t delayed_matches_first{};

        try
        {
            const auto now = Clock::now();

            if (!baseline_captured)
            {
                if (post_registration)
                {
                    return;
                }

                baseline_guild = selected_guild;

                baseline =
                    build_semantic_snapshot(selected_guild);

                emit_semantic_observation_snapshot(
                    "baseline",
                    0,
                    0,
                    baseline
                );

                if (!baseline.valid)
                {
                    g_semantic_repeatability_complete.store(
                        true,
                        std::memory_order_release
                    );

                    emit_marker(
                        "[ModIntegratedStorageCpp] "
                        "SEMANTIC_OBSERVATION RESULT=INCOMPLETE"
                    );
                    return;
                }

                baseline_captured = true;
                return;
            }

            if (!(selected_guild == baseline_guild))
            {
                g_semantic_repeatability_complete.store(
                    true,
                    std::memory_order_release
                );

                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "SEMANTIC_OBSERVATION RESULT=INCOMPLETE"
                );
                return;
            }

            if (!post_registration)
            {
                return;
            }

            if (
                !g_full_plan_registration_completed.load(
                    std::memory_order_acquire
                )
            )
            {
                return;
            }

            if (!immediate_captured)
            {
                immediate =
                    build_semantic_snapshot(selected_guild);

                emit_semantic_observation_snapshot(
                    "immediate",
                    1,
                    0,
                    immediate
                );

                if (!immediate.valid)
                {
                    g_semantic_repeatability_complete.store(
                        true,
                        std::memory_order_release
                    );

                    emit_marker(
                        "[ModIntegratedStorageCpp] "
                        "SEMANTIC_OBSERVATION RESULT=INCOMPLETE"
                    );
                    return;
                }

                immediate_captured = true;
                next_delayed =
                    now + std::chrono::seconds(5);
                return;
            }

            if (now < next_delayed)
            {
                return;
            }

            const auto delayed =
                build_semantic_snapshot(selected_guild);

            const auto delayed_index =
                delayed_samples + 1;

            emit_semantic_observation_snapshot(
                "delayed",
                delayed_index + 1,
                delayed_index * 5,
                delayed
            );

            if (!delayed.valid)
            {
                g_semantic_repeatability_complete.store(
                    true,
                    std::memory_order_release
                );

                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "SEMANTIC_OBSERVATION RESULT=INCOMPLETE"
                );
                return;
            }

            if (delayed_samples == 0)
            {
                first_delayed = delayed;
                delayed_matches_first = 1;
            }
            else if (
                semantic_snapshots_equal(
                    delayed,
                    first_delayed
                )
            )
            {
                ++delayed_matches_first;
            }

            if (
                semantic_snapshots_equal(
                    delayed,
                    baseline
                )
            )
            {
                ++delayed_matches_baseline;
            }

            if (
                semantic_snapshots_equal(
                    delayed,
                    immediate
                )
            )
            {
                ++delayed_matches_immediate;
            }

            ++delayed_samples;

            if (delayed_samples < 3)
            {
                next_delayed =
                    now + std::chrono::seconds(5);
                return;
            }

            const bool immediate_changed =
                !semantic_snapshots_equal(
                    immediate,
                    baseline
                );

            const bool delayed_changed =
                !semantic_snapshots_equal(
                    first_delayed,
                    baseline
                );

            const bool delayed_consistent =
                delayed_matches_first ==
                    delayed_samples;

            const bool retained_change =
                delayed_changed &&
                delayed_consistent;

            const bool unchanged =
                !immediate_changed &&
                delayed_matches_baseline ==
                    delayed_samples;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "SEMANTIC_OBSERVATION "
                "samples=5 delayed_samples=%llu "
                "delayed_matches_baseline=%llu "
                "delayed_matches_immediate=%llu "
                "delayed_matches_first=%llu "
                "baseline_fingerprint=%016llx "
                "immediate_fingerprint=%016llx "
                "delayed_fingerprint=%016llx "
                "immediate_changed=%d delayed_changed=%d "
                "delayed_consistent=%d retained_change=%d "
                "cross_restart_stable=0",
                static_cast<unsigned long long>(
                    delayed_samples
                ),
                static_cast<unsigned long long>(
                    delayed_matches_baseline
                ),
                static_cast<unsigned long long>(
                    delayed_matches_immediate
                ),
                static_cast<unsigned long long>(
                    delayed_matches_first
                ),
                static_cast<unsigned long long>(
                    baseline.fingerprint
                ),
                static_cast<unsigned long long>(
                    immediate.fingerprint
                ),
                static_cast<unsigned long long>(
                    first_delayed.fingerprint
                ),
                immediate_changed ? 1 : 0,
                delayed_changed ? 1 : 0,
                delayed_consistent ? 1 : 0,
                retained_change ? 1 : 0
            );

            g_semantic_repeatability_complete.store(
                true,
                std::memory_order_release
            );

            if (retained_change)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "SEMANTIC_OBSERVATION RESULT=CHANGED"
                );
            }
            else if (unchanged)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "SEMANTIC_OBSERVATION RESULT=UNCHANGED"
                );
            }
            else
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "SEMANTIC_OBSERVATION RESULT=INCOMPLETE"
                );
            }
        }
        catch (...)
        {
            g_semantic_repeatability_complete.store(
                true,
                std::memory_order_release
            );

            emit_marker(
                "[ModIntegratedStorageCpp] "
                "SEMANTIC_OBSERVATION RESULT=EXCEPTION"
            );
        }
    }





    auto run_access_owner_class_identity_probe(
        RC::Unreal::UObject* chest,
        bool plan_complete
    ) noexcept -> void
    {
        if (
            g_access_owner_class_identity_reported.load(
                std::memory_order_acquire
            )
        )
        {
            return;
        }

        if (
            !plan_complete ||
            chest == nullptr
        )
        {
            return;
        }

        bool expected{false};

        if (
            !g_access_owner_class_identity_reported.
                compare_exchange_strong(
                    expected,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                )
        )
        {
            return;
        }

        try
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "GUILD_CHEST_MODULE START"
            );

            constexpr std::size_t k_max_guild_chests{64};

            auto* known_container_id =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/Pal.PalContainerId")
                    );

            auto* known_guid =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UScriptStruct*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/CoreUObject.Guid")
                    );

            auto* known_module_class =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UClass*
                    >(
                        nullptr,
                        nullptr,
                        STR(
                            "/Script/Pal."
                            "PalMapObjectItemContainerModule"
                        )
                    );

            auto* known_item_container_class =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UClass*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/Pal.PalItemContainer")
                    );

            auto* known_guild_chest_class =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UClass*
                    >(
                        nullptr,
                        nullptr,
                        STR(
                            "/Script/Pal."
                            "PalMapObjectGuildChestModel"
                        )
                    );

            auto* known_guild_storage_class =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UClass*
                    >(
                        nullptr,
                        nullptr,
                        STR("/Script/Pal.PalGuildItemStorage")
                    );

            auto* known_basecamp_storage_class =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UClass*
                    >(
                        nullptr,
                        nullptr,
                        STR(
                            "/Script/Pal."
                            "PalBaseCampModuleItemStorage"
                        )
                    );

            auto* known_item_storage_model_class =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UClass*
                    >(
                        nullptr,
                        nullptr,
                        STR(
                            "/Script/Pal."
                            "PalMapObjectItemStorageModel"
                        )
                    );

            const bool types_complete =
                known_container_id != nullptr &&
                known_guid != nullptr &&
                known_module_class != nullptr &&
                known_item_container_class != nullptr &&
                known_guild_chest_class != nullptr &&
                known_guild_storage_class != nullptr &&
                known_basecamp_storage_class != nullptr &&
                known_item_storage_model_class != nullptr;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "GUILD_CHEST_MODULE_TYPES "
                "container_id=%d guid=%d module=%d "
                "item_container=%d guild_chest=%d "
                "guild_storage=%d basecamp_storage=%d "
                "item_storage_model=%d",
                known_container_id != nullptr ? 1 : 0,
                known_guid != nullptr ? 1 : 0,
                known_module_class != nullptr ? 1 : 0,
                known_item_container_class != nullptr ? 1 : 0,
                known_guild_chest_class != nullptr ? 1 : 0,
                known_guild_storage_class != nullptr ? 1 : 0,
                known_basecamp_storage_class != nullptr ? 1 : 0,
                known_item_storage_model_class != nullptr ? 1 : 0
            );

            if (!types_complete)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "GUILD_CHEST_MODULE RESULT=INCOMPLETE_TYPES"
                );
                return;
            }

            static auto* managers =
                new std::vector<
                    RC::Unreal::UObject*
                >();

            static auto* guild_chests =
                new std::vector<
                    RC::Unreal::UObject*
                >();

            managers->clear();
            guild_chests->clear();

            RC::Unreal::UObjectGlobals::FindAllOf(
                STR("PalItemContainerManager"),
                *managers
            );

            RC::Unreal::UObjectGlobals::FindAllOf(
                STR("PalMapObjectGuildChestModel"),
                *guild_chests
            );

            RC::Unreal::UObject*
                item_container_manager{};

            std::uint64_t nonnull_managers{};

            for (auto* candidate : *managers)
            {
                if (candidate == nullptr)
                {
                    continue;
                }

                ++nonnull_managers;

                if (item_container_manager == nullptr)
                {
                    item_container_manager =
                        candidate;
                }
            }

            const bool exact_one_manager =
                managers->size() == 1 &&
                nonnull_managers == 1 &&
                item_container_manager != nullptr;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "GUILD_CHEST_MODULE_DISCOVERY "
                "guild_chest_objects=%zu max=%zu "
                "manager_objects=%zu manager_nonnull=%llu "
                "manager_exact_one=%d",
                guild_chests->size(),
                k_max_guild_chests,
                managers->size(),
                static_cast<unsigned long long>(
                    nonnull_managers
                ),
                exact_one_manager ? 1 : 0
            );

            if (!exact_one_manager)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "GUILD_CHEST_MODULE RESULT=INCOMPLETE_MANAGER"
                );
                return;
            }

            if (
                guild_chests->empty() ||
                guild_chests->size() >
                    k_max_guild_chests
            )
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "GUILD_CHEST_MODULE RESULT=INCOMPLETE_DISCOVERY"
                );
                return;
            }

            auto module_getter_layout_ok =
                [
                    known_module_class
                ](
                    RC::Unreal::UFunction* function
                ) -> bool
                {
                    if (function == nullptr)
                    {
                        return false;
                    }

                    std::uint64_t parameters{};
                    std::uint64_t inputs{};
                    std::uint64_t returns{};
                    std::uint64_t exact_returns{};
                    int return_offset{-1};
                    int return_size{-1};

                    for (
                        auto* property :
                        function->ForEachProperty()
                    )
                    {
                        if (
                            property == nullptr ||
                            !property->HasAnyPropertyFlags(
                                RC::Unreal::EPropertyFlags::
                                    CPF_Parm
                            )
                        )
                        {
                            continue;
                        }

                        ++parameters;

                        const bool is_return =
                            property->HasAnyPropertyFlags(
                                RC::Unreal::EPropertyFlags::
                                    CPF_ReturnParm
                            );

                        if (!is_return)
                        {
                            ++inputs;
                            continue;
                        }

                        ++returns;

                        auto* object_property =
                            RC::Unreal::CastField<
                                RC::Unreal::
                                    FObjectPropertyBase
                            >(property);

                        if (
                            object_property != nullptr &&
                            object_property->
                                GetPropertyClass().Get() ==
                                known_module_class
                        )
                        {
                            ++exact_returns;
                            return_offset =
                                property->
                                    GetOffset_Internal();
                            return_size =
                                property->GetSize();
                        }
                    }

                    return
                        function->GetParmsSize() == 8 &&
                        parameters == 1 &&
                        inputs == 0 &&
                        returns == 1 &&
                        exact_returns == 1 &&
                        return_offset == 0 &&
                        return_size == 8;
                };

            auto container_id_layout_ok =
                [
                    known_container_id
                ](
                    RC::Unreal::UFunction* function
                ) -> bool
                {
                    if (function == nullptr)
                    {
                        return false;
                    }

                    std::uint64_t parameters{};
                    std::uint64_t inputs{};
                    std::uint64_t returns{};
                    std::uint64_t exact_returns{};
                    int return_offset{-1};
                    int return_size{-1};

                    for (
                        auto* property :
                        function->ForEachProperty()
                    )
                    {
                        if (
                            property == nullptr ||
                            !property->HasAnyPropertyFlags(
                                RC::Unreal::EPropertyFlags::
                                    CPF_Parm
                            )
                        )
                        {
                            continue;
                        }

                        ++parameters;

                        const bool is_return =
                            property->HasAnyPropertyFlags(
                                RC::Unreal::EPropertyFlags::
                                    CPF_ReturnParm
                            );

                        if (!is_return)
                        {
                            ++inputs;
                            continue;
                        }

                        ++returns;

                        auto* struct_property =
                            RC::Unreal::CastField<
                                RC::Unreal::FStructProperty
                            >(property);

                        if (
                            struct_property != nullptr &&
                            struct_property->
                                GetStruct().Get() ==
                                known_container_id
                        )
                        {
                            ++exact_returns;
                            return_offset =
                                property->
                                    GetOffset_Internal();
                            return_size =
                                property->GetSize();
                        }
                    }

                    return
                        function->GetParmsSize() == 16 &&
                        parameters == 1 &&
                        inputs == 0 &&
                        returns == 1 &&
                        exact_returns == 1 &&
                        return_offset == 0 &&
                        return_size == 16;
                };

            auto* get_container_function =
                item_container_manager->
                    GetFunctionByNameInChain(
                        STR("GetContainer")
                    );

            auto* query_function =
                item_container_manager->
                    GetFunctionByNameInChain(
                        STR(
                            "GetGroupIdByItemContainerId"
                        )
                    );

            auto get_container_layout_ok =
                [
                    known_container_id,
                    known_item_container_class
                ](
                    RC::Unreal::UFunction* function
                ) -> bool
                {
                    if (function == nullptr)
                    {
                        return false;
                    }

                    std::uint64_t parameters{};
                    std::uint64_t inputs{};
                    std::uint64_t returns{};
                    std::uint64_t cid_inputs{};
                    std::uint64_t exact_returns{};
                    int id_offset{-1};
                    int id_size{-1};
                    int return_offset{-1};
                    int return_size{-1};

                    for (
                        auto* property :
                        function->ForEachProperty()
                    )
                    {
                        if (
                            property == nullptr ||
                            !property->HasAnyPropertyFlags(
                                RC::Unreal::EPropertyFlags::
                                    CPF_Parm
                            )
                        )
                        {
                            continue;
                        }

                        ++parameters;

                        const bool is_return =
                            property->HasAnyPropertyFlags(
                                RC::Unreal::EPropertyFlags::
                                    CPF_ReturnParm
                            );

                        if (is_return)
                        {
                            ++returns;

                            auto* object_property =
                                RC::Unreal::CastField<
                                    RC::Unreal::
                                        FObjectPropertyBase
                                >(property);

                            if (
                                object_property != nullptr &&
                                object_property->
                                    GetPropertyClass().Get() ==
                                    known_item_container_class
                            )
                            {
                                ++exact_returns;
                                return_offset =
                                    property->
                                        GetOffset_Internal();
                                return_size =
                                    property->GetSize();
                            }

                            continue;
                        }

                        ++inputs;

                        auto* struct_property =
                            RC::Unreal::CastField<
                                RC::Unreal::FStructProperty
                            >(property);

                        if (
                            struct_property != nullptr &&
                            struct_property->
                                GetStruct().Get() ==
                                known_container_id
                        )
                        {
                            ++cid_inputs;
                            id_offset =
                                property->
                                    GetOffset_Internal();
                            id_size =
                                property->GetSize();
                        }
                    }

                    return
                        function->GetParmsSize() == 24 &&
                        parameters == 2 &&
                        inputs == 1 &&
                        returns == 1 &&
                        cid_inputs == 1 &&
                        exact_returns == 1 &&
                        id_offset == 0 &&
                        id_size == 16 &&
                        return_offset == 16 &&
                        return_size == 8;
                };

            auto query_layout_ok =
                [
                    known_container_id,
                    known_guid
                ](
                    RC::Unreal::UFunction* function
                ) -> bool
                {
                    if (function == nullptr)
                    {
                        return false;
                    }

                    std::uint64_t parameters{};
                    std::uint64_t inputs{};
                    std::uint64_t returns{};
                    std::uint64_t object_inputs{};
                    std::uint64_t cid_inputs{};
                    std::uint64_t guid_returns{};
                    int object_offset{-1};
                    int object_size{-1};
                    int cid_offset{-1};
                    int cid_size{-1};
                    int return_offset{-1};
                    int return_size{-1};

                    for (
                        auto* property :
                        function->ForEachProperty()
                    )
                    {
                        if (
                            property == nullptr ||
                            !property->HasAnyPropertyFlags(
                                RC::Unreal::EPropertyFlags::
                                    CPF_Parm
                            )
                        )
                        {
                            continue;
                        }

                        ++parameters;

                        const bool is_return =
                            property->HasAnyPropertyFlags(
                                RC::Unreal::EPropertyFlags::
                                    CPF_ReturnParm
                            );

                        if (is_return)
                        {
                            ++returns;

                            auto* struct_property =
                                RC::Unreal::CastField<
                                    RC::Unreal::FStructProperty
                                >(property);

                            if (
                                struct_property != nullptr &&
                                struct_property->
                                    GetStruct().Get() ==
                                    known_guid
                            )
                            {
                                ++guid_returns;
                                return_offset =
                                    property->
                                        GetOffset_Internal();
                                return_size =
                                    property->GetSize();
                            }

                            continue;
                        }

                        ++inputs;

                        auto* object_property =
                            RC::Unreal::CastField<
                                RC::Unreal::
                                    FObjectPropertyBase
                            >(property);

                        if (object_property != nullptr)
                        {
                            ++object_inputs;
                            object_offset =
                                property->
                                    GetOffset_Internal();
                            object_size =
                                property->GetSize();
                            continue;
                        }

                        auto* struct_property =
                            RC::Unreal::CastField<
                                RC::Unreal::FStructProperty
                            >(property);

                        if (
                            struct_property != nullptr &&
                            struct_property->
                                GetStruct().Get() ==
                                known_container_id
                        )
                        {
                            ++cid_inputs;
                            cid_offset =
                                property->
                                    GetOffset_Internal();
                            cid_size =
                                property->GetSize();
                        }
                    }

                    return
                        function->GetParmsSize() == 40 &&
                        parameters == 3 &&
                        inputs == 2 &&
                        returns == 1 &&
                        object_inputs == 1 &&
                        cid_inputs == 1 &&
                        guid_returns == 1 &&
                        object_offset == 0 &&
                        object_size == 8 &&
                        cid_offset == 8 &&
                        cid_size == 16 &&
                        return_offset == 24 &&
                        return_size == 16;
                };

            const bool get_layout =
                get_container_layout_ok(
                    get_container_function
                );

            const bool query_layout =
                query_layout_ok(
                    query_function
                );

            emit_format(
                "[ModIntegratedStorageCpp] "
                "GUILD_CHEST_MODULE_MANAGER_META "
                "get_container_exact=%d query_exact=%d",
                get_layout ? 1 : 0,
                query_layout ? 1 : 0
            );

            if (
                !get_layout ||
                !query_layout
            )
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "GUILD_CHEST_MODULE RESULT=INCOMPLETE_MANAGER_META"
                );
                return;
            }

            auto* update_function =
                RC::Unreal::UObjectGlobals::
                    StaticFindObject<
                        RC::Unreal::UFunction*
                    >(
                        nullptr,
                        nullptr,
                        STR(
                            "/Script/Pal."
                            "PalMapObjectGuildChestModel:"
                            "OnUpdateItemContainerInGuildItemStorage"
                        )
                    );

            std::uint64_t update_parameters{};
            std::uint64_t update_inputs{};
            std::uint64_t update_returns{};
            std::uint64_t update_object_inputs{};
            RC::Unreal::UClass*
                update_input_class{};

            if (update_function != nullptr)
            {
                for (
                    auto* property :
                    update_function->ForEachProperty()
                )
                {
                    if (
                        property == nullptr ||
                        !property->HasAnyPropertyFlags(
                            RC::Unreal::EPropertyFlags::
                                CPF_Parm
                        )
                    )
                    {
                        continue;
                    }

                    ++update_parameters;

                    const bool is_return =
                        property->HasAnyPropertyFlags(
                            RC::Unreal::EPropertyFlags::
                                CPF_ReturnParm
                        );

                    if (is_return)
                    {
                        ++update_returns;
                        continue;
                    }

                    ++update_inputs;

                    auto* object_property =
                        RC::Unreal::CastField<
                            RC::Unreal::
                                FObjectPropertyBase
                        >(property);

                    if (object_property != nullptr)
                    {
                        ++update_object_inputs;
                        update_input_class =
                            object_property->
                                GetPropertyClass().Get();
                    }
                }
            }

            const bool update_layout =
                update_function != nullptr &&
                update_function->GetParmsSize() == 8 &&
                update_parameters == 1 &&
                update_inputs == 1 &&
                update_returns == 0 &&
                update_object_inputs == 1 &&
                update_input_class != nullptr;

            const bool update_input_guild_storage =
                update_input_class ==
                    known_guild_storage_class;

            const bool update_input_item_container =
                update_input_class ==
                    known_item_container_class;

            const bool update_input_module =
                update_input_class ==
                    known_module_class;

            const bool update_input_basecamp_storage =
                update_input_class ==
                    known_basecamp_storage_class;

            const bool update_input_guild_chest =
                update_input_class ==
                    known_guild_chest_class;

            const bool update_input_item_storage_model =
                update_input_class ==
                    known_item_storage_model_class;

            const bool update_input_known =
                update_input_guild_storage ||
                update_input_item_container ||
                update_input_module ||
                update_input_basecamp_storage ||
                update_input_guild_chest ||
                update_input_item_storage_model;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "GUILD_CHEST_MODULE_UPDATE_META "
                "resolved=%d exact_layout=%d "
                "input_guild_storage=%d "
                "input_item_container=%d input_module=%d "
                "input_basecamp_storage=%d input_guild_chest=%d "
                "input_item_storage_model=%d input_other=%d "
                "candidate_calls=0",
                update_function != nullptr ? 1 : 0,
                update_layout ? 1 : 0,
                update_input_guild_storage ? 1 : 0,
                update_input_item_container ? 1 : 0,
                update_input_module ? 1 : 0,
                update_input_basecamp_storage ? 1 : 0,
                update_input_guild_chest ? 1 : 0,
                update_input_item_storage_model ? 1 : 0,
                (
                    update_layout &&
                    !update_input_known
                ) ? 1 : 0
            );

            if (!update_layout)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "GUILD_CHEST_MODULE RESULT=INCOMPLETE_UPDATE_META"
                );
                return;
            }

            struct AnchorObservation
            {
                bool module_function{};
                bool module_layout{};
                bool module_exact{};
                bool module_is_a{};
                bool module_direct_subclass{};
                std::int32_t module_super_depth{-1};
                int module_class_fname_index{-1};
                int module_super_fname_index{-1};
                bool guild_chest_module_property{};
                bool guild_chest_module_bool{};
                std::int32_t guild_chest_module_offset{-1};
                std::int32_t guild_chest_module_size{-1};
                bool id_function{};
                bool id_layout{};
                bool id_nonzero{};
                bool container_exact{};
                bool guild_chest_container_property{};
                bool guild_chest_container_bool{};
                std::int32_t guild_chest_container_offset{-1};
                std::int32_t guild_chest_container_size{-1};
                bool complete{};
                GuildKey container_id{};
                GuildKey group{};
                RC::Unreal::UObject* module{};
                RC::Unreal::UObject* container{};
            };

            auto observe_model =
                [
                    &module_getter_layout_ok,
                    &container_id_layout_ok,
                    item_container_manager,
                    get_container_function,
                    query_function,
                    known_module_class,
                    known_item_container_class
                ](
                    RC::Unreal::UObject* model
                ) -> AnchorObservation
                {
                    AnchorObservation observation{};

                    if (model == nullptr)
                    {
                        return observation;
                    }

                    auto* module_function =
                        model->GetFunctionByNameInChain(
                            STR("GetItemContainerModule")
                        );

                    observation.module_function =
                        module_function != nullptr;

                    observation.module_layout =
                        module_getter_layout_ok(
                            module_function
                        );

                    if (!observation.module_layout)
                    {
                        return observation;
                    }

                    std::array<std::byte, 8>
                        module_buffer{};

                    model->ProcessEvent(
                        module_function,
                        module_buffer.data()
                    );

                    std::memcpy(
                        &observation.module,
                        module_buffer.data(),
                        sizeof(observation.module)
                    );

                    auto* module_class =
                        observation.module != nullptr
                            ? observation.module->GetClassPrivate()
                            : nullptr;

                    const RC::Unreal::UStruct* direct_super =
                        module_class != nullptr
                            ? module_class->GetSuperStruct()
                            : nullptr;

                    observation.module_exact =
                        module_class == known_module_class;

                    observation.module_direct_subclass =
                        direct_super == known_module_class;

                    if (module_class != nullptr)
                    {
                        observation.module_class_fname_index =
                            module_class->GetFName().GetComparisonIndex();
                    }

                    if (direct_super != nullptr)
                    {
                        observation.module_super_fname_index =
                            direct_super->GetFName().GetComparisonIndex();
                    }

                    const RC::Unreal::UStruct* super_cursor =
                        module_class;

                    for (std::int32_t depth{}; depth <= 8; ++depth)
                    {
                        if (super_cursor == nullptr)
                        {
                            break;
                        }

                        if (super_cursor == known_module_class)
                        {
                            observation.module_is_a = true;
                            observation.module_super_depth = depth;
                            break;
                        }

                        super_cursor = super_cursor->GetSuperStruct();
                    }

                    if (observation.module != nullptr)
                    {
                        auto* guild_module_property =
                            observation.module->GetPropertyByNameInChain(
                                STR("bIsGuildChestModule")
                            );

                        observation.guild_chest_module_property =
                            guild_module_property != nullptr;

                        observation.guild_chest_module_bool =
                            RC::Unreal::CastField<
                                RC::Unreal::FBoolProperty
                            >(guild_module_property) != nullptr;

                        if (guild_module_property != nullptr)
                        {
                            observation.guild_chest_module_offset =
                                guild_module_property->GetOffset_Internal();
                            observation.guild_chest_module_size =
                                guild_module_property->GetSize();
                        }
                    }

                    if (!observation.module_is_a)
                    {
                        return observation;
                    }

                    auto* id_function =
                        observation.module->
                            GetFunctionByNameInChain(
                                STR("GetContainerId")
                            );

                    observation.id_function =
                        id_function != nullptr;

                    observation.id_layout =
                        container_id_layout_ok(
                            id_function
                        );

                    if (!observation.id_layout)
                    {
                        return observation;
                    }

                    std::array<std::byte, 16>
                        id_buffer{};

                    observation.module->ProcessEvent(
                        id_function,
                        id_buffer.data()
                    );

                    std::memcpy(
                        &observation.container_id,
                        id_buffer.data(),
                        sizeof(observation.container_id)
                    );

                    observation.id_nonzero =
                        !guid_is_zero(
                            observation.container_id
                        );

                    if (!observation.id_nonzero)
                    {
                        return observation;
                    }

                    std::array<std::byte, 24>
                        get_buffer{};

                    std::memcpy(
                        get_buffer.data(),
                        &observation.container_id,
                        sizeof(observation.container_id)
                    );

                    item_container_manager->
                        ProcessEvent(
                            get_container_function,
                            get_buffer.data()
                        );

                    std::memcpy(
                        &observation.container,
                        get_buffer.data() + 16,
                        sizeof(observation.container)
                    );

                    observation.container_exact =
                        observation.container != nullptr &&
                        observation.container->
                            GetClassPrivate() ==
                            known_item_container_class;

                    if (!observation.container_exact)
                    {
                        return observation;
                    }

                    auto* guild_container_property =
                        observation.container->GetPropertyByNameInChain(
                            STR("bIsGuildChestContainer")
                        );

                    observation.guild_chest_container_property =
                        guild_container_property != nullptr;

                    observation.guild_chest_container_bool =
                        RC::Unreal::CastField<
                            RC::Unreal::FBoolProperty
                        >(guild_container_property) != nullptr;

                    if (guild_container_property != nullptr)
                    {
                        observation.guild_chest_container_offset =
                            guild_container_property->GetOffset_Internal();
                        observation.guild_chest_container_size =
                            guild_container_property->GetSize();
                    }

                    std::array<std::byte, 40>
                        query_buffer{};

                    std::memcpy(
                        query_buffer.data(),
                        &model,
                        sizeof(model)
                    );

                    std::memcpy(
                        query_buffer.data() + 8,
                        &observation.container_id,
                        sizeof(observation.container_id)
                    );

                    item_container_manager->
                        ProcessEvent(
                            query_function,
                            query_buffer.data()
                        );

                    std::memcpy(
                        &observation.group,
                        query_buffer.data() + 24,
                        sizeof(observation.group)
                    );

                    observation.complete = true;

                    return observation;
                };

            const auto ordinary =
                observe_model(chest);

            const auto ordinary_id_hex =
                guid_to_hex(
                    ordinary.container_id
                );

            const auto ordinary_group_hex =
                guid_to_hex(
                    ordinary.group
                );

            const bool ordinary_membership_zero =
                ordinary.complete &&
                guid_is_zero(ordinary.group);

            emit_format(
                "[ModIntegratedStorageCpp] "
                "GUILD_CHEST_MODULE_ORDINARY "
                "complete=%d container_id=%s group=%s "
                "membership_zero=%d",
                ordinary.complete ? 1 : 0,
                ordinary_id_hex.data(),
                ordinary_group_hex.data(),
                ordinary_membership_zero ? 1 : 0
            );

            if (!ordinary_membership_zero)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "GUILD_CHEST_MODULE RESULT=INCOMPLETE_ORDINARY_CONTROL"
                );
                return;
            }

            std::uint64_t nonnull_chests{};
            std::uint64_t exact_class_chests{};
            std::uint64_t module_exact_chests{};
            std::uint64_t module_is_a_chests{};
            std::uint64_t module_direct_subclass_chests{};
            std::uint64_t module_class_identity_consistent{};
            std::uint64_t guild_chest_module_properties{};
            std::uint64_t guild_chest_module_bool_properties{};
            std::uint64_t guild_chest_container_properties{};
            std::uint64_t guild_chest_container_bool_properties{};
            RC::Unreal::UClass* first_module_class{};
            std::uint64_t complete_chests{};
            std::uint64_t membership_nonzero{};
            std::uint64_t membership_zero{};
            std::uint64_t guild_storage_properties{};
            std::uint64_t guild_storage_nonnull{};
            std::uint64_t guild_storage_exact{};
            std::uint64_t guild_storage_item_container_nonnull{};
            std::uint64_t guild_storage_item_container_same{};
            std::uint64_t guild_storage_group_available{};
            std::uint64_t guild_storage_group_matches_model{};
            std::uint64_t model_exceptions{};

            std::size_t model_index{};

            for (auto* model : *guild_chests)
            {
                const auto current_index =
                    model_index++;

                if (model == nullptr)
                {
                    continue;
                }

                ++nonnull_chests;

                const bool exact_class =
                    model->GetClassPrivate() ==
                        known_guild_chest_class;

                if (!exact_class)
                {
                    emit_format(
                        "[ModIntegratedStorageCpp] "
                        "GUILD_CHEST_MODULE_MODEL "
                        "index=%zu exact_class=0 skipped=1",
                        current_index
                    );
                    continue;
                }

                ++exact_class_chests;

                try
                {
                    const auto observation =
                        observe_model(model);

                    if (observation.module_exact)
                    {
                        ++module_exact_chests;
                    }

                    if (observation.module_is_a)
                    {
                        ++module_is_a_chests;
                    }

                    if (observation.module_direct_subclass)
                    {
                        ++module_direct_subclass_chests;
                    }

                    if (observation.module != nullptr)
                    {
                        auto* current_module_class =
                            observation.module->GetClassPrivate();

                        if (first_module_class == nullptr)
                        {
                            first_module_class = current_module_class;
                        }

                        if (
                            current_module_class != nullptr &&
                            current_module_class == first_module_class
                        )
                        {
                            ++module_class_identity_consistent;
                        }
                    }

                    if (observation.guild_chest_module_property)
                    {
                        ++guild_chest_module_properties;
                    }

                    if (observation.guild_chest_module_bool)
                    {
                        ++guild_chest_module_bool_properties;
                    }

                    if (observation.guild_chest_container_property)
                    {
                        ++guild_chest_container_properties;
                    }

                    if (observation.guild_chest_container_bool)
                    {
                        ++guild_chest_container_bool_properties;
                    }

                    if (observation.complete)
                    {
                        ++complete_chests;
                    }

                    const bool group_zero =
                        observation.complete &&
                        guid_is_zero(
                            observation.group
                        );

                    if (observation.complete)
                    {
                        if (group_zero)
                        {
                            ++membership_zero;
                        }
                        else
                        {
                            ++membership_nonzero;
                        }
                    }

                    const auto guild_storage =
                        read_object_property_candidate(
                            model,
                            STR("GuildItemStorage")
                        );

                    const bool guild_storage_property =
                        guild_storage.exists &&
                        guild_storage.object_property;

                    if (guild_storage_property)
                    {
                        ++guild_storage_properties;
                    }

                    const bool guild_storage_has_value =
                        guild_storage_property &&
                        guild_storage.value != nullptr;

                    if (guild_storage_has_value)
                    {
                        ++guild_storage_nonnull;
                    }

                    const bool guild_storage_is_exact =
                        guild_storage_has_value &&
                        guild_storage.value->
                            GetClassPrivate() ==
                            known_guild_storage_class;

                    if (guild_storage_is_exact)
                    {
                        ++guild_storage_exact;
                    }

                    RC::Unreal::UObject*
                        guild_item_container{};

                    bool storage_container_nonnull{};
                    bool storage_container_same{};
                    bool storage_group_available{};
                    bool storage_group_matches{};

                    GuildKey storage_group{};

                    if (guild_storage_is_exact)
                    {
                        const auto storage_container =
                            read_object_property_candidate(
                                guild_storage.value,
                                STR("ItemContainer")
                            );

                        if (
                            storage_container.exists &&
                            storage_container.object_property &&
                            storage_container.value != nullptr &&
                            storage_container.value->
                                GetClassPrivate() ==
                                known_item_container_class
                        )
                        {
                            guild_item_container =
                                storage_container.value;

                            storage_container_nonnull = true;

                            ++guild_storage_item_container_nonnull;

                            storage_container_same =
                                observation.complete &&
                                guild_item_container ==
                                    observation.container;

                            if (storage_container_same)
                            {
                                ++guild_storage_item_container_same;
                            }

                            auto* belong_property =
                                guild_item_container->
                                    GetPropertyByNameInChain(
                                        STR("BelongInfo")
                                    );

                            auto* belong_struct_property =
                                RC::Unreal::CastField<
                                    RC::Unreal::FStructProperty
                                >(belong_property);

                            if (
                                belong_property != nullptr &&
                                belong_struct_property != nullptr
                            )
                            {
                                auto* belong_definition =
                                    belong_struct_property->
                                        GetStruct().Get();

                                auto* belong_data =
                                    belong_property->
                                        ContainerPtrToValuePtr<
                                            void
                                        >(
                                            guild_item_container
                                        );

                                const auto nested_group =
                                    read_nested_struct_candidate(
                                        belong_definition,
                                        belong_data,
                                        belong_property->
                                            GetSize(),
                                        STR("GroupId")
                                    );

                                if (nested_group.size_is_16)
                                {
                                    storage_group =
                                        nested_group.value;

                                    storage_group_available =
                                        true;

                                    ++guild_storage_group_available;

                                    storage_group_matches =
                                        observation.complete &&
                                        !guid_is_zero(
                                            observation.group
                                        ) &&
                                        storage_group ==
                                            observation.group;

                                    if (storage_group_matches)
                                    {
                                        ++guild_storage_group_matches_model;
                                    }
                                }
                            }
                        }
                    }

                    const auto id_hex =
                        guid_to_hex(
                            observation.container_id
                        );

                    const auto group_hex =
                        guid_to_hex(
                            observation.group
                        );

                    const auto storage_group_hex =
                        guid_to_hex(
                            storage_group
                        );

                    emit_format(
                        "[ModIntegratedStorageCpp] "
                        "GUILD_CHEST_MODULE_MODEL "
                        "index=%zu exact_class=1 "
                        "module_function=%d module_layout=%d "
                        "module_exact=%d module_is_a=%d "
                        "module_direct_subclass=%d module_super_depth=%d "
                        "module_class_fname=%d module_super_fname=%d "
                        "guild_module_property=%d guild_module_bool=%d "
                        "guild_module_offset=%d guild_module_size=%d "
                        "id_function=%d id_layout=%d id_nonzero=%d "
                        "container_exact=%d "
                        "guild_container_property=%d "
                        "guild_container_bool=%d "
                        "guild_container_offset=%d "
                        "guild_container_size=%d complete=%d "
                        "container_id=%s group=%s membership_zero=%d "
                        "guild_storage_property=%d "
                        "guild_storage_nonnull=%d guild_storage_exact=%d "
                        "storage_container_nonnull=%d "
                        "storage_container_same=%d "
                        "storage_group_available=%d storage_group=%s "
                        "storage_group_matches_model=%d",
                        current_index,
                        observation.module_function ? 1 : 0,
                        observation.module_layout ? 1 : 0,
                        observation.module_exact ? 1 : 0,
                        observation.module_is_a ? 1 : 0,
                        observation.module_direct_subclass ? 1 : 0,
                        observation.module_super_depth,
                        observation.module_class_fname_index,
                        observation.module_super_fname_index,
                        observation.guild_chest_module_property ? 1 : 0,
                        observation.guild_chest_module_bool ? 1 : 0,
                        observation.guild_chest_module_offset,
                        observation.guild_chest_module_size,
                        observation.id_function ? 1 : 0,
                        observation.id_layout ? 1 : 0,
                        observation.id_nonzero ? 1 : 0,
                        observation.container_exact ? 1 : 0,
                        observation.guild_chest_container_property ? 1 : 0,
                        observation.guild_chest_container_bool ? 1 : 0,
                        observation.guild_chest_container_offset,
                        observation.guild_chest_container_size,
                        observation.complete ? 1 : 0,
                        id_hex.data(),
                        group_hex.data(),
                        (
                            observation.complete &&
                            guid_is_zero(
                                observation.group
                            )
                        ) ? 1 : 0,
                        guild_storage_property ? 1 : 0,
                        guild_storage_has_value ? 1 : 0,
                        guild_storage_is_exact ? 1 : 0,
                        storage_container_nonnull ? 1 : 0,
                        storage_container_same ? 1 : 0,
                        storage_group_available ? 1 : 0,
                        storage_group_hex.data(),
                        storage_group_matches ? 1 : 0
                    );
                }
                catch (...)
                {
                    ++model_exceptions;

                    emit_format(
                        "[ModIntegratedStorageCpp] "
                        "GUILD_CHEST_MODULE_MODEL "
                        "index=%zu exact_class=1 exception=1",
                        current_index
                    );
                }
            }

            emit_format(
                "[ModIntegratedStorageCpp] "
                "GUILD_CHEST_MODULE_SUMMARY "
                "objects=%zu nonnull=%llu exact_class=%llu "
                "module_exact=%llu module_is_a=%llu "
                "module_direct_subclass=%llu "
                "module_class_identity_consistent=%llu "
                "guild_module_properties=%llu guild_module_bool=%llu "
                "guild_container_properties=%llu guild_container_bool=%llu "
                "complete=%llu membership_nonzero=%llu "
                "membership_zero=%llu "
                "guild_storage_properties=%llu "
                "guild_storage_nonnull=%llu guild_storage_exact=%llu "
                "storage_container_nonnull=%llu "
                "storage_container_same=%llu "
                "storage_group_available=%llu "
                "storage_group_matches_model=%llu "
                "exceptions=%llu ordinary_control=1 "
                "update_layout=1 candidate_calls=0 "
                "runtime_name_conversion=0",
                guild_chests->size(),
                static_cast<unsigned long long>(
                    nonnull_chests
                ),
                static_cast<unsigned long long>(
                    exact_class_chests
                ),
                static_cast<unsigned long long>(
                    module_exact_chests
                ),
                static_cast<unsigned long long>(
                    module_is_a_chests
                ),
                static_cast<unsigned long long>(
                    module_direct_subclass_chests
                ),
                static_cast<unsigned long long>(
                    module_class_identity_consistent
                ),
                static_cast<unsigned long long>(
                    guild_chest_module_properties
                ),
                static_cast<unsigned long long>(
                    guild_chest_module_bool_properties
                ),
                static_cast<unsigned long long>(
                    guild_chest_container_properties
                ),
                static_cast<unsigned long long>(
                    guild_chest_container_bool_properties
                ),
                static_cast<unsigned long long>(
                    complete_chests
                ),
                static_cast<unsigned long long>(
                    membership_nonzero
                ),
                static_cast<unsigned long long>(
                    membership_zero
                ),
                static_cast<unsigned long long>(
                    guild_storage_properties
                ),
                static_cast<unsigned long long>(
                    guild_storage_nonnull
                ),
                static_cast<unsigned long long>(
                    guild_storage_exact
                ),
                static_cast<unsigned long long>(
                    guild_storage_item_container_nonnull
                ),
                static_cast<unsigned long long>(
                    guild_storage_item_container_same
                ),
                static_cast<unsigned long long>(
                    guild_storage_group_available
                ),
                static_cast<unsigned long long>(
                    guild_storage_group_matches_model
                ),
                static_cast<unsigned long long>(
                    model_exceptions
                )
            );

            if (
                exact_class_chests == 0 ||
                module_is_a_chests == 0 ||
                complete_chests == 0 ||
                model_exceptions != 0
            )
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "GUILD_CHEST_MODULE RESULT=INCOMPLETE"
                );
                return;
            }

            emit_marker(
                "[ModIntegratedStorageCpp] "
                "GUILD_CHEST_MODULE RESULT=PASS"
            );
        }
        catch (...)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "GUILD_CHEST_MODULE RESULT=EXCEPTION"
            );
        }
    }

    auto run_controlled_full_plan_registration(
        const std::vector<RegistrationExecutionPair>& execution_pairs,
        const RegistrationCallMetadata& metadata,
        bool plan_complete,
        std::uint64_t planned_run
    ) -> void
    {
        static const bool armed =
            stage4d7a_arm_file_present();

        if (
            !g_full_plan_registration_gate_reported.exchange(
                true,
                std::memory_order_acq_rel
            )
        )
        {
            if (armed)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "FULL_PLAN_REGISTER GATE=ARMED"
                );
            }
            else
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "FULL_PLAN_REGISTER GATE=DISABLED"
                );
            }
        }

        if (!armed)
        {
            return;
        }

        const bool game_thread =
            RC::Unreal::IsInGameThreadRaw();

        const bool dedicated =
            g_is_dedicated.load(
                std::memory_order_acquire
            ) == 1;

        const bool parameter_layout_valid =
            metadata.passed &&
            metadata.function != nullptr &&
            metadata.parameter_offset >= 0 &&
            metadata.property_size ==
                static_cast<std::int32_t>(
                    sizeof(
                        RC::Unreal::UObject*
                    )
                ) &&
            metadata.parameter_bytes >=
                sizeof(
                    RC::Unreal::UObject*
                ) &&
            static_cast<std::size_t>(
                metadata.parameter_offset
            ) <=
                metadata.parameter_bytes -
                    sizeof(
                        RC::Unreal::UObject*
                    );

        const bool safe_to_begin =
            plan_complete &&
            !execution_pairs.empty() &&
            game_thread &&
            dedicated &&
            parameter_layout_valid;

        if (!safe_to_begin)
        {
            if (
                !g_full_plan_registration_blocked_reported.exchange(
                    true,
                    std::memory_order_acq_rel
                )
            )
            {
                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "FULL_PLAN_REGISTER run=%llu "
                    "plan=%d pairs=%zu game_thread=%d "
                    "dedicated=%d metadata=%d",
                    static_cast<unsigned long long>(
                        planned_run
                    ),
                    plan_complete ? 1 : 0,
                    execution_pairs.size(),
                    game_thread ? 1 : 0,
                    dedicated ? 1 : 0,
                    parameter_layout_valid ? 1 : 0
                );

                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "FULL_PLAN_REGISTER RESULT=BLOCKED"
                );
            }

            return;
        }

        bool expected_attempted{false};

        if (
            !g_full_plan_registration_attempted.
                compare_exchange_strong(
                    expected_attempted,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                )
        )
        {
            return;
        }

        emit_format(
            "[ModIntegratedStorageCpp] "
            "FULL_PLAN_REGISTER START run=%llu "
            "planned=%zu parms=%zu offset=%d size=%d",
            static_cast<unsigned long long>(
                planned_run
            ),
            execution_pairs.size(),
            metadata.parameter_bytes,
            metadata.parameter_offset,
            metadata.property_size
        );

        auto parameters =
            std::make_unique<std::byte[]>(
                metadata.parameter_bytes
            );

        std::size_t attempted{};
        std::size_t completed{};
        std::size_t blocked{};
        std::size_t exceptions{};
        std::size_t function_mismatches{};
        std::size_t guild_mismatches{};
        std::size_t camp_mismatches{};
        std::size_t storage_class_mismatches{};
        bool first_failure_reported{};

        // Prevalidate the entire execution set before the first mutation.
        // A malformed pair therefore cannot leave a partially executed pass.
        for (const auto& pair : execution_pairs)
        {
            GuildKey chest_guild{};
            GuildKey storage_guild{};

            const bool chest_guild_valid =
                copy_guild_key(
                    pair.chest_camp,
                    chest_guild
                ) &&
                !guid_is_zero(chest_guild);

            const bool storage_guild_valid =
                copy_guild_key(
                    pair.storage_camp,
                    storage_guild
                ) &&
                !guid_is_zero(storage_guild);

            const bool same_guild =
                chest_guild_valid &&
                storage_guild_valid &&
                chest_guild == storage_guild &&
                chest_guild == pair.guild;

            const bool different_camps =
                pair.chest_camp != nullptr &&
                pair.storage_camp != nullptr &&
                pair.chest_camp != pair.storage_camp;

            const bool storage_class_valid =
                class_is(
                    pair.storage,
                    get_storage_module_class()
                );

            RC::Unreal::UFunction* pair_function{};

            if (pair.storage != nullptr)
            {
                pair_function =
                    pair.storage->
                        GetFunctionByNameInChain(
                            STR(
                                "OnAvailableConcreteModel_"
                                "ServerInternal"
                            )
                        );
            }

            const bool function_matches =
                pair_function != nullptr &&
                pair_function == metadata.function;

            const bool pair_safe =
                pair.chest != nullptr &&
                pair.storage != nullptr &&
                different_camps &&
                same_guild &&
                storage_class_valid &&
                function_matches;

            if (pair_safe)
            {
                continue;
            }

            ++blocked;

            if (!same_guild)
            {
                ++guild_mismatches;
            }

            if (!different_camps)
            {
                ++camp_mismatches;
            }

            if (!storage_class_valid)
            {
                ++storage_class_mismatches;
            }

            if (!function_matches)
            {
                ++function_mismatches;
            }

            if (!first_failure_reported)
            {
                first_failure_reported = true;

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "FULL_PLAN_REGISTER FIRST_BLOCK "
                    "run=%llu chest=%d chest_camp=%d "
                    "storage=%d storage_camp=%d "
                    "different_camps=%d same_guild=%d "
                    "storage_class=%d function=%d",
                    static_cast<unsigned long long>(
                        planned_run
                    ),
                    pair.chest != nullptr ? 1 : 0,
                    pair.chest_camp != nullptr ? 1 : 0,
                    pair.storage != nullptr ? 1 : 0,
                    pair.storage_camp != nullptr ? 1 : 0,
                    different_camps ? 1 : 0,
                    same_guild ? 1 : 0,
                    storage_class_valid ? 1 : 0,
                    function_matches ? 1 : 0
                );
            }
        }

        if (blocked != 0)
        {
            emit_format(
                "[ModIntegratedStorageCpp] "
                "FULL_PLAN_REGISTER SUMMARY run=%llu "
                "planned=%zu attempted=0 completed=0 "
                "blocked=%zu exceptions=0 "
                "function_mismatches=%zu guild_mismatches=%zu "
                "camp_mismatches=%zu storage_class_mismatches=%zu "
                "game_thread=1 dedicated=1 metadata=1",
                static_cast<unsigned long long>(
                    planned_run
                ),
                execution_pairs.size(),
                blocked,
                function_mismatches,
                guild_mismatches,
                camp_mismatches,
                storage_class_mismatches
            );

            emit_marker(
                "[ModIntegratedStorageCpp] "
                "FULL_PLAN_REGISTER RESULT=INCOMPLETE"
            );

            return;
        }

        for (const auto& pair : execution_pairs)
        {
            ++attempted;

            try
            {
                std::memset(
                    parameters.get(),
                    0,
                    metadata.parameter_bytes
                );

                auto* chest = pair.chest;

                std::memcpy(
                    parameters.get() +
                        static_cast<std::size_t>(
                            metadata.parameter_offset
                        ),
                    &chest,
                    sizeof(chest)
                );

                pair.storage->ProcessEvent(
                    metadata.function,
                    parameters.get()
                );

                ++completed;
            }
            catch (...)
            {
                ++exceptions;

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "FULL_PLAN_REGISTER FIRST_EXCEPTION "
                    "run=%llu index=%zu",
                    static_cast<unsigned long long>(
                        planned_run
                    ),
                    attempted - 1
                );

                break;
            }
        }

        const bool passed =
            exceptions == 0 &&
            attempted == execution_pairs.size() &&
            completed == execution_pairs.size();

        emit_format(
            "[ModIntegratedStorageCpp] "
            "FULL_PLAN_REGISTER SUMMARY run=%llu "
            "planned=%zu attempted=%zu completed=%zu "
            "blocked=%zu exceptions=%zu "
            "function_mismatches=%zu guild_mismatches=%zu "
            "camp_mismatches=%zu storage_class_mismatches=%zu "
            "game_thread=1 dedicated=1 metadata=1",
            static_cast<unsigned long long>(
                planned_run
            ),
            execution_pairs.size(),
            attempted,
            completed,
            blocked,
            exceptions,
            function_mismatches,
            guild_mismatches,
            camp_mismatches,
            storage_class_mismatches
        );

        if (passed)
        {
            g_full_plan_registration_completed.store(
                true,
                std::memory_order_release
            );

            emit_marker(
                "[ModIntegratedStorageCpp] "
                "FULL_PLAN_REGISTER RESULT=PASS"
            );
        }
        else
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "FULL_PLAN_REGISTER RESULT=INCOMPLETE"
            );
        }
    }

    auto run_read_only_chest_association() -> void
    {
        auto& chests = get_chest_discovery_buffer();
        chests.clear();

        RC::Unreal::UObjectGlobals::FindAllOf(
            STR("PalMapObjectItemChestModel"),
            chests
        );

        auto* base_camp_class =
            get_base_camp_class();

        RC::Unreal::UFunction* association_function{};

        std::size_t parameter_size{};
        std::size_t return_offset{};

        std::unique_ptr<std::byte[]> parameters{};

        std::unordered_map<
            GuildKey,
            std::size_t,
            GuildKeyHash
        > guild_chests{};

        std::unordered_set<
            RC::Unreal::UObject*
        > associated_camps{};

        std::size_t valid_chests{};
        std::size_t null_chests{};
        std::size_t associated_chests{};
        std::size_t unassociated_chests{};
        std::size_t missing_functions{};
        std::size_t invalid_parameter_layouts{};
        std::size_t invalid_camps{};
        std::size_t missing_guild_properties{};
        std::size_t zero_guild_keys{};

        auto& registration_probe_camps =
            get_registration_probe_camp_buffer();

        registration_probe_camps.clear();

        RC::Unreal::UObjectGlobals::FindAllOf(
            STR("PalBaseCampModel"),
            registration_probe_camps
        );

        std::unordered_map<
            GuildKey,
            RegistrationPlanGuild,
            GuildKeyHash
        > registration_plan{};

        std::unordered_map<
            RC::Unreal::UObject*,
            GuildKey
        > storage_guilds{};

        std::unordered_map<
            RC::Unreal::UObject*,
            GuildKey
        > chest_guilds{};

        std::size_t plan_null_camps{};
        std::size_t plan_invalid_camps{};
        std::size_t plan_missing_guild{};
        std::size_t plan_zero_guild{};
        std::size_t plan_without_storage{};

        std::size_t duplicate_storage_refs{};
        std::size_t storage_camp_conflicts{};
        std::size_t storage_guild_conflicts{};

        std::size_t duplicate_chest_refs{};
        std::size_t chest_camp_conflicts{};
        std::size_t chest_guild_conflicts{};

        for (
            auto* candidate_camp :
                registration_probe_camps
        )
        {
            if (candidate_camp == nullptr)
            {
                ++plan_null_camps;
                continue;
            }

            if (
                !association_class_is(
                    candidate_camp,
                    base_camp_class
                )
            )
            {
                ++plan_invalid_camps;
                continue;
            }

            GuildKey candidate_guild{};

            if (
                !copy_guild_key(
                    candidate_camp,
                    candidate_guild
                )
            )
            {
                ++plan_missing_guild;
                continue;
            }

            if (guid_is_zero(candidate_guild))
            {
                ++plan_zero_guild;
                continue;
            }

            auto& guild =
                registration_plan[
                    candidate_guild
                ];

            const auto storage_count =
                for_each_storage_module(
                    candidate_camp,
                    [&](RC::Unreal::UObject* storage)
                    {
                        const auto [
                            global_iterator,
                            global_inserted
                        ] =
                            storage_guilds.emplace(
                                storage,
                                candidate_guild
                            );

                        if (
                            !global_inserted &&
                            global_iterator->second !=
                                candidate_guild
                        )
                        {
                            ++storage_guild_conflicts;
                        }

                        const auto [
                            storage_iterator,
                            storage_inserted
                        ] =
                            guild.storage_camps.emplace(
                                storage,
                                candidate_camp
                            );

                        if (!storage_inserted)
                        {
                            ++duplicate_storage_refs;

                            if (
                                storage_iterator->second !=
                                    candidate_camp
                            )
                            {
                                ++storage_camp_conflicts;
                            }
                        }
                    }
                );

            if (storage_count == 0)
            {
                ++plan_without_storage;
            }
        }

        for (auto* chest : chests)
        {
            if (chest == nullptr)
            {
                ++null_chests;
                continue;
            }

            ++valid_chests;

            if (association_function == nullptr)
            {
                association_function =
                    chest->GetFunctionByNameInChain(
                        STR("GetBaseCampModelBelongTo")
                    );

                if (association_function == nullptr)
                {
                    ++missing_functions;
                    continue;
                }

                parameter_size =
                    static_cast<std::size_t>(
                        association_function->
                            GetParmsSize()
                    );

                return_offset =
                    static_cast<std::size_t>(
                        association_function->
                            GetReturnValueOffset()
                    );

                if (
                    parameter_size <
                        sizeof(
                            RC::Unreal::UObject*
                        ) ||
                    return_offset >
                        parameter_size -
                            sizeof(
                                RC::Unreal::UObject*
                            )
                )
                {
                    association_function = nullptr;
                    parameter_size = 0;
                    return_offset = 0;

                    ++invalid_parameter_layouts;
                    continue;
                }

                parameters =
                    std::make_unique<
                        std::byte[]
                    >(parameter_size);
            }

            std::memset(
                parameters.get(),
                0,
                parameter_size
            );

            chest->ProcessEvent(
                association_function,
                parameters.get()
            );

            RC::Unreal::UObject* camp{};

            std::memcpy(
                &camp,
                parameters.get() + return_offset,
                sizeof(camp)
            );

            if (camp == nullptr)
            {
                ++unassociated_chests;
                continue;
            }

            if (
                !association_class_is(
                    camp,
                    base_camp_class
                )
            )
            {
                ++invalid_camps;
                continue;
            }

            GuildKey guild_key{};

            if (!copy_guild_key(camp, guild_key))
            {
                ++missing_guild_properties;
                continue;
            }

            if (guid_is_zero(guild_key))
            {
                ++zero_guild_keys;
                continue;
            }

            ++associated_chests;
            ++guild_chests[guild_key];
            associated_camps.insert(camp);

            auto& plan_guild =
                registration_plan[guild_key];

            const auto [
                global_chest_iterator,
                global_chest_inserted
            ] =
                chest_guilds.emplace(
                    chest,
                    guild_key
                );

            if (
                !global_chest_inserted &&
                global_chest_iterator->second !=
                    guild_key
            )
            {
                ++chest_guild_conflicts;
            }

            const auto [
                chest_iterator,
                chest_inserted
            ] =
                plan_guild.chest_camps.emplace(
                    chest,
                    camp
                );

            if (!chest_inserted)
            {
                ++duplicate_chest_refs;

                if (
                    chest_iterator->second != camp
                )
                {
                    ++chest_camp_conflicts;
                }
            }
        }

        const auto planned_run =
            g_chest_association_runs.load(
                std::memory_order_acquire
            ) + 1;

        std::vector<GuildKey> guild_order{};
        guild_order.reserve(
            registration_plan.size()
        );

        for (
            const auto& [guild_key, ignored_guild] :
                registration_plan
        )
        {
            static_cast<void>(ignored_guild);
            guild_order.push_back(guild_key);
        }

        std::sort(
            guild_order.begin(),
            guild_order.end()
        );

        std::unordered_set<
            RegistrationPlanPair,
            RegistrationPlanPairHash
        > planned_pair_set{};

        std::vector<RegistrationExecutionPair>
            planned_execution_pairs{};

        planned_execution_pairs.reserve(512);

        RC::Unreal::UObject*
            registration_probe_chest{};

        RC::Unreal::UObject*
            registration_probe_chest_camp{};

        RC::Unreal::UObject*
            registration_probe_target_storage{};

        RC::Unreal::UObject*
            registration_probe_target_camp{};

        GuildKey registration_probe_guild{};

        std::size_t guilds_with_pairs{};
        std::size_t planned_chests{};
        std::size_t planned_storages{};
        std::size_t planned_pairs{};
        std::size_t own_camp_pairs{};
        std::size_t duplicate_pairs{};

        std::uint64_t fingerprint_xor{};
        std::uint64_t fingerprint_sum{};

        for (const auto& guild_key : guild_order)
        {
            const auto guild_iterator =
                registration_plan.find(
                    guild_key
                );

            if (
                guild_iterator ==
                    registration_plan.end()
            )
            {
                continue;
            }

            const auto& guild =
                guild_iterator->second;

            planned_chests +=
                guild.chest_camps.size();

            planned_storages +=
                guild.storage_camps.size();

            std::size_t guild_pairs{};
            std::size_t guild_own_camp{};

            for (
                const auto& [
                    chest,
                    chest_camp
                ] : guild.chest_camps
            )
            {
                for (
                    const auto& [
                        storage,
                        storage_camp
                    ] : guild.storage_camps
                )
                {
                    if (chest_camp == storage_camp)
                    {
                        ++guild_own_camp;
                        ++own_camp_pairs;
                        continue;
                    }

                    const RegistrationPlanPair pair{
                        chest,
                        storage
                    };

                    const auto [
                        ignored_pair_iterator,
                        pair_inserted
                    ] =
                        planned_pair_set.insert(
                            pair
                        );

                    static_cast<void>(
                        ignored_pair_iterator
                    );

                    if (!pair_inserted)
                    {
                        ++duplicate_pairs;
                        continue;
                    }

                    ++guild_pairs;
                    ++planned_pairs;

                    planned_execution_pairs.push_back(
                        RegistrationExecutionPair{
                            chest,
                            chest_camp,
                            storage,
                            storage_camp,
                            guild_key
                        }
                    );

                    const auto fingerprint =
                        registration_pair_fingerprint(
                            guild_key,
                            pair
                        );

                    fingerprint_xor ^=
                        fingerprint;

                    fingerprint_sum +=
                        fingerprint;

                    if (
                        registration_probe_chest ==
                            nullptr
                    )
                    {
                        registration_probe_chest =
                            chest;

                        registration_probe_chest_camp =
                            chest_camp;

                        registration_probe_target_storage =
                            storage;

                        registration_probe_target_camp =
                            storage_camp;

                        registration_probe_guild =
                            guild_key;
                    }
                }
            }

            if (guild_pairs > 0)
            {
                ++guilds_with_pairs;
            }

            const auto guild_hex =
                guid_to_hex(guild_key);

            emit_format(
                "[ModIntegratedStorageCpp] "
                "WOULD_REGISTER_GUILD "
                "run=%llu guild=%s "
                "chests=%zu storages=%zu "
                "pairs=%zu own_camp=%zu",
                static_cast<unsigned long long>(
                    planned_run
                ),
                guild_hex.data(),
                guild.chest_camps.size(),
                guild.storage_camps.size(),
                guild_pairs,
                guild_own_camp
            );
        }

        const bool plan_complete =
            !registration_plan.empty() &&
            guilds_with_pairs > 0 &&
            planned_chests > 0 &&
            planned_chests ==
                associated_chests &&
            planned_storages > 0 &&
            planned_pairs > 0 &&
            planned_execution_pairs.size() ==
                planned_pairs &&
            registration_probe_chest != nullptr &&
            registration_probe_target_storage !=
                nullptr &&
            registration_probe_chest_camp !=
                nullptr &&
            registration_probe_target_camp !=
                nullptr &&
            !guid_is_zero(
                registration_probe_guild
            ) &&
            duplicate_chest_refs == 0 &&
            duplicate_storage_refs == 0 &&
            duplicate_pairs == 0 &&
            chest_camp_conflicts == 0 &&
            storage_camp_conflicts == 0 &&
            chest_guild_conflicts == 0 &&
            storage_guild_conflicts == 0 &&
            plan_null_camps == 0 &&
            plan_invalid_camps == 0 &&
            plan_missing_guild == 0 &&
            plan_zero_guild == 0 &&
            plan_without_storage == 0;

        emit_format(
            "[ModIntegratedStorageCpp] "
            "WOULD_REGISTER run=%llu "
            "guilds=%zu active_guilds=%zu "
            "chests=%zu storages=%zu "
            "pairs=%zu own_camp=%zu "
            "duplicate_chests=%zu "
            "duplicate_storages=%zu "
            "duplicate_pairs=%zu "
            "chest_camp_conflicts=%zu "
            "storage_camp_conflicts=%zu "
            "chest_guild_conflicts=%zu "
            "storage_guild_conflicts=%zu "
            "null_camps=%zu invalid_camps=%zu "
            "missing_guild=%zu zero_guild=%zu "
            "without_storage=%zu "
            "fingerprint_xor=%016llx "
            "fingerprint_sum=%016llx",
            static_cast<unsigned long long>(
                planned_run
            ),
            registration_plan.size(),
            guilds_with_pairs,
            planned_chests,
            planned_storages,
            planned_pairs,
            own_camp_pairs,
            duplicate_chest_refs,
            duplicate_storage_refs,
            duplicate_pairs,
            chest_camp_conflicts,
            storage_camp_conflicts,
            chest_guild_conflicts,
            storage_guild_conflicts,
            plan_null_camps,
            plan_invalid_camps,
            plan_missing_guild,
            plan_zero_guild,
            plan_without_storage,
            static_cast<unsigned long long>(
                fingerprint_xor
            ),
            static_cast<unsigned long long>(
                fingerprint_sum
            )
        );

        if (plan_complete)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "WOULD_REGISTER RESULT=PASS"
            );
        }
        else
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "WOULD_REGISTER RESULT=INCOMPLETE"
            );
        }

        const auto registration_metadata =
            run_read_only_registration_metadata_probe(
                registration_probe_chest,
                registration_probe_target_storage
            );

        run_read_only_transport_metadata_probe(
            registration_plan,
            plan_complete,
            planned_run
        );

        run_read_only_observability_metadata_probe(
            registration_probe_chest,
            registration_probe_target_storage,
            plan_complete
        );

        run_read_only_item_storage_linkage_probe(
            registration_probe_chest,
            registration_probe_target_storage,
            plan_complete
        );

        run_read_only_container_query_metadata_probe(
            plan_complete
        );

        run_read_only_container_query_assembly_probe(
            registration_probe_chest,
            registration_probe_guild,
            plan_complete
        );

        run_read_only_deep_layout_metadata_probe(
            registration_probe_guild,
            plan_complete
        );

        run_read_only_slot_fingerprint_probe(
            registration_probe_guild,
            plan_complete
        );

        run_read_only_slot_identity_layout_probe(
            registration_probe_guild,
            plan_complete
        );

        run_read_only_ordinal_identity_probe(
            registration_probe_guild,
            plan_complete
        );

        run_controlled_semantic_observation(
            registration_probe_guild,
            plan_complete,
            false
        );

        run_access_owner_class_identity_probe(
            registration_probe_chest,
            plan_complete
        );

        run_controlled_full_plan_registration(
            planned_execution_pairs,
            registration_metadata,
            plan_complete,
            planned_run
        );

        run_controlled_semantic_observation(
            registration_probe_guild,
            plan_complete,
            true
        );

        if (plan_complete)
        {
            g_cached_registration_plan = registration_plan;

            g_cached_camp_id_to_camp.clear();
            g_cached_camp_to_guild.clear();

            for (
                const auto& [cached_guild_key, guild] :
                    registration_plan
            )
            {
                std::unordered_set<
                    RC::Unreal::UObject*
                > guild_camp_set{};

                for (
                    const auto& [
                        ignored_storage,
                        camp
                    ] : guild.storage_camps
                )
                {
                    static_cast<void>(ignored_storage);

                    if (camp != nullptr)
                    {
                        guild_camp_set.insert(camp);
                    }
                }

                for (
                    const auto& [
                        ignored_chest,
                        camp
                    ] : guild.chest_camps
                )
                {
                    static_cast<void>(ignored_chest);

                    if (camp != nullptr)
                    {
                        guild_camp_set.insert(camp);
                    }
                }

                for (auto* camp : guild_camp_set)
                {
                    GuildKey camp_id{};

                    if (
                        !copy_camp_id(camp, camp_id) ||
                        guid_is_zero(camp_id)
                    )
                    {
                        continue;
                    }

                    g_cached_camp_id_to_camp[camp_id] =
                        camp;

                    g_cached_camp_to_guild[camp] =
                        cached_guild_key;
                }
            }

            g_cached_registration_plan_valid = true;

            emit_format(
                "[ModIntegratedStorageCpp] "
                "TRANSPORT_CACHE camps=%zu guilds=%zu",
                g_cached_camp_id_to_camp.size(),
                g_cached_registration_plan.size()
            );

            emit_format(
                "[ModIntegratedStorageCpp] "
                "BUILD_GATE_ACTIVITY "
                "enter_fires=%llu exit_fires=%llu "
                "check_fires=%llu request_build_fires=%llu "
                "tracked=%zu pending=%zu",
                static_cast<unsigned long long>(
                    g_enter_base_camp_hook_fires.load(
                        std::memory_order_relaxed
                    )
                ),
                static_cast<unsigned long long>(
                    g_exit_base_camp_hook_fires.load(
                        std::memory_order_relaxed
                    )
                ),
                static_cast<unsigned long long>(
                    g_material_check_hook_fires.load(
                        std::memory_order_relaxed
                    )
                ),
                static_cast<unsigned long long>(
                    g_request_build_hook_fires.load(
                        std::memory_order_relaxed
                    )
                ),
                g_player_current_camp.size(),
                g_pending_material_checks.size()
            );
        }

        const auto run =
            g_chest_association_runs.fetch_add(
                1,
                std::memory_order_acq_rel
            ) + 1;

        emit_format(
            "[ModIntegratedStorageCpp] CHEST_ASSOC "
            "run=%llu objects=%zu valid=%zu "
            "associated=%zu unassociated=%zu "
            "guilds=%zu camps=%zu null=%zu "
            "missing_function=%zu "
            "invalid_parameters=%zu invalid_camp=%zu "
            "missing_guild=%zu zero_guild=%zu",
            static_cast<unsigned long long>(run),
            chests.size(),
            valid_chests,
            associated_chests,
            unassociated_chests,
            guild_chests.size(),
            associated_camps.size(),
            null_chests,
            missing_functions,
            invalid_parameter_layouts,
            invalid_camps,
            missing_guild_properties,
            zero_guild_keys
        );

        for (
            const auto& [guild_key, chest_count]
                : guild_chests
        )
        {
            const auto hex =
                guid_to_hex(guild_key);

            emit_format(
                "[ModIntegratedStorageCpp] "
                "CHEST_GUILD id=%s chests=%zu",
                hex.data(),
                chest_count
            );
        }

        const bool complete =
            base_camp_class != nullptr &&
            valid_chests > 0 &&
            valid_chests ==
                associated_chests +
                unassociated_chests &&
            null_chests == 0 &&
            missing_functions == 0 &&
            invalid_parameter_layouts == 0 &&
            invalid_camps == 0 &&
            missing_guild_properties == 0 &&
            zero_guild_keys == 0;

        if (complete)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "CHEST_ASSOC RESULT=PASS"
            );
        }
        else if (chests.empty())
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "CHEST_ASSOC RESULT=EMPTY"
            );
        }
        else
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "CHEST_ASSOC RESULT=INCOMPLETE"
            );
        }
    }

    auto on_engine_tick(
        RC::Unreal::Hook::TCallbackIterationData<void>&,
        RC::Unreal::UEngine*,
        float,
        bool
    ) -> void
    {
        EngineTickEntryGuard entry_guard{};

        if (
            !g_chest_association_enabled.load(
                std::memory_order_acquire
            )
        )
        {
            return;
        }

        if (!RC::Unreal::IsInGameThreadRaw())
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "ENGINE_TICK THREAD=INVALID"
            );

            return;
        }

        if (
            g_role_probe_requested.exchange(
                false,
                std::memory_order_acq_rel
            )
        )
        {
            try
            {
                auto* context =
                    find_game_thread_role_context();

                if (context == nullptr)
                {
                    emit_marker(
                        "[ModIntegratedStorageCpp] "
                        "ROLE RESULT=NO_CONTEXT"
                    );
                }
                else
                {
                    emit_marker(
                        "[ModIntegratedStorageCpp] "
                        "ROLE THREAD=GAME"
                    );

                    static_cast<void>(
                        resolve_dedicated_role(
                            context
                        )
                    );
                }
            }
            catch (...)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "ROLE RESULT=EXCEPTION"
                );
            }
        }

        if (
            g_is_dedicated.load(
                std::memory_order_acquire
            ) == 1
        )
        {
            try
            {
                process_pending_transport_requests();
            }
            catch (...)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "TRANSPORT_REQUEST RESULT=EXCEPTION"
                );
            }

            try
            {
                process_pending_material_reconciliation();
            }
            catch (...)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "MATERIAL_RECONCILE RESULT=EXCEPTION"
                );
            }
        }

        if (
            !g_chest_association_requested.exchange(
                false,
                std::memory_order_acq_rel
            )
        )
        {
            return;
        }

        if (
            g_chest_association_running.exchange(
                true,
                std::memory_order_acq_rel
            )
        )
        {
            g_chest_association_requested.store(
                true,
                std::memory_order_release
            );

            return;
        }

        AssociationRunningGuard running_guard{};

        try
        {
            run_read_only_chest_association();
        }
        catch (...)
        {
            emit_marker(
                "[ModIntegratedStorageCpp] "
                "CHEST_ASSOC RESULT=EXCEPTION"
            );
        }
    }

    class ModIntegratedStorageCpp final
        : public RC::CppUserModBase
    {
      public:
        ModIntegratedStorageCpp()
        {
            ModName =
                STR("IntegratedStorageCpp");

            ModVersion =
                STR("0.1.0-linux-stage4d.9f-leak-growth-probe");

            ModDescription =
                STR(
                    "Linux dedicated-server read-only transport "
                    "metadata and bounded foreign-pool probe, plus "
                    "opt-in isolated FName::ToString() diagnostics: "
                    "GetFName()-obtained, memcpy'd/pool-sourced, "
                    "pool-sourced-without-RC::to_string()-conversion, "
                    "pool-sourced-with-intentionally-leaked-result, "
                    "pool-sourced-leaked-result-with-chardata-read, and "
                    "a repeating leak-and-read growth probe that leaks "
                    "one ToString() result per engine tick and reports "
                    "process VmRSS over time."
                );

            ModAuthors =
                STR("Sarfflow; Linux port by ManaPirate");

            emit_marker(
                "[ModIntegratedStorageCpp] constructor"
            );
        }

        ~ModIntegratedStorageCpp() override
        {
            g_transport_mod_shutting_down.store(
                true,
                std::memory_order_release
            );

            g_chest_association_enabled.store(
                false,
                std::memory_order_release
            );

            g_role_probe_requested.store(
                false,
                std::memory_order_release
            );

            g_chest_association_requested.store(
                false,
                std::memory_order_release
            );

            const auto callback_id =
                g_engine_tick_callback_id;

            g_engine_tick_callback_id =
                RC::Unreal::Hook::ERROR_ID;

            if (
                callback_id !=
                    RC::Unreal::Hook::ERROR_ID
            )
            {
                const bool removed =
                    RC::Unreal::Hook::
                        UnregisterCallback(
                            callback_id
                        );

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "ENGINE_TICK unregister=%d",
                    removed ? 1 : 0
                );
            }

            bool quiescent{};

            for (int attempt = 0; attempt < 500; ++attempt)
            {
                if (
                    g_engine_tick_entries.load(
                        std::memory_order_acquire
                    ) == 0 &&
                    !g_chest_association_running.load(
                        std::memory_order_acquire
                    )
                )
                {
                    quiescent = true;
                    break;
                }

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10)
                );
            }

            emit_format(
                "[ModIntegratedStorageCpp] "
                "ENGINE_TICK quiescent=%d",
                quiescent ? 1 : 0
            );

            emit_marker(
                "[ModIntegratedStorageCpp] destructor"
            );
        }

        auto on_program_start() -> void override
        {
            emit_marker(
                "[ModIntegratedStorageCpp] on_program_start"
            );
        }

        auto on_cpp_mods_loaded() -> void override
        {
            emit_marker(
                "[ModIntegratedStorageCpp] on_cpp_mods_loaded"
            );
        }

        auto on_unreal_init() -> void override
        {
            emit_marker(
                "[ModIntegratedStorageCpp] on_unreal_init"
            );

            auto* object_class =
                RC::Unreal::UObjectGlobals::StaticFindObject<
                    RC::Unreal::UObject*>(
                    nullptr,
                    nullptr,
                    STR("/Script/CoreUObject.Object")
                );

            if (object_class == nullptr)
            {
                emit_marker(
                    "[ModIntegratedStorageCpp] "
                    "StaticFindObject result=null"
                );

                emit_marker(
                    "[ModIntegratedStorageCpp] RESULT=FAIL"
                );

                return;
            }

            emit_marker(
                "[ModIntegratedStorageCpp] "
                "StaticFindObject result=valid"
            );

            emit_marker(
                "[ModIntegratedStorageCpp] RESULT=PASS"
            );

            emit_marker(
                "[ModIntegratedStorageCpp] "
                "STAGE4A read-only discovery enabled"
            );

            if (
                g_engine_tick_callback_id ==
                    RC::Unreal::Hook::ERROR_ID
            )
            {
                const auto callback_id =
                    RC::Unreal::Hook::
                        RegisterEngineTickPreCallback(
                            &on_engine_tick,
                            {}
                        );

                g_engine_tick_callback_id =
                    callback_id;

                const bool registered =
                    callback_id !=
                        RC::Unreal::Hook::ERROR_ID;

                g_chest_association_enabled.store(
                    registered,
                    std::memory_order_release
                );

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "ENGINE_TICK registered=%d",
                    registered ? 1 : 0
                );
            }

            if (
                !g_transport_hook_registered.exchange(
                    true,
                    std::memory_order_acq_rel
                )
            )
            {
                bool hook_registered = false;

                try
                {
                    RC::Unreal::UObjectGlobals::
                        RegisterHook(
                            STR(
                                "/Script/Pal."
                                "PalPlayerController:"
                                "Debug_CheatCommand_"
                                "ToServer"
                            ),
                            &on_transport_request_hook_pre,
                            &on_transport_request_hook_post,
                            nullptr
                        );

                    hook_registered = true;
                }
                catch (...)
                {
                    hook_registered = false;
                }

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "TRANSPORT_HOOK registered=%d",
                    hook_registered ? 1 : 0
                );
            }

            if (
                !g_build_gate_hooks_registered.exchange(
                    true,
                    std::memory_order_acq_rel
                )
            )
            {
                resolve_build_gate_layout();

                bool hooks_registered = false;

                if (g_build_gate_layout.ok)
                {
                    try
                    {
                        RC::Unreal::UObjectGlobals::
                            RegisterHook(
                                STR(
                                    "/Script/Pal."
                                    "PalBuilderComponent:"
                                    "OnEnterBaseCamp"
                                ),
                                &on_enter_base_camp_hook_pre,
                                &on_enter_base_camp_hook_post,
                                nullptr
                            );

                        RC::Unreal::UObjectGlobals::
                            RegisterHook(
                                STR(
                                    "/Script/Pal."
                                    "PalBuilderComponent:"
                                    "OnExitBaseCamp"
                                ),
                                &on_exit_base_camp_hook_pre,
                                &on_exit_base_camp_hook_post,
                                nullptr
                            );

                        RC::Unreal::UObjectGlobals::
                            RegisterHook(
                                STR(
                                    "/Script/Pal."
                                    "PalBuilderComponent:"
                                    "IsExistsMaterialFor"
                                    "BuildObject"
                                ),
                                &on_build_material_check_hook_pre,
                                &on_build_material_check_hook_post,
                                nullptr
                            );

                        hooks_registered = true;
                    }
                    catch (...)
                    {
                        hooks_registered = false;
                    }
                }

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "BUILD_GATE_HOOKS registered=%d",
                    hooks_registered ? 1 : 0
                );
            }

            if (
                !g_request_build_hook_registered.exchange(
                    true,
                    std::memory_order_acq_rel
                )
            )
            {
                bool probe_registered = false;

                if (
                    g_build_gate_layout.
                        request_build_probe_ok
                )
                {
                    try
                    {
                        RC::Unreal::UObjectGlobals::
                            RegisterHook(
                                STR(
                                    "/Script/Pal."
                                    "PalNetworkPlayerComponent:"
                                    "RequestBuild_ToServer"
                                ),
                                &on_request_build_probe_hook_pre,
                                &on_request_build_probe_hook_post,
                                nullptr
                            );

                        probe_registered = true;
                    }
                    catch (...)
                    {
                        probe_registered = false;
                    }
                }

                emit_format(
                    "[ModIntegratedStorageCpp] "
                    "BUILD_REQUEST_PROBE_HOOK registered=%d",
                    probe_registered ? 1 : 0
                );
            }

        }

        auto on_update() -> void override
        {
            const auto now = Clock::now();

            if (
                timepoint_is_empty(g_last_world_probe) ||
                now - g_last_world_probe >= WorldProbeInterval
            )
            {
                g_last_world_probe = now;

                auto* context = find_role_context();

                if (
                    context != nullptr &&
                    observe_world(context) &&
                    g_is_dedicated.load(
                        std::memory_order_acquire
                    ) < 0
                )
                {
                    g_role_probe_requested.store(
                        true,
                        std::memory_order_release
                    );
                }
            }

            if (
                g_is_dedicated.load(
                    std::memory_order_acquire
                ) != 1
            )
            {
                return;
            }

            if (
                timepoint_is_empty(g_last_discovery) ||
                now - g_last_discovery >= DiscoveryInterval
            )
            {
                g_last_discovery = now;
                run_read_only_discovery();
                request_read_only_chest_association();
            }
        }
    };
}

extern "C"
__attribute__((visibility("default")))
auto start_mod() -> RC::CppUserModBase*
{
    const bool module_pinned =
        ModIntegratedStorageModulePin::
            pin_for_process_lifetime();

    std::fprintf(
        stderr,
        "[ModIntegratedStorageCpp] "
        "MODULE_PIN result=%s\n",
        module_pinned ? "PASS" : "FAIL"
    );

    std::fflush(stderr);

    if (!module_pinned)
    {
        return nullptr;
    }


    emit_marker(
        "[ModIntegratedStorageCpp] start_mod export"
    );

    return new ModIntegratedStorageCpp{};
}

extern "C"
__attribute__((visibility("default")))
auto uninstall_mod(RC::CppUserModBase* mod) -> void
{
    emit_marker(
        "[ModIntegratedStorageCpp] uninstall_mod export"
    );

    delete mod;
}
