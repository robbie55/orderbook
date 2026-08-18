add_executable(ob_bench bench.cpp)

# `ob` supplies the include/ and src/ search roots plus warnings-as-errors.
target_link_libraries(ob_bench PRIVATE ob)
