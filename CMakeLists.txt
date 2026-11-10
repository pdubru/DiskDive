cmake_minimum_required(VERSION 3.20)
project(DiskDive LANGUAGES CXX)

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
add_library(diskdive_core
  core/AudioFile.cpp
  core/Mp3File.cpp
  core/WavFile.cpp
  core/AiffFile.cpp
  core/ChunkScan.cpp
  core/Sha256.cpp
  core/TrackId.cpp)
target_include_directories(diskdive_core PUBLIC core)
target_link_libraries(diskdive_core PRIVATE ${TAGLIB_TARGET})
if(MSVC)
  target_compile_options(diskdive_core PRIVATE /W4 /utf-8)
else()
  target_compile_options(diskdive_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Library layer: SQLite + folder scanner. Depends on core; core never depends on it. SQLite is PRIVATE.
find_package(unofficial-sqlite3 CONFIG QUIET)
if(TARGET unofficial::sqlite3::sqlite3)
  set(SQLITE_TARGET unofficial::sqlite3::sqlite3)
else()
  find_package(SQLite3 REQUIRED)
  set(SQLITE_TARGET SQLite::SQLite3)
endif()

add_library(diskdive_library
  library/Database.cpp
  library/Library.cpp)
target_include_directories(diskdive_library PUBLIC library)
target_link_libraries(diskdive_library PUBLIC diskdive_core PRIVATE ${SQLITE_TARGET})
if(MSVC)
  target_compile_options(diskdive_library PRIVATE /W4 /utf-8)
else()
  target_compile_options(diskdive_library PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Console tool
add_executable(diskdive cli/main.cpp)
target_link_libraries(diskdive PRIVATE diskdive_core diskdive_library)

# Unit tests: `ctest --test-dir build/<preset>` or run build/<preset>/diskdive_tests[.exe] directly
enable_testing()
add_executable(diskdive_tests
  tests/test_main.cpp
  tests/test_sha256.cpp
  tests/test_track_id.cpp
  tests/test_library.cpp)
target_link_libraries(diskdive_tests PRIVATE diskdive_core diskdive_library)
add_test(NAME diskdive_tests COMMAND diskdive_tests)

# Later: add_subdirectory(ui) for Qt
