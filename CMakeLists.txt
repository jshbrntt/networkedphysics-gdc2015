cmake_minimum_required(VERSION 3.10)
project(Protocol)

# Option to choose physics engine
option(USE_JOLT_PHYSICS "Use Jolt Physics instead of ODE" OFF)

# Set C++ standard
find_package(Jolt REQUIRED)

# Add compile definition if using Jolt Physics
if(USE_JOLT_PHYSICS)
    add_compile_definitions(USE_JOLT_PHYSICS)
    message(STATUS "Using Jolt Physics engine")
else()
    message(STATUS "Using ODE Physics engine (default)")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set C standard for C files
set(CMAKE_C_STANDARD 99)

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Global include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}/external/ode/include
    ${CMAKE_SOURCE_DIR}/tools
    ${CMAKE_SOURCE_DIR}
)

# Global library directories
link_directories(
    ${CMAKE_SOURCE_DIR}/external/ode/ode/src/.libs
)

# Platform detection
if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

# Global compiler flags
add_compile_options(-Wall -Wextra)
add_compile_options(-Wno-deprecated-declarations)

# Disable AVX for WSL2 compatibility, but enable SSE4.1 for Jolt Physics
add_compile_options(-mno-avx -mno-avx2 -mno-avx512f -msse4.1)

# Fast floating point
add_compile_options(-ffast-math)

# Static runtime configuration
if(LINUX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
endif()

# Configuration-specific settings
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions(-DNDEBUG)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
endif()

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Helper function to build ODE if needed
function(build_ode_if_needed)
    if(UNIX AND NOT APPLE)
        add_custom_command(
            OUTPUT ${CMAKE_SOURCE_DIR}/external/ode/ode/src/.libs/libode.a
            COMMAND ${CMAKE_SOURCE_DIR}/build_ode.sh
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Building ODE library if needed"
        )
        add_custom_target(ode_build DEPENDS ${CMAKE_SOURCE_DIR}/external/ode/ode/src/.libs/libode.a)
    endif()
endfunction()

# Build ODE
build_ode_if_needed()

# ============================================================================
# Static Libraries
# ============================================================================

# Core Library
file(GLOB CORE_SOURCES src/core/*.cpp)
file(GLOB CORE_HEADERS src/core/*.h)
add_library(Core STATIC ${CORE_SOURCES} ${CORE_HEADERS})

# Network Library
file(GLOB NETWORK_SOURCES src/network/*.cpp)
file(GLOB NETWORK_HEADERS src/network/*.h)
add_library(Network STATIC ${NETWORK_SOURCES} ${NETWORK_HEADERS})
target_link_libraries(Network Core)

# Protocol Library
file(GLOB PROTOCOL_SOURCES src/protocol/*.cpp)
file(GLOB PROTOCOL_HEADERS src/protocol/*.h)
add_library(Protocol STATIC ${PROTOCOL_SOURCES} ${PROTOCOL_HEADERS})
target_link_libraries(Protocol Network Core)

# ClientServer Library
file(GLOB CLIENTSERVER_SOURCES src/ClientServer/*.cpp)
file(GLOB CLIENTSERVER_HEADERS src/ClientServer/*.h)
add_library(ClientServer STATIC ${CLIENTSERVER_SOURCES} ${CLIENTSERVER_HEADERS})
target_link_libraries(ClientServer Protocol Network Core)

# VirtualGo Library
file(GLOB VIRTUALGO_SOURCES src/virtualgo/*.cpp)
file(GLOB VIRTUALGO_HEADERS src/virtualgo/*.h)
add_library(VirtualGo STATIC ${VIRTUALGO_SOURCES} ${VIRTUALGO_HEADERS})
target_link_libraries(VirtualGo Core)
target_link_libraries(VirtualGo Jolt::Jolt)

# Cubes Library
file(GLOB CUBES_SOURCES src/cubes/*.cpp)
file(GLOB CUBES_HEADERS src/cubes/*.h)
add_library(Cubes STATIC ${CUBES_SOURCES} ${CUBES_HEADERS})
target_link_libraries(Cubes Core)
target_link_libraries(Cubes Jolt::Jolt)

# nvImage Library
file(GLOB NVIMAGE_SOURCES external/nvImage/*.cpp)
file(GLOB NVIMAGE_HEADERS external/nvImage/*.h)
add_library(nvImage STATIC ${NVIMAGE_SOURCES} ${NVIMAGE_HEADERS})

# tinycthread Library (C library)
file(GLOB TINYCTHREAD_SOURCES external/tinycthread/*.c)
file(GLOB TINYCTHREAD_HEADERS external/tinycthread/*.h)
add_library(tinycthread STATIC ${TINYCTHREAD_SOURCES} ${TINYCTHREAD_HEADERS})

# ============================================================================
# Test Executables
# ============================================================================

# TestCore
file(GLOB TESTCORE_SOURCES tests/core/*.cpp)
add_executable(TestCore ${TESTCORE_SOURCES})
target_link_libraries(TestCore Core)

# TestNetwork
file(GLOB TESTNETWORK_SOURCES tests/network/Test*.cpp)
add_executable(TestNetwork ${TESTNETWORK_SOURCES})
target_link_libraries(TestNetwork ClientServer Protocol Network Core)

# TestProtocol
file(GLOB TESTPROTOCOL_SOURCES tests/protocol/Test*.cpp)
add_executable(TestProtocol ${TESTPROTOCOL_SOURCES})
target_link_libraries(TestProtocol ClientServer Protocol Network Core)

# TestClientServer
file(GLOB TESTCLIENTSERVER_SOURCES tests/ClientServer/Test*.cpp)
add_executable(TestClientServer ${TESTCLIENTSERVER_SOURCES})
target_link_libraries(TestClientServer ClientServer Protocol Network Core)

# TestVirtualGo
file(GLOB TESTVIRTUALGO_SOURCES tests/virtualgo/*.cpp)
add_executable(TestVirtualGo ${TESTVIRTUALGO_SOURCES})
target_link_libraries(TestVirtualGo VirtualGo Core ode Jolt::Jolt)
if(TARGET ode_build)
    add_dependencies(TestVirtualGo ode_build)
endif()

# ============================================================================
# Soak Test Executables
# ============================================================================

# SoakProtocol
add_executable(SoakProtocol tests/protocol/SoakProtocol.cpp)
target_link_libraries(SoakProtocol ClientServer Protocol Network Core)

# SoakClientServer
add_executable(SoakClientServer tests/ClientServer/SoakClientServer.cpp)
target_link_libraries(SoakClientServer ClientServer Protocol Network Core)

# ============================================================================
# Profile Executables
# ============================================================================

# ProfileProtocol
add_executable(ProfileProtocol tests/protocol/ProfileProtocol.cpp)
target_link_libraries(ProfileProtocol ClientServer Protocol Network Core)

# ProfileClientServer
add_executable(ProfileClientServer tests/ClientServer/ProfileClientServer.cpp)
target_link_libraries(ProfileClientServer ClientServer Protocol Network Core)

# ============================================================================
# Tool Executables
# ============================================================================

# FontTool
file(GLOB FONTTOOL_SOURCES tools/Font/*.cpp)
add_executable(FontTool ${FONTTOOL_SOURCES})
target_link_libraries(FontTool Core freetype jansson)

# StoneTool
file(GLOB STONETOOL_SOURCES tools/Stone/*.cpp)
add_executable(StoneTool ${STONETOOL_SOURCES})
target_link_libraries(StoneTool VirtualGo Core jansson)

# ============================================================================
# Main Game Executables
# ============================================================================

# Client
file(GLOB GAME_SOURCES src/game/*.cpp)
add_executable(Client ${GAME_SOURCES})
target_compile_definitions(Client PRIVATE CLIENT)
target_link_libraries(Client
    VirtualGo Cubes ClientServer Protocol Network Core
    nvImage tinycthread ode Jolt::Jolt
)

if(TARGET ode_build)
    add_dependencies(Client ode_build)
endif()

# Platform-specific linking for Client
if(APPLE)
    target_link_libraries(Client
        GLEW glfw
        "-framework GLUT"
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework CoreVideo"
        "-framework IOKit"
    )
elseif(LINUX)
    # Comprehensive static linking strategy with explicit library control
    # The key insight: -l flags must be passed as link libraries, not as linker options
    # We use -Wl,-Bstatic and -Wl,-Bdynamic to control static vs dynamic linking
    # and pass the actual libraries through target_link_libraries

    target_link_libraries(Client
        # Start static linking group
        "-Wl,-Bstatic"
        # Static libraries
        GLEW GLU
        X11 Xrandr Xi Xxf86vm
        Xext Xrender xcb
        Xau Xdmcp
        pthread
        # Switch to dynamic linking
        "-Wl,-Bdynamic"
        # Dynamic libraries
        glfw GL
        dl m
    )
endif()

# Server
add_executable(Server ${GAME_SOURCES})
target_link_libraries(Server
    Cubes ClientServer Protocol Network Core ode Jolt::Jolt
)

if(TARGET ode_build)
    add_dependencies(Server ode_build)
endif()

# ============================================================================
# Custom Targets (equivalent to Premake custom actions)
# ============================================================================

# Enable testing
enable_testing()

# Add tests - CTest needs full paths to executables
add_test(NAME TestCore
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/TestCore
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME TestNetwork
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/TestNetwork
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME TestProtocol
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/TestProtocol
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME TestClientServer
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/TestClientServer
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME TestVirtualGo
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/TestVirtualGo
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Custom target: run all tests (mimics premake5 test action)
# This runs the tests directly without CTest for better output formatting
# Uses "|| true" to continue even if a test segfaults (matching premake5 behavior)
add_custom_target(test_all
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/TestCore || true
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/TestNetwork || true
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/TestProtocol || true
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/TestClientServer || true
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/TestVirtualGo || true
    DEPENDS TestCore TestNetwork TestProtocol TestClientServer TestVirtualGo
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running all unit tests"
    VERBATIM
)

# Custom target: build fonts
add_custom_target(fonts
    COMMAND ${CMAKE_SOURCE_DIR}/bin/FontTool ${CMAKE_SOURCE_DIR}/assets/fonts/Fonts.json
    DEPENDS FontTool
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building fonts"
)

# Custom target: build stones
add_custom_target(stones
    COMMAND rm -rf ${CMAKE_SOURCE_DIR}/data/stones
    COMMAND mkdir -p ${CMAKE_SOURCE_DIR}/data/stones
    COMMAND ${CMAKE_SOURCE_DIR}/bin/StoneTool
    DEPENDS StoneTool
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building stones"
)

# Custom target: run client
add_custom_target(run_client
    COMMAND ${CMAKE_SOURCE_DIR}/bin/Client
    DEPENDS Client
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running game client"
)

# Custom target: run server
add_custom_target(run_server
    COMMAND ${CMAKE_SOURCE_DIR}/bin/Server
    DEPENDS Server
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running game server"
)

# Custom target: run various demos
add_custom_target(run_stone
    COMMAND ${CMAKE_SOURCE_DIR}/bin/Client +load stone
    DEPENDS Client
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running stone demo"
)

add_custom_target(run_cubes
    COMMAND rm -rf ${CMAKE_SOURCE_DIR}/output && mkdir -p ${CMAKE_SOURCE_DIR}/output
    COMMAND ${CMAKE_SOURCE_DIR}/bin/Client +load cubes
    DEPENDS Client
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running cubes demo"
)

add_custom_target(run_lockstep
    COMMAND rm -rf ${CMAKE_SOURCE_DIR}/output && mkdir -p ${CMAKE_SOURCE_DIR}/output
    COMMAND ${CMAKE_SOURCE_DIR}/bin/Client +load lockstep
    DEPENDS Client
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running lockstep demo"
)

add_custom_target(run_snapshot
    COMMAND rm -rf ${CMAKE_SOURCE_DIR}/output && mkdir -p ${CMAKE_SOURCE_DIR}/output
    COMMAND ${CMAKE_SOURCE_DIR}/bin/Client +load snapshot
    DEPENDS Client
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running snapshot demo"
)

add_custom_target(run_compression
    COMMAND rm -rf ${CMAKE_SOURCE_DIR}/output && mkdir -p ${CMAKE_SOURCE_DIR}/output
    COMMAND ${CMAKE_SOURCE_DIR}/bin/Client +load compression
    DEPENDS Client
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running compression demo"
)

add_custom_target(run_delta
    COMMAND rm -rf ${CMAKE_SOURCE_DIR}/output && mkdir -p ${CMAKE_SOURCE_DIR}/output
    COMMAND ${CMAKE_SOURCE_DIR}/bin/Client +load delta
    DEPENDS Client
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running delta demo"
)

add_custom_target(run_sync
    COMMAND rm -rf ${CMAKE_SOURCE_DIR}/output && mkdir -p ${CMAKE_SOURCE_DIR}/output
    COMMAND ${CMAKE_SOURCE_DIR}/bin/Client +load sync
    DEPENDS Client
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running state sync demo"
)

add_custom_target(run_playback
    COMMAND rm -rf ${CMAKE_SOURCE_DIR}/output && mkdir -p ${CMAKE_SOURCE_DIR}/output
    COMMAND ${CMAKE_SOURCE_DIR}/bin/Client +playback
    DEPENDS Client
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Playback replay recording"
)

# Custom target: line count (loc)
add_custom_target(loc
    COMMAND find ${CMAKE_SOURCE_DIR} -name "*.h" -o -name "*.cpp" | xargs wc -l
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Counting lines of code"
)

# Print configuration summary
message(STATUS "")
message(STATUS "=============================================")
message(STATUS "Protocol Project - CMake Configuration")
message(STATUS "=============================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Output directories:")
message(STATUS "  Libraries: ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
message(STATUS "  Executables: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "=============================================")
message(STATUS "")
