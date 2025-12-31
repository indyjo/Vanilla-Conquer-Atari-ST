# ATARILIB - Atari ST Platform Library

This directory contains Atari ST-specific implementations corresponding to the Windows-specific code in `WIN32LIB`.

## Purpose

The `ATARILIB` folder is the Atari ST equivalent of `WIN32LIB`, providing platform-specific implementations for:
- Graphics rendering (replacing DirectDraw)
- Audio (replacing DirectSound)
- Input handling (keyboard, mouse)
- File I/O
- Networking
- System services (timers, memory management)
- Window management (Atari GEM/AES)

## Structure

The file structure mirrors `WIN32LIB` to maintain compatibility. Each module needs to be ported to use Atari ST/MiNT APIs instead of Windows APIs.

## Porting Status

| Module | WIN32LIB File(s) | Atari ST Equivalent | Status |
|--------|----------------|---------------------|--------|
| **Graphics** | `ddraw.cpp/h`, `gbuffer.cpp/h`, `buffer.cpp/h` | Atari VDI, XBIOS | TODO |
| **Audio** | `audio.h`, sound system | Atari YM2149, DMA sound | TODO |
| **Input** | `keyboard.cpp/h`, `mouseww.cpp` | Atari IKBD, mouse events | TODO |
| **File I/O** | `file.h`, `rawfile.h`, `fastfile.h` | Atari GEMDOS, MiNT | TODO |
| **Networking** | `wsa.cpp/h`, `wincomm.h` | MiNT sockets | TODO |
| **Timers** | `timer.cpp/h` | Atari VBL, MiNT timers | TODO |
| **Memory** | `mem.cpp`, `alloc.cpp` | Atari memory management | TODO |
| **Fonts** | `font.cpp/h`, `loadfont.cpp` | Atari VDI fonts | TODO |
| **Palettes** | `palette.cpp/h`, `loadpal.cpp` | Atari VDI palette | TODO |
| **Shapes** | `shape.h`, `iconset.cpp`, `getshape.cpp` | Custom rendering | TODO |
| **Windows** | `windows.cpp`, `winhide.cpp` | Atari GEM/AES | TODO |
| **Assembly** | `*.asm` files | m68k assembly | TODO |

## Key Differences from Windows

### Graphics
- **Windows**: DirectDraw surfaces, hardware acceleration
- **Atari ST**: VDI (Video Device Interface), XBIOS, or custom framebuffer
- **Resolution**: Typically 320x200, 640x200, 640x400 (ST/STE/TT)

### Audio
- **Windows**: DirectSound, multiple channels
- **Atari ST**: YM2149 PSG (3 channels), DMA sound (STE+), or external sound cards

### Input
- **Windows**: Windows message queue (WM_KEYDOWN, WM_MOUSEMOVE, etc.)
- **Atari ST**: IKBD interrupts, GEM event messages

### File System
- **Windows**: Win32 file API (CreateFile, ReadFile, etc.)
- **Atari ST**: GEMDOS (Fopen, Fread, etc.) or MiNT (POSIX-like)

### Networking
- **Windows**: WinSock (WSA functions)
- **Atari ST**: MiNT sockets (BSD-style)

### Memory
- **Windows**: Virtual memory, heap allocation
- **Atari ST**: Physical memory, Malloc/Free (GEMDOS) or MiNT malloc

## Implementation Notes

1. **Header Compatibility**: Headers should maintain the same interface as WIN32LIB where possible, using `#ifdef` to switch between platforms.

2. **Function Signatures**: Try to match WIN32LIB function signatures to minimize changes in main game code.

3. **Assembly Code**: x86 assembly files need to be rewritten for m68k architecture.

4. **DirectX Replacement**: DirectDraw/DirectSound have no direct Atari equivalent - custom implementations needed.

5. **Window Management**: Replace Windows windowing with Atari GEM/AES or direct framebuffer access.

## Build Integration

The Makefile should be updated to:
- Include `ATARILIB` when building for Atari ST target
- Exclude `WIN32LIB` when building for Atari ST target
- Use appropriate compiler flags for Atari ST

## References

- Atari ST Programmer's Reference
- GEM Programmer's Guide
- MiNT Documentation
- VDI Programming Guide
- XBIOS Reference

