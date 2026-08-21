#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
# shellcheck source=scripts/ios_setup_common.sh
source "$SCRIPT_DIR/ios_setup_common.sh"

JUCE_REPOSITORY=${JUCE_REPOSITORY:-https://github.com/juce-framework/JUCE.git}
JUCE_COMMIT=${JUCE_COMMIT:-f8f8864172464b9adf9eba6101e1f784838d1597}
LLVM_VERSION=${LLVM_VERSION:-22.1.8}
LLVM_SOURCE_URL=${LLVM_SOURCE_URL:-"https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/llvm-project-${LLVM_VERSION}.src.tar.xz"}
OPENSSL_VERSION=${OPENSSL_VERSION:-3.5.4}
OPENSSL_SOURCE_URL=${OPENSSL_SOURCE_URL:-"https://www.openssl.org/source/old/3.5/openssl-${OPENSSL_VERSION}.tar.gz"}

legacy_clang_root="$IOS_SETUP_PROJECT_ROOT/../PocClangIOS"
legacy_sign_root="$IOS_SETUP_PROJECT_ROOT/../PocSignIOS"
downloads_root="$IOS_SETUP_DEPENDENCY_ROOT/downloads"
temporary_root=$(mktemp -d)

cleanup()
{
    rm -rf "$temporary_root"
}
trap cleanup EXIT

usage()
{
    cat <<'USAGE'
Usage: prepare_dependencies.sh

Prepare dependency sources under OnDeviceBuild/dependencies/.
Move existing sibling directories on the first run, or download missing sources.

Override the sources with these environment variables:
  JUCE_REPOSITORY / JUCE_COMMIT
  LLVM_VERSION / LLVM_SOURCE_URL
  OPENSSL_VERSION / OPENSSL_SOURCE_URL

To update an existing dependency, remove its destination directory before rerunning.
USAGE
}

if [ "$#" -gt 0 ]; then
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        *)
            ios_setup_die "Unknown option: $1"
            ;;
    esac
fi

for command_name in curl git tar; do
    ios_setup_require_command "$command_name"
done

ios_setup_require_command perl

if [ ! -f "$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/CMakeLists.txt" ]; then
    bash "$SCRIPT_DIR/prepare_ondevice_source.sh"
fi

mkdir -p "$IOS_SETUP_DEPENDENCY_ROOT" "$downloads_root"

relocate_cmake_caches()
{
    local dependency_root=$1
    local old_root=$2
    local new_root
    local cache_file

    [ -d "$dependency_root" ] || return 0
    old_root=$(cd "$(dirname "$old_root")" && pwd -P)/$(basename "$old_root")
    new_root=$(cd "$dependency_root" && pwd -P)

    # A moved dependency may contain generated CMake files that still refer to
    # its old absolute location.  Updating only CMakeCache.txt is sufficient:
    # the next cmake invocation regenerates build.ninja and the other generated
    # files using the new path, without throwing away the existing objects.
    while IFS= read -r -d '' cache_file; do
        IOS_SETUP_RELOCATE_OLD="$old_root" \
        IOS_SETUP_RELOCATE_NEW="$new_root" \
            perl -pi -e 's/\Q$ENV{IOS_SETUP_RELOCATE_OLD}\E/$ENV{IOS_SETUP_RELOCATE_NEW}/g' "$cache_file"
    done < <(find "$dependency_root" -name CMakeCache.txt -type f -print0 2>/dev/null)
}

move_legacy_dependency()
{
    local label=$1
    local destination=$2
    local legacy=$3
    local required_file=$4

    if [ -e "$destination/$required_file" ]; then
        ios_setup_log "$label: using $destination"
        relocate_cmake_caches "$destination" "$legacy"
        return 0
    fi

    if [ -e "$legacy/$required_file" ]; then
        if [ -e "$destination" ]; then
            ios_setup_die "$label: incomplete dependency directory; remove it and rerun: $destination"
        fi
        ios_setup_log "$label: moving existing sibling directory into the repository"
        mv "$legacy" "$destination"
        relocate_cmake_caches "$destination" "$legacy"
        return 0
    fi

    return 1
}

find_existing_juce_root()
{
    local destination="$JUCE_ROOT"
    local marker="modules/juce_core/juce_core.h"
    local candidate

    # Check only known dependency locations.  The marker prevents unrelated
    # folders from being mistaken for a usable JUCE source tree.  JUCE-9.0.0
    # is retained only as a one-time legacy migration source.
    for candidate in \
        "$IOS_SETUP_PROJECT_ROOT/JUCE" \
        "$IOS_SETUP_PROJECT_ROOT/JUCE-9.0.0" \
        "$IOS_SETUP_DEPENDENCY_ROOT/JUCE-9.0.0" \
        "$IOS_SETUP_PROJECT_ROOT/../JUCE" \
        "$IOS_SETUP_PROJECT_ROOT/../JUCE-9.0.0"; do
        if [ "$candidate" != "$destination" ] && [ -f "$candidate/$marker" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

download_archive()
{
    local label=$1
    local url=$2
    local destination=$3

    if [ -f "$destination" ]; then
        ios_setup_log "$label: using existing archive: $destination"
        return 0
    fi

    local temporary_archive="$destination.part"
    ios_setup_log "$label: download started"
    ios_setup_log "  URL: $url"
    curl --fail --location --retry 3 --progress-bar "$url" -o "$temporary_archive"
    mv "$temporary_archive" "$destination"
    ios_setup_log "$label: download completed ($(du -h "$destination" | awk '{print $1}'))"
}

extract_single_root_archive()
{
    local label=$1
    local archive=$2
    local destination=$3
    local expected_root=$4
    local extraction_root="$temporary_root/extract-$label"

    mkdir -p "$extraction_root"
    case "$archive" in
        *.tar.xz) tar -xJf "$archive" -C "$extraction_root" ;;
        *.tar.gz) tar -xzf "$archive" -C "$extraction_root" ;;
        *) ios_setup_die "$label: unsupported archive format: $archive" ;;
    esac

    local extracted_root
    extracted_root=$(find "$extraction_root" -mindepth 1 -maxdepth 1 -type d -print -quit)
    [ -n "$extracted_root" ] || ios_setup_die "$label: extracted source directory was not found"
    [ "$(basename "$extracted_root")" = "$expected_root" ] || \
        ios_setup_die "$label: unexpected archive layout: $(basename "$extracted_root")"

    mkdir -p "$(dirname "$destination")"
    if [ -e "$destination" ]; then
        ios_setup_die "$label: extraction destination already exists: $destination"
    fi
    mv "$extracted_root" "$destination"
}

prepare_juce()
{
    local destination="$JUCE_ROOT"
    local marker="modules/juce_core/juce_core.h"
    local existing_root

    if [ -f "$destination/$marker" ]; then
        ios_setup_log "JUCE: using $destination"
        return 0
    fi

    existing_root=$(find_existing_juce_root || true)
    if [ -n "$existing_root" ]; then
        ios_setup_log "JUCE: found existing source at $existing_root"
    fi
    if [ -n "$existing_root" ] && \
        move_legacy_dependency "JUCE" "$destination" "$existing_root" "$marker"; then
        return 0
    fi

    local checkout_root="$temporary_root/JUCE"
    ios_setup_log "JUCE: fetching pinned commit $JUCE_COMMIT"
    git init -q "$checkout_root"
    git -C "$checkout_root" remote add origin "$JUCE_REPOSITORY"
    git -C "$checkout_root" fetch --quiet --depth 1 origin "$JUCE_COMMIT"
    git -C "$checkout_root" checkout --quiet --detach FETCH_HEAD
    [ "$(git -C "$checkout_root" rev-parse HEAD)" = "$JUCE_COMMIT" ] || \
        ios_setup_die "JUCE: fetched commit does not match the requested commit"
    mv "$checkout_root" "$destination"
}

prepare_llvm()
{
    local destination="$POC_CLANG_ROOT"
    if move_legacy_dependency "LLVM/Clang" "$destination" "$legacy_clang_root" "llvm-project/llvm/CMakeLists.txt"; then
        return 0
    fi
    if [ -e "$destination" ]; then
        ios_setup_die "LLVM/Clang: incomplete dependency directory; remove it and rerun: $destination"
    fi

    local archive="$downloads_root/llvm-project-${LLVM_VERSION}.src.tar.xz"
    local extracted="$temporary_root/llvm-project-${LLVM_VERSION}.src"
    download_archive "LLVM/Clang ${LLVM_VERSION}" "$LLVM_SOURCE_URL" "$archive"
    extract_single_root_archive "llvm" "$archive" "$extracted" "llvm-project-${LLVM_VERSION}.src"
    mkdir -p "$destination"
    mv "$extracted" "$destination/llvm-project"
}

prepare_openssl()
{
    local destination="$POC_SIGN_ROOT"
    if move_legacy_dependency "OpenSSL" "$destination" "$legacy_sign_root" "openssl-src/Configure"; then
        return 0
    fi
    if [ -e "$destination" ]; then
        ios_setup_die "OpenSSL: incomplete dependency directory; remove it and rerun: $destination"
    fi

    local archive="$downloads_root/openssl-${OPENSSL_VERSION}.tar.gz"
    local extracted="$temporary_root/openssl-${OPENSSL_VERSION}"
    download_archive "OpenSSL ${OPENSSL_VERSION}" "$OPENSSL_SOURCE_URL" "$archive"
    extract_single_root_archive "openssl" "$archive" "$extracted" "openssl-${OPENSSL_VERSION}"
    mkdir -p "$destination"
    mv "$extracted" "$destination/openssl-src"
}

started_at=$(date +%s)
ios_setup_log "Preparing dependency sources under OnDeviceBuild/dependencies"
prepare_juce
prepare_llvm
prepare_openssl
bash "$SCRIPT_DIR/fetch_third_party.sh"
ios_setup_log "Dependency preparation completed in $(ios_setup_format_duration $(( $(date +%s) - started_at )))"
