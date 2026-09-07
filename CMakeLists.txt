cmake_minimum_required(VERSION 3.25)
project(Muzakere VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Sql Concurrent)
include(CTest)

add_library(muz_domain src/domain/model.cpp)
target_include_directories(muz_domain PUBLIC include)
add_library(muz_application src/application/import_batch.cpp)
target_link_libraries(muz_application PUBLIC muz_domain)
add_library(muz_infrastructure
    src/infrastructure/local_files.cpp
    src/infrastructure/sqlite_repository.cpp
    src/infrastructure/local_workspace.cpp)
target_link_libraries(muz_infrastructure PUBLIC muz_application PRIVATE Qt6::Core Qt6::Sql)
target_include_directories(muz_infrastructure PRIVATE src/infrastructure)
file(READ migrations/001_initial.sql MUZ_SCHEMA_V1)
configure_file(src/infrastructure/schema.hpp.in generated/schema.hpp @ONLY)
target_include_directories(muz_infrastructure PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_executable(muzakere WIN32 src/main.cpp src/ui/main_window.cpp)
target_include_directories(muzakere PRIVATE src/ui)
target_link_libraries(muzakere PRIVATE muz_infrastructure Qt6::Widgets Qt6::Concurrent)

foreach(target muz_domain muz_application muz_infrastructure muzakere)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    add_executable(muz_tests tests/test_main.cpp tests/domain_tests.cpp tests/import_tests.cpp)
    target_link_libraries(muz_tests PRIVATE muz_infrastructure Qt6::Core Qt6::Sql Catch2::Catch2)
    target_compile_definitions(muz_tests PRIVATE MUZ_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME unit-and-integration COMMAND muz_tests)
    add_test(NAME desktop-smoke COMMAND muzakere --smoke-test)
    set_tests_properties(unit-and-integration desktop-smoke PROPERTIES TIMEOUT 60)
    set_tests_properties(desktop-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
