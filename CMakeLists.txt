cmake_minimum_required(VERSION 3.25)
# version.txt holds the version; release-please bumps it together with plugin/plugin.toml.
file(STRINGS version.txt SB_VERSION_TEXT LIMIT_COUNT 1 REGEX "^[0-9]+\\.[0-9]+\\.[0-9]+$")
if(NOT SB_VERSION_TEXT)
    message(FATAL_ERROR "version.txt must hold a MAJOR.MINOR.PATCH version")
endif()
file(STRINGS plugin/plugin.toml SB_MANIFEST_VERSION REGEX "^version = \"[^\"]*\"$")
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

add_library(sb_core STATIC
    src/common.cpp
    src/rename.cpp
    src/config.cpp
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
    src/modules/trigger_intern.cpp
    src/modules/verifypath_listing.cpp)
target_include_directories(sb_core PUBLIC third_party src)
target_link_libraries(sb_core PUBLIC tomlplusplus::tomlplusplus mimalloc-static shell32 ole32 uuid)
target_compile_definitions(sb_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(sb_core PUBLIC /W4 /utf-8 /permissive- /EHsc)
endif()

# File version shown in the DLL's Properties, from the same version.txt.
configure_file(src/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/sb_version.rc @ONLY)
add_library(bellows SHARED src/plugin.cpp ${CMAKE_CURRENT_BINARY_DIR}/sb_version.rc)
target_link_libraries(bellows PRIVATE sb_core)
# Keep every module's exported counters even though nothing in the DLL references them.
if(MSVC)
    target_link_options(bellows PRIVATE /OPT:NOREF)
endif()

if(SB_BUILD_TESTS)
    enable_testing()
    foreach(test rename trigger_intern token_dict_cache metascript_subst config access_profile path_rules zip_reader range_stats)
        add_executable(test_${test} tests/test_${test}.cpp src/common.cpp src/config.cpp src/rename.cpp src/modules/access_profile.cpp src/modules/zip_reader.cpp)
        target_include_directories(test_${test} PRIVATE third_party src)
        target_link_libraries(test_${test} PRIVATE tomlplusplus::tomlplusplus shell32 ole32 uuid)
        target_compile_definitions(test_${test} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        if(MSVC)
            # /UNDEBUG: the tests use assert(), which Release would compile out.
            target_compile_options(test_${test} PRIVATE /utf-8 /permissive- /EHsc /UNDEBUG)
        endif()
        add_test(NAME ${test} COMMAND test_${test})
    endforeach()
endif()

# Plugin folder layout: <prefix>/bellows/{plugin.toml, bellows.dll, LICENSE}
install(TARGETS bellows RUNTIME DESTINATION bellows COMPONENT plugin)
install(FILES plugin/plugin.toml LICENSE DESTINATION bellows COMPONENT plugin)
install(DIRECTORY plugin/profiles DESTINATION bellows COMPONENT plugin)
# Third-party code compiled into the DLL (MIT): ship its license texts.
install(FILES ${mimalloc_SOURCE_DIR}/LICENSE DESTINATION bellows/third-party RENAME mimalloc-LICENSE.txt COMPONENT plugin)
install(FILES ${tomlplusplus_SOURCE_DIR}/LICENSE DESTINATION bellows/third-party RENAME tomlplusplus-LICENSE.txt COMPONENT plugin)
