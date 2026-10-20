cmake_minimum_required(VERSION 3.28)
# version.txt holds the version; release-please bumps it together with plugin/plugin.toml.
file(STRINGS version.txt SB_VERSION_TEXT LIMIT_COUNT 1 REGEX "^[0-9]+\\.[0-9]+\\.[0-9]+$")
if(NOT SB_VERSION_TEXT)
    message(FATAL_ERROR "version.txt must hold a MAJOR.MINOR.PATCH version")
endif()
file(STRINGS plugin/plugin.toml SB_MANIFEST_VERSION LIMIT_COUNT 1 REGEX "^version = \"[^\"]*\"$")
if(NOT SB_MANIFEST_VERSION STREQUAL "version = \"${SB_VERSION_TEXT}\"")
    message(FATAL_ERROR "plugin/plugin.toml (${SB_MANIFEST_VERSION}) does not match version.txt (${SB_VERSION_TEXT})")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS version.txt plugin/plugin.toml)
project(bellows VERSION ${SB_VERSION_TEXT} LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "64-bit Windows only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SB_BUILD_TESTS "Build unit tests" ON)

include(FetchContent)
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_OVERRIDE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG v3.5.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(tomlplusplus mimalloc)

# The Anvil SDK: the AnvilPlugin CMake package (anvil_add_plugin, the headers,
# anvil.exe for `anvil gen` and `anvil check`).
#   - By default the SDK archive of the Anvil release SB_ANVIL_VERSION, checked
#     against SB_ANVIL_SDK_SHA256 (the release's SHA256SUMS.txt). A newer
#     release changes both, on purpose, in its own pull request.
#   - An unpacked SDK archive, or an SDK installed from a local Anvil checkout
#     (`cmake --install <anvil build> --component sdk`): -DAnvilPlugin_DIR=<sdk>/cmake.
set(SB_ANVIL_VERSION 1.0.0)
set(SB_ANVIL_SDK_SHA256 e8ca738da4ead81f17e8e548447057c8f2b1df0529c7b00185a2f531d4ea89fe)
if(AnvilPlugin_DIR)
    find_package(AnvilPlugin REQUIRED)
else()
    FetchContent_Declare(anvil_sdk
        URL https://github.com/happy-dwarf-team/anvil/releases/download/v${SB_ANVIL_VERSION}/anvil-sdk-windows-x64.zip
        URL_HASH SHA256=${SB_ANVIL_SDK_SHA256})
    FetchContent_MakeAvailable(anvil_sdk)
endif()

# bellows.dll and its folder <build>/plugin/bellows/ (plugin.toml, symbols.toml,
# settings.toml, the DLL), laid out by the package; `anvil gen` writes
# symbols.gen.hpp (sym::) and settings.gen.hpp (cfg::) at build time.
set(SB_SOURCES
    src/common.cpp
    src/rename.cpp
    src/settings.cpp
    src/modules/access_profile.cpp
    src/modules/attr_cache.cpp
    src/modules/zip_profile.cpp
    src/modules/zip_reader.cpp
    src/modules/ram_prefetch.cpp
    src/modules/exists_index.cpp
    src/modules/metascript_subst.cpp
    src/modules/mimalloc_heap.cpp
    src/modules/open_warmup.cpp
    src/modules/token_dict_cache.cpp
    src/modules/token_dict_profile.cpp
    src/modules/range_profile.cpp
    src/modules/range_witness.cpp
    src/modules/trigger_intern.cpp
    src/modules/verifypath_listing.cpp)
# File version shown in the DLL's Properties, from the same version.txt.
configure_file(src/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/sb_version.rc @ONLY)
anvil_add_plugin(bellows PLUGIN_DIR plugin
    SOURCES src/plugin.cpp ${SB_SOURCES} ${CMAKE_CURRENT_BINARY_DIR}/sb_version.rc)
target_include_directories(bellows PRIVATE src)
target_link_libraries(bellows PRIVATE tomlplusplus::tomlplusplus mimalloc-static shell32 ole32 uuid)
target_compile_definitions(bellows PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(bellows PRIVATE /EHsc)
    # Keep every module's exported counters even though nothing in the DLL references them.
    target_link_options(bellows PRIVATE /OPT:NOREF)
endif()
# The shipped profiles next to the DLL, as in the release package.
add_custom_command(TARGET bellows POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/plugin/profiles
        ${CMAKE_BINARY_DIR}/plugin/bellows/profiles
    VERBATIM)

# anvil.exe of the SDK, for CI (`anvil check --format github`): its path in
# <build>/anvil-tool-<config>.txt.
get_property(SB_ANVIL_TOOL GLOBAL PROPERTY ANVIL_SDK_TOOL)
if(TARGET ${SB_ANVIL_TOOL})
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/anvil-tool-$<CONFIG>.txt CONTENT "$<TARGET_FILE:${SB_ANVIL_TOOL}>")
else()
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/anvil-tool-$<CONFIG>.txt CONTENT "${SB_ANVIL_TOOL}")
endif()

if(SB_BUILD_TESTS)
    enable_testing()
    foreach(test rename trigger_intern token_dict_cache metascript_subst settings access_profile path_rules zip_reader range_stats range_witness)
        add_executable(test_${test} tests/test_${test}.cpp src/common.cpp src/settings.cpp src/rename.cpp
            src/modules/access_profile.cpp src/modules/zip_reader.cpp)
        # The SDK headers and the generated symbols.gen.hpp / settings.gen.hpp of the plugin.
        target_include_directories(test_${test} PRIVATE src $<TARGET_PROPERTY:bellows,INCLUDE_DIRECTORIES>)
        add_dependencies(test_${test} bellows)
        target_link_libraries(test_${test} PRIVATE tomlplusplus::tomlplusplus shell32 ole32 uuid)
        target_compile_definitions(test_${test} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
            SB_SETTINGS_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/plugin/settings.toml")
        if(MSVC)
            # /UNDEBUG: the tests use assert(), which Release would compile out.
            target_compile_options(test_${test} PRIVATE /utf-8 /permissive- /EHsc /UNDEBUG)
        endif()
        add_test(NAME ${test} COMMAND test_${test})
    endforeach()
    # bellows.dll's anvil_plugin_load against a stand-in host (tests/test_plugin_load.cpp).
    add_executable(test_plugin_load tests/test_plugin_load.cpp)
    target_include_directories(test_plugin_load PRIVATE $<TARGET_PROPERTY:bellows,INCLUDE_DIRECTORIES>)
    add_dependencies(test_plugin_load bellows)
    if(MSVC)
        target_compile_options(test_plugin_load PRIVATE /utf-8 /permissive- /EHsc /UNDEBUG)
    endif()
    foreach(scenario ok refuse)
        add_test(NAME plugin_load_${scenario}
            COMMAND test_plugin_load $<TARGET_FILE:bellows> ${CMAKE_BINARY_DIR}/plugin/bellows ${scenario})
    endforeach()
    # plugin/symbols.lock matches plugin/symbols.toml and the last `anvil check --exe`
    # on the real eu4.exe was clean; also checks plugin.toml and settings.toml.
    add_test(NAME symbols_lock COMMAND ${SB_ANVIL_TOOL} check ${CMAKE_CURRENT_SOURCE_DIR}/plugin)
endif()

# Plugin folder layout: <prefix>/bellows/{plugin.toml, symbols.toml, settings.toml, bellows.dll, profiles/, LICENSE}
install(TARGETS bellows RUNTIME DESTINATION bellows COMPONENT plugin)
install(FILES plugin/plugin.toml plugin/symbols.toml plugin/settings.toml LICENSE DESTINATION bellows COMPONENT plugin)
install(DIRECTORY plugin/profiles DESTINATION bellows COMPONENT plugin)
# Third-party code compiled into the DLL (MIT): ship its license texts.
install(FILES ${mimalloc_SOURCE_DIR}/LICENSE DESTINATION bellows/third-party RENAME mimalloc-LICENSE.txt COMPONENT plugin)
install(FILES ${tomlplusplus_SOURCE_DIR}/LICENSE DESTINATION bellows/third-party RENAME tomlplusplus-LICENSE.txt COMPONENT plugin)
