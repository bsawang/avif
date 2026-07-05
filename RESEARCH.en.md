# AVIF Thumbnail Handler — Research & Investigation Record

> **Date**: 2026-07-05 (Evening update)
> **Environment**: Windows 10 Pro 22H2 (19045.4529)
> **Status**: ✅ AVIF thumbnails working (DisableProcessIsolation is the key)

---

## Table of Contents

1. [Background](#1-background)
2. [Explorer Thumbnail Mechanism](#2-explorer-thumbnail-mechanism)
3. [Attempts](#3-attempts)
   - 3.12 [MSVC 2022 Rebuild + IExtractImage2 Support](#312-msvc-2022-rebuild--iextractimage2-support)
   - 3.13 [PhotoMetadataHandler Call Failure Analysis](#313-photometadatahandler-call-failure-analysis)
   - 3.14 [WIC Decoder Path Verification](#314-wic-decoder-path-verification)
4. [Summary of All Approaches](#4-summary-of-all-approaches)
5. [Root Cause](#5-root-cause)
6. [Solution: DisableProcessIsolation](#6-solution-disableprocessisolation)
7. [Untried Directions](#7-untried-directions)

---

## 1. Background

There are many AI-generated `.avif` images under `H:\comfyUI\ref\`, and we want thumbnail previews directly in Windows Explorer.

## 2. Explorer Thumbnail Mechanism

(See morning document — summarizes how Explorer loads thumbnail providers via COM, the IThumbnailProvider interface, and the dllhost.exe COM Surrogate isolation mechanism.)

---

## 3. Attempts

### 3.1–3.11

See morning document. All 11 approaches failed. These included WIC decoder registration, TreatAs redirection, PerceivedType, ContentType, ProgID-based approaches, and various registry modifications.

---

### 3.12 MSVC 2022 Rebuild + IExtractImage2 Support

**Motivation**: Suspected MinGW-compiled DLL was incompatible with system COM infrastructure. vswhere couldn't find MSVC.

**Actions**:
1. Downloaded VS 2022 Build Tools (`vs_BuildTools.exe`) and silently installed to `C:\BuildTools`
2. Installed Windows SDK 10.0.26100.0 (includes wincodec.h etc.)
3. Fixed multiple bugs in `build.bat`:
   - `/DEF:"dllmain.cpp"` → correct `.def` file referencing
   - Removed `/NODEFAULTLIB` (caused missing CRT symbols)
   - Added `advapi32.lib` (registry API)
4. Added `exports.def` with `PRIVATE` export marking
5. Removed `__declspec(dllexport)` from source files, switched to `.def` file
6. Added `IExtractImage2` support to `avif_wic2.cpp` (`GetLocation` + `Extract` methods)
7. Fixed WIC decoder stub implementations:
   - `QueryCapability` → returns `WICBitmapDecoderCapabilityCanDecodeAllImages`
   - `GetContainerFormat` → returns `GUID_ContainerFormatHeif`

**Build Results**:
- `AvifThumbCpp.dll` — 184KB (was 143KB for MinGW)
- `AvifWIC.dll` — 154KB (was 147KB for MinGW)

**Test Results**:
- ✅ In-process COM activation works (no difference from MinGW)
- ✅ WIC decoder works via IWICImagingFactory API
- ✅ IShellItemImageFactory returns valid 256×256 thumbnails
- ❌ dllhost.exe still refuses to load the DLL
- ❌ PhotoMetadataHandler still doesn't recognize AVIF

**Conclusion**: Compiler choice (MSVC vs MinGW) is not the issue. The blockage is at the OS layer.

---

### 3.13 PhotoMetadataHandler Call Failure Analysis

**Observation**: Direct PhotoMetadataHandler loading test returns
```
Initialize: hr=0x00000000
GetThumbnail(256): hr=0xc00d36b4 (hbmp=NULL)
```

**Error Code Analysis**:
- Facility = 0x0D = 13 = `FACILITY_MEDIASERVER`
- Severity = 1 (error)
- This is NOT a WIC error (WIC facility = 0x898)

**Hypothesis**: PhotoMetadataHandler does NOT use the public `IWICImagingFactory` API. Internally it uses the **Media Foundation** decoding pipeline. This explains why the WIC decoder registers correctly but PhotoMetadataHandler ignores it — it never scans the WIC decoder category.

**Registry Verification**:
- `SystemFileAssociations\image\ShellEx\{e357fccd-...}` is owned by **TrustedInstaller**
- Administrators only have ReadKey permissions, cannot write
- Replacing the system image handler path is impossible

---

### 3.14 WIC Decoder Path Verification

**Key Finding**: The WIC decoder **works correctly** on Windows 10 22H2, but only through the public `IWICImagingFactory` API:

```
✅ IWICImagingFactory::CreateDecoderFromStream() → auto-detects AVIF successfully
✅ IWICImagingFactory::CreateDecoderFromFilename() → decodes successfully
✅ IWICImagingFactory::CreateComponentEnumerator(WICBitmapDecoders) → enumerates our decoder
```

The WIC infrastructure is open — third-party decoders can register and work. The problem is that Explorer's thumbnail pipeline **does not go through this API**.

---

## 4. Summary of All Approaches

| # | Approach | Type | Result | Failure Reason |
|---|----------|------|--------|----------------|
| 1–11 | See morning doc | Various | ❌ | — |
| 12 | MSVC 2022 Rebuild | Build | ❌ | Compiler-independent, OS guard |
| 13 | IExtractImage2 Implementation | COM Extension | ❌ | IExtractImage also goes through dllhost |
| 14 | Shell Extensions Approved | Registry | ❌ | Additional trust checks |
| 15 | **DisableProcessIsolation** | **Registry** | **✅** | **Key fix** |

## 5. Root Cause

### dllhost Process Isolation

Windows 10 loads thumbnail handlers in an isolated **dllhost.exe (COM Surrogate)** process by default. This isolated process only supports the `IInitializeWithStream` interface. Our implementation uses `IInitializeWithFile` (needed for the file path to spawn avifdec), causing COM to **silently fail** in the isolated process — the DLL is never loaded.

### Path Analysis

```
Path B: .avif\ShellEx\{e357fccd-...} (IThumbnailProvider)
  → Registration correct, but dllhost isolation rejects loading ⛔
  → Cause: uses IInitializeWithFile instead of IInitializeWithStream
```

### PhotoMetadataHandler Doesn't Use Our WIC Decoder

- Even though the WIC decoder is fully functional via the public `IWICImagingFactory` API
- PhotoMetadataHandler uses the Media Foundation pipeline internally
- Error `0xc00d36b4` is a MediaServer error (unrelated to this issue)

### WIC Decoder Registration Has No Effect on Thumbnail Pipeline

- WIC decoder registered at `HKLM\...\WIC\Decoders\.avif` is complete and functional
- But Explorer's thumbnail pipeline doesn't decode through `IWICImagingFactory`

---

## 6. Solution: DisableProcessIsolation

### Fix

Add `DisableProcessIsolation = 1` (REG_DWORD) under the CLSID registry key:

```
HKLM\SOFTWARE\Classes\CLSID\{DF4C9A6E-5B3A-4F2A-9E8C-7D1E3F2A5B0C}
    DisableProcessIsolation = 1 (REG_DWORD)
```

### Why It Works

Microsoft's official documentation [Building Thumbnail Providers](https://learn.microsoft.com/en-us/windows/win32/shell/building-thumbnail-providers) clearly states:

> *"If your thumbnail handler does not implement IInitializeWithStream, it must opt out of running in the isolated process. To opt out, set DisableProcessIsolation = 1."*

After setting this, Explorer loads our DLL directly in its own process, bypassing dllhost.exe. The `IInitializeWithFile` interface works normally.

### Final Result

- Explorer calls `IThumbnailProvider::GetThumbnail(cx=256)` directly
- Each call: avifdec.exe → PNG → GDI+ resize → HBITMAP
- All AVIF file thumbnails display correctly

---

## 7. Untried Directions

### 7.1 IExtractImage Path

The DLL already implements `IExtractImage2`, but IExtractImage also goes through dllhost isolation. `DisableProcessIsolation` already solves the problem.

### 7.2 Digital Signature / COM Permissions / DLL Injection / Win11 etc.

All other directions are unnecessary.

### 7.3 Caveats

- `DisableProcessIsolation` is intended for debugging/development only; Microsoft does not recommend it for production
- For our personal workstation this is acceptable
- If the DLL crashes, it may also crash Explorer (whereas with dllhost, only dllhost would crash)

---

[中文版](RESEARCH.md)
