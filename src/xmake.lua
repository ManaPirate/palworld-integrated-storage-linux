local projectName = "ModIntegratedStorageCpp"

target(projectName)
    add_rules("ue4ss.mod")
    add_files("src/dllmain.cpp")
    -- Needed for the trampoline detour on the native material collector. polyhook_2 is already
    -- required by UE4SS (deps/xmake.lua); the mod links it directly so PLH::x64Detour resolves.
    add_packages("polyhook_2")
