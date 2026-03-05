# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Focus

We are working on **SheepShaver** (`SheepShaver/`), a PowerPC Mac OS runtime environment that runs MacOS 7.5.2–9.0.4. The `BasiliskII/` directory is not our concern.

## Building SheepShaver (Unix/Linux)

```bash
cd SheepShaver/src/Unix
./autogen.sh       # generates configure script
./configure        # basic build; see options below
make -j$(nproc)
```

Common configure options:
- `--enable-jit` — enable JIT compiler (default: yes)
- `--enable-ppc-emulator=auto|kpx|dyngen|precompiled` — select PPC emulator backend
- `--with-gtk` — build GTK preferences GUI
- `--with-mon` — include cxmon debugger
- `--enable-vosf` — video-on-SEGV signals optimization
- `--enable-addressing=real|direct` — memory addressing mode (default: real)

## Architecture

### Source layout

| Path | Role |
|------|------|
| `src/*.cpp` | Platform-independent Mac emulation core |
| `src/include/` | Public interfaces between core and platform layers |
| `src/Unix/` | Unix/Linux platform backend |
| `src/kpx_cpu/` | Kheperix PPC emulator engine (primary) |
| `src/emul_ppc/` | Simple PPC interpreter fallback |
| `src/Unix/dyngen_precompiled/` | Precompiled dyngen ops (older backend) |
| `BasiliskII/src/CrossPlatform/` | Shared: `sigsegv.cpp`, `vm_alloc.cpp`, `video_blit.cpp` |

### Operating modes

SheepShaver tracks the current execution mode in `XLM_RUN_MODE` (Mac address 0x2810):

- **MODE_68K** (0): The ROM's own 68k emulator is running (most of MacOS is 68k code interpreted by the PPC ROM's nanokernel)
- **MODE_NATIVE** (1): Native PPC code is executing (MacOS PPC libraries, nanokernel, etc.)
- **MODE_EMUL_OP** (2): Inside a special EMUL_OP trap handler — host C++ code is running in response to a synthetic opcode encountered by the 68k emulator

### Memory map (Unix, EMULATED_PPC)

| Region | Address | Size | Notes |
|--------|---------|------|-------|
| Low Memory / XLMs | 0x0000–0x2FFF | 12 KB | MacOS low-memory globals; XLMs at 0x2800+ |
| Mac RAM | 0x10000000 (default) | configurable | `RAMBase`/`RAMBaseHost` |
| Mac ROM | 0x40800000 | 4 MB | `ROMBase`/`ROMBaseHost`; write-protected after patching |
| SheepMem | 0x60000000 | 512 KB | Emulator data, thunks, temp vars (see below) |
| Kernel Data | 0x68ffe000 and 0x5fffe000 | 8 KB | Nanokernel's data area, mapped at two addrs via SHM |
| DR Emulator | 0x68070000 | 64 KB | Dynamic recompilation emulator code |
| DR Cache | 0x69000000 | 512 KB | Cache for DR |

### XLM (Extra Low Memory) globals

Range 0x2800–0x28FF defined in `src/include/xlowmem.h`. Key entries:

- `XLM_SIGNATURE` (0x2800): `'Baah'` — lets code detect it's running under SheepShaver
- `XLM_RUN_MODE` (0x2810): current mode
- `XLM_IRQ_NEST` (0x2818): interrupt disable nesting counter (>0 = disabled)
- `XLM_68K_R25` (0x2814): saved 68k interrupt level (r25 of the 68k emulator's PPC registers)
- `XLM_EXEC_RETURN_OPCODE` (0x284c): the synthetic opcode that ends an `Execute68k()` call
- `XLM_ETHER_*`, `XLM_VIDEO_DOIO` (0x28B0+): function pointers to native driver entry points

### ROM loading and patching (`rom_patches.cpp`)

1. ROM file (`ROM` or `Mac OS ROM`) is read and decoded — supports: plain 4 MB binary, CHRP/LZSS compressed, MacOS 9.x parcel format.
2. ROM type is detected: `ROMTYPE_NEWWORLD` (MacOS 8.5+) vs. `ROMTYPE_GOSSAMER` vs. older.
3. `PatchROM()` modifies the ROM in-place before execution:
   - `patch_nanokernel_boot()`: patches boot entry point
   - `patch_68k_emul()`: redirects the 68k emulator's dispatch
   - `patch_nanokernel()`: patches interrupt and context-switch routines
   - `patch_68k()`: replaces Mac driver code with stubs containing `M68K_EMUL_OP_xxx` opcodes
4. After patching, the ROM is write-protected (`vm_protect`).

### Native function call mechanism (`thunks.cpp`, `src/include/thunks.h`)

SheepShaver must call host C++ functions from within the emulated PPC environment:

- **EMULATED_PPC path**: `NativeOpcode(selector)` returns a synthetic PPC opcode (primary opcode 6 with encoded selector). When `sheepshaver_cpu::execute_sheep()` encounters it, it calls `execute_native_op(selector)` which dispatches directly to host functions.
- **Real PPC path**: `NativeTVECT(selector)` / `NativeFunction(selector)` return addresses of function descriptor pairs (TVECTs) that MacOS can call with `bctrl`.
- `NativeRoutineDescriptor(selector)` returns a Routine Descriptor suitable for `CallUniversal()`.

All `NATIVE_*` selectors are enumerated in `thunks.h`. Implemented in `sheepshaver_glue.cpp`'s `execute_native_op()`.

### SheepMem shared memory region (`src/include/thunks.h`)

A 512 KB region at 0x60000000 (in Mac address space) is used as a shared scratch area:

- **Procedure region** (grows up from base): permanent 68k/PPC code stubs installed at init time
- **Data region** (grows down from top): temporary stack-like allocation; use `SheepVar`, `SheepVar32`, `SheepString`, `SheepArray<N>` RAII helpers
- A **zero page** (read-only, all-zero) sits between procedure and data regions

### The PPC CPU engine (`src/kpx_cpu/`)

`sheepshaver_cpu` inherits from `powerpc_cpu` (Kheperix engine) and adds:

- A custom "sheep" decoder entry for the synthetic opcode 6, dispatching to:
  - `EMUL_RETURN` (0): quit emulator
  - `EXEC_RETURN` (1): return from `Execute68k()`
  - `EXEC_NATIVE` (2): call a `NATIVE_*` handler
  - `EMUL_OP` (3+): enter EMUL_OP mode and call `EmulOp()`
- `execute_68k(entry, r)`: saves PPC context, sets up registers to run the 68k emulator (emulator dispatch table is in KernelData), executes via `execute()`, restores PPC context
- `execute_macos_code(tvect, nargs, args)`: calls a MacOS PPC routine via its TVECT
- `interrupt(entry)`: delivers a MacOS interrupt by setting up nanokernel registers and executing the nanokernel interrupt handler
- JIT support: `compile1()` handles "sheep" opcodes in the JIT compiler, generating direct host function calls

### Driver architecture

Mac hardware drivers are replaced by stubs embedded in the ROM or Name Registry. Each stub consists of 68k code containing `M68K_EMUL_OP_xxx` opcodes. When the 68k emulator encounters these, execution switches to `EmulOp()` (`emul_op.cpp`) which calls the appropriate host C++ function.

| Driver | Stub type | Source |
|--------|-----------|--------|
| `.Sony` (floppy) | 68k stub in ROM | `rom_patches.cpp` |
| Disk / CDROM | 68k stub in ROM | `rom_patches.cpp` |
| `.AppleSoundInput` | 68k stub in System rsrc | `rsrc_patches.cpp` |
| Video (`VidComp`) | PPC NDRV in Name Registry | `name_registry.cpp` |
| Ethernet (DLPI) | PPC NDRV in Name Registry | `name_registry.cpp` |
| Serial | Pure PPC native via thunks | `rom_patches.cpp` |

### Name Registry (`name_registry.cpp`)

NewWorld ROMs require an Open Firmware device tree. `DoPatchNameRegistry()` (called via `NATIVE_PATCH_NAME_REGISTRY` after boot) creates entries for:
- CPU node (with `cpu-version`, `clock-frequency`, etc.)
- Video card PCI node (installs the video NDRV binary from `VideoDriverStub.i`)
- Ethernet card PCI node (installs the ethernet NDRV from `EthernetDriverStub.i` or `EthernetDriverFull.i`)

### Resource patches (`rsrc_patches.cpp`)

`GetResource()` is intercepted via thunks. `check_load_invoc()` / `named_check_load_invoc()` are called after every resource load and can modify resources on the fly (e.g., patching the Sound Manager, installing `.AppleSoundInput`).

### Interrupt and threading model

Three pthreads run concurrently:

1. **Emulation thread** (`emul_thread`): the main thread; calls `jump_to_rom(ROMBase + 0x310000)` which enters the PPC emulation loop
2. **60Hz tick thread** (`tick_thread`): fires every ~16.6 ms; calls `SetInterruptFlag(INTFLAG_VIA)` + `TriggerInterrupt()`; also updates the Mac clock once per second
3. **NVRAM watchdog** (`nvram_thread`): saves XPRAM to disk every ~60 seconds if it changed

`TriggerInterrupt()` calls `ppc_cpu->trigger_interrupt()`, causing the emulation loop to call `HandleInterrupt()`. That function checks `XLM_RUN_MODE` and `XLM_IRQ_NEST` and delivers the interrupt in the appropriate way for the current mode.

### SIGSEGV handling

`sigsegv_handler()` in `sheepshaver_glue.cpp`:
- Passes VOSF screen faults to `Screen_fault_handler()`
- Ignores writes to the ROM (returns `SIGSEGV_RETURN_SKIP_INSTRUCTION`)
- Has hardcoded workarounds for specific MacOS 8.x ROM addresses that access non-existent hardware
- Ignores writes to the zero page
- If `ignoresegv` pref is set, skips all faults in Mac address space
- Otherwise: dumps registers + disassembly, enters mon debugger, quits

### Kernel Data structure

`KernelData` (at 0x68ffe000) is a 1024-word array used by the nanokernel. Key offsets (accessed via `kernel_data->v[offset >> 2]`):
- 0x65c: pointer to the current thread context block
- 0x660, 0x674, 0x67c: interrupt-related fields
- 0xf60–0xf6c (NewWorld) / 0xf80–0xf8c (older): PVR, CPU/bus/timebase clock frequencies

### Video

`video.cpp` is the generic layer; `video_x.cpp` (X11) or SDL video handles the platform display. The video driver is a PPC NDRV registered in the Name Registry. Driver I/O is dispatched via `NATIVE_VIDEO_DO_DRIVER_IO` → `VideoDoDriverIO()`. `gfxaccel.cpp` provides native QuickDraw acceleration hooks (`NQD_bitblt`, `NQD_fillrect`, `NQD_invrect`).

### Key macros and conventions

- `ReadMacInt8/16/32/64(addr)` / `WriteMacInt*(addr, v)`: Mac memory access (with address translation for EMULATED_PPC; direct dereference for real PPC)
- `Mac2HostAddr(addr)` / `Host2MacAddr(ptr)`: translate between Mac and host address spaces
- `PL(X)`: byte-swap a 32-bit literal for embedding in PPC instruction arrays (no-op on big-endian)
- `PW(X)`: same for 16-bit 68k instruction arrays
- `BUILD_SHEEPSHAVER_PROCEDURE(name)`: macro to lazily allocate a permanent code stub in SheepMem
- `D(bug(...))`: debug logging, compiled out when `DEBUG 0`
- `CallMacOS1..6(ptr, tvect, ...)`: call a MacOS routine from host code (via `execute_macos_code`)

## Modifications made (modern Linux compatibility)

### Audio backend (`src/Unix/audio_oss_esd.cpp`)

Added PulseAudio Simple API support. On modern Linux, OSS (`/dev/dsp`) and ESD are unavailable; PulseAudio is tried first.

- New `open_pulse()` function using `pa_simple_new()` / `pa_simple_write()` / `pa_simple_free()`
- `open_audio()` tries PulseAudio → ESD → OSS/DSP in that order
- `stream_func()` branches on `is_pulse_audio` to call `pa_simple_write()` instead of `write(audio_fd, ...)`
- Links against `-lpulse-simple -lpulse` (detected via `pkg-config libpulse-simple` in `configure.ac`)

### GTK4 prefs editor (`src/Unix/prefs_editor_gtk.cpp`)

- **Volumes tab**: replaced plain list with 3-column `GtkTreeView` — Location, CD-ROM (toggleable checkbox), Size
  - `add_to_volume_list()` uses `stat()` to detect block devices (auto CD-ROM) and compute file size
  - `on_cdrom_toggled()` callback makes the checkbox user-clickable for ISO files
- **GTK4 CheckButton API**: all `gtk_toggle_button_set/get_active()` on check buttons replaced with `gtk_check_button_set/get_active()` — in GTK4, `GtkCheckButton` is no longer a subclass of `GtkToggleButton`
- **`display_alert()`**: replaced GTK2 `gtk_main()` loop with GTK4 `GtkAlertDialog` + `GMainLoop`
- Removed `gtk_set_locale()` and changed `gtk_init(&argc, &argv)` → `gtk_init()` (GTK4 signatures)

### Build system (`configure.ac`, `Makefile.in`)

- `AC_HEADER_STDC` is a no-op in autoconf 2.70+ — replaced with unconditional `AC_DEFINE([STDC_HEADERS], [1], ...)` to prevent `#error "You don't have ANSI C header files."` at build time
- Added `srcdir = @srcdir@` to `Makefile.in` (was missing, broke `$(srcdir)/../deps` path)
- Moved `all: $(PROGS)` before `deps:` target in `Makefile.in` so `make` builds the emulator by default
- Added `--with-pulseaudio` option and `PKG_CHECK_MODULES([PULSE], [libpulse-simple])` detection

### Dependencies (`src/deps/`)

GTK4 4.18+ built from bundled sources in `src/deps/`. Key fixes needed when rebuilding:
- Build dir check: `[ ! -f "$bld/build.ninja" ]` (not `[ ! -d "$bld" ]`) to handle partial builds
- GTK4 meson option: `-Dvulkan=disabled` (no `glslc` on system)
- `deps/install/lib/pkgconfig/libdrm.pc` must exist (system has headers but no .pc file); create manually with `Cflags: -I/usr/include/drm`
- After GTK4 install, create canonical symlink: `ln -sf .../lib/x86_64-linux-gnu/pkgconfig/gtk4.pc .../lib/pkgconfig/gtk4.pc`
