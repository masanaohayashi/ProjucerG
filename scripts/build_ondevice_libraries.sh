#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
# shellcheck source=scripts/ios_setup_common.sh
source "$SCRIPT_DIR/ios_setup_common.sh"

target=$(ios_setup_default_target)
configuration=all

usage()
{
    cat <<'USAGE'
Usage: build_ondevice_libraries.sh [options]

Options:
  --target device|simulator|all  Build one or both targets (default: all)
  --configuration Debug|Release|all
                                 Build configuration(s) (default: all)
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
        --configuration)
            [ "$#" -ge 2 ] || ios_setup_die "--configuration requires a value"
            configuration=$2
            shift 2
            ;;
        --configuration=*)
            configuration=${1#*=}
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
case "$configuration" in
    Debug|Release|debug|release|all) ;;
    *) ios_setup_die "--configuration must be Debug, Release, or all" ;;
esac

ios_setup_require_dir "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild"
ios_setup_require_file "$POC_CLANG_ROOT/build-ios/llvm-ios.a"
ios_setup_require_file "$POC_CLANG_ROOT/build-rt-ios/lib/darwin/libclang_rt.ios.a"
ios_setup_require_file "$POC_SIGN_ROOT/openssl-ios/lib/libssl.a"
ios_setup_require_file "$POC_SIGN_ROOT/openssl-ios/lib/libcrypto.a"

source_file_count=$(find "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/src" \
    "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/third_party/zsign/src" \
    -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.mm' \) -print 2>/dev/null | wc -l | tr -d ' ')
event_pattern='(CompileC|CompileCXX|CompileSwift|Ld |Libtool |PhaseScriptExecution)'

configurations=()
case "$configuration" in
    Debug|debug) configurations=(Debug) ;;
    Release|release) configurations=(Release) ;;
    all) configurations=(Debug Release) ;;
esac

configure_target()
{
    local selected_target=$1
    local build_dir=$2
    local sysroot=$3
    local simulator_flag=$4
    local llvm_archive=$5
    local openssl_root=$6

    ios_setup_log "Checking OnDeviceBuild $selected_target CMake configuration"
    cmake -S "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild" -B "$build_dir" -G Xcode \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT="$sysroot" \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_DEPLOYMENT_TARGET" \
        -DONDEVICE_IOS_SIMULATOR="$simulator_flag" \
        -DONDEVICE_LLVM_IOS_ARCHIVE="$llvm_archive" \
        -DONDEVICE_LLVM_IOS_DIR="$POC_CLANG_ROOT/$([ "$simulator_flag" = ON ] && printf 'build-ios-simulator' || printf 'build-ios')" \
        -DONDEVICE_LLVM_SRC="$POC_CLANG_ROOT/llvm-project" \
        -DONDEVICE_OPENSSL_IOS="$openssl_root"
}

build_target()
{
    local selected_target=$1
    local build_dir=$2
    local sysroot=$3
    local simulator_flag=$4
    local llvm_archive=$5
    local openssl_root=$6

    configure_target "$selected_target" "$build_dir" "$sysroot" "$simulator_flag" "$llvm_archive" "$openssl_root"
    for selected_configuration in "${configurations[@]}"; do
        local label="Build OnDeviceBuild $selected_target $selected_configuration static libraries"
        local total=$((source_file_count + 4))
        python3 "$SCRIPT_DIR/progress_runner.py" \
            --label "$label" \
            --total "$total" \
            --event-pattern "$event_pattern" \
            --suppress-unmatched \
            --heartbeat 10 \
            -- cmake --build "$build_dir" --config "$selected_configuration" --parallel "$IOS_SETUP_JOBS"

        local output_suffix
        if [ "$selected_target" = "simulator" ]; then
            output_suffix=iphonesimulator
        else
            output_suffix=iphoneos
        fi
        ios_setup_require_file "$build_dir/$selected_configuration-$output_suffix/libOnDeviceBuild.a"
        ios_setup_require_file "$build_dir/$selected_configuration-$output_suffix/libzsign.a"
    done
}

case "$target" in
    device)
        build_target device "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/build-ios" iphoneos OFF \
            "$POC_CLANG_ROOT/build-ios/llvm-ios.a" "$POC_SIGN_ROOT/openssl-ios"
        ;;
    simulator)
        ios_setup_require_file "$POC_CLANG_ROOT/build-ios-simulator/llvm-ios-simulator.a"
        ios_setup_require_file "$POC_CLANG_ROOT/build-rt-ios/lib/darwin/libclang_rt.iossim.a"
        ios_setup_require_file "$POC_SIGN_ROOT/openssl-ios-simulator/lib/libssl.a"
        ios_setup_require_file "$POC_SIGN_ROOT/openssl-ios-simulator/lib/libcrypto.a"
        build_target simulator "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/build-ios-simulator" iphonesimulator ON \
            "$POC_CLANG_ROOT/build-ios-simulator/llvm-ios-simulator.a" "$POC_SIGN_ROOT/openssl-ios-simulator"
        ;;
    all)
        build_target device "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/build-ios" iphoneos OFF \
            "$POC_CLANG_ROOT/build-ios/llvm-ios.a" "$POC_SIGN_ROOT/openssl-ios"
        ios_setup_require_file "$POC_CLANG_ROOT/build-ios-simulator/llvm-ios-simulator.a"
        ios_setup_require_file "$POC_CLANG_ROOT/build-rt-ios/lib/darwin/libclang_rt.iossim.a"
        ios_setup_require_file "$POC_SIGN_ROOT/openssl-ios-simulator/lib/libssl.a"
        ios_setup_require_file "$POC_SIGN_ROOT/openssl-ios-simulator/lib/libcrypto.a"
        build_target simulator "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/build-ios-simulator" iphonesimulator ON \
            "$POC_CLANG_ROOT/build-ios-simulator/llvm-ios-simulator.a" "$POC_SIGN_ROOT/openssl-ios-simulator"
        ;;
esac
