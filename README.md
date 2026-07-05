# Windows 10 资源管理器 AVIF 缩略图支持

![Windows 10](https://img.shields.io/badge/Windows_10-22H2-0078D6?logo=windows)
![C++](https://img.shields.io/badge/C++-COM-00599C?logo=cplusplus)
![MSVC](https://img.shields.io/badge/MSVC-2022-800080)
![GitHub Release](https://img.shields.io/github/v/release/bsawang/avif?logo=github)
![License](https://img.shields.io/badge/License-Non--Commercial_|_Dual-red)

**Windows 10 AVIF 缩略图解决方案** — 原生 C++ COM Shell 扩展，为 Windows 10 资源管理器添加 AVIF 缩略图预览支持。基于 libavif 内联解码，无需依赖外部解码器。

> **状态**: Windows 10 22H2 测试通过，MSVC 2022 编译。资源管理器和文件对话框均正常工作。
>
> [English version](README.en.md)

---

## Windows 版本说明

| 版本 | AVIF 支持情况 |
|------|--------------|
| **Windows 10** | ❌ 无原生 AVIF 缩略图支持 — **本工具可以解决** |
| **Windows 11** | ✅ 自带原生 AVIF 支持，开箱即用 |

Windows 11 已内置 AVIF 编解码器支持，可直接显示缩略图。本工具主要面向 **Windows 10 用户**，无需升级系统即可获得 AVIF 缩略图预览。

---

## 快速开始

### 环境要求

- Windows 10（推荐 22H2）
- 管理员权限（COM 注册需要）

### 安装方法

**方式 A：安装脚本（推荐）**

1. 下载[最新发布包](https://github.com/bsawang/avif/releases)
2. 解压到任意目录
3. **右键** `install.bat` → **以管理员身份运行**
4. 完成 — 打开 AVIF 文件夹即可看到缩略图
5. **卸载** — 以管理员身份运行 `uninstall.bat`

**方式 B：手动安装**

```cmd
:: 复制 DLL
copy bin\AvifThumbCpp.dll C:\Windows\System32\

:: 注册 COM 组件
regsvr32 C:\Windows\System32\AvifThumbCpp.dll

:: 重启资源管理器
taskkill /f /im explorer.exe & start explorer.exe
```

### 清理缩略图缓存

如果缩略图没有立即显示，请以管理员身份运行 `clear_cache.bat`。

### 工作原理

```
资源管理器（进程内加载）
  └─ AvifThumbCpp.dll (1.9MB, libavif 内联解码)
       └─ IThumbnailProvider::GetThumbnail(cx=256)
            └─ libavif C API 解码 → YUV → BGRA
            └─ GDI+ 缩放 → HBITMAP
```

### 性能对比

| 版本 | 方式 | 每张耗时 | 外部依赖 |
|------|------|---------|---------|
| v1（旧版） | avifdec.exe 子进程 | 200-500ms | avifdec.exe (12MB) |
| **v2（当前）** | **libavif 内联解码** | **10-50ms** | **无** |

### 常见问题

**问：安装时提示文件被锁定？**
因为 Explorer 会加载 DLL，安装时可能提示文件占用。脚本会自动重启 Explorer。如果失败，关闭所有资源管理器窗口后重试。

**问：文件对话框（浏览器上传等）打开 AVIF 文件夹卡顿？**
v2 已修复此问题。如果还遇到，确认 PropertyHandler 已删除（运行 `install.bat` 时会自动处理）。

---

## 开发者参考

### 项目结构

```
publish/
├── bin/
│   └── AvifThumbCpp.dll      ← 编译好的 DLL（直接使用）
├── src/
│   ├── dllmain.cpp            ← COM 实现（单个源文件）
│   ├── avif/                  ← libavif 头文件
│   ├── exports.def            ← DLL 导出定义
│   └── build.bat              ← MSVC 构建脚本
├── lib/
│   └── avif.lib               ← libavif + dav1d 静态库
├── install.bat                ← 安装脚本
└── clear_cache.bat            ← 清理缩略图缓存
```

### 构建要求

- Visual Studio 2022 Build Tools
  - 工作负载："使用 C++ 的桌面开发"
  - Windows SDK 10.0.26100.0+
- cmake、meson、ninja（`pip install cmake meson ninja`）
- nasm 2.16+

完整构建指南见 [RESEARCH.md](RESEARCH.md#83-从源码构建)。

### 接口实现

| 接口 | 用途 |
|------|------|
| `IInitializeWithFile` | 资源管理器初始化（文件路径） |
| `IInitializeWithStream` | dllhost 初始化（数据流） |
| `IThumbnailProvider` | 缩略图生成 |
| `IExtractImage2` | 旧版兼容 |

---

## 许可证

**双授权模式** — 详见 [LICENSE](LICENSE)。

- ✅ **非商业用途** — 免费使用，需保留版权声明
- ❌ **商业用途** — 需取得授权，请联系 **bsawang@126.com**

本工具使用了 libavif（BSD）、dav1d（BSD）及 IJG 等第三方组件。

---

[English version](README.en.md) | [完整研究文档](RESEARCH.md)
