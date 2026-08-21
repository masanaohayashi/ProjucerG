#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
# shellcheck source=scripts/ios_setup_common.sh
source "$SCRIPT_DIR/ios_setup_common.sh"

target=$(ios_setup_default_target)
check_artifacts=0

usage()
{
    cat <<'USAGE'
Usage: check_ios_setup.sh [options]

Options:
  --target device|simulator|all  Check one or both iOS targets (default: all)
  --artifacts                   Also require every generated output
  -h, --help                    Show this help
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
        --artifacts)
            check_artifacts=1
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

if [ "$(uname -s)" != "Darwin" ]; then
    ios_setup_die "iOS setup must run on macOS"
fi

for command_name in curl git xcodebuild xcrun cmake ninja make perl python3 zip unzip libtool; do
    ios_setup_require_command "$command_name"
done

ios_setup_require_dir "$IOS_SETUP_PROJECT_ROOT"
ios_setup_require_dir "$JUCE_ROOT"
ios_setup_require_dir "$POC_CLANG_ROOT"
ios_setup_require_file "$POC_CLANG_ROOT/llvm-project/llvm/CMakeLists.txt"
ios_setup_require_file "$POC_CLANG_ROOT/llvm-project/compiler-rt/lib/builtins/CMakeLists.txt"
ios_setup_require_dir "$POC_SIGN_ROOT"
ios_setup_require_file "$POC_SIGN_ROOT/openssl-src/Configure"
ios_setup_require_file "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/CMakeLists.txt"
ios_setup_require_file "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/third_party/zsign/LICENSE"
ios_setup_require_file "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/third_party/zsign/src/common/json.cpp"
ios_setup_require_file "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/third_party/zsign/src/third-party/zlib/zlib.h"

if ! xcode-select --print-path >/dev/null 2>&1; then
    ios_setup_die "Xcode command line tools are not selected: sudo xcode-select --switch /Applications/Xcode.app"
fi

ios_setup_log "Xcode: $(xcodebuild -version | tr '\n' ' ')"
ios_setup_log "CMake: $(cmake --version | head -n 1)"
ios_setup_log "LLVM source: $POC_CLANG_ROOT/llvm-project"
ios_setup_log "OpenSSL source: $POC_SIGN_ROOT/openssl-src"
ios_setup_log "Parallel jobs: $IOS_SETUP_JOBS"

check_sdk()
{
    local sdk_name=$1
    local sdk_path
    sdk_path=$(xcrun --sdk "$sdk_name" --show-sdk-path)
    ios_setup_require_dir "$sdk_path"
    ios_setup_log "$sdk_name SDK: $sdk_path"
}

case "$target" in
    device)
        check_sdk iphoneos
        ;;
    simulator)
        check_sdk iphonesimulator
        ;;
    all)
        check_sdk iphoneos
        check_sdk iphonesimulator
        ;;
esac

if [ "$check_artifacts" -eq 1 ]; then
    require_target_artifacts()
    {
        local selected_target=$1
        if [ "$selected_target" = "device" ]; then
            ios_setup_require_file "$POC_CLANG_ROOT/build-ios/llvm-ios.a"
            ios_setup_require_file "$POC_CLANG_ROOT/build-rt-ios/lib/darwin/libclang_rt.ios.a"
            ios_setup_require_file "$POC_SIGN_ROOT/openssl-ios/lib/libssl.a"
            ios_setup_require_file "$POC_SIGN_ROOT/openssl-ios/lib/libcrypto.a"
            ios_setup_require_file "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/build-ios/Debug-iphoneos/libOnDeviceBuild.a"
            ios_setup_require_file "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/build-ios/Debug-iphoneos/libzsign.a"
            ios_setup_require_file "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/build-artifacts/iPhoneOS.sdk.zip"
        else
            ios_setup_require_file "$POC_CLANG_ROOT/build-ios-simulator/llvm-ios-simulator.a"
            ios_setup_require_file "$POC_CLANG_ROOT/build-rt-ios/lib/darwin/libclang_rt.iossim.a"
            ios_setup_require_file "$POC_SIGN_ROOT/openssl-ios-simulator/lib/libssl.a"
            ios_setup_require_file "$POC_SIGN_ROOT/openssl-ios-simulator/lib/libcrypto.a"
            ios_setup_require_file "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/build-ios-simulator/Debug-iphonesimulator/libOnDeviceBuild.a"
            ios_setup_require_file "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/build-ios-simulator/Debug-iphonesimulator/libzsign.a"
            ios_setup_require_file "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/build-artifacts/iPhoneSimulator.sdk.zip"
        fi
    }

    case "$target" in
        device) require_target_artifacts device ;;
        simulator) require_target_artifacts simulator ;;
        all)
            require_target_artifacts device
            require_target_artifacts simulator
            ;;
    esac
fi

ios_setup_log "Prerequisite check completed"
