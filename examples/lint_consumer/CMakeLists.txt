# Buildable leaf fixture for SciForge's reusable build-cpp self-test: a header-only
# library with an installed config package (find_package) and a ctest test —
# enough for build-cpp to configure/build/test/install/find, mirroring a real leaf
# consumer (a numeric/utility library with no dependencies) at minimal size.
cmake_minimum_required(VERSION 3.16)
project(lintdemo VERSION 1.0.0 LANGUAGES CXX)

option(LINTDEMO_SANITIZE "Build the test under ASan + UBSan" OFF)

add_library(lintdemo INTERFACE)
add_library(lintdemo::lintdemo ALIAS lintdemo)
target_include_directories(lintdemo INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(lintdemo INTERFACE cxx_std_20)

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS lintdemo EXPORT lintdemoTargets)
install(EXPORT lintdemoTargets
    NAMESPACE lintdemo::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lintdemo)
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/lintdemoConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/lintdemoConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lintdemo)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/lintdemoConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/lintdemoConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/lintdemoConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lintdemo)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_executable(lintdemo_test tests/test_lintdemo.cpp)
    target_link_libraries(lintdemo_test PRIVATE lintdemo::lintdemo)
    set_target_properties(lintdemo_test PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    if(LINTDEMO_SANITIZE)
        target_compile_options(lintdemo_test PRIVATE -fsanitize=address,undefined -g -O1)
        target_link_options(lintdemo_test PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME lintdemo COMMAND lintdemo_test)
endif()
