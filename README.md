# Disk Dive
Quick application to modify tags in music files, currently only tested and functional in windows. Currently accepted file formats:
* MP3
## Quick Start
### Clone

```bash
git clone --recursive https://github.com/pdubru/DiskDive.git
```
### Install toolchain
#### CMake
Install CMake 3.21 or newer from their official website: https://cmake.org/download/

If you use the installer, make sure to tick "Add CMake to the system PATH"


If you extract the .zip, have it sit in the 'tools' folder of the project!

#### MinGW-w64
Download latest version from their official webpage: https://winlibs.com/

Exctract the downloaded version in the 'tools' directory of the project

### Set up dependencies
Obtain necessary library dependencies using vcpkg:

```bash
DiskDive\tools\vcpkg\bootstrap-vcpkg.bat -disableMetrics
```
This bootstrap should not need to be run again unless the submodule is updated

### Build
```bash
cmake --preset windows-mingw
cmake --build --preset windows-mingw
```

## Commands
Show

Set
