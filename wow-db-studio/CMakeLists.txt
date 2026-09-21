cmake_minimum_required(VERSION 3.21)
# Visual Studio 2022 (не Ninja):
#   cmake -S . -B out/build/vs -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64
#   затем открыть out/build/vs/WowDbStudio.sln, конфигурация Release | x64.
project(WowDbStudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Sql Network)

add_executable(WowDbStudio
    src/main.cpp
    src/main_window.h src/main_window.cpp
    src/database_service.h src/database_service.cpp
    src/network_guard.h src/network_guard.cpp
    src/db2_importer.h src/db2_importer.cpp
    src/client_patch_service.h src/client_patch_service.cpp
    # Движок патча клиента: чистый C++20, без Qt (см. tools/CMakeLists.txt —
    # там то же самое собирается в консольный патчер и в тесты без Qt вообще).
    src/wow_pe.h src/wow_pe.cpp
    src/wow_patch_core.h src/wow_patch_core.cpp
    src/ai_service.h src/ai_service.cpp
    src/knowledge_base.h src/knowledge_base.cpp
    src/project_context.h src/project_context.cpp
    src/schema_context.h src/schema_context.cpp
    src/core_repository_service.h src/core_repository_service.cpp
    src/jarvis_engine.h src/jarvis_engine.cpp
    src/wago_service.h src/wago_service.cpp
    src/core_detect_service.h src/core_detect_service.cpp
    src/server_launcher.h src/server_launcher.cpp
    src/studio_file_log.h src/studio_file_log.cpp
    src/sql_wizard_service.h src/sql_wizard_service.cpp
    src/content_editor_service.h src/content_editor_service.cpp
    src/qa_bot_service.h src/qa_bot_service.cpp
    src/qa_retail_source.h src/qa_retail_source.cpp
    src/qa_sniff_source.h src/qa_sniff_source.cpp
    src/retail_reference_service.h src/retail_reference_service.cpp
    src/retail_community_reference_service.h src/retail_community_reference_service.cpp
)
target_link_libraries(WowDbStudio PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Network)
if(WIN32)
    target_link_libraries(WowDbStudio PRIVATE version)
endif()

# ---------------------------------------------------------------------------
# Консольный патчер и автономные тесты движка (без Qt внутри, но собираются
# внутри этого проекта для удобства). Включено по умолчанию; отключается:
#   cmake -DWOWDBSTUDIO_BUILD_TOOLS=OFF ...
# Тесты можно прогнать так:
#   ctest --test-dir out/build/vs -C Release --output-on-failure
# ---------------------------------------------------------------------------
option(WOWDBSTUDIO_BUILD_TOOLS "Build wow_patch_cli and the engine selftests" ON)
if(WOWDBSTUDIO_BUILD_TOOLS)
    add_library(wow_patch_core STATIC
        src/wow_pe.h src/wow_pe.cpp
        src/wow_patch_core.h src/wow_patch_core.cpp
    )
    target_include_directories(wow_patch_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_executable(wow_patch_cli tools/wow_patch_cli.cpp)
    target_link_libraries(wow_patch_cli PRIVATE wow_patch_core)
    target_include_directories(wow_patch_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
    if(WIN32)
        target_link_libraries(wow_patch_cli PRIVATE version)
    endif()

    enable_testing()
    add_executable(wow_pe_selftest tools/wow_pe_selftest.cpp)
    target_link_libraries(wow_pe_selftest PRIVATE wow_patch_core)
    target_include_directories(wow_pe_selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
    add_test(NAME wow_pe_selftest COMMAND wow_pe_selftest)

    add_executable(wow_patch_selftest tools/wow_patch_selftest.cpp)
    target_link_libraries(wow_patch_selftest PRIVATE wow_patch_core)
    target_include_directories(wow_patch_selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
    add_test(NAME wow_patch_selftest COMMAND wow_patch_selftest)

    if(MSVC)
        foreach(t wow_patch_core wow_patch_cli wow_pe_selftest wow_patch_selftest)
            target_compile_options(${t} PRIVATE /utf-8)
        endforeach()
    endif()
endif()

# QMYSQL is a Qt runtime plugin. Install the MySQL/MariaDB client and Qt's SQL driver.
