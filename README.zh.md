# Windows 10 资源管理器 AVIF 缩略图支持

原生 C++ COM Shell 扩展，为 Windows 10 资源管理器添加 **AVIF 缩略图预览** 支持。

> **状态**: Windows 10 22H2 测试通过，MSVC 2022 编译。

---

## Windows 版本说明

| 版本 | AVIF 支持情况 |
|------|--------------|
| **Windows 10** | ❌ 无原生 AVIF 缩略图支持 — **本工具可以解决** |
| **Windows 11** | ✅ 自带原生 AVIF 支持，开箱即用 |

Windows 11 已内置 AVIF 编解码器支持，可直接显示缩略图。本工具主要面向 **Windows 10 用户**，无需升级系统即可获得 AVIF 缩略图预览。

---

## 第一部分：快速开始（普通用户）

### 环境要求

- Windows 10（推荐 22H2）
- 管理员权限（COM 注册需要）

### 安装方法

**方式 A：安装脚本（推荐）**

1. 下载[最新发布包](https://github.com/bsawang/avif/releases)
2. 解压到任意目录
3. **右键** `install.bat` → **以管理员身份运行**
4. 完成 — 打开 AVIF 文件夹即可看到缩略图

**方式 B：手动安装**

```cmd
:: 复制 DLL
copy bin\AvifThumbCpp.dll C:\Windows\System32\

:: 复制解码器
mkdir "C:\Program Files\AvifThumbHandler"
copy bin\avifdec.exe "C:\Program Files\AvifThumbHandler\"

:: 注册 COM 组件
regsvr32 C:\Windows\System32\AvifThumbCpp.dll

:: 重启资源管理器
taskkill /f /im explorer.exe & start explorer.exe
```

### 清理缩略图缓存

如果缩略图没有立即显示，请以管理员身份运行 `clear_cache.bat`。

### 常见问题

**问：缩略图还是不显示？**
- 检查调试日志：`C:\Windows\Temp\AvifThumbCpp.log`
- 清理缩略图缓存：`clear_cache.bat`
- 确认解码器存在：`C:\Program Files\AvifThumbHandler\avifdec.exe`

**问：安装时提示文件被锁定？**
因为 Explorer 会加载 DLL，安装时可能提示文件占用。脚本会自动重启 Explorer。如果失败，请关闭所有资源管理器窗口后重试。

### 工作原理

```
资源管理器（进程内加载）
  └─ AvifThumbCpp.dll
       └─ IThumbnailProvider::GetThumbnail(cx=256)
            └─ avifdec.exe（子进程）
                 └─ 解码 .avif → PNG
            └─ GDI+ 加载 PNG → 缩放 → 输出位图
```

本实现使用 `IInitializeWithFile` 接口，因此需要设置 `DisableProcessIsolation = 1`。详细探索过程见 [RESEARCH.zh.md](RESEARCH.zh.md)。

---

## 第二部分：开发者参考

### 架构说明

本方案包含两个 COM 组件：

| 组件 | 源文件 | 用途 |
|------|--------|------|
| **缩略图提供器** | `src/dllmain.cpp` | COM ShellEx：`IThumbnailProvider` + `IExtractImage2` + `IInitializeWithFile` |
| **WIC 解码器** | `src/avif_wic2.cpp` | WIC 位图解码器（已注册但缩略图路径不使用） |

缩略图通过启动 **avifdec.exe** 子进程生成（而非直接链接 libavif），保持 DLL 小巧，COM 架构简单。

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
- Visual Studio 2022 Build Tools（或完整版 VS 2022）
  - 工作负载："使用 C++ 的桌面开发"
  - Windows SDK 10.0.26100.0+
- libavif 工具（`avifdec.exe`）— 已包含在 `bin/` 目录

**构建：**

```cmd
cd src
build.bat
```

构建脚本会自动检测 MSVC 安装路径。

**构建产物：**
- `AvifThumbCpp.dll` — 缩略图处理器
- `AvifWIC.dll` — WIC 解码器（可选，缩略图不需要）

调试时可查看跟踪日志：`C:\Windows\Temp\AvifThumbCpp.log`

### 实现细节

- 实现三个 COM 接口以获得最大兼容性：`IThumbnailProvider`（现代）、`IExtractImage2`（旧版兼容）和 `IInitializeWithFile`（初始化）
- 非 AVIF 文件委托给系统的 `PhotoMetadataHandler.dll` 处理
- WIC 解码器（`AvifWIC.dll`）是独立组件，可通过 `IWICImagingFactory` API 工作，但**不**被资源管理器缩略图管道使用

### 关键发现

所有失败方案的根本原因是 **dllhost 进程隔离**：

1. Windows 10 默认将缩略图处理器加载到隔离的 `dllhost.exe` 进程中运行
2. 隔离进程只支持 `IInitializeWithStream` 接口
3. 我们的实现使用 `IInitializeWithFile`，导致静默失败
4. 设置 `DisableProcessIsolation = 1` 后，Explorer 直接在自身进程加载 DLL，绕过 dllhost

### 许可证

MIT — 详见 [LICENSE](LICENSE)。

---

[← 返回语言选择](README.md) | [English version](README.en.md)
