#!/bin/sh

set -eu

EXPECTED_FLOPPY_SIZE=1474560
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
IMAGE_PATH="$REPO_ROOT/Browsy.dsk"
INFO_FILE=$(mktemp "${TMPDIR:-/tmp}/browsy-floppy.XXXXXX")

cleanup() {
	rm -f "$INFO_FILE"
}

trap cleanup EXIT INT TERM HUP

die() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

usage() {
	printf 'Usage: %s [diskN|/dev/diskN]\n' "$0" >&2
	exit 64
}

normalize_disk() {
	case "$1" in
		disk[0-9]*) printf '%s\n' "$1" ;;
		/dev/disk[0-9]*) printf '%s\n' "${1#/dev/}" ;;
		*) return 1 ;;
	esac
}

load_disk_info() {
	diskutil info -plist "/dev/$1" >"$INFO_FILE" 2>/dev/null
}

field() {
	plutil -extract "$1" raw -o - "$INFO_FILE" 2>/dev/null
}

field_or_default() {
	key=$1
	default_value=$2
	if value=$(field "$key"); then
		printf '%s\n' "$value"
	else
		printf '%s\n' "$default_value"
	fi
}

disk_is_safe_floppy() {
	disk=$1

	load_disk_info "$disk" || return 1

	# Refuse anything that is not an exact 1.44 MB physical removable disk.
	[ "$(field WholeDisk)" = "true" ] || return 1
	[ "$(field VirtualOrPhysical)" = "Physical" ] || return 1
	[ "$(field Internal)" = "false" ] || return 1
	[ "$(field Writable)" = "true" ] || return 1
	[ "$(field Removable)" = "true" ] || return 1
	[ "$(field Ejectable)" = "true" ] || return 1
	[ "$(field Size)" = "$EXPECTED_FLOPPY_SIZE" ] || return 1
}

print_disk_details() {
	disk=$1

	load_disk_info "$disk" || die "could not read info for /dev/$disk"

	printf 'Using /dev/%s\n' "$disk"
	printf '  Model: %s\n' "$(field_or_default IORegistryEntryName unknown)"
	printf '  Bus: %s\n' "$(field_or_default BusProtocol unknown)"
	printf '  Size: %s bytes\n' "$(field_or_default Size unknown)"
}

list_whole_disks() {
	diskutil list -plist \
		| plutil -extract AllDisks json -o - - \
		| tr -d '[]"' \
		| tr ',' '\n' \
		| sed 's/^[[:space:]]*//; s/[[:space:]]*$//' \
		| grep -E '^disk[0-9]+$'
}

discover_disk() {
	match_count=0
	selected_disk=
	selected_list=

	for disk in $(list_whole_disks); do
		if disk_is_safe_floppy "$disk"; then
			match_count=$((match_count + 1))
			selected_disk=$disk
			selected_list="${selected_list}${selected_list:+, }/dev/$disk"
		fi
	done

	case "$match_count" in
		1)
			printf '%s\n' "$selected_disk"
			return 0
			;;
		0)
			printf 'No suitable floppy disk was found automatically.\n' >&2
			printf 'Pass a disk manually after checking diskutil list, for example: %s disk17\n' "$0" >&2
			return 1
			;;
		*)
			printf 'Multiple floppy-like disks matched the safety checks: %s\n' "$selected_list" >&2
			printf 'Refusing automatic selection. Pass the intended disk explicitly.\n' >&2
			return 1
			;;
	esac
}

command -v diskutil >/dev/null 2>&1 || die "diskutil is required"
command -v plutil >/dev/null 2>&1 || die "plutil is required"
command -v sudo >/dev/null 2>&1 || die "sudo is required"
command -v dd >/dev/null 2>&1 || die "dd is required"

case $# in
	0)
		TARGET_DISK=$(discover_disk) || exit 1
		;;
	1)
		TARGET_DISK=$(normalize_disk "$1") || usage
		disk_is_safe_floppy "$TARGET_DISK" || die "/dev/$TARGET_DISK does not pass the floppy safety checks; expected a physical, removable, ejectable, writable whole disk with size $EXPECTED_FLOPPY_SIZE bytes"
		;;
	*)
		usage
		;;
esac

print_disk_details "$TARGET_DISK"

[ -f "$IMAGE_PATH" ] || die "image not found: $IMAGE_PATH"

sudo diskutil unmountDisk "$TARGET_DISK"
sudo dd if="$IMAGE_PATH" of="/dev/$TARGET_DISK" bs=512
sudo diskutil unmountDisk "$TARGET_DISK"
