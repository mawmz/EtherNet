# EtherNet foundation and shared-camera core. Native gameplay adapters are gated.
add_executable(EtherNet
    ethernet/BattlePlayers.cpp
    ethernet/BattleTargets.cpp
    ethernet/BattleEntry.cpp
    ethernet/CombatPanel.cpp
    ethernet/PlayerFieldAudio.cpp
    ethernet/FieldRecovery.cpp
    ethernet/Notifications.cpp
    ethernet/PlayerDroppedItems.cpp
    ethernet/PlayerInteractions.cpp
    ethernet/PlayerMenus.cpp
    ethernet/PartnerMovement.cpp
    ethernet/PlayerWorldDetection.cpp
    ethernet/PlayerProximity.cpp
    ethernet/SaveLoader.cpp
    ethernet/camera/SharedFraming.cpp
    ethernet/camera/SharedFieldCamera.cpp
    ethernet/tether/FieldTether.cpp
    ethernet/tether/NativeFieldTether.cpp
    ethernet/core/main.cpp
    ethernet/core/FunctionalHooks.cpp
    ethernet/core/NnFile.cpp
    ethernet/core/ImGuiExtensions.cpp
    ethernet/core/State.cpp
    ethernet/core/Logger.cpp
    ethernet/core/HidInput.cpp
    ethernet/core/Version.cpp
    ethernet/core/menu/Menu.cpp
    ethernet/core/menu/Section.cpp
    ethernet/core/menu/MenuLog.cpp
    ethernet/core/UpdatableModule.cpp
    ethernet/LocalMultiplayer.cpp
    ethernet/core/util/debug_util.cpp
    ../vendor/skylaunch/src/entry/crt0.s
    ../vendor/skylaunch/src/entry/crti.c
    ../vendor/skylaunch/src/entry/cxa.c
    ../vendor/skylaunch/src/entry/main.cpp
    ../vendor/skylaunch/src/entry/module.cpp
    ../vendor/skylaunch/src/entry/operator.cpp
    ../vendor/skylaunch/src/inlinehook/And64InlineHook.cpp
    ../vendor/skylaunch/src/inlinehook/controlledpages.cpp
    ../vendor/skylaunch/src/hookng/Hooks.cpp
    ../vendor/skylaunch/src/logger/Logger.cpp
    ../vendor/skylaunch/src/logger/TcpLogger.cpp
    ../vendor/skylaunch/src/utils/cpputils.cpp
    ../vendor/skylaunch/src/utils/ipc.cpp
    ../vendor/skylaunch/src/utils/utils.c
    ../vendor/skylaunch/src/nx/kernel/condvar.c
    ../vendor/skylaunch/src/nx/kernel/detect.c
    ../vendor/skylaunch/src/nx/kernel/jit.c
    ../vendor/skylaunch/src/nx/kernel/mutex.c
    ../vendor/skylaunch/src/nx/kernel/shmem.c
    ../vendor/skylaunch/src/nx/kernel/thread.c
    ../vendor/skylaunch/src/nx/kernel/virtmem.c
    ../vendor/skylaunch/src/nx/runtime/env.c
    ../vendor/skylaunch/src/nx/runtime/hosversion.c
    ../vendor/skylaunch/src/nx/runtime/init.c
    ../vendor/skylaunch/src/nx/sf/sessionmgr.c
    ../vendor/skylaunch/src/utils/armutils.s
    ../vendor/skylaunch/src/nx/arm/cache.s
    ../vendor/skylaunch/src/nx/kernel/svc.s
)
set_target_properties(EtherNet PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    COMPILE_FLAGS "-fPIC -enable-libstdcxx-allocator=new")
target_compile_definitions(EtherNet PRIVATE
    __ETHERNET_CODENAME_bf2 _ETHERNET_CODENAME_STR="bf2"
    TOML_FLOAT_CHARCONV=1 TOML_ENABLE_FLOAT16=0)
target_include_directories(EtherNet PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/platform/switch/include"
    "${PROJECT_SOURCE_DIR}/vendor/skylaunch/include"
    "${PROJECT_SOURCE_DIR}/vendor/imgui-xeno/include"
    "${PROJECT_SOURCE_DIR}/vendor/imgui-xeno/extern/imgui")
target_link_options(EtherNet PRIVATE
    -fPIC -specs=switch.specs -Wl,-Map,EtherNet.map
    "-Wl,--version-script=${PROJECT_SOURCE_DIR}/linkerscripts/exported.txt"
    -nodefaultlibs -Wl,-init=__custom_init -Wl,-fini=__custom_fini
    -Wl,--export-dynamic
    "-L${DEVKITPRO}/portlibs/switch/lib" "-L${DEVKITPRO}/libnx/lib")
target_link_libraries(EtherNet PRIVATE
    tomlplusplus::tomlplusplus fmt::fmt glm::glm imgui_xeno magic_enum)
add_nso_target(EtherNet)
add_custom_command(TARGET EtherNet POST_BUILD
    COMMAND "${NPDMTOOL}" "${PROJECT_SOURCE_DIR}/npdm/bf2.json" bf2-unpatched.npdm
    COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/scripts/patchNpdm.py" bf2-unpatched.npdm bf2.npdm
    COMMAND "${CMAKE_COMMAND}" -E make_directory exefs
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different EtherNet.nso exefs/subsdk9
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different bf2.npdm exefs/main.npdm
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}" VERBATIM)
