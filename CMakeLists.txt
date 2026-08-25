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

# --- Core library -----------------------------------------------------------
# include/ is header-only (value types, Level, OrderPool, the BookLike concept).
# src/ holds the book implementations, also header-only (class templates defined
# in their .hpp), so `ob` is an INTERFACE library: it carries include roots and
# flags, and has nothing to compile.
#
# Both roots stay on the search path, so headers refer to each other by bare
# name: `#include <level.hpp>` from src/, `#include <array_book.hpp>` from bench/.
add_library(ob INTERFACE)
target_include_directories(ob INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
# INTERFACE so the warning set and the sanitizer flags reach tests/ and bench/;
# ASan in particular must be enabled consistently across every executable that
# links it.
target_link_libraries(ob INTERFACE ob_warnings ob_asan)

enable_testing()
add_subdirectory(tests)
add_subdirectory(bench)
