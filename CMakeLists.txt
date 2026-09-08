cmake_minimum_required(VERSION 3.25)
project(Muzakere VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Sql Concurrent)
include(CTest)
include(cmake/Dependencies.cmake)

add_library(muz_domain src/domain/model.cpp src/domain/accounting.cpp)
target_include_directories(muz_domain PUBLIC include)
add_library(muz_application src/application/import_batch.cpp)
target_link_libraries(muz_application PUBLIC muz_domain)
add_library(muz_infrastructure
    src/infrastructure/local_files.cpp
    src/infrastructure/sqlite_repository.cpp
    src/infrastructure/local_workspace.cpp)
target_sources(muz_infrastructure PRIVATE src/infrastructure/zip_import.cpp
    src/infrastructure/pdf_text.cpp src/infrastructure/document_parser.cpp
    src/infrastructure/accounting_codec.cpp src/infrastructure/accounting_export.cpp)
target_sources(muz_infrastructure PRIVATE src/infrastructure/accounting_return.cpp
    src/infrastructure/hamdata_workbook.cpp
    src/infrastructure/response_pdf.cpp resources/response_templates.qrc)
target_link_libraries(muz_infrastructure PUBLIC muz_application PRIVATE Qt6::Core Qt6::Sql QXlsx::QXlsx miniz)
target_include_directories(muz_infrastructure PRIVATE src/infrastructure)
file(READ migrations/001_initial.sql MUZ_SCHEMA_V1)
file(READ migrations/002_accounting.sql MUZ_SCHEMA_V2)
file(READ migrations/003_debtor_rows.sql MUZ_SCHEMA_V3)
file(READ migrations/004_accounting_returns.sql MUZ_SCHEMA_V4)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS migrations/001_initial.sql migrations/002_accounting.sql migrations/003_debtor_rows.sql migrations/004_accounting_returns.sql)
configure_file(src/infrastructure/schema.hpp.in generated/schema.hpp @ONLY)
target_include_directories(muz_infrastructure PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_executable(muzakere WIN32 src/main.cpp src/ui/main_window.cpp src/ui/response_panel.cpp)
add_executable(muz_pdf_worker src/pdf_worker.cpp)
target_link_libraries(muz_pdf_worker PRIVATE Qt6::Core)
add_dependencies(muzakere muz_pdf_worker)
target_include_directories(muzakere PRIVATE src/ui)
target_link_libraries(muzakere PRIVATE muz_infrastructure Qt6::Widgets Qt6::Concurrent)

foreach(target muz_domain muz_application muz_infrastructure muzakere muz_pdf_worker)
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
    add_executable(muz_tests tests/test_main.cpp tests/domain_tests.cpp tests/import_tests.cpp tests/accounting_tests.cpp)
    target_sources(muz_tests PRIVATE src/ui/main_window.cpp src/ui/response_panel.cpp tests/response_tests.cpp)
    add_dependencies(muz_tests muz_pdf_worker)
    target_link_libraries(muz_tests PRIVATE muz_infrastructure Qt6::Widgets Qt6::Concurrent Qt6::Sql Catch2::Catch2 QXlsx::QXlsx miniz)
    target_include_directories(muz_tests PRIVATE src/infrastructure src/ui "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(muz_tests PRIVATE MUZ_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME unit-and-integration COMMAND muz_tests)
    add_test(NAME desktop-smoke COMMAND muzakere --smoke-test)
    set_tests_properties(unit-and-integration desktop-smoke PROPERTIES TIMEOUT 60)
    set_tests_properties(unit-and-integration desktop-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
