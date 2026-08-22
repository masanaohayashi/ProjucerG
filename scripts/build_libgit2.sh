#!/usr/bin/env bash
# Fetch libgit2 at a pinned tag and build static archives for macOS, iOS, and
# the iOS Simulator.  The Projucer links these so that git works on iOS, where
# there is no shell to run the real git binary.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
# shellcheck source=scripts/ios_setup_common.sh
source "$SCRIPT_DIR/ios_setup_common.sh"

LIBGIT2_REPOSITORY=${LIBGIT2_REPOSITORY:-https://github.com/libgit2/libgit2.git}
LIBGIT2_TAG=${LIBGIT2_TAG:-v1.9.7}
LIBGIT2_COMMIT=${LIBGIT2_COMMIT:-49e408b3208bc3093757a1c2db938d3590f3f412}

libgit2_root="$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/third_party/libgit2"
targets="all"

usage()
{
    cat <<'USAGE'
Usage: build_libgit2.sh [options]

Options:
  --target macos|device|simulator|all  Which archives to build (default: all)
  -h, --help                           Show this help

Override the source with LIBGIT2_REPOSITORY / LIBGIT2_TAG / LIBGIT2_COMMIT.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --target)
            [ "$#" -ge 2 ] || ios_setup_die "--target requires a value"
            targets=$2
            shift 2
            ;;
        --target=*)
            targets=${1#*=}
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

case "$targets" in
    macos|device|simulator|all) ;;
    *) ios_setup_die "--target must be macos, device, simulator, or all" ;;
esac

ios_setup_require_command git
ios_setup_require_command cmake

if [ ! -f "$libgit2_root/CMakeLists.txt" ]; then
    ios_setup_log "Fetching libgit2 $LIBGIT2_TAG"
    rm -rf "$libgit2_root"
    mkdir -p "$(dirname "$libgit2_root")"
    git clone --quiet --depth 1 --branch "$LIBGIT2_TAG" "$LIBGIT2_REPOSITORY" "$libgit2_root"
fi

revision=$(git -C "$libgit2_root" rev-parse HEAD)
[ "$revision" = "$LIBGIT2_COMMIT" ] \
    || ios_setup_die "libgit2 is at $revision but $LIBGIT2_COMMIT was expected. Remove $libgit2_root and rerun."

# 共通の構成。CLI もテストも要らないので切る。SSH は使わず HTTPS + トークンのみ。
common_arguments=(
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_TESTS=OFF
    -DBUILD_CLI=OFF
    -DBUILD_EXAMPLES=OFF
    -DUSE_SSH=OFF
    -DUSE_NTLMCLIENT=OFF
    -DUSE_GSSAPI=OFF
    -DREGEX_BACKEND=builtin
)

# TLS は SecureTransport を使う。OpenSSL バックエンドだと CA バンドルを
# 自前で持ち歩く必要があり、iOS には置き場所が無い。SecureTransport なら
# OS の信頼ストアをそのまま使える。
build_apple_target()
{
    local label=$1 build_dir=$2 sysroot=$3

    ios_setup_log "Configuring libgit2 for $label"
    cmake -S "$libgit2_root" -B "$libgit2_root/$build_dir" \
        "${common_arguments[@]}" \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT="$sysroot" \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_DEPLOYMENT_TARGET" \
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
        -DUSE_HTTPS=SecureTransport >/dev/null

    ios_setup_log "Building libgit2 for $label"
    cmake --build "$libgit2_root/$build_dir" >/dev/null
    ios_setup_log "Built $libgit2_root/$build_dir/libgit2.a"
}

if [ "$targets" = "macos" ] || [ "$targets" = "all" ]; then
    ios_setup_log "Configuring libgit2 for macOS"
    cmake -S "$libgit2_root" -B "$libgit2_root/build-macos" \
        "${common_arguments[@]}" \
        -DUSE_HTTPS=SecureTransport \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 >/dev/null
    cmake --build "$libgit2_root/build-macos" >/dev/null
    ios_setup_log "Built $libgit2_root/build-macos/libgit2.a"
fi

if [ "$targets" = "device" ] || [ "$targets" = "all" ]; then
    build_apple_target "iOS" build-ios iphoneos
fi

if [ "$targets" = "simulator" ] || [ "$targets" = "all" ]; then
    build_apple_target "iOS Simulator" build-ios-simulator iphonesimulator
fi
