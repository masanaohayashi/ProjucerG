#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
# shellcheck source=scripts/ios_setup_common.sh
source "$SCRIPT_DIR/ios_setup_common.sh"

target=all
force_archive=0
dry_run=0
skip_sdk=0
skip_dependencies=0
skip_llvm=0
skip_runtime=0
skip_openssl=0
skip_libgit2=0
skip_ondevice=0

usage()
{
    cat <<'USAGE'
Usage: setup_ios.sh [options]

Setup stages:
  1. Create OnDeviceBuild from the tracked source template
  2. Prepare JUCE / LLVM / OpenSSL / zsign under OnDeviceBuild
  3. Check prerequisites and Xcode SDKs
  4. Create iPhoneOS / Simulator SDK zip archives
  5. Build host LLVM/Clang and iOS static libraries
  6. Build iOS / Simulator compiler-rt static libraries
  7. Build iOS / Simulator OpenSSL static libraries
  8. Build macOS / iOS / Simulator libgit2 static libraries
  9. Build OnDeviceBuild and zsign for Debug / Release

Options:
  --target device|simulator|all  Target (default: all)
  --jobs N                       Number of parallel compiler jobs
  --force-archive                Recreate the combined LLVM archives
  --skip-dependencies            Skip dependency preparation
  --skip-third-party             Compatibility alias for --skip-dependencies
  --skip-sdk                     Skip SDK zip creation
  --skip-llvm                    Skip LLVM/Clang builds
  --skip-runtime                 Skip compiler-rt builds
  --skip-openssl                 Skip OpenSSL builds
  --skip-libgit2                 Skip libgit2 builds
  --skip-ondevice                Skip OnDeviceBuild builds
  --dry-run                      Print stages without running them
  -h, --help                     Show this help

Re-run the same command after an interruption; Ninja and Make resume from existing outputs.
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
        --force-archive)
            force_archive=1
            shift
            ;;
        --skip-dependencies|--skip-third-party)
            skip_dependencies=1
            shift
            ;;
        --skip-sdk)
            skip_sdk=1
            shift
            ;;
        --skip-llvm)
            skip_llvm=1
            shift
            ;;
        --skip-runtime)
            skip_runtime=1
            shift
            ;;
        --skip-libgit2)
            skip_libgit2=1
            shift
            ;;
        --skip-openssl)
            skip_openssl=1
            shift
            ;;
        --skip-ondevice)
            skip_ondevice=1
            shift
            ;;
        --dry-run)
            dry_run=1
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

mkdir -p "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/build-artifacts"
log_file=${IOS_SETUP_LOG_FILE:-"$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/build-artifacts/ios-setup-$(date '+%Y%m%d-%H%M%S').log"}
mkdir -p "$(dirname "$log_file")"
exec > >(tee -a "$log_file") 2>&1

ios_setup_log "iOS setup started: target=$target jobs=$IOS_SETUP_JOBS"
ios_setup_log "Log: $log_file"

run_child()
{
    local label=$1
    shift
    ios_setup_log "$label"
    if [ "$dry_run" -eq 1 ]; then
        printf '  '
        printf '%q ' "$@"
        printf '\n'
        return 0
    fi
    "$@"
}

if [ "$skip_dependencies" -eq 0 ]; then
    run_child "[1/9] Prepare OnDeviceBuild source" \
        bash "$SCRIPT_DIR/prepare_ondevice_source.sh"
    run_child "[2/9] Prepare dependency sources under OnDeviceBuild" \
        bash "$SCRIPT_DIR/prepare_dependencies.sh"
else
    run_child "[1/9] Prepare OnDeviceBuild source" \
        bash "$SCRIPT_DIR/prepare_ondevice_source.sh"
    ios_setup_log "[2/9] Skipping dependency preparation"
fi

run_child "[3/9] Check prerequisites" \
    bash "$SCRIPT_DIR/check_ios_setup.sh" --target "$target"

if [ "$skip_sdk" -eq 0 ]; then
    case "$target" in
        device)
            run_child "[4/9] Create iPhoneOS SDK zip" \
                bash "$SCRIPT_DIR/create_ios_device_sdk_zip.sh"
            ;;
        simulator)
            run_child "[4/9] Create Simulator SDK zip" \
                bash "$SCRIPT_DIR/create_ios_simulator_sdk_zip.sh"
            ;;
        all)
            run_child "[4/9] Create iPhoneOS SDK zip" \
                bash "$SCRIPT_DIR/create_ios_device_sdk_zip.sh"
            run_child "[4/9] Create Simulator SDK zip" \
                bash "$SCRIPT_DIR/create_ios_simulator_sdk_zip.sh"
            ;;
    esac
else
    ios_setup_log "[4/9] Skipping SDK zip creation"
fi

if [ "$skip_llvm" -eq 0 ]; then
    llvm_args=(--target "$target" --jobs "$IOS_SETUP_JOBS")
    if [ "$force_archive" -eq 1 ]; then
        llvm_args+=(--force-archive)
    fi
    run_child "[5/9] Build LLVM/Clang static libraries" \
        bash "$SCRIPT_DIR/build_ios_llvm.sh" "${llvm_args[@]}"
else
    ios_setup_log "[5/9] Skipping LLVM/Clang builds"
fi

if [ "$skip_runtime" -eq 0 ]; then
    run_child "[6/9] Build compiler-rt static libraries" \
        bash "$SCRIPT_DIR/build_compiler_rt_ios.sh" --target "$target" --jobs "$IOS_SETUP_JOBS"
else
    ios_setup_log "[6/9] Skipping compiler-rt builds"
fi

if [ "$skip_openssl" -eq 0 ]; then
    run_child "[7/9] Build OpenSSL static libraries" \
        bash "$SCRIPT_DIR/build_openssl_ios.sh" --target "$target" --jobs "$IOS_SETUP_JOBS"
else
    ios_setup_log "[7/9] Skipping OpenSSL builds"
fi

if [ "$skip_libgit2" -eq 0 ]; then
    run_child "[8/9] Build libgit2 static libraries" \
        bash "$SCRIPT_DIR/build_libgit2.sh" --target "$target"
else
    ios_setup_log "[8/9] Skipping libgit2 builds"
fi

if [ "$skip_ondevice" -eq 0 ]; then
    run_child "[9/9] Build OnDeviceBuild / zsign static libraries" \
        bash "$SCRIPT_DIR/build_ondevice_libraries.sh" --target "$target" --configuration all --jobs "$IOS_SETUP_JOBS"
else
    ios_setup_log "[9/9] Skipping OnDeviceBuild builds"
fi

if [ "$dry_run" -eq 0 ]; then
    bash "$SCRIPT_DIR/check_ios_setup.sh" --target "$target" --artifacts
    ios_setup_log "iOS setup completed"
    ios_setup_log "For iPad use, copy the generated iPhoneOS.sdk.zip and OnDeviceSigning/ to Projucer Documents in Files"
else
    ios_setup_log "Dry run completed; no files were generated"
fi
