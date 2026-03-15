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

### Mini vMac (no networking)

Mini vMac emulates a Mac Plus (System 6). Good for UI testing; no TCP/IP.

1. Get a Mini vMac build and a System 6 disk image, place them in `emulator/`:
   - `emulator/Mini vMac.app`
   - `emulator/6.08_40MB.img` (boot disk)
2. Run:
   ```sh
   make run
   ```
   This builds `Browsy.dsk` and opens Mini vMac with both disks.

### Basilisk II (with MacTCP networking)

Basilisk II emulates a Quadra 650 running System 7.5.3, with SLiRP networking
(NAT via host). This is required to test HTTP.

#### Setup (once)

1. Obtain these files (not included in the repo — too large):
   - `emulator/Quadra-650.ROM` — 32-bit Quadra 650 ROM (1 MB)
   - `emulator/System7.img` — System 7.5.3 disk image
   - `emulator/shared/MacTCP-2-0.6.dsk` — MacTCP 2.0.6 installer floppy

2. The prefs file at `emulator/basilisk/basilisk_ii_prefs` is preconfigured.
   Basilisk II reads `~/.basilisk_ii_prefs` on launch, so symlink it:

   ```sh
   ln -sf "$(pwd)/emulator/basilisk/basilisk_ii_prefs" ~/.basilisk_ii_prefs
   ```

3. Install MacTCP 2.0.6 inside System 7 (one-time setup):
   - Boot Basilisk II, mount `MacTCP-2-0.6.dsk`, run the installer.
   - Open MacTCP control panel, click **More...**, set:
     - Gateway: `10.0.2.2`
     - Domain: your domain or leave blank
     - DNS server: `10.0.2.3`
   - Select **Server** for IP address (DHCP-style via SLiRP).
   - Restart the emulator.

#### Running Browsy

```sh
make run-basilisk
```

This copies `Browsy.bin` to `emulator/shared/` and opens Basilisk II.

Inside Basilisk II, `Browsy.bin` is in the `Unix` shared drive. Double-click
it to launch. You can also mount `Browsy.dsk` directly from the desktop —
it appears as a floppy disk.

#### Network details (SLiRP)

| Address   | Purpose            |
| --------- | ------------------ |
| 10.0.2.2  | Host gateway       |
| 10.0.2.3  | DNS resolver       |
| 10.0.2.15 | Guest IP (typical) |

SLiRP provides outbound TCP only. Inbound connections are not supported.

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
