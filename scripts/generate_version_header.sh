#!/usr/bin/env bash
set -euo pipefail

export LC_ALL=C

usage() {
  cat <<'EOF'
Usage: generate_version_header.sh [OPTIONS]

Generates include/common/version.h from build metadata.
Without --output the header is written to stdout.

Options:
  --name NAME        Program name
  --version VERSION  Version string, including commit/dirty suffix
  --toolchain TEXT   Compiler/toolchain description
  --unicorn TEXT     Unicorn version description (may be empty)
  --chips TEXT       Available chips
  --bundled TEXT     Bundled firmware summary
  -o, --output PATH  Write the header to PATH instead of stdout
  -h, --help         Show this help
EOF
}

die() {
  printf 'generate_version_header.sh: %s\n' "$*" >&2
  exit 1
}

escape() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

name=""
version=""
toolchain=""
unicorn=""
chips=""
bundled=""
output=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --name)
      [[ $# -ge 2 ]] || die "missing value for $1"
      name="$2"
      shift 2
      ;;
    --version)
      [[ $# -ge 2 ]] || die "missing value for $1"
      version="$2"
      shift 2
      ;;
    --toolchain)
      [[ $# -ge 2 ]] || die "missing value for $1"
      toolchain="$2"
      shift 2
      ;;
    --unicorn)
      [[ $# -ge 2 ]] || die "missing value for $1"
      unicorn="$2"
      shift 2
      ;;
    --chips)
      [[ $# -ge 2 ]] || die "missing value for $1"
      chips="$2"
      shift 2
      ;;
    --bundled)
      [[ $# -ge 2 ]] || die "missing value for $1"
      bundled="$2"
      shift 2
      ;;
    -o|--output)
      [[ $# -ge 2 ]] || die "missing value for $1"
      output="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
done

[[ -n "$name" ]] || die "missing --name"
[[ -n "$version" ]] || die "missing --version"
[[ -n "$toolchain" ]] || die "missing --toolchain"
[[ -n "$chips" ]] || die "missing --chips"
[[ -n "$bundled" ]] || die "missing --bundled"

build_date="$(date '+%Y-%m-%d %H:%M:%S %Z')"
build_ts="$(date '+%s')"

tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

{
  printf '#ifndef COMMON_VERSION_H\n'
  printf '#define COMMON_VERSION_H\n\n'
  printf '#define GSCEMU_NAME "%s"\n' "$(escape "$name")"
  printf '#define GSCEMU_VERSION "%s"\n' "$(escape "$version")"
  printf '#define GSCEMU_TOOLCHAIN "%s"\n' "$(escape "$toolchain")"
  printf '#define GSCEMU_UNICORN "%s"\n' "$(escape "$unicorn")"
  printf '#define GSCEMU_CHIPS "%s"\n' "$(escape "$chips")"
  printf '#define GSCEMU_BUNDLED_FIRMWARE "%s"\n' "$(escape "$bundled")"
  printf '#define GSCEMU_BUILD_DATE "%s"\n' "$(escape "$build_date")"
  printf '#define GSCEMU_BUILD_UNIX_TS "%s"\n' "$build_ts"
  printf '\n#endif /* COMMON_VERSION_H */\n'
} >"$tmp"

if [[ -n "$output" ]]; then
  [[ ! -d "$output" ]] || die "output is a directory: $output"
  mkdir -p "$(dirname -- "$output")"
  chmod 644 "$tmp"
  mv -- "$tmp" "$output"
  trap - EXIT
else
  cat -- "$tmp"
fi
