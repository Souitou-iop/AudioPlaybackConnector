# AudioPlaybackConnector

<p align="center">
  <strong>专为 Windows 10 与 11 打造的现代、轻量且全功能的蓝牙音频接收 (A2DP Sink) 管理工具。</strong>
</p>

<p align="center">
  <a href="README.md">English</a> | <strong>简体中文</strong>
</p>

<p align="center">
  <a href="https://github.com/Souitou-iop/AudioPlaybackConnector/releases"><img src="https://img.shields.io/github/v/release/Souitou-iop/AudioPlaybackConnector?color=blue&label=Release" alt="Release"></a>
  <a href="https://github.com/Souitou-iop/AudioPlaybackConnector/blob/master/LICENSE"><img src="https://img.shields.io/badge/License-MIT-green.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-0078D6.svg" alt="Platform">
  <img src="https://img.shields.io/badge/Architecture-x64%20%7C%20ARM64-orange.svg" alt="Architecture">
  <a href="https://github.com/ysc3839/AudioPlaybackConnector"><img src="https://img.shields.io/badge/Forked%20From-ysc3839%2FAudioPlaybackConnector-purple.svg" alt="Upstream"></a>
</p>

---

## 📖 项目简介

**AudioPlaybackConnector** 能够将你的 Windows 10 / 11 电脑变成一台高性能的蓝牙音频接收器（A2DP Sink）。你可以将**手机、平板、其他笔记本电脑**上的音乐、播客、游戏音效或语音通知，实时串流至电脑的扬声器或耳机中播放。

虽然微软从 Windows 10 (2004+) 开始内置了蓝牙 A2DP Sink 底层支持，但系统自身缺少完善的管理界面。本项目是基于原作者 [ysc3839/AudioPlaybackConnector](https://github.com/ysc3839/AudioPlaybackConnector) 的**全面现代化重构增强版本 (Modernized Fork)**，带来了多设备并发连接、AAC/SBC 编码识别与电量徽章、全动态深浅主题实时跟随、多语言热重载无缝切换以及丰富的 CLI 自动化控制等核心能力。

---

## 🎬 界面预览与动图演示

<table align="center">
  <tr>
    <td align="center" width="50%">
      <strong>📱 设备管理面板（左键点击托盘图标）</strong>
    </td>
    <td align="center" width="50%">
      <strong>⚙️ 设置与托盘菜单（右键点击托盘图标）</strong>
    </td>
  </tr>
  <tr>
    <td>
      <img src="Left%E2%80%91click%20Menu%20Demo.gif" alt="左键面板动图演示" width="100%" />
    </td>
    <td>
      <img src="Right%E2%80%91click%20Menu%20Demo.gif" alt="右键菜单动图演示" width="100%" />
    </td>
  </tr>
</table>

---

## ✨ 核心特性

### 🎧 音频与多设备协同
* **多设备并发连接与串流**：支持多台蓝牙设备同时连接电脑并同时发声（例如：平板看网课视频 + 手机接收微信语音消息），标题栏清晰展示容量状态（`[0/2]`、`[1/2]`、`[2/2]`）。
* **智能音频通道自动重路由**：基于 CoreAudio（`IMMNotificationClient`）实时监听默认输出声卡切换（如插拔耳机、切换音箱），秒级静默重定向，彻底告别“重连无声”问题。

### ⏯️ 深度系统整合与控制
* **Windows 10/11 SMTC 媒体控制联动**：深度接入系统媒体传输中心（`SystemMediaTransportControls`），锁屏界面、系统媒体悬浮窗同步显示当前蓝牙设备名称与播放状态。
* **键盘多媒体按键双向转发**：支持使用键盘物理多媒体按键（播放/暂停、上一曲、下一曲）反向控制手机/平板端的音乐播放。
* **硬件编码格式与电量徽章**：实时检测并高亮显示当前蓝牙连接的音频 Codec（高清 **AAC** 或标准 **SBC**），并实时显示设备剩余电量百分比（如 `🔋 85%`）。
* **⭐️ 星标首选常用设备**：一键置顶星标常用设备，快捷指令与双击快连绝对优先。

### 🎨 现代 Fluent 视觉与极致体验
* **原生 Acrylic 亚克力设计**：极简清爽的 Fluent 风格卡片布局，自带平滑阴影与右上角专属关闭按钮。
* **全动态实时深浅色主题跟随**：实时响应 Windows 主题切换（`WM_THEMECHANGED` / `WM_SETTINGCHANGE`），自适应高对比度前景色，无需重启软件。
* **友好空状态配对引导**：未配对设备时展示温馨提示卡片与 `[ ＋ 配对新设备 ]` 按钮，点击一键唤起系统蓝牙设置（`ms-settings:bluetooth`）。

### 🌐 6 种语言即时热重载
* **全面覆盖 6 种语言**：
  * 🌐 **跟随系统 (System Default)**
  * 🇨🇳 **简体中文 (zh-CN)**
  * 🇭🇰 **繁體中文 (zh-TW)**
  * 🇺🇸 **English (en-US)**
  * 🇯🇵 **日本語 (ja-JP)**
  * 🇰🇷 **한국어 (ko-KR)**
* **免重启热切换 (Hot-Reload)**：在托盘右键菜单中选择语言，内存中即时清空旧缓存并重载新语言二进制表，托盘菜单与 Fluent 面板瞬间刷新生效。

### ⚡️ 命令行扩展与自动化
* **丰富的 CLI 命令行参数**：支持通过 `/Status`、`/Toggle`、`/Connect`、`/Disconnect`、`/Show`、`/Exit` 实现快捷指令、Stream Deck 或 AutoHotkey 脚本联动。
* **毫秒级 JSON 状态输出**：执行 `AudioPlaybackConnector64.exe /Status` 即刻向终端输出包含连接状态、设备列表、电量与编码的标准 JSON 数据。

### 🛡️ 系统守护与电源选项
* **自动连接附近设备**：设备进入蓝牙信号范围时自动发起重连。
* **开机自启动**：一键设置注册表，开机无感常驻后台。
* **串流时阻止系统睡眠**：音频串流期间自动保持电脑唤醒状态。
* **禁用蓝牙绝对音量**：解决手机按音量键导致电脑总音量被强行改变的系统痛点。

---

## 📥 下载与使用

### 1. 下载程序
前往 [GitHub Releases](https://github.com/Souitou-iop/AudioPlaybackConnector/releases) 下载最新预编译版本：
* **`AudioPlaybackConnector64.exe`** — 适用于 64 位 Windows 10 / 11 (x64)
* **`AudioPlaybackConnectorArm64.exe`** — 适用于 ARM64 设备（Surface Pro, 骁龙 X Elite 等）

*(绿色便携版，单个 exe 直接运行，无需安装！)*

### 2. 快速上手
1. **配对设备**：打开 Windows **设置 > 蓝牙和其他设备**，配对你的手机、平板等设备。
2. **运行软件**：双击启动 `AudioPlaybackConnector64.exe`，任务栏右下角通知区域会出现软件托盘图标。
3. **建立连接**：鼠标**左键点击**托盘图标，在弹出的设备列表中点击 **连接 (Connect)**。
4. **尽情享受**：在手机上播放音频，即可直接从电脑扬声器/耳机输出！

---

## 💻 CLI 命令行使用指南

| 命令行参数 | 功能说明 |
| :--- | :--- |
| `AudioPlaybackConnector64.exe /Show` | 唤起并显示设备管理面板。 |
| `AudioPlaybackConnector64.exe /Toggle` | 快捷连接或断开首选 / 星标设备。 |
| `AudioPlaybackConnector64.exe /Connect [设备名称]` | 连接指定名称的设备（若未指定则连接首个可用设备）。 |
| `AudioPlaybackConnector64.exe /Disconnect [设备名称]` | 断开指定名称的设备（若未指定则断开全部连接）。 |
| `AudioPlaybackConnector64.exe /Status` | 向终端标准输出输出结构化的当前连接状态 **JSON**。 |
| `AudioPlaybackConnector64.exe /Exit` | 安全退出正在运行的后台程序。 |

#### `/Status` JSON 输出示例：
```json
{
  "running": true,
  "version": "1.0.0.4",
  "connectedCount": 1,
  "maxCapacity": 2,
  "devices": [
    {
      "name": "iPhone 15 Pro",
      "id": "Bluetooth#Bluetooth...",
      "connected": true,
      "isPlaying": true,
      "codec": "AAC",
      "battery": 85,
      "isStarred": true
    }
  ]
}
```

---

## ⚙️ 配置文件说明 (`AudioPlaybackConnector.json`)

软件会在自身所在目录下自动生成并维护 `AudioPlaybackConnector.json` 配置文件：

```json
{
  "reconnect": true,
  "runAtStartup": true,
  "autoConnectNearby": true,
  "preventSleepWhileStreaming": true,
  "multiDeviceMode": true,
  "showNotifications": true,
  "language": "auto",
  "starredDevices": [
    "Bluetooth#Bluetooth..."
  ]
}
```

---

## 🛠️ 源码编译

### 编译环境要求
* **Windows 10 / 11** (内部版本 19041 及以上)
* **Visual Studio 2022** 安装以下组件：
  * 使用 C++ 的桌面开发
  * C++20 标准支持（MSVC v143 工具集）
  * Windows 10/11 SDK (10.0.19041.0 或更高版本)
  * ARM64 生成工具（可选，用于 ARM64 架构编译）
* **Python 3.x**（用于执行 `translate/po2ymo.py` 生成二进制语言包）

### 编译步骤
1. 克隆代码仓库：
   ```bash
   git clone https://github.com/Souitou-iop/AudioPlaybackConnector.git
   cd AudioPlaybackConnector
   ```
2. 生成二进制多语言表：
   ```bash
   python translate/po2ymo.py
   ```
3. 使用 Visual Studio 2022 打开 `AudioPlaybackConnector.sln`，选择 `Release` 与目标架构（`x64` 或 `ARM64`），点击 **生成解决方案** (`Ctrl+Shift+B`) 即可。

---

## 🙏 特别鸣谢与致敬 (Acknowledgments)

本项目的发展离不开开源社区的卓越贡献：

* **衷心鸣谢原作者 [@ysc3839 (Richard Yu)](https://github.com/ysc3839)**：本项目基于原开源项目 [ysc3839/AudioPlaybackConnector](https://github.com/ysc3839/AudioPlaybackConnector) 进行开发。感谢原作者在 Windows A2DP Sink Win32 底层实现以及 XAML Islands 现代界面框架方面奠定的坚实基础与开源贡献！
* **[Microsoft Windows Implementation Libraries (WIL)](https://github.com/microsoft/wil)**：提供健壮、异常安全的 Windows C++ 封装。
* **Direct2D 与 Windows XAML Islands**：提供现代硬件加速渲染与 Win32 原生 Fluent 控件支持。
* **所有社区贡献者与翻译者**：感谢各位在多语言本地化、功能建议与测试中的支持！

---

## 📄 开源许可证

本项目基于 [MIT License](LICENSE) 开源。
