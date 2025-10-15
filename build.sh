#!/bin/bash
# CMake build script for Protocol project

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default values
BUILD_TYPE="Release"
BUILD_DIR="build"
CLEAN=false
VERBOSE=false
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -r|--release)
            BUILD_TYPE="Release"
            shift
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  -d, --debug      Build in Debug mode (default: Release)"
            echo "  -r, --release    Build in Release mode"
            echo "  -c, --clean      Clean build directory before building"
            echo "  -v, --verbose    Verbose output"
            echo "  -j, --jobs N     Number of parallel jobs (default: auto-detected)"
            echo "  -h, --help       Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                    # Build in Release mode"
            echo "  $0 --debug            # Build in Debug mode"
            echo "  $0 --clean --release  # Clean build in Release mode"
            echo "  $0 -j 8               # Build with 8 parallel jobs"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use -h or --help for usage information"
            exit 1
            ;;
    esac
done

echo -e "${GREEN}=============================================${NC}"
echo -e "${GREEN}Protocol Project - CMake Build Script${NC}"
echo -e "${GREEN}=============================================${NC}"
echo -e "Build type: ${YELLOW}$BUILD_TYPE${NC}"
echo -e "Build directory: ${YELLOW}$BUILD_DIR${NC}"
echo -e "Parallel jobs: ${YELLOW}$JOBS${NC}"
echo -e "${GREEN}=============================================${NC}"
echo ""

# Clean if requested
if [ "$CLEAN" = true ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "$BUILD_DIR"
    rm -rf bin lib obj
    echo -e "${GREEN}Clean complete${NC}"
    echo ""
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
echo -e "${YELLOW}Configuring CMake...${NC}"
if [ "$VERBOSE" = true ]; then
    cmake .. -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
else
    cmake .. -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null
fi
echo -e "${GREEN}Configuration complete${NC}"
echo ""

# Build
echo -e "${YELLOW}Building project...${NC}"
if [ "$VERBOSE" = true ]; then
    cmake --build . -j "$JOBS"
else
    cmake --build . -j "$JOBS" 2>&1 | grep -E "(Building|Linking|\[.*%\]|error:|warning:)" || true
fi

# Check if build succeeded
if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}=============================================${NC}"
    echo -e "${GREEN}Build completed successfully!${NC}"
    echo -e "${GREEN}=============================================${NC}"
    echo ""
    echo "Executables are in: ./bin"
    echo "Libraries are in: ./lib"
    echo ""
    echo "To run tests: cd build && ctest --output-on-failure"
    echo "Or use custom targets:"
    echo "  cmake --build build --target test_all"
    echo "  cmake --build build --target run_client"
    echo "  cmake --build build --target run_server"
else
    echo ""
    echo -e "${RED}=============================================${NC}"
    echo -e "${RED}Build failed!${NC}"
    echo -e "${RED}=============================================${NC}"
    exit 1
fi
