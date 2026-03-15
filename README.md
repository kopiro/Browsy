# Browsy

A web browser for classic Mac OS (System 6/7), targeting 68k Macs.

![Browsy screenshot](https://cloud.githubusercontent.com/assets/95347/3683770/631346c6-12ed-11e4-8031-6242d7e36cfc.png)

## Dependencies

- [Retro68](https://github.com/autc04/Retro68/) — 68k cross-compiler
- [c-streams](https://github.com/clehner/c-streams) — streaming I/O
- [http-parser](https://github.com/joyent/http-parser) — HTTP parsing

## Building

### Prerequisites

- Docker (tested with Docker Desktop; Colima also works)
- `hfsutils` on the host for `.dsk` image creation (`brew install hfsutils`)
- `hfsprogs` inside the container (included in the Docker image)

The build uses a custom Docker image (`retro68-nopie`) that patches Retro68's
`Elf2Mac` and `Rez` tools to run as non-PIE binaries, which is required for
Rosetta 2 emulation on Apple Silicon.

### Build the Docker image (once)

```sh
docker build -f Dockerfile.retro68 -t retro68-nopie .
```

This clones Retro68, rebuilds `Elf2Mac` and `Rez` with `-no-pie`, and
installs `hfsutils` inside the container.

### Fetch dependencies

```sh
make deps
```

This clones `http-parser` and `c-streams` into `dep/`.

### Build the app binary

```sh
make docker-build
```

Produces `Browsy.bin` (MacBinary III format, runnable on classic Mac OS).

### Build a floppy disk image

```sh
make docker-dsk
```

Produces `Browsy.dsk` — an 800 KB HFS floppy image containing `Browsy.bin`.

The disk image can be written to a real floppy (1.44 MB HD or 800 KB DD) with:

Find the correct disk node with `diskutil list` before running `dd`. Replace
`diskN` with the raw disk device (e.g. `disk3`, not `disk3s1`). **Eject the
disk first** with `diskutil unmountDisk /dev/diskN`.

```sh
# macOS - if disk14 is your floppy
sudo diskutil unmountDisk disk14 && sudo dd if=Browsy.dsk of=/dev/disk14 bs=512 && sudo diskutil unmountDisk disk14
```

On Linux:

```sh
sudo dd if=Browsy.dsk of=/dev/sdX bs=512
```

## Testing

### System 6 with Mini vMac (no networking)

Mini vMac emulates a Mac Plus (System 6). Good for UI testing; no TCP/IP.

```sh
make run-sys6
```

Opens Basilisk II with System 6. `Browsy.dsk` mounts as a floppy containing
both Browsy and `page.html`. Navigate to `file:///Browsy/page.html`.

### System 7 with Basilisk II (networking with MacTCP)

Basilisk II emulates a 68k Mac.

Emulates a Quadra 650 running System 7.5.3 with SLiRP networking (NAT via
host). Required to test HTTP.

```sh
make run-sys7
```

## Stuff used / how it works

- **Retro68**: GCC-based cross-compiler producing 68k Mac apps. The linker
  (`Elf2Mac`) outputs MacBinary or raw resource forks. `Rez` appends compiled
  resources (menus, icons, etc.).
- **MacTCP**: Apple's TCP/IP stack for System 6/7. Browsy uses the `TCPiopb`
  parameter block interface (`PBControl`) and the DNR glue (`StrToAddr`,
  `AddrToStr`) loaded from the MacTCP driver resource file.
- **c-streams**: Provides a streaming read/write abstraction over MacTCP
  connections and files.
- **http-parser**: Incremental HTTP response parser; feeds data chunk by chunk.
- **HTML parser**: Custom streaming parser in `src/parser.c` that renders into
  a TextEdit field with styled text (fonts, sizes, bold/italic/underline).

## TODO

- Scrolling
- Images
- Forms
- CSS (basic)
- Chunked transfer encoding (fix edge cases)
