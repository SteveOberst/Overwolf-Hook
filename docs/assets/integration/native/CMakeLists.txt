cmake_minimum_required(VERSION 3.25)
project(my_app LANGUAGES C)

# --8<-- [start:dependency]
set(OVERWOLF_HOOK_VERSION "@OWH_SDK_VERSION@")
include(FetchContent)
FetchContent_Declare(overwolf_hook_sdk
    URL https://github.com/SteveOberst/Overwolf-Hook/releases/download/v${OVERWOLF_HOOK_VERSION}/overwolf-hook-${OVERWOLF_HOOK_VERSION}-windows-x64.zip)
FetchContent_MakeAvailable(overwolf_hook_sdk)
find_package(OverwolfHook ${OVERWOLF_HOOK_VERSION} EXACT CONFIG REQUIRED
    PATHS "${overwolf_hook_sdk_SOURCE_DIR}" NO_DEFAULT_PATH)
# --8<-- [end:dependency]

add_executable(my_app main.c)

# --8<-- [start:link]
target_compile_features(my_app PRIVATE c_std_17)
target_link_libraries(my_app PRIVATE OverwolfHook::Client)
overwolf_hook_deploy_runtime(my_app)
# --8<-- [end:link]
