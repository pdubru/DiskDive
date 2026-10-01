cmake_minimum_required(VERSION 3.20)
project(tagger LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# TagLib: vcpkg / TagLib 2.x ship a CMake config; distro 1.x packages usually only ship pkg-config.
find_package(TagLib CONFIG QUIET)
if(TARGET TagLib::tag)
  set(TAGLIB_TARGET TagLib::tag)
elseif(TARGET TagLib::TagLib)
  set(TAGLIB_TARGET TagLib::TagLib)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
  set(TAGLIB_TARGET PkgConfig::TAGLIB)
endif()

# Core library: no UI includes, TagLib is a PRIVATE dependency (never leaks into headers)
add_library(tagger_core
  core/AudioFile.cpp
  core/Mp3File.cpp)
target_include_directories(tagger_core PUBLIC core)
target_link_libraries(tagger_core PRIVATE ${TAGLIB_TARGET})
if(MSVC)
  target_compile_options(tagger_core PRIVATE /W4 /utf-8)
else()
  target_compile_options(tagger_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Step-1/2 console tool
add_executable(tagcli cli/main.cpp)
target_link_libraries(tagcli PRIVATE tagger_core)

# Later: add_subdirectory(ui) for Qt, add_subdirectory(tests) for unit tests
