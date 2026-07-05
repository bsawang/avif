# AVIF Explorer Thumbnail Handler — Research & Investigation Record

> **Date**: 2026-07-06 (Final update)
> **Environment**: Windows 10 Pro 22H2 (19045.4529)
> **Version**: v1.0.1 — libavif inline decode + file dialog performance fix
>
> [中文版](RESEARCH.md)

---

## Table of Contents

1. [Background](#1-background)
2. [Explorer Thumbnail Mechanism](#2-explorer-thumbnail-mechanism)
3. [Attempts](#3-attempts)
   - 3.12 [MSVC 2022 Rebuild + IExtractImage2 Support](#312-msvc-2022-rebuild--iextractimage2-support)
   - 3.13 [PhotoMetadataHandler Call Failure Analysis](#313-photometadatahandler-call-failure-analysis)
   - 3.14 [WIC Decoder Path Verification](#314-wic-decoder-path-verification)
   - 3.15 [libavif Inline Decode (replacing avifdec subprocess)](#315-libavif-inline-decode-replacing-avifdec-subprocess)
   - 3.16 [PropertyHandler Causes File Dialog Hang](#316-propertyhandler-causes-file-dialog-hang)
4. [Summary of All Approaches](#4-summary-of-all-approaches)
5. [Root Cause](#5-root-cause)
6. [Solution](#6-solution)
7. [Untried Directions](#7-untried-directions)
8. [Appendix: Developer Reference](#8-appendix-developer-reference)

---

## 1. Background

AVIF (.avif) images lack thumbnail preview support in Windows 10 File Explorer. This project implements a native COM Shell Extension to provide thumbnails.

## 2. Explorer Thumbnail Mechanism

Windows loads thumbnail providers via COM Shell Extensions. Key aspects:
- **IThumbnailProvider** (modern, recommended interface)
- **IExtractImage2** (legacy fallback)
- Runs in **dllhost.exe** (COM Surrogate) isolated process by default
- The isolated process only supports `IInitializeWithStream`

---

## 3. Attempts

### 3.1–3.11

See morning document. All 11 approaches failed — WIC decoder registration, TreatAs redirection, PerceivedType, ContentType, ProgID modifications, etc.

---

### 3.12 MSVC 2022 Rebuild + IExtractImage2 Support

**Conclusion**: Compiler choice (MSVC vs MinGW) is not the root issue. Blocked at the OS layer.

### 3.13 PhotoMetadataHandler Call Failure Analysis

Returns `0xc00d36b4` (MediaServer error) — uses internal Media Foundation pipeline, NOT IWICImagingFactory.

### 3.14 WIC Decoder Path Verification

WIC decoders work via `IWICImagingFactory` API, but Explorer's thumbnail pipeline doesn't use this API.

---

### 3.15 libavif Inline Decode (replacing avifdec subprocess)

**Motivation**: Original `CreateProcess("avifdec.exe")` approach took 200-500ms per thumbnail. After adding `IInitializeWithStream`, stream data could be decoded directly without subprocess.

**Actions**:
1. Built libavif v1.4.2 + dav1d v1.5.3 as static library (4.3MB)
2. Used CMake + Meson + Ninja with `AVIF_CODEC_DAV1D=ON`
3. Replaced `CreateProcess` with libavif C API: `avifDecoderCreate` → `avifDecoderSetIOMemory` → `avifDecoderParse` → `avifDecoderNextImage` → `avifImageYUVToRGB`
4. Added `IInitializeWithStream` — reads stream into memory, decodes directly (no temp files)
5. DLL grew from 184KB → 1.9MB (inline decoder, no external executables)

**Result**: Decode time from 200-500ms down to 10-50ms. Eliminated avifdec.exe dependency.

---

### 3.16 PropertyHandler Causes File Dialog Hang

**Symptoms**: File dialog (Common File Dialog / IFileOpenDialog) hangs 6-7 seconds when opening AVIF folders, showing "Processing..." message. File Explorer is not affected.

**Root Cause**:
1. Setting `PerceivedType=image` causes Windows to associate a property handler with `.avif`
2. `PropertyHandlers\.avif` → `PhotoMetadataHandler.dll` → `MSHEIF.dll` (Media Foundation decoder)
3. File dialogs read file properties (dimensions, EXIF) for each file, triggering this slow path
4. The MF decoder takes 6-7 seconds for first-time initialization
5. File Explorer doesn't read image properties, so it's unaffected

**Fix**: Delete `PropertyHandlers\.avif` registry entry. Thumbnails don't need this handler.

**Result**: File dialog opens instantly.

---

## 4. Summary of All Approaches

| # | Approach | Type | Result | Failure Reason |
|---|----------|------|--------|----------------|
| 1–11 | See morning doc | Various | ❌ | — |
| 12 | MSVC 2022 Rebuild | Build | ❌ | Compiler-independent, OS guard |
| 13 | IExtractImage2 Implementation | COM Extension | ❌ | IExtractImage also goes through dllhost |
| 14 | Shell Extensions Approved | Registry | ❌ | Additional trust checks |
| 15 | **DisableProcessIsolation** | **Registry** | **✅** | **Key fix** |
| 16 | **libavif inline decode** | **Code** | **✅** | 10x speed vs subprocess |
| 17 | **Delete PropertyHandler** | **Registry** | **✅** | Fix file dialog hang |

## 5. Root Cause

### Core Issue: dllhost Process Isolation

Windows 10 loads thumbnail handlers in isolated dllhost.exe by default. dllhost only supports `IInitializeWithStream`. Our `IInitializeWithFile` implementation fails silently in dllhost.

### File Dialog Slowness

`PerceivedType=image` triggers the Windows PhotoMetadataHandler (→ MSHEIF.dll, Media Foundation). File dialogs read properties for each file, triggering slow MF decoder loading. File Explorer bypasses property reading.

### Subprocess Overhead

avifdec.exe spawning costs 200-500ms per thumbnail. Replaced with libavif inline decode at 10-50ms.

## 6. Solution

### 6.1 DisableProcessIsolation

```reg
HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}
    DisableProcessIsolation = 1 (REG_DWORD)
```

Explorer loads our DLL in-process, bypassing dllhost.

### 6.2 libavif Inline Decode

Static linking of libavif + dav1d. Direct C API calls in-process:
- `avifDecoderCreate()` → decode → `avifImageYUVToRGB()` → BGRA pixels

### 6.3 Delete PropertyHandler

```
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\PropertySystem\PropertyHandlers\.avif
```

Removed to prevent file dialog from triggering slow property reads.

## 7. Untried Directions

### 7.1 Digital Signature / COM Permissions / Win11 etc.

All unnecessary — the above solutions work reliably on Windows 10 22H2.

### 7.2 Caveats

- `DisableProcessIsolation` is intended for debugging/development; Microsoft does not recommend it for production
- A DLL crash may crash Explorer (vs. only crashing dllhost with isolation)

---

## 8. Appendix: Developer Reference

### 8.1 Architecture

```
Explorer (in-process)
  └─ AvifThumbCpp.dll (1.9MB, libavif inline decode)
       └─ IInitializeWithFile / IInitializeWithStream
       └─ IThumbnailProvider::GetThumbnail(cx=256)
            └─ libavif C API decode → YUV
            └─ avifImageYUVToRGB → BGRA
            └─ GDI+ scale → HBITMAP
```

| Interface | Usage |
|-----------|-------|
| `IInitializeWithFile` | Explorer path (via DisableProcessIsolation) |
| `IInitializeWithStream` | dllhost isolated path |
| `IThumbnailProvider` | Thumbnail generation (primary path) |
| `IExtractImage2` | Legacy fallback |

### 8.2 Registry Settings

| Key | Value |
|-----|-------|
| `CLSID\{DF4C9A6E-...C}\InprocServer32` (default) | `C:\Windows\System32\AvifThumbCpp.dll` |
| `CLSID\{...}\InprocServer32\ThreadingModel` | `Apartment` |
| `CLSID\{...}\DisableProcessIsolation` | `1` (REG_DWORD) |
| `.avif\ShellEx\{e357fccd-...}` | `{DF4C9A6E-...C}` (IThumbnailProvider) |
| `.avif\ShellEx\{BB2E617C-...}` | `{DF4C9A6E-...C}` (IExtractImage fallback) |

**Note**: `PropertyHandlers\.avif` is intentionally deleted — not needed.

### 8.3 Building from Source

**Requirements:**
- Visual Studio 2022 Build Tools (or full VS 2022)
  - Workload: "Desktop development with C++"
  - Windows SDK 10.0.26100.0+
- cmake, meson, ninja (`pip install cmake meson ninja`)
- nasm 2.16+ (https://www.nasm.us/)
- libavif source v1.4.2 (included in `../libavif_src/`)

**Build libavif static library:**
```cmd
mkdir build & cd build
cmake .. -G "Ninja" -DAVIF_BUILD_APPS=OFF -DAVIF_BUILD_TESTS=OFF ^
    -DAVIF_CODEC_DAV1D=ON -DAVIF_LOCAL_DAV1D=ON ^
    -DCMAKE_C_FLAGS_RELEASE="/MD /O2 /GS-"
ninja
copy avif.lib ..\..\publish\lib\
```

**Build DLL:**
```cmd
cd src
build.bat
```

**Output:** `AvifThumbCpp.dll` — 1.9MB thumbnail handler with libavif inline decode

### 8.4 Internals

- Dual initialization: `IInitializeWithFile` (Explorer) and `IInitializeWithStream` (dllhost)
- Stream data read into `avifRWData` buffer, decoded by libavif directly (no temp files)
- Non-AVIF files delegated to system's `PhotoMetadataHandler.dll`
- Output: GDI+-scaled HBITMAP matching requested thumbnail size
- Source: single file `src/dllmain.cpp` (~630 lines)

### 8.5 Debugging

Log location: `%TEMP%\AvifThumbCpp.log` (dllhost path) or `C:\Windows\Temp\AvifThumbCpp.log` (Explorer in-process path).

---

[中文版](RESEARCH.md)
