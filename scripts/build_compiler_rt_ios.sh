#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
# shellcheck source=scripts/ios_setup_common.sh
source "$SCRIPT_DIR/ios_setup_common.sh"

target=$(ios_setup_default_target)

usage()
{
    cat <<'USAGE'
Usage: build_compiler_rt_ios.sh [options]

Options:
  --target device|simulator|all  Build runtime for one or both targets (default: all)
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

ios_setup_require_dir "$POC_CLANG_ROOT/llvm-project/compiler-rt/lib/builtins"
ios_setup_require_file "$POC_CLANG_ROOT/llvm-project/llvm/cmake/platforms/iOS.cmake"

runtime_source="$POC_CLANG_ROOT/llvm-project/compiler-rt/lib/builtins"
runtime_build="$POC_CLANG_ROOT/build-rt-ios"
toolchain_file="$POC_CLANG_ROOT/llvm-project/llvm/cmake/platforms/iOS.cmake"

ios_setup_log "Checking compiler-rt CMake configuration"
cmake -S "$runtime_source" -B "$runtime_build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain_file" \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_DEPLOYMENT_TARGET" \
    -DCMAKE_C_COMPILER_TARGET="arm64-apple-ios$IOS_DEPLOYMENT_TARGET" \
    -DCOMPILER_RT_ENABLE_IOS=ON \
    -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
    -DCOMPILER_RT_BAREMETAL_BUILD=ON \
    -DCOMPILER_RT_BUILTINS_ENABLE_PIC=ON \
    -DCOMPILER_RT_BUILTINS_HIDE_SYMBOLS=ON \
    -DCOMPILER_RT_EXCLUDE_ATOMIC_BUILTIN=ON \
    -DCOMPILER_RT_ENABLE_MACCATALYST=OFF \
    -DCOMPILER_RT_ENABLE_TVOS=OFF \
    -DCOMPILER_RT_ENABLE_WATCHOS=OFF \
    -DCOMPILER_RT_ENABLE_XROS=OFF \
    -DCOMPILER_RT_INCLUDE_TESTS=OFF \
    -DCOMPILER_RT_ENABLE_WERROR=OFF \
    -DCMAKE_BUILD_TYPE=Release

case "$target" in
    device)
        ios_setup_run_ninja "Build compiler-rt iOS static library" "$runtime_build" clang_rt.ios
        ;;
    simulator)
        ios_setup_run_ninja "Build compiler-rt Simulator static library" "$runtime_build" clang_rt.iossim
        ;;
    all)
        ios_setup_run_ninja "Build compiler-rt iOS/Simulator static libraries" "$runtime_build" clang_rt.ios clang_rt.iossim
        ;;
esac

case "$target" in
    device)
        ios_setup_require_file "$runtime_build/lib/darwin/libclang_rt.ios.a"
        ;;
    simulator)
        ios_setup_require_file "$runtime_build/lib/darwin/libclang_rt.iossim.a"
        ;;
    all)
        ios_setup_require_file "$runtime_build/lib/darwin/libclang_rt.ios.a"
        ios_setup_require_file "$runtime_build/lib/darwin/libclang_rt.iossim.a"
        ;;
esac
ios_setup_log "compiler-rt artifacts verified"
