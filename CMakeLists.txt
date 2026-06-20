cmake_minimum_required(VERSION 3.25)
project(toe CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG        v7.0.0
)
FetchContent_MakeAvailable(ftxui)

set(TOE_WARNINGS -Wall -Wextra -Werror)

add_library(toe_core STATIC
    src/core/core.cpp
)
target_include_directories(toe_core PUBLIC src)
target_compile_features(toe_core PUBLIC cxx_std_23)
target_compile_options(toe_core PRIVATE ${TOE_WARNINGS})

add_executable(toe
    src/app/main.cpp
)
target_compile_options(toe PRIVATE ${TOE_WARNINGS})
target_link_libraries(toe PRIVATE
    toe_core
    ftxui::screen
    ftxui::dom
    ftxui::component
)

enable_testing()
include(GoogleTest)

add_executable(toe_tests
    tests/core_tests.cpp
)
target_compile_options(toe_tests PRIVATE ${TOE_WARNINGS})
target_link_libraries(toe_tests PRIVATE
    toe_core
    GTest::gtest_main
)
gtest_discover_tests(toe_tests)
