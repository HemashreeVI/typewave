cmake_minimum_required(VERSION 3.16)
project(TypeWave VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static MSVC runtime: the .exe runs without installing the VC++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Platform-independent core: synth, mixer, settings, custom packs
add_library(typewave_core STATIC
  src/keys.cpp
  src/synth.cpp
  src/engine.cpp
  src/settings.cpp
  src/audio.cpp)
target_include_directories(typewave_core PUBLIC src)
if(UNIX AND NOT APPLE)
  find_package(Threads REQUIRED)
  target_link_libraries(typewave_core PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

if(MSVC)
  target_compile_options(typewave_core PRIVATE /W4 /utf-8)
else()
  target_compile_options(typewave_core PRIVATE -Wall -Wextra)
endif()

# The Windows tray app
if(WIN32)
  add_executable(typewave WIN32 src/main_win.cpp res/typewave.rc)
  target_link_libraries(typewave PRIVATE typewave_core user32 shell32 ole32 advapi32 uuid)
  target_compile_definitions(typewave PRIVATE UNICODE _UNICODE)
  if(MINGW)
    target_link_options(typewave PRIVATE -municode -static -s)
  endif()
endif()

# Tests and the preview tool build everywhere
enable_testing()
add_executable(typewave_tests tests/test_core.cpp)
target_link_libraries(typewave_tests PRIVATE typewave_core)
add_test(NAME core COMMAND typewave_tests)

add_executable(typewave_preview tools/preview.cpp)
target_link_libraries(typewave_preview PRIVATE typewave_core)
