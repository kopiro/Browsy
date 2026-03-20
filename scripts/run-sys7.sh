#!/bin/sh

set -eu

if [ "$#" -ne 5 ]; then
	printf 'Usage: %s BASILISK_BIN PREFS AUTOLAUNCH BOOT_DELAY OPEN_DELAY\n' "$0" >&2
	exit 64
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
BASILISK_BIN=$1
PREFS_PATH=$2
AUTOLAUNCH=$3
BOOT_DELAY=$4
OPEN_DELAY=$5
BROWSY_DISK=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)/Browsy.dsk

launch_autolaunch_helper() {
	if [ "$AUTOLAUNCH" != "1" ]; then
		return
	fi

	if [ ! -f "$BROWSY_DISK" ]; then
		printf 'warning: skipping System 7 auto-launch because %s is missing\n' "$BROWSY_DISK" >&2
		return
	fi

	if ! command -v osascript >/dev/null 2>&1; then
		printf 'warning: skipping System 7 auto-launch because osascript is unavailable\n' >&2
		return
	fi

	(
		if ! osascript "$SCRIPT_DIR/autolaunch-sys7.applescript" "$BOOT_DELAY" "$OPEN_DELAY"; then
			printf 'warning: automatic Browsy launch failed; open it manually inside Basilisk II\n' >&2
		fi
	) &
}

launch_autolaunch_helper
exec "$BASILISK_BIN" --config "$PREFS_PATH"
