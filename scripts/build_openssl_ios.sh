#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
# shellcheck source=scripts/ios_setup_common.sh
source "$SCRIPT_DIR/ios_setup_common.sh"

target=$(ios_setup_default_target)

usage()
{
    cat <<'USAGE'
Usage: build_openssl_ios.sh [options]

Options:
  --target device|simulator|all  Build one or both targets (default: all)
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

ios_setup_require_dir "$POC_SIGN_ROOT/openssl-src"
ios_setup_require_file "$POC_SIGN_ROOT/openssl-src/Configure"
ios_setup_require_command rsync
ios_setup_require_command perl
ios_setup_require_command make

openssl_source="$POC_SIGN_ROOT/openssl-src"
openssl_workspace="$POC_SIGN_ROOT/.ios-setup/openssl"
mkdir -p "$openssl_workspace"

prepare_workspace()
{
    local work_dir=$1
    mkdir -p "$work_dir"
    # Keep generated Makefiles and object files out of the source checkout.  The
    # workspace is intentionally persistent so an interrupted make can resume.
    rsync -a \
        --exclude='Makefile' \
        --exclude='configdata.pm' \
        --exclude='builddata.pm' \
        --exclude='installdata.pm' \
        --exclude='*.o' \
        --exclude='*.a' \
        --exclude='*.d' \
        --exclude='*.d.tmp' \
        --exclude='.openssl' \
        "$openssl_source/" "$work_dir/"
}

build_one()
{
    local selected_target=$1
    local openssl_target=$2
    local min_flag=$3
    local output_dir=$4
    local work_dir="$openssl_workspace/$selected_target"

    prepare_workspace "$work_dir"
    mkdir -p "$output_dir"

    ios_setup_log "Checking OpenSSL $selected_target configuration"
    (
        cd "$work_dir"
        perl Configure "$openssl_target" \
            no-shared no-tests no-apps no-docs no-legacy \
            no-zlib no-zlib-dynamic \
            "--prefix=$output_dir" \
            "$min_flag=$IOS_DEPLOYMENT_TARGET"
    )

    ios_setup_run_make "Build OpenSSL $selected_target static libraries" "$work_dir" build_sw
    ios_setup_run_command "Install OpenSSL $selected_target" \
        make -C "$work_dir" install_sw

    ios_setup_require_file "$output_dir/include/openssl/crypto.h"
    ios_setup_require_file "$output_dir/lib/libcrypto.a"
    ios_setup_require_file "$output_dir/lib/libssl.a"
    ios_setup_log "OpenSSL $selected_target completed: $(du -h "$output_dir/lib/libcrypto.a" | awk '{print $1}') crypto.a"
}

case "$target" in
    device)
        build_one device ios64-xcrun -mios-version-min "$POC_SIGN_ROOT/openssl-ios"
        ;;
    simulator)
        build_one simulator iossimulator-arm64-xcrun -mios-simulator-version-min "$POC_SIGN_ROOT/openssl-ios-simulator"
        ;;
    all)
        build_one device ios64-xcrun -mios-version-min "$POC_SIGN_ROOT/openssl-ios"
        build_one simulator iossimulator-arm64-xcrun -mios-simulator-version-min "$POC_SIGN_ROOT/openssl-ios-simulator"
        ;;
esac
