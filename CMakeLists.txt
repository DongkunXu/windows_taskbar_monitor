cmake_minimum_required(VERSION 3.25)

project(TaskbarMonitor
  VERSION 1.0.0
  DESCRIPTION "Hardware readout on the Windows 11 taskbar"
  LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "TaskbarMonitor targets Windows only.")
endif()

option(TBM_BUILD_TESTS "Build unit tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Settings shared by every target.
add_library(tbm_options INTERFACE)
target_compile_definitions(tbm_options INTERFACE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_compile_options(tbm_options INTERFACE
  -Wall -Wextra -fno-exceptions -fno-rtti)
# Static runtime: the executable depends on system DLLs only.
target_link_options(tbm_options INTERFACE -static $<$<CONFIG:Release>:-s>)

# Pure formatting logic, shared with the unit tests.
add_library(tbm_format STATIC src/format.cpp)
target_include_directories(tbm_format PUBLIC src)
target_link_libraries(tbm_format PUBLIC tbm_options)

add_executable(TaskbarMonitor WIN32
  src/main.cpp
  src/app.cpp
  src/autostart.cpp
  src/log.cpp
  src/overlay.cpp
  src/sampler.cpp
  src/sensors.cpp
  src/taskbar.cpp
  res/app.rc)
configure_file(src/version.h.in generated/version.h @ONLY)
target_include_directories(TaskbarMonitor PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_options(TaskbarMonitor PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-municode>)
target_link_options(TaskbarMonitor PRIVATE -municode)
target_link_libraries(TaskbarMonitor PRIVATE
  tbm_format pdh powrprof wtsapi32 shell32 gdi32 user32 advapi32)

if(TBM_BUILD_TESTS)
  enable_testing()
  add_executable(format_test tests/format_test.cpp)
  target_link_libraries(format_test PRIVATE tbm_format)
  add_test(NAME format COMMAND format_test)
endif()
