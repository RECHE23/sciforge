# Downstream consumer: locates lintdemo purely through find_package, proving the
# installed config package works.
cmake_minimum_required(VERSION 3.16)
project(lintdemo_smoke LANGUAGES CXX)
find_package(lintdemo CONFIG REQUIRED)
add_executable(smoke smoke.cpp)
target_link_libraries(smoke PRIVATE lintdemo::lintdemo)
set_target_properties(smoke PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
enable_testing()
add_test(NAME smoke COMMAND smoke)
