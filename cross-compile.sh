#!/bin/bash
# Exit on error, but we'll handle specific errors manually
set -e

# ==============================================================================
# Cross-compilation script for Ubuntu
# ==============================================================================
# 
# This script handles cross-compilation for ARM64/ARMHF/i386 architectures on
# Ubuntu/Debian systems. It automatically handles common issues:
#
# - Configures Ubuntu Ports repository for ARM packages
# - Fixes architecture restrictions in both old and new (DEB822) apt formats
# - Protects critical system packages (Python) from cross-arch conflicts
# - Handles file conflicts (e.g., Pango GIR files) with --force-overwrite
# - Automatically fixes broken package dependencies
# - Builds host tools (juceaide) before cross-compiling
# - Creates proper sysroot with target architecture libraries
#
# Usage: ./cross-compile.sh <architecture>
# Supported architectures: arm64, armhf, i386
#
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to check and fix broken packages
fix_broken_packages() {
    log_info "Checking for broken packages..."
    if ! dpkg -l | grep -q "^iU\|^iF"; then
        log_info "No broken packages found"
        return 0
    fi
    
    log_warn "Broken packages detected, attempting to fix..."
    sudo apt-get install -f -y || {
        log_warn "apt-get install -f failed, trying to remove problematic packages..."
        # Try to identify and remove problematic ARM packages
        dpkg -l | grep ":${DEBIAN_ARCH}" | grep "^iU\|^iF" | awk '{print $2}' | while read pkg; do
            sudo dpkg --remove --force-all "$pkg" 2>/dev/null || true
        done
        sudo apt-get install -f -y || log_warn "Still have issues, continuing anyway..."
    }
}

# Check if architecture is provided
if [ -z "$1" ]; then
    log_error "No architecture specified"
    echo "Usage: $0 <architecture>"
    echo "Supported architectures: arm64, armhf, i386"
    exit 1
fi

TARGET_ARCH="$1"

# Validate architecture
case "$TARGET_ARCH" in
    arm64|aarch64)
        TARGET_ARCH="arm64"
        DEBIAN_ARCH="arm64"
        CMAKE_SYSTEM_PROCESSOR="aarch64"
        CROSS_COMPILE_PREFIX="aarch64-linux-gnu"
        ;;
    armhf|armv7)
        TARGET_ARCH="armhf"
        DEBIAN_ARCH="armhf"
        CMAKE_SYSTEM_PROCESSOR="arm"
        CROSS_COMPILE_PREFIX="arm-linux-gnueabihf"
        ;;
    i386|x86)
        TARGET_ARCH="i386"
        DEBIAN_ARCH="i386"
        CMAKE_SYSTEM_PROCESSOR="i686"
        CROSS_COMPILE_PREFIX="i686-linux-gnu"
        ;;
    *)
        log_error "Unsupported architecture: $TARGET_ARCH"
        echo "Supported architectures: arm64, armhf, i386"
        exit 1
        ;;
esac

log_info "Cross-compiling for architecture: $TARGET_ARCH"

# Source dependencies
DEPS_FILE="$PROJECT_ROOT/linux-dependencies.sh"
if [ ! -f "$DEPS_FILE" ]; then
    log_error "Dependencies file not found: $DEPS_FILE"
    exit 1
fi

log_info "Sourcing dependencies from $DEPS_FILE"
source "$DEPS_FILE"

if [ -z "${DEPENDENCIES+x}" ] || [ ${#DEPENDENCIES[@]} -eq 0 ]; then
    log_error "DEPENDENCIES array not set or empty in $DEPS_FILE"
    exit 1
fi

# Setup directories
SYSROOT_DIR="$PROJECT_ROOT/sysroot-$TARGET_ARCH"
BUILD_DIR="$PROJECT_ROOT/build-$TARGET_ARCH"
TOOLCHAIN_FILE="$PROJECT_ROOT/toolchain-$TARGET_ARCH.cmake"

log_info "Sysroot directory: $SYSROOT_DIR"
log_info "Build directory: $BUILD_DIR"

# Check if running as root for apt operations
check_sudo() {
    if ! sudo -n true 2>/dev/null; then
        log_warn "This script requires sudo access for installing packages"
        sudo -v || {
            log_error "Failed to obtain sudo access"
            exit 1
        }
    fi
}

# Setup multiarch and install cross-compilation tools
setup_cross_tools() {
    log_info "Setting up cross-compilation tools..."
    
    check_sudo
    
    # For ARM architectures, ensure ports.ubuntu.com is configured
    if [ "$DEBIAN_ARCH" = "arm64" ] || [ "$DEBIAN_ARCH" = "armhf" ]; then
        log_info "Configuring Ubuntu Ports repository for ARM architecture..."
        UBUNTU_CODENAME=$(lsb_release -cs)
        
        # Handle new DEB822 format (.sources files)
        UBUNTU_SOURCES="/etc/apt/sources.list.d/ubuntu.sources"
        if [ -f "$UBUNTU_SOURCES" ]; then
            UBUNTU_SOURCES_BACKUP="/etc/apt/sources.list.d/ubuntu.sources.bak"
            if [ ! -f "$UBUNTU_SOURCES_BACKUP" ]; then
                log_info "Backing up ubuntu.sources..."
                sudo cp "$UBUNTU_SOURCES" "$UBUNTU_SOURCES_BACKUP"
            fi
            
            log_info "Restricting ubuntu.sources to amd64 architecture..."
            # Add "Architectures: amd64" line after each "Types: deb" line if not already present
            sudo awk '/^Types: deb/ && !done {print; print "Architectures: amd64"; done=1; next} 
                      /^Types: deb/ {print; print "Architectures: amd64"; next}
                      /^Architectures:/ {next}
                      {print}' "$UBUNTU_SOURCES_BACKUP" | sudo tee "$UBUNTU_SOURCES" > /dev/null
        fi
        
        # Handle old format sources.list if it exists and has content
        SOURCES_LIST="/etc/apt/sources.list"
        if [ -s "$SOURCES_LIST" ]; then
            SOURCES_BACKUP="/etc/apt/sources.list.backup-cross-compile"
            if [ ! -f "$SOURCES_BACKUP" ]; then
                log_info "Backing up sources.list..."
                sudo cp "$SOURCES_LIST" "$SOURCES_BACKUP"
            fi
            
            log_info "Restricting sources.list to amd64..."
            sudo sed -i '/^deb / { /\[arch/ !s/^deb /deb [arch=amd64] / }' "$SOURCES_LIST"
        fi
        
        # Create Ubuntu Ports sources file
        PORTS_LIST="/etc/apt/sources.list.d/ubuntu-ports.list"
        cat << EOF | sudo tee "$PORTS_LIST" > /dev/null
# Ubuntu Ports for ARM architectures
deb [arch=arm64,armhf] http://ports.ubuntu.com/ubuntu-ports ${UBUNTU_CODENAME} main restricted universe multiverse
deb [arch=arm64,armhf] http://ports.ubuntu.com/ubuntu-ports ${UBUNTU_CODENAME}-updates main restricted universe multiverse
deb [arch=arm64,armhf] http://ports.ubuntu.com/ubuntu-ports ${UBUNTU_CODENAME}-security main restricted universe multiverse
deb [arch=arm64,armhf] http://ports.ubuntu.com/ubuntu-ports ${UBUNTU_CODENAME}-backports main restricted universe multiverse
EOF
        
        log_info "Ubuntu Ports repository configured"
    fi
    
    # Add foreign architecture
    if ! dpkg --print-foreign-architectures | grep -q "^${DEBIAN_ARCH}$"; then
        log_info "Adding foreign architecture: $DEBIAN_ARCH"
        sudo dpkg --add-architecture "$DEBIAN_ARCH"
        sudo apt-get update
    else
        log_info "Architecture $DEBIAN_ARCH already configured"
        # Still update to ensure we have the ports repo indexed
        if [ "$DEBIAN_ARCH" = "arm64" ] || [ "$DEBIAN_ARCH" = "armhf" ]; then
            log_info "Updating package lists for ports repository..."
            sudo apt-get update
        fi
    fi
    
    # Install cross-compilation toolchain and essential build tools
    log_info "Installing cross-compilation toolchain and build tools..."
    
    # Check and fix any broken packages first
    fix_broken_packages
    
    # First ensure Python and core packages are locked to amd64
    log_info "Protecting critical system packages from cross-arch installation..."
    sudo apt-mark hold python3:$DEBIAN_ARCH python3-minimal:$DEBIAN_ARCH python3.12:$DEBIAN_ARCH python3.12-minimal:$DEBIAN_ARCH 2>/dev/null || true
    
    # Install toolchain with error handling
    if ! sudo apt-get install -y \
        build-essential \
        crossbuild-essential-${DEBIAN_ARCH} \
        g++-${CROSS_COMPILE_PREFIX} \
        gcc-${CROSS_COMPILE_PREFIX} \
        binutils-${CROSS_COMPILE_PREFIX}; then
        log_warn "Some toolchain packages failed, attempting to fix..."
        fix_broken_packages
        sudo apt-get install -y \
            build-essential \
            crossbuild-essential-${DEBIAN_ARCH} \
            g++-${CROSS_COMPILE_PREFIX} \
            gcc-${CROSS_COMPILE_PREFIX} \
            binutils-${CROSS_COMPILE_PREFIX} || log_error "Failed to install toolchain"
    fi
    
    # Install build tools if not present
    sudo apt-get install -y \
        cmake \
        ninja-build \
        pkg-config \
        rsync || log_warn "Some build tools may not have installed"
    
    # Verify compilers are installed
    if ! command -v ${CROSS_COMPILE_PREFIX}-gcc &> /dev/null; then
        log_error "Cross-compiler ${CROSS_COMPILE_PREFIX}-gcc not found after installation"
        exit 1
    fi
    
    if ! command -v ${CROSS_COMPILE_PREFIX}-g++ &> /dev/null; then
        log_error "Cross-compiler ${CROSS_COMPILE_PREFIX}-g++ not found after installation"
        exit 1
    fi
    
    log_info "Cross-compilation toolchain verified:"
    log_info "  C compiler: $(${CROSS_COMPILE_PREFIX}-gcc --version | head -n1)"
    log_info "  C++ compiler: $(${CROSS_COMPILE_PREFIX}-g++ --version | head -n1)"
}

# Install dependencies for target architecture
install_dependencies() {
    log_info "Installing dependencies for $TARGET_ARCH..."
    
    check_sudo
    
    # Check and fix any broken packages first
    fix_broken_packages
    
    # Build list of packages with architecture suffix
    local packages=()
    for dep in "${DEPENDENCIES[@]}"; do
        # Skip wildcard packages for cross-arch installation
        if [[ "$dep" != *"*"* ]]; then
            packages+=("${dep}:${DEBIAN_ARCH}")
        fi
    done
    
    log_info "Installing packages: ${packages[*]}"
    
    # Install dependencies with conflict handling
    if [ ${#packages[@]} -gt 0 ]; then
        if ! sudo apt-get install -y "${packages[@]}" 2>&1 | tee /tmp/apt-install-$$.log; then
            log_warn "Initial installation failed, checking for conflicts..."
            
            # Check for file conflicts (like Pango GIR files)
            if grep -q "trying to overwrite shared" /tmp/apt-install-$$.log; then
                log_info "Detected file conflicts, using --force-overwrite for conflicting packages..."
                
                # Get list of conflicting packages
                local conflict_pkgs=$(grep "trying to overwrite shared" /tmp/apt-install-$$.log | grep -oP "package \K[^:]+:\S+" | sort -u)
                
                # Download and force install conflicting packages
                for pkg in $conflict_pkgs; do
                    log_info "Force installing $pkg..."
                    sudo apt-get download "$pkg" 2>/dev/null || true
                    local deb_file=$(ls -t ${pkg%%:*}_*.deb 2>/dev/null | head -1)
                    if [ -f "$deb_file" ]; then
                        sudo dpkg -i --force-overwrite "$deb_file" || true
                        rm -f "$deb_file"
                    fi
                done
                
                # Now fix dependencies
                sudo apt-get install -f -y || log_warn "Some dependencies may be incomplete"
                
                # Retry installation
                sudo apt-get install -y "${packages[@]}" || {
                    log_warn "Some packages still failed to install, continuing anyway..."
                }
            else
                log_warn "Installation failed but no obvious conflicts detected"
                fix_broken_packages
                # Try once more
                sudo apt-get install -y "${packages[@]}" || {
                    log_error "Failed to install some dependencies"
                }
            fi
            log_warn "Continuing anyway, build might fail if critical packages are missing"
        fi
        
        # Cleanup temp log
        rm -f /tmp/apt-install-$$.log
    fi
}

# Create sysroot
create_sysroot() {
    log_info "Creating sysroot..."
    
    mkdir -p "$SYSROOT_DIR/usr/lib"
    mkdir -p "$SYSROOT_DIR/usr/include"
    
    # Copy system libraries and headers
    if [ -d "/usr/lib/${CROSS_COMPILE_PREFIX}" ]; then
        log_info "Copying cross-compilation libraries..."
        rsync -av "/usr/lib/${CROSS_COMPILE_PREFIX}/" "$SYSROOT_DIR/usr/lib/" || true
    fi
    
    if [ -d "/usr/include/${CROSS_COMPILE_PREFIX}" ]; then
        log_info "Copying cross-compilation headers..."
        rsync -av "/usr/include/${CROSS_COMPILE_PREFIX}/" "$SYSROOT_DIR/usr/include/" || true
    fi
    
    # Copy arch-specific libraries (e.g., /usr/lib/aarch64-linux-gnu)
    local arch_lib_dir="/usr/${CROSS_COMPILE_PREFIX}/lib"
    if [ -d "$arch_lib_dir" ]; then
        log_info "Copying ${CROSS_COMPILE_PREFIX} libraries..."
        rsync -av "$arch_lib_dir/" "$SYSROOT_DIR/usr/lib/" || true
    fi
    
    # Copy arch-specific includes
    local arch_inc_dir="/usr/${CROSS_COMPILE_PREFIX}/include"
    if [ -d "$arch_inc_dir" ]; then
        log_info "Copying ${CROSS_COMPILE_PREFIX} headers..."
        rsync -av "$arch_inc_dir/" "$SYSROOT_DIR/usr/include/" || true
    fi
    
    # Also check alternative locations
    if [ -d "/usr/lib/${DEBIAN_ARCH}-linux-gnu" ]; then
        log_info "Copying architecture-specific libraries from alt location..."
        rsync -av "/usr/lib/${DEBIAN_ARCH}-linux-gnu/" "$SYSROOT_DIR/usr/lib/${DEBIAN_ARCH}-linux-gnu/" || true
    fi
    
    if [ -d "/usr/include/${DEBIAN_ARCH}-linux-gnu" ]; then
        log_info "Copying architecture-specific headers from alt location..."
        rsync -av "/usr/include/${DEBIAN_ARCH}-linux-gnu/" "$SYSROOT_DIR/usr/include/${DEBIAN_ARCH}-linux-gnu/" || true
    fi
    
    log_info "Sysroot created at $SYSROOT_DIR"
}

# Generate CMake toolchain file
generate_toolchain() {
    log_info "Generating CMake toolchain file..."
    
    # Get the path to pre-built juceaide
    local HOST_BUILD_DIR="$PROJECT_ROOT/build-host-tools"
    local JUCEAIDE_PATH="$HOST_BUILD_DIR/JUCE/tools/juceaide"
    
    cat > "$TOOLCHAIN_FILE" << EOF
# CMake toolchain file for cross-compiling to $TARGET_ARCH
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR $CMAKE_SYSTEM_PROCESSOR)

# Specify the cross compiler
set(CMAKE_C_COMPILER ${CROSS_COMPILE_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${CROSS_COMPILE_PREFIX}-g++)

# Sysroot
set(CMAKE_SYSROOT $SYSROOT_DIR)
set(CMAKE_FIND_ROOT_PATH $SYSROOT_DIR)

# Search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Don't build helper tools when cross-compiling
set(JUCE_BUILD_HELPER_TOOLS OFF CACHE BOOL "Don't build helper tools" FORCE)

# Point to pre-built juceaide from host build
if(EXISTS "$JUCEAIDE_PATH")
    set(JUCE_JUCEAIDE_PATH "$JUCEAIDE_PATH" CACHE FILEPATH "Path to juceaide" FORCE)
    message(STATUS "Using pre-built juceaide: $JUCEAIDE_PATH")
endif()

# Compiler flags
set(CMAKE_C_FLAGS "\${CMAKE_C_FLAGS} --sysroot=$SYSROOT_DIR")
set(CMAKE_CXX_FLAGS "\${CMAKE_CXX_FLAGS} --sysroot=$SYSROOT_DIR")
set(CMAKE_EXE_LINKER_FLAGS "\${CMAKE_EXE_LINKER_FLAGS} --sysroot=$SYSROOT_DIR")
set(CMAKE_SHARED_LINKER_FLAGS "\${CMAKE_SHARED_LINKER_FLAGS} --sysroot=$SYSROOT_DIR")

# pkg-config configuration
set(PKG_CONFIG_EXECUTABLE /usr/bin/pkg-config)
set(ENV{PKG_CONFIG_PATH} "$SYSROOT_DIR/usr/lib/pkgconfig:$SYSROOT_DIR/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "$SYSROOT_DIR/usr/lib/pkgconfig:$SYSROOT_DIR/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "$SYSROOT_DIR")
EOF

    log_info "Toolchain file created at $TOOLCHAIN_FILE"
}

# Build juceaide for host system
build_host_juceaide() {
    log_info "Building juceaide for host system..."
    
    local HOST_BUILD_DIR="$PROJECT_ROOT/build-host-tools"
    
    # Check if juceaide already exists and works
    if [ -f "$HOST_BUILD_DIR/JUCE/tools/juceaide" ] && "$HOST_BUILD_DIR/JUCE/tools/juceaide" --help &>/dev/null; then
        log_info "juceaide already built and working for host system"
        export PATH="$HOST_BUILD_DIR/JUCE/tools:$PATH"
        return 0
    fi
    
    # Ensure native build dependencies are installed for host
    log_info "Installing native build dependencies for juceaide..."
    sudo apt-get install -y \
        libfreetype-dev \
        libfontconfig1-dev \
        pkg-config || log_warn "Some native dependencies failed to install"
    
    mkdir -p "$HOST_BUILD_DIR"
    cd "$HOST_BUILD_DIR"
    
    # Configure entire project for host (just to build juceaide)
    # Make sure to unset any cross-compilation variables
    log_info "Configuring project for host architecture to build juceaide..."
    (unset CMAKE_TOOLCHAIN_FILE CMAKE_SYSROOT; \
     cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -G Ninja \
        "$PROJECT_ROOT") || {
        log_warn "Failed to configure for host, continuing anyway..."
        return 0
    }
    
    # Try to build just the juceaide target if it exists
    log_info "Building juceaide target..."
    if cmake --build . --target juceaide 2>/dev/null; then
        log_info "Host juceaide built successfully"
        export PATH="$HOST_BUILD_DIR/JUCE/tools:$PATH"
    else
        log_warn "Could not build juceaide specifically, trying full build..."
        # Build everything for host - this ensures juceaide is available
        cmake --build . || {
            log_warn "Failed to build juceaide, cross-compilation may fail..."
            return 0
        }
        log_info "Host tools built successfully"
        export PATH="$HOST_BUILD_DIR/JUCE/tools:$PATH"
    fi
}

# Configure CMake
configure_cmake() {
    log_info "Configuring CMake..."
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    cmake \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DCMAKE_BUILD_TYPE=Release \
        -DJUCE_BUILD_HELPER_TOOLS=OFF \
        -G Ninja \
        "$PROJECT_ROOT"
    
    log_info "CMake configuration complete"
}

# Build project
build_project() {
    log_info "Building project..."
    
    cd "$BUILD_DIR"
    cmake --build . --config Release -j$(nproc)
    
    log_info "Build complete!"
}

# Main execution
main() {
    log_info "Starting cross-compilation for $TARGET_ARCH"
    log_info "================================================"
    
    setup_cross_tools
    install_dependencies
    create_sysroot
    build_host_juceaide
    generate_toolchain
    configure_cmake
    build_project
    
    log_info "================================================"
    log_info "Cross-compilation successful!"
    log_info "Build artifacts are in: $BUILD_DIR"
    log_info "Architecture: $TARGET_ARCH"
}

# Run main function
main
