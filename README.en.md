# AVIF Thumbnail Handler for Windows 10 Explorer

![Windows 10](https://img.shields.io/badge/Windows_10-22H2-0078D6?logo=windows)
![C++](https://img.shields.io/badge/C++-COM-00599C?logo=cplusplus)
![MSVC](https://img.shields.io/badge/MSVC-2022-800080)
![GitHub Release](https://img.shields.io/github/v/release/bsawang/avif?logo=github)
![License](https://img.shields.io/badge/License-Non--Commercial_|_Dual-red)

**AVIF thumbnail preview for Windows 10 Explorer.** A native C++ COM Shell Extension using libavif inline decode — no external decoder dependencies.

> **Status**: Working on Windows 10 22H2. Built with MSVC 2022. Both Explorer and file dialogs work smoothly.
>
> [中文版](README.md)

---

## Windows Version Notes

| Version | AVIF Support |
|---------|--------------|
| **Windows 10** | ❌ No native AVIF thumbnail — **this tool is for you** |
| **Windows 11** | ✅ Native AVIF support built-in (no extra tools needed) |

Windows 11 has native AVIF codec support and shows thumbnails out of the box. This handler is primarily designed for **Windows 10 users**.

---

## Quick Start

### Prerequisites

- Windows 10 (22H2 recommended)
- Administrator access (for COM registration)

### Installation

**Option A: Install Script (Recommended)**

1. Download the [latest release](https://github.com/bsawang/avif/releases)
2. Extract the archive
3. **Right-click** `install.bat` → **Run as Administrator**
4. Done — open any folder with `.avif` files to see thumbnails
5. **Uninstall** — run `uninstall.bat` as Administrator

**Option B: Manual Steps**

```cmd
:: Copy DLL
copy bin\AvifThumbCpp.dll C:\Windows\System32\

:: Register COM
regsvr32 C:\Windows\System32\AvifThumbCpp.dll

:: Restart Explorer
taskkill /f /im explorer.exe & start explorer.exe
```

### Clearing Thumbnail Cache

If thumbnails don't show immediately, run `clear_cache.bat` as Administrator.

### How It Works

```
Explorer (in-process)
  └─ AvifThumbCpp.dll (1.9MB, libavif inline decode)
       └─ IThumbnailProvider::GetThumbnail(cx=256)
            └─ libavif C API decode → YUV → BGRA
            └─ GDI+ scale → HBITMAP
```

### Performance Comparison

| Version | Method | Per-thumbnail | External Dependencies |
|---------|--------|--------------|----------------------|
| v1 (legacy) | avifdec.exe subprocess | 200-500ms | avifdec.exe (12MB) |
| **v1.0.1 (current)** | **libavif inline decode** | **10-50ms** | **None** |

### Troubleshooting

**Q: "File locked" when installing?**
The script restarts Explorer automatically. If it fails, close all Explorer windows and try again.

**Q: File dialog (browser upload, etc.) slow when opening AVIF folders?**
v1.0.1 fixed this by removing the slow system PropertyHandler. Run `install.bat` to apply.

---

## Developer Reference

### Project Structure

```
publish/
├── bin/
│   └── AvifThumbCpp.dll      ← Pre-built DLL
├── src/
│   ├── dllmain.cpp            ← COM implementation (single file)
│   ├── avif/                  ← libavif headers
│   ├── exports.def            ← DLL exports
│   └── build.bat              ← MSVC build script
├── lib/
│   └── avif.lib               ← libavif + dav1d static lib
├── install.bat                ← Install script
└── clear_cache.bat            ← Clear thumbnail cache
```

### Interfaces

| Interface | Purpose |
|-----------|---------|
| `IInitializeWithFile` | Explorer init (file path) |
| `IInitializeWithStream` | dllhost init (stream data) |
| `IThumbnailProvider` | Thumbnail generation |
| `IExtractImage2` | Legacy fallback |

### Build Requirements

- Visual Studio 2022 Build Tools
  - Workload: "Desktop development with C++"
  - Windows SDK 10.0.26100.0+
- cmake, meson, ninja (`pip install cmake meson ninja`)
- nasm 2.16+

Full build guide in [RESEARCH.en.md](RESEARCH.en.md#83-building-from-source).

---

## License

**Dual License** — see [LICENSE](LICENSE).

- ✅ **Non-commercial use** — free, with attribution required
- ❌ **Commercial use** — requires a separate license agreement; contact **bsawang@126.com**

This software incorporates third-party components: libavif (BSD), dav1d (BSD), and IJG.

---

[中文版](README.md) | [Full Research Document](RESEARCH.en.md)
