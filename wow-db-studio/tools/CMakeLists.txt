# ---------------------------------------------------------------------------
# Автономная сборка движка патча БЕЗ Qt.
#
#   cmake -S tools -B build-tools
#   cmake --build build-tools -c Release
#
# Получаются четыре исполняемых файла:
#   wow_patch_cli       — консольный патчер Wow.exe («Firestorm-стиль»)
#   wow_mem_patcher     — патч клиента в ПАМЯТИ (без изменения файла на диске;
#                         для билдов 12.x, где on-disk патч прибивается защитой)
#   wow_pe_selftest     — проверки PE-движка (разбор, секции, checksum, hunks)
#   wow_patch_selftest  — проверки всей логики патча (ключи, профиль, портал,
#                         Config.wtf, PEM, рецепты, верификация)
#
# Qt здесь не нужен и не ищется: ядро (src/wow_pe.*, src/wow_patch_core.*) —
# чистый C++20. Это же удобно в CI и на машине без Qt.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)
project(WowPatchCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Ядро: ни Qt, ни чего-либо ещё.
add_library(wow_patch_core STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/wow_pe.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/wow_pe.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/wow_patch_core.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/wow_patch_core.cpp
)
target_include_directories(wow_patch_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
if(MSVC)
    target_compile_options(wow_patch_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(wow_patch_core PRIVATE -Wall -Wextra)
endif()

add_executable(wow_patch_cli wow_patch_cli.cpp)
target_link_libraries(wow_patch_cli PRIVATE wow_patch_core)
target_include_directories(wow_patch_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
    # GetFileVersionInfoSizeA/VerQueryValueA — версия клиента из ресурса exe.
    target_link_libraries(wow_patch_cli PRIVATE version)
endif()
if(MSVC)
    target_compile_options(wow_patch_cli PRIVATE /utf-8)
endif()

# Патч клиента В ПАМЯТИ (без изменения Wow.exe на диске) — для билдов, где
# on-disk патченый exe прибивается рантайм-защитой при старте.
add_executable(wow_mem_patcher wow_mem_patcher.cpp)
target_link_libraries(wow_mem_patcher PRIVATE wow_patch_core)
target_include_directories(wow_mem_patcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(wow_mem_patcher PRIVATE /utf-8)
endif()

enable_testing()

add_executable(wow_pe_selftest wow_pe_selftest.cpp)
target_link_libraries(wow_pe_selftest PRIVATE wow_patch_core)
target_include_directories(wow_pe_selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME wow_pe_selftest COMMAND wow_pe_selftest)

add_executable(wow_patch_selftest wow_patch_selftest.cpp)
target_link_libraries(wow_patch_selftest PRIVATE wow_patch_core)
target_include_directories(wow_patch_selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME wow_patch_selftest COMMAND wow_patch_selftest)

if(MSVC)
    foreach(t wow_pe_selftest wow_patch_selftest)
        target_compile_options(${t} PRIVATE /utf-8)
    endforeach()
endif()
