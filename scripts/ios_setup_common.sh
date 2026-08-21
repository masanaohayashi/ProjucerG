#!/usr/bin/env bash

set -euo pipefail

IOS_SETUP_SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
IOS_SETUP_PROJECT_ROOT=$(cd "$IOS_SETUP_SCRIPT_DIR/.." && pwd -P)
IOS_SETUP_USER_HOME=${HOME:?HOME is not set}

export IOS_SETUP_PROJECT_ROOT
export IOS_SETUP_DEPENDENCY_ROOT=${IOS_SETUP_DEPENDENCY_ROOT:-"$IOS_SETUP_PROJECT_ROOT/OnDeviceBuild/dependencies"}
export POC_CLANG_ROOT=${POC_CLANG_ROOT:-"$IOS_SETUP_DEPENDENCY_ROOT/PocClangIOS"}
export POC_SIGN_ROOT=${POC_SIGN_ROOT:-"$IOS_SETUP_DEPENDENCY_ROOT/PocSignIOS"}
export JUCE_ROOT=${JUCE_ROOT:-"$IOS_SETUP_DEPENDENCY_ROOT/JUCE"}
export IOS_DEPLOYMENT_TARGET=${IOS_DEPLOYMENT_TARGET:-17.0}
export IOS_SETUP_JOBS=${IOS_SETUP_JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || printf '4')}

ios_setup_timestamp()
{
    date '+%H:%M:%S'
}

ios_setup_format_duration()
{
    local total_seconds=${1:-0}
    local hours=$((total_seconds / 3600))
    local minutes=$(((total_seconds % 3600) / 60))
    local seconds=$((total_seconds % 60))

    if [ "$hours" -gt 0 ]; then
        printf '%dh %02dm %02ds' "$hours" "$minutes" "$seconds"
    elif [ "$minutes" -gt 0 ]; then
        printf '%dm %02ds' "$minutes" "$seconds"
    else
        printf '%ds' "$seconds"
    fi
}

ios_setup_log()
{
    printf '[%s] %s\n' "$(ios_setup_timestamp)" "$*"
}

ios_setup_warn()
{
    printf '[%s] WARNING: %s\n' "$(ios_setup_timestamp)" "$*" >&2
}

ios_setup_die()
{
    printf '[%s] ERROR: %s\n' "$(ios_setup_timestamp)" "$*" >&2
    exit 1
}

ios_setup_require_command()
{
    command -v "$1" >/dev/null 2>&1 || ios_setup_die "Required command is missing: $1"
}

ios_setup_require_file()
{
    [ -f "$1" ] || ios_setup_die "Required file is missing: $1"
}

ios_setup_require_dir()
{
    [ -d "$1" ] || ios_setup_die "Required directory is missing: $1"
}

ios_setup_pending_ninja_tasks()
{
    local build_dir=$1
    shift
    ninja -C "$build_dir" -n "$@" 2>/dev/null | awk 'NF { count += 1 } END { print count + 0 }'
}

ios_setup_run_ninja()
{
    local label=$1
    local build_dir=$2
    shift 2

    local pending
    pending=$(ios_setup_pending_ninja_tasks "$build_dir" "$@")
    ios_setup_log "$label: $pending pending tasks, $IOS_SETUP_JOBS jobs"

    if [ "$pending" -eq 0 ]; then
        ios_setup_log "$label: nothing to build; using existing artifacts"
        return 0
    fi

    python3 "$IOS_SETUP_SCRIPT_DIR/progress_runner.py" \
        --label "$label" \
        --total "$pending" \
        --single-line \
        -- ninja -C "$build_dir" -j "$IOS_SETUP_JOBS" "$@"
}

ios_setup_run_make()
{
    local label=$1
    local build_dir=$2
    local make_target=$3

    local pending
    pending=$(make -C "$build_dir" -n "$make_target" 2>/dev/null | awk 'NF { count += 1 } END { print count + 0 }')
    ios_setup_log "$label: $pending pending commands, $IOS_SETUP_JOBS jobs"

    if [ "$pending" -eq 0 ]; then
        ios_setup_log "$label: nothing to build; using existing artifacts"
        return 0
    fi

    python3 "$IOS_SETUP_SCRIPT_DIR/progress_runner.py" \
        --label "$label" \
        --total "$pending" \
        --count-lines \
        --single-line \
        -- make -C "$build_dir" -j "$IOS_SETUP_JOBS" "$make_target"
}

ios_setup_run_command()
{
    local label=$1
    shift
    local started_at
    started_at=$(date +%s)
    ios_setup_log "$label: started"
    "$@"
    ios_setup_log "$label: completed in $(ios_setup_format_duration $(( $(date +%s) - started_at )))"
}

ios_setup_default_target()
{
    printf '%s' "${IOS_SETUP_TARGET:-all}"
}
