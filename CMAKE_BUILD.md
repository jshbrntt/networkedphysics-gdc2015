# Building with CMake

This project has been migrated to use CMake as its build system. The original Premake5 build system is still available in [premake5.lua](premake5.lua) for reference.

## Prerequisites

### System Requirements
- CMake 3.10 or higher
- C++11 compatible compiler (GCC, Clang, or MSVC)
- Make or Ninja build system

### Dependencies

#### On macOS (using Homebrew):
```bash
brew install cmake glfw glew jansson glm
```

#### On Ubuntu/Debian:
```bash
sudo apt-get install cmake libglfw3-dev libglew-dev libjansson-dev libglm-dev
```

#### ODE Physics Engine
The ODE (Open Dynamics Engine) library is included in the `external/ode` directory. It will be automatically built when needed, but you can also build it manually:

```bash
cd external/ode
./configure
make all
```

## Quick Start

### Using the build script (Linux/macOS):
```bash
# Release build
./build.sh

# Debug build
./build.sh --debug

# Clean build
./build.sh --clean

# Build with 8 parallel jobs
./build.sh -j 8

# Verbose output
./build.sh --verbose
```

### Manual CMake build:
```bash
# Create build directory
mkdir -p build
cd build

# Configure (Release)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Or configure for Debug
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build . -j $(nproc)

# Run tests
ctest --output-on-failure
```

## Build Targets

### All Targets
Build everything:
```bash
cmake --build build
```

### Libraries
The following static libraries will be built:
- **Core** - Core utility library
- **Network** - Network communication library
- **Protocol** - Protocol implementation
- **ClientServer** - Client/Server architecture
- **VirtualGo** - Virtual Go game implementation
- **Cubes** - Cubes demo library
- **nvImage** - Image loading library
- **tinycthread** - Threading library

### Test Executables
```bash
# Build all tests
cmake --build build --target TestCore
cmake --build build --target TestNetwork
cmake --build build --target TestProtocol
cmake --build build --target TestClientServer
cmake --build build --target TestVirtualGo

# Run all tests (two methods):

# Method 1: Using CTest (clean summary output)
cd build && ctest --output-on-failure

# Method 2: Using test_all target (verbose output, like premake5 test)
cmake --build build --target test_all
```

Both methods run the same tests. CTest provides a summary report, while `test_all` shows detailed test output similar to the Premake5 `premake5 test` command.

### Main Applications
```bash
# Build and run the client
cmake --build build --target Client
cmake --build build --target run_client

# Build and run the server
cmake --build build --target Server
cmake --build build --target run_server
```

### Tools
```bash
# Build font tool
cmake --build build --target FontTool
cmake --build build --target fonts

# Build stone tool
cmake --build build --target StoneTool
cmake --build build --target stones
```

### Demos
Run various networking demos:

```bash
# Cubes demo
cmake --build build --target run_cubes

# Lockstep demo
cmake --build build --target run_lockstep

# Snapshot interpolation demo
cmake --build build --target run_snapshot

# Compression demo
cmake --build build --target run_compression

# Delta compression demo
cmake --build build --target run_delta

# State synchronization demo
cmake --build build --target run_sync

# Stone demo
cmake --build build --target run_stone

# Playback replay
cmake --build build --target run_playback
```

### Profiling and Soak Tests
```bash
# Soak tests
cmake --build build --target SoakProtocol
cmake --build build --target SoakClientServer

# Profile tests
cmake --build build --target ProfileProtocol
cmake --build build --target ProfileClientServer
```

### Utility Targets
```bash
# Count lines of code
cmake --build build --target loc
```

## Output Directories

After building, you'll find:
- **Executables**: `./bin/`
- **Libraries**: `./lib/`
- **Build files**: `./build/`

## Configuration Options

### Build Types
CMake supports the following build types:
- **Release** (default): Optimized build with -O3
- **Debug**: Debug build with symbols

### Compiler Flags
The following flags are applied globally:
- C++11 standard
- Warning flags: `-Wall -Wextra`
- Disable AVX for WSL2 compatibility: `-mno-avx -mno-avx2 -mno-avx512f`
- Fast math: `-ffast-math`
- Static C++ runtime on Linux: `-static-libgcc -static-libstdc++`

## Platform-Specific Notes

### Linux
The Client executable uses comprehensive static linking for X11 and graphics libraries where possible. The following libraries are linked:
- **Static**: GLEW, GLU, X11, Xrandr, Xi, Xxf86vm, Xext, Xrender, xcb, Xau, Xdmcp, pthread
- **Dynamic**: glfw, GL, dl, m

**Technical Note**: The CMake configuration uses `-Wl,-Bstatic` and `-Wl,-Bdynamic` linker flags passed through `target_link_libraries()` to control which libraries are statically vs dynamically linked. This matches the Premake5 `linkoptions` configuration exactly.

### macOS
The Client links against system frameworks:
- GLUT.framework
- OpenGL.framework
- Cocoa.framework
- CoreVideo.framework
- IOKit.framework

## Troubleshooting

### ODE Build Issues
If the ODE library fails to build automatically, build it manually:
```bash
cd external/ode
./configure
make clean
make all
```

### Missing Dependencies
If you get linking errors, ensure all dependencies are installed:
```bash
# macOS
brew install glfw glew jansson glm

# Ubuntu/Debian
sudo apt-get install libglfw3-dev libglew-dev libjansson-dev libglm-dev
```

### CMake Version
Ensure you have CMake 3.10 or higher:
```bash
cmake --version
```

## Comparison with Premake5

The CMake build system provides equivalent functionality to the original Premake5 setup:

| Premake5 Command | CMake Equivalent |
|------------------|------------------|
| `premake5 gmake` | `cmake ..` |
| `make all` | `cmake --build .` |
| `premake5 test` | `ctest` or `cmake --build . --target test_all` |
| `premake5 client` | `cmake --build . --target run_client` |
| `premake5 server` | `cmake --build . --target run_server` |
| `premake5 cubes` | `cmake --build . --target run_cubes` |
| `premake5 lockstep` | `cmake --build . --target run_lockstep` |
| `premake5 snapshot` | `cmake --build . --target run_snapshot` |
| `premake5 compression` | `cmake --build . --target run_compression` |
| `premake5 delta` | `cmake --build . --target run_delta` |
| `premake5 sync` | `cmake --build . --target run_sync` |

## IDE Integration

### Visual Studio Code
CMake Tools extension will automatically detect the CMakeLists.txt file. You can:
- Configure: `Ctrl+Shift+P` → "CMake: Configure"
- Build: `Ctrl+Shift+P` → "CMake: Build"
- Select target: `Ctrl+Shift+P` → "CMake: Select Launch Target"

### CLion
CLion will automatically detect and load the CMake project.

### Visual Studio
Use "Open Folder" and Visual Studio will detect the CMake configuration.

## Additional Resources

- [CMake Documentation](https://cmake.org/documentation/)
- [Original GDC 2015 Presentation](https://gdcvault.com/play/1022195/Physics-for-Game-Programmers-Networking)
- [Original Project Repository](https://github.com/gafferongames/networkedphysics-gdc2015)
