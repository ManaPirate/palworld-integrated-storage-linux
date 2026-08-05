// Integrated Storage native Linux dedicated-server scaffold.
//
// Derived from NullPrism RE-UE4SS-Linux's validated native C++
// acceptance fixture.
//
// This milestone validates loading, lifecycle callbacks and read-only
// Unreal access only. It does not yet implement storage pooling,
// container discovery, cross-registration or material consumption.
//
// The upstream Windows implementation remains in ../dllmain.cpp and
// is intentionally unchanged.

#include <cstddef>
#include <unistd.h>

#include <Mod/CppUserModBase.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace
{
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

    class ModIntegratedStorageCpp final
        : public RC::CppUserModBase
    {
      public:
        ModIntegratedStorageCpp()
        {
            emit_marker(
                "[ModIntegratedStorageCpp] constructor"
            );
        }

        ~ModIntegratedStorageCpp() override
        {
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
        }
    };
}

extern "C"
__attribute__((visibility("default")))
auto start_mod() -> RC::CppUserModBase*
{
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
