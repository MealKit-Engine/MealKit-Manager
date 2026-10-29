# MealKit Manager: a hub app that gathers and opens MealKit projects, engines, and tools (SDL3 + Dear ImGui, same framework as the MealKit Audio tool).
# Doesn't link the engine: connects to the engine/editor/tools only through files (mealkit.project, user folder lists, mealkit-tool.txt) and command lines.
#   build.bat                 (Windows: vcvars64 + Ninja, build\bin\MealKitManager.exe, including tests)
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build
#   cmake --install build --prefix "%LOCALAPPDATA%\MealKit\manager"   (the editor launches this when opened without a project)
cmake_minimum_required(VERSION 3.25)
project(MealKitManager VERSION 0.1.0 LANGUAGES C CXX)

# SDL3 (static, without tests/examples)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)

set(imgui_dir ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(mkmanager_imgui STATIC
    ${imgui_dir}/imgui.cpp ${imgui_dir}/imgui_draw.cpp ${imgui_dir}/imgui_tables.cpp ${imgui_dir}/imgui_widgets.cpp
    ${imgui_dir}/backends/imgui_impl_sdl3.cpp ${imgui_dir}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(mkmanager_imgui PUBLIC ${imgui_dir} ${imgui_dir}/backends)
target_link_libraries(mkmanager_imgui PUBLIC SDL3::SDL3-static)

# Data layer (no window, used by tests)
add_library(mkmanager_core STATIC src/Catalog.h src/Catalog.cpp src/Strings.h src/Strings.cpp)
target_include_directories(mkmanager_core PUBLIC src)
target_compile_features(mkmanager_core PUBLIC cxx_std_20)

add_executable(MealKitManager WIN32 src/main.cpp src/App.h src/App.cpp src/Job.h src/Job.cpp)
target_link_libraries(MealKitManager PRIVATE mkmanager_core mkmanager_imgui SDL3::SDL3-static)
target_compile_definitions(MealKitManager PRIVATE MKMANAGER_VERSION="${PROJECT_VERSION}")

foreach(target mkmanager_core MealKitManager)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endforeach()
if(MSVC)
    target_compile_options(mkmanager_imgui PRIVATE /utf-8)
endif()

set_target_properties(MealKitManager PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} upper)
    set_target_properties(MealKitManager PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${upper} ${CMAKE_BINARY_DIR}/bin)
endforeach()

enable_testing()
add_executable(mkmanager_tests tests/CatalogTests.cpp)
target_link_libraries(mkmanager_tests PRIVATE mkmanager_core)
target_compile_definitions(mkmanager_tests PRIVATE MKMANAGER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
    target_compile_options(mkmanager_tests PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(mkmanager_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME mkmanager_tests COMMAND mkmanager_tests)

install(TARGETS MealKitManager RUNTIME DESTINATION .)
