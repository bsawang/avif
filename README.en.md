# AVIF Thumbnail Handler for Windows 10 Explorer

Native C++ COM Shell Extension that adds **AVIF thumbnail preview** to Windows 10 Explorer.

> **Status**: Working on Windows 10 22H2. Built with MSVC 2022.

---

## Windows Version Notes

| Version | AVIF Support |
|---------|--------------|
| **Windows 10** | ❌ No native AVIF thumbnail — **this tool is for you** |
| **Windows 11** | ✅ Native AVIF support built-in (no extra tools needed) |

Windows 11 has native AVIF codec support and shows thumbnails out of the box. This handler is primarily designed for **Windows 10 users** who want AVIF thumbnail preview without upgrading their OS.

---

## Part 1: Quick Start (End Users)

### Prerequisites

- Windows 10 (22H2 recommended)
- Administrator access (for COM registration)

### Installation

**Option A: Install Script (Recommended)**

1. Download the [latest release](https://github.com/bsawang/avif/releases)
2. Extract the archive
3. **Right-click** `install.bat` → **Run as Administrator**
4. Done — open any folder with `.avif` files to see thumbnails

**Option B: Manual Steps**

```cmd
:: Copy DLL
copy bin\AvifThumbCpp.dll C:\Windows\System32\

:: Copy decoder
mkdir "C:\Program Files\AvifThumbHandler"
copy bin\avifdec.exe "C:\Program Files\AvifThumbHandler\"

:: Register COM
regsvr32 C:\Windows\System32\AvifThumbCpp.dll

:: Restart Explorer
taskkill /f /im explorer.exe & start explorer.exe
```

### Clearing Thumbnail Cache

If thumbnails don't show immediately, run `clear_cache.bat` as Administrator.

### Troubleshooting

**Q: Thumbnails still not showing?**
- Check debug log: `C:\Windows\Temp\AvifThumbCpp.log`
- Clear thumbnail cache: `clear_cache.bat`
- Ensure `avifdec.exe` exists at `C:\Program Files\AvifThumbHandler\`

**Q: "File locked" when installing?**
- The DLL is in use because Explorer loads it in-process. The script restarts Explorer automatically. If it fails, close all Explorer windows and try again.

### How It Works

```
Explorer (in-process)
  └─ AvifThumbCpp.dll
       └─ IThumbnailProvider::GetThumbnail(cx=256)
            └─ avifdec.exe (subprocess)
                 └─ decodes .avif → PNG
            └─ GDI+ loads PNG → scales → HBITMAP
```

The handler uses `DisableProcessIsolation = 1` because it implements `IInitializeWithFile` instead of `IInitializeWithStream`. See [RESEARCH.en.md](RESEARCH.en.md) for full investigation details and developer reference.

## License

**Dual License** — see [LICENSE](LICENSE).

- ✅ **Non-commercial use** — free, with attribution required
- ❌ **Commercial use** — requires a separate license agreement; contact **bsawang@126.com**

This software incorporates third-party components: libavif (BSD), dav1d (BSD), libyuv (BSD), and IJG.

## Development Environment

This project was built entirely with AI-assisted programming:

| Tool | Version |
|------|---------|
| VS Code | Latest |
| Claude Code | Claude 3.5 Sonnet (2024-10-22) |
| DeepSeek | DeepSeek R1 (assisted code generation) |
| Compiler | MSVC 2022 (VS 17.x) |
| OS | Windows 10 Pro 22H2 |

---

[中文版](README.md)
