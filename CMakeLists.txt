cmake_minimum_required(VERSION 3.16)

project(aurora_kernel_v01 C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(aurora_core
    src/core/aso.c
    src/core/ledger.c
    src/core/memory.c
    src/core/psl_gate.c
    src/core/scheduler.c
    src/core/src.c
    src/core/telemetry.c
)

target_include_directories(aurora_core PUBLIC include)

if (MSVC)
    target_compile_options(aurora_core PRIVATE /W4)
else()
    target_compile_options(aurora_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(aurora_sim tools/aurora_sim.c)
target_link_libraries(aurora_sim PRIVATE aurora_core)

add_executable(aurora_tests tests/test_core.c)
target_link_libraries(aurora_tests PRIVATE aurora_core)

enable_testing()
add_test(NAME aurora_tests COMMAND aurora_tests)
