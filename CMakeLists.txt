cmake_minimum_required(VERSION 3.25)
project(eu4_startup_boost VERSION 0.1.0 LANGUAGES C CXX)

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
    src/config.cpp
    src/modules/exists_index.cpp
    src/modules/metascript_subst.cpp
    src/modules/mimalloc_heap.cpp
    src/modules/trigger_intern.cpp
    src/modules/verifypath_listing.cpp)
target_include_directories(sb_core PUBLIC third_party src)
target_link_libraries(sb_core PUBLIC tomlplusplus::tomlplusplus mimalloc-static)
target_compile_definitions(sb_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(sb_core PUBLIC /W4 /utf-8 /permissive- /EHsc)
endif()

add_library(eu4_startup_boost SHARED src/plugin.cpp)
target_link_libraries(eu4_startup_boost PRIVATE sb_core)
# Keep every module's exported counters even though nothing in the DLL references them.
if(MSVC)
    target_link_options(eu4_startup_boost PRIVATE /OPT:NOREF)
endif()

if(SB_BUILD_TESTS)
    enable_testing()
    foreach(test trigger_intern metascript_subst config)
        add_executable(test_${test} tests/test_${test}.cpp src/common.cpp src/config.cpp)
        target_include_directories(test_${test} PRIVATE third_party src)
        target_link_libraries(test_${test} PRIVATE tomlplusplus::tomlplusplus)
        target_compile_definitions(test_${test} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        if(MSVC)
            # /UNDEBUG: the tests use assert(), which Release would compile out.
            target_compile_options(test_${test} PRIVATE /utf-8 /permissive- /EHsc /UNDEBUG)
        endif()
        add_test(NAME ${test} COMMAND test_${test})
    endforeach()
endif()

# Plugin folder layout: <prefix>/eu4-startup-boost/{plugin.toml, eu4_startup_boost.dll, LICENSE}
install(TARGETS eu4_startup_boost RUNTIME DESTINATION eu4-startup-boost COMPONENT plugin)
install(FILES plugin/plugin.toml LICENSE DESTINATION eu4-startup-boost COMPONENT plugin)
# Third-party code compiled into the DLL (MIT): ship its license texts.
install(FILES ${mimalloc_SOURCE_DIR}/LICENSE DESTINATION eu4-startup-boost/third-party RENAME mimalloc-LICENSE.txt COMPONENT plugin)
install(FILES ${tomlplusplus_SOURCE_DIR}/LICENSE DESTINATION eu4-startup-boost/third-party RENAME tomlplusplus-LICENSE.txt COMPONENT plugin)
