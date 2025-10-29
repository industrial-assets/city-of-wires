#!/bin/bash

# Procedural City Engine - Build and Run Script
# Usage: ./run.sh [clean|rebuild]

set -e  # Exit on any error

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
EXECUTABLE="$BUILD_DIR/procedural_city"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

cleanup() {
    print_status "Cleaning up..."
    # Kill any running instances
    pkill -f procedural_city 2>/dev/null || true
}

# Handle cleanup on script exit
trap cleanup EXIT

# Check if we should clean build directory
if [[ "$1" == "clean" || "$1" == "rebuild" ]]; then
    print_status "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# Check if build directory exists, create if not
if [[ ! -d "$BUILD_DIR" ]]; then
    print_status "Creating build directory..."
    mkdir -p "$BUILD_DIR"
fi

# Configure with CMake
print_status "Configuring project with CMake..."
cd "$PROJECT_DIR"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

# Build the project
print_status "Building project..."
cmake --build "$BUILD_DIR" -j

# Check if executable was created
if [[ ! -f "$EXECUTABLE" ]]; then
    print_error "Build failed - executable not found!"
    exit 1
fi

print_success "Build completed successfully!"

# Check if we should just build (for rebuild command)
if [[ "$1" == "rebuild" ]]; then
    print_success "Rebuild completed. Run './run.sh' to start the game."
    exit 0
fi

# Run the game
print_status "Starting Procedural City Engine..."
print_warning "Press Ctrl+C to stop the game"
print_warning "Press 'R' in the game window to toggle shader hot reloading"

# Make executable and run
chmod +x "$EXECUTABLE"
"$EXECUTABLE"

print_success "Game exited cleanly."
