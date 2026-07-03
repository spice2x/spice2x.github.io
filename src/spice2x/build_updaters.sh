#!/usr/bin/env bash

# builds the standalone updater scripts from the shared PowerShell logic in
# update_spice.ps1. each output is a self-contained polyglot .bat: a small batch
# header (with the release channel baked in) followed by the shared PowerShell
# body after the marker line.
#
# usage: build_updaters.sh OUT_DIR
#   OUT_DIR  directory to write the generated .bat files into

set -eu

if [ $# -lt 1 ]; then
	echo "usage: build_updaters.sh OUT_DIR" >&2
	exit 1
fi

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PS_BODY="${SRC_DIR}/update_spice.ps1"
OUT_DIR="$1"

[ -f "$PS_BODY" ] || { echo "error: $PS_BODY not found" >&2; exit 1; }

MARKER='#___PS___'

gen() {
	channel="$1"; label="$2"; out="$3"
	{
		cat <<HEADER
@echo off
setlocal
REM ============================================================================
REM  spice2x updater  -  updates spice.exe, spice64.exe and spicecfg.exe in
REM  this folder to the latest spice2x release from GitHub.
REM
REM  release channel: ${label}
REM  make sure spice/spicecfg are NOT running before updating.
REM ============================================================================

REM expose the script folder + channel to the embedded PowerShell, then run the
REM PowerShell part: re-read this file and execute everything after the marker.
set "SPICE_DIR=%~dp0"
set "SPICE_CHANNEL=${channel}"
powershell -NoProfile -ExecutionPolicy Bypass -Command "\$m='#___'+'PS'+'___'; \$code=((Get-Content -LiteralPath '%~f0' -Raw) -split \$m)[-1]; Invoke-Expression \$code"
set "RC=%ERRORLEVEL%"
endlocal & exit /b %RC%

${MARKER}
HEADER
		cat "$PS_BODY"
	} | sed -e 's/\r$//' -e 's/$/\r/' > "${OUT_DIR}/${out}"
	echo "  generated ${OUT_DIR}/${out}"
}

echo "Generating updater scripts from $(basename "$PS_BODY")..."
gen "" "stable (pre-releases excluded)" "update_spice.bat"
gen "beta" "beta (newest, pre-releases included)" "update_spice_beta.bat"
