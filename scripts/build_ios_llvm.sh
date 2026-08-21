#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
# shellcheck source=scripts/ios_setup_common.sh
source "$SCRIPT_DIR/ios_setup_common.sh"

target=$(ios_setup_default_target)
force_archive=0

usage()
{
    cat <<'USAGE'
Usage: build_ios_llvm.sh [options]

Options:
  --target device|simulator|all  Build one or both targets (default: all)
  --force-archive                Recreate the combined .a archive
  --jobs N                       Number of parallel compiler jobs
  -h, --help                     Show this help
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --target)
            [ "$#" -ge 2 ] || ios_setup_die "--target requires a value"
            target=$2
            shift 2
            ;;
        --target=*)
            target=${1#*=}
            shift
            ;;
        --force-archive)
            force_archive=1
            shift
            ;;
        --jobs)
            [ "$#" -ge 2 ] || ios_setup_die "--jobs requires a value"
            IOS_SETUP_JOBS=$2
            shift 2
            ;;
        --jobs=*)
            IOS_SETUP_JOBS=${1#*=}
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            ios_setup_die "Unknown option: $1"
            ;;
    esac
done

case "$target" in
    device|simulator|all) ;;
    *) ios_setup_die "--target must be device, simulator, or all" ;;
esac

ios_setup_require_dir "$POC_CLANG_ROOT/llvm-project/llvm"
ios_setup_require_file "$POC_CLANG_ROOT/llvm-project/llvm/cmake/platforms/iOS.cmake"
ios_setup_require_command cmake
ios_setup_require_command ninja
ios_setup_require_command xcrun

llvm_source="$POC_CLANG_ROOT/llvm-project/llvm"
host_build="$POC_CLANG_ROOT/build-host"
device_build="$POC_CLANG_ROOT/build-ios"
simulator_build="$POC_CLANG_ROOT/build-ios-simulator"
toolchain_file="$POC_CLANG_ROOT/llvm-project/llvm/cmake/platforms/iOS.cmake"

configure_host()
{
    ios_setup_log "Checking LLVM host CMake configuration"
    cmake -S "$llvm_source" -B "$host_build" -G Ninja \
        -DLLVM_ENABLE_PROJECTS="clang;lld" \
        -DLLVM_TARGETS_TO_BUILD=AArch64 \
        -DCMAKE_BUILD_TYPE=Release
}

configure_target()
{
    local selected_target=$1
    local build_dir=$2
    local sysroot=$3

    ios_setup_log "Checking LLVM $selected_target CMake configuration"
    cmake -S "$llvm_source" -B "$build_dir" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain_file" \
        -DCMAKE_OSX_SYSROOT="$sysroot" \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_DEPLOYMENT_TARGET" \
        -DLLVM_TARGET_ARCH=arm64 \
        -DLLVM_TARGETS_TO_BUILD=AArch64 \
        -DLLVM_ENABLE_PROJECTS="clang;lld" \
        -DCMAKE_BUILD_TYPE=Release \
        -DLLVM_TABLEGEN="$host_build/bin/llvm-tblgen" \
        -DCLANG_TABLEGEN="$host_build/bin/clang-tblgen" \
        -DLLVM_BUILD_TOOLS=OFF \
        -DCLANG_BUILD_TOOLS=OFF \
        -DLLVM_BUILD_UTILS=OFF \
        -DLLVM_INCLUDE_UTILS=OFF \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DCLANG_ENABLE_STATIC_ANALYZER=OFF \
        -DCLANG_ENABLE_ARCMT=OFF \
        -DLLVM_ENABLE_FFI=OFF \
        -DLLVM_ENABLE_ZLIB=OFF \
        -DLLVM_ENABLE_ZSTD=OFF \
        -DLLVM_ENABLE_LIBXML2=OFF \
        -DLLVM_ENABLE_THREADS=OFF \
        -DLLVM_DISABLE_ASSEMBLY_FILES=ON \
        -DLLVM_ENABLE_ASSERTIONS=OFF
}

build_and_archive()
{
    local selected_target=$1
    local build_dir=$2
    local archive=$3
    local sdk_name=$4

    local pending
    pending=$(ios_setup_pending_ninja_tasks "$build_dir")
    ios_setup_run_ninja "Build LLVM $selected_target static libraries" "$build_dir"

    local libraries=("$build_dir"/lib/*.a)
    if [ "${#libraries[@]}" -eq 0 ] || [ ! -f "${libraries[0]}" ]; then
        ios_setup_die "LLVM $selected_target input static libraries are missing: $build_dir/lib"
    fi

    if [ "$force_archive" -eq 0 ] && [ "$pending" -eq 0 ] && [ -f "$archive" ]; then
        ios_setup_log "LLVM $selected_target combined archive is up to date: $archive"
        return 0
    fi

    mkdir -p "$(dirname "$archive")"
    local temp_archive="$archive.tmp.$$"
    local library_count=${#libraries[@]}
    local archive_started
    archive_started=$(date +%s)
    ios_setup_log "Combining $library_count LLVM $selected_target static libraries"
    ios_setup_log "Individual compiler progress is unavailable for this step; start and finish times are shown"
    "$(xcrun --sdk "$sdk_name" -find libtool)" -static -o "$temp_archive" "${libraries[@]}"
    mv -f "$temp_archive" "$archive"
    ios_setup_log "LLVM $selected_target archive completed: $(du -h "$archive" | awk '{print $1}'), elapsed $(ios_setup_format_duration $(( $(date +%s) - archive_started )))"
}

configure_host
ios_setup_run_ninja "Build LLVM host static libraries" "$host_build"
ios_setup_require_file "$host_build/bin/llvm-tblgen"
ios_setup_require_file "$host_build/bin/clang-tblgen"

case "$target" in
    device)
        configure_target device "$device_build" iphoneos
        build_and_archive device "$device_build" "$device_build/llvm-ios.a" iphoneos
        ;;
    simulator)
        configure_target simulator "$simulator_build" iphonesimulator
        build_and_archive simulator "$simulator_build" "$simulator_build/llvm-ios-simulator.a" iphonesimulator
        ;;
    all)
        configure_target device "$device_build" iphoneos
        build_and_archive device "$device_build" "$device_build/llvm-ios.a" iphoneos
        configure_target simulator "$simulator_build" iphonesimulator
        build_and_archive simulator "$simulator_build" "$simulator_build/llvm-ios-simulator.a" iphonesimulator
        ;;
esac
