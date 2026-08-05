cmake_minimum_required(VERSION 3.24)
project(orderbook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Warnings-as-errors, applied only to our own targets -------------------
add_library(ob_warnings INTERFACE)
target_compile_options(ob_warnings INTERFACE
    -Wall -Wextra -Werror -Wconversion -Wsign-conversion -pedantic)

# --- Sanitizers (Debug preset turns this on) -------------------------------
option(OB_ASAN "Enable Address + UB sanitizers" OFF)
add_library(ob_asan INTERFACE)
if(OB_ASAN)
    target_compile_options(ob_asan INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(ob_asan INTERFACE -fsanitize=address,undefined)
endif()

# --- Header-only library ----------------------------------------------------
add_library(ob INTERFACE)
target_include_directories(ob INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ob INTERFACE ob_warnings ob_asan)

enable_testing()
add_subdirectory(tests)

# Enabled at the benchmark stage (Week 4):
# add_subdirectory(bench)
