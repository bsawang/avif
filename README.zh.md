# Windows 10 资源管理器 AVIF 缩略图支持

原生 C++ COM Shell 扩展，为 Windows 10 资源管理器添加 **AVIF 缩略图预览** 支持。基于 libavif 内联解码，性能较 v1 子进程方案提升 10 倍。

> **状态**: Windows 10 22H2 测试通过，MSVC 2022 编译。

---

## Windows 版本说明

| 版本 | AVIF 支持情况 |
|------|--------------|
| **Windows 10** | ❌ 无原生 AVIF 缩略图支持 — **本工具可以解决** |
| **Windows 11** | ✅ 自带原生 AVIF 支持，开箱即用 |

---

## 第一部分：快速开始（普通用户）

### 安装方法

**方式 A：安装脚本（推荐）**

1. 下载[最新发布包](https://github.com/bsawang/avif/releases)
2. 解压到任意目录
3. **右键** `install.bat` → **以管理员身份运行**
4. 完成

**方式 B：手动安装**

```cmd
copy bin\AvifThumbCpp.dll C:\Windows\System32\
regsvr32 C:\Windows\System32\AvifThumbCpp.dll
taskkill /f /im explorer.exe & start explorer.exe
```

### 清理缩略图缓存

如果缩略图没有立即显示，请以管理员身份运行 `clear_cache.bat`。

### 工作原理

```
资源管理器（进程内加载）
  └─ AvifThumbCpp.dll (1.9MB)
       └─ IThumbnailProvider::GetThumbnail(cx=256)
            └─ libavif C API 解码 → YUV → BGRA
            └─ GDI+ 缩放 → HBITMAP
```

### 性能对比

| 版本 | 方式 | 每张耗时 | 外部依赖 |
|------|------|---------|---------|
| v1（旧） | avifdec.exe 子进程 | 200-500ms | avifdec.exe (12MB) |
| **v1.0.1（当前）** | **libavif 内联解码** | **10-50ms** | **无** |

### 常见问题

**问：文件对话框（浏览器上传等）打开 AVIF 文件夹卡顿？**
v1.0.1 已解决。`install.bat` 会自动删除导致卡顿的系统属性处理器。

---

## 第二部分：开发者参考

### 项目结构

```
publish/
├── bin/AvifThumbCpp.dll    ← 编译好的 DLL
├── src/
│   ├── dllmain.cpp          ← COM 实现（单个源文件）
│   ├── avif/                ← libavif 头文件
│   ├── exports.def          ← DLL 导出定义
│   └── build.bat            ← MSVC 构建脚本
├── lib/avif.lib             ← libavif + dav1d 静态库（4.3MB）
├── install.bat              ← 安装脚本
└── clear_cache.bat          ← 清理缩略图缓存
```

### 架构

| 接口 | 说明 |
|------|------|
| `IInitializeWithFile` | 资源管理器使用（文件路径初始化） |
| `IInitializeWithStream` | dllhost 隔离进程使用（数据流初始化） |
| `IThumbnailProvider` | 缩略图生成（主要路径） |
| `IExtractImage2` | 旧版兼容 |

非 AVIF 文件委托给系统的 `PhotoMetadataHandler.dll` 处理。

### 注册表配置

| 注册表路径 | 值 |
|-----------|-----|
| `CLSID\{DF4C9A6E-...C}\InprocServer32`（默认值） | `C:\Windows\System32\AvifThumbCpp.dll` |
| `CLSID\{...}\InprocServer32\ThreadingModel` | `Apartment` |
| `CLSID\{...}\DisableProcessIsolation` | `1` (REG_DWORD) |
| `.avif\ShellEx\{e357fccd-...}` | `{DF4C9A6E-...C}` (IThumbnailProvider) |
| `.avif\ShellEx\{BB2E617C-...}` | `{DF4C9A6E-...C}` (IExtractImage 兼容) |

### 从源码构建

**构建要求：**
- Visual Studio 2022 Build Tools
  - 工作负载："使用 C++ 的桌面开发"
  - Windows SDK 10.0.26100.0+
- cmake、meson、ninja（`pip install cmake meson ninja`）
- nasm 2.16+

**构建 libavif 静态库：**
```cmd
mkdir build & cd build
cmake .. -G "Ninja" -DAVIF_BUILD_APPS=OFF -DAVIF_BUILD_TESTS=OFF ^
    -DAVIF_CODEC_DAV1D=ON -DAVIF_LOCAL_DAV1D=ON ^
    -DCMAKE_C_FLAGS_RELEASE="/MD /O2 /GS-"
ninja
copy avif.lib ..\..\publish\lib\
```

**构建 DLL：**
```cmd
cd src
build.bat
```

**输出：** `AvifThumbCpp.dll` — 1.9MB

### 调试

日志位置：`%TEMP%\AvifThumbCpp.log` 或 `C:\Windows\Temp\AvifThumbCpp.log`

---

## 许可证

**双授权模式** — 详见 [LICENSE](LICENSE)。

- ✅ **非商业用途** — 免费使用，需保留版权声明
- ❌ **商业用途** — 需取得授权，请联系 **bsawang@126.com**

---
