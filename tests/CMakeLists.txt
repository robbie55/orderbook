include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
# Do not let GoogleTest override our compiler/linker settings.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(ob_tests
    test_types.cpp
    test_order_pool.cpp
    test_array_book_core.cpp
    test_reference_book.cpp
    test_bench_support.cpp
)
# `ob` carries our warnings-as-errors; GoogleTest's own headers are pulled in
# as SYSTEM includes by the imported targets, so -Werror does not fire on them.
target_link_libraries(ob_tests PRIVATE ob GTest::gtest_main)
target_include_directories(ob_tests PRIVATE ${PROJECT_SOURCE_DIR}/bench)

include(GoogleTest)
gtest_discover_tests(ob_tests)
