# A standalone, EXTERNAL project that consumes SciForge exactly as a real
# ecosystem repository would — but pulled from the local source tree rather than a
# git remote, which breaks the chicken-and-egg during local bring-up (the remote
# does not exist yet). A real consumer / CI swaps SOURCE_DIR for GIT_REPOSITORY +
# GIT_TAG (pinned to a CalVer tag); the rest is identical.
cmake_minimum_required(VERSION 3.16)
project(dummy_consumer LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(sciforge SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
FetchContent_MakeAvailable(sciforge)

add_executable(dummy main.cpp)
target_link_libraries(dummy PRIVATE sciforge::test)
set_target_properties(dummy PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

enable_testing()
add_test(NAME dummy COMMAND $<TARGET_FILE:dummy>)
