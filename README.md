# Windows 10 资源管理器 AVIF 缩略图支持

原生 C++ COM Shell 扩展，为 Windows 10 资源管理器添加 **AVIF 缩略图预览** 支持。

> **状态**: Windows 10 22H2 测试通过，MSVC 2022 编译。
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

本实现使用 `IInitializeWithFile` 接口，因此需要设置 `DisableProcessIsolation = 1`。完整开发者参考（架构、源码构建、注册表配置）见 [RESEARCH.md](RESEARCH.md)。

---

[English version](README.en.md)
