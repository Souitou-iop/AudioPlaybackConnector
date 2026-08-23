# AudioPlaybackConnector

<p align="center">
  <strong>A modern, lightweight, and feature-rich Bluetooth Audio (A2DP Sink) receiver for Windows 10 & 11.</strong>
</p>

<p align="center">
  <strong>English</strong> | <a href="README.zh_CN.md">简体中文</a>
</p>

<p align="center">
  <a href="https://github.com/Souitou-iop/AudioPlaybackConnector/releases"><img src="https://img.shields.io/github/v/release/Souitou-iop/AudioPlaybackConnector?color=blue&label=Release" alt="Release"></a>
  <a href="https://github.com/Souitou-iop/AudioPlaybackConnector/blob/master/LICENSE"><img src="https://img.shields.io/badge/License-MIT-green.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-0078D6.svg" alt="Platform">
  <img src="https://img.shields.io/badge/Architecture-x64%20%7C%20ARM64-orange.svg" alt="Architecture">
  <a href="https://github.com/ysc3839/AudioPlaybackConnector"><img src="https://img.shields.io/badge/Forked%20From-ysc3839%2FAudioPlaybackConnector-purple.svg" alt="Upstream"></a>
</p>

---

## 📖 Introduction

**AudioPlaybackConnector** turns your Windows 10 / 11 PC into a high-performance Bluetooth audio receiver (A2DP Sink). Stream music, podcasts, game audio, or voice notifications from your **phone, tablet, or another laptop** straight to your computer's speakers or headphones.

While Microsoft introduced Bluetooth A2DP Sink in Windows 10 (2004+), native management options are minimal. This project is a **heavily modernized, feature-packed fork** of the original [ysc3839/AudioPlaybackConnector](https://github.com/ysc3839/AudioPlaybackConnector), bringing multi-device concurrent streaming, Bluetooth Codec & battery badges, real-time light/dark theme adaptation, dynamic multi-language switching, and powerful CLI automation.

---

## 🎬 Previews & Demos

<table align="center">
  <tr>
    <td align="center" width="50%">
      <strong>📱 Device Management Panel (Left-Click Tray)</strong>
    </td>
    <td align="center" width="50%">
      <strong>⚙️ Settings & Context Menu (Right-Click Tray)</strong>
    </td>
  </tr>
  <tr>
    <td>
      <img src="Left%E2%80%91click%20Menu%20Demo.gif" alt="Left-click Menu Demo" width="100%" />
    </td>
    <td>
      <img src="Right%E2%80%91click%20Menu%20Demo.gif" alt="Right-click Menu Demo" width="100%" />
    </td>
  </tr>
</table>

---

## ✨ Key Features

### 🎧 Audio & Multi-Device
* **Multi-Device Concurrent Streaming**: Connect multiple Bluetooth sources simultaneously (e.g. tablet playing video + phone receiving voice messages) with live capacity badges (`[0/2]`, `[1/2]`, `[2/2]`).
* **Smart Audio Re-routing**: Automatically detects default output audio device changes (`IMMNotificationClient`) and rebinds active streams in real-time—eliminating silent stream bugs when plugging/unplugging headphones.

### ⏯️ System Integration & Controls
* **Windows 10/11 SMTC Media Control**: Fully synced with Windows System Media Transport Controls (lock screen, system media flyouts, and taskbar). Displays device names and playing status.
* **Physical Media Key Relay**: Press keyboard Play/Pause, Next Track, and Previous Track to remotely control media playback on your phone or tablet.
* **Hardware Codec & Battery Badges**: Real-time detection of high-quality **AAC** or standard **SBC** audio codecs, along with live Bluetooth battery level percentage (`🔋 85%`).
* **⭐️ Star & Pin Preferred Devices**: Mark favorite devices with a single click to pin them to the top of the list for quick connection.

### 🎨 Modern Fluent UI & UX
* **Acrylic Blur Design**: Clean, modern Fluent UI flyout with crisp typography, smooth card animations, and a dedicated close button.
* **Real-Time Light / Dark Theme Sync**: Automatically adapts foreground contrast, icons, and XAML Islands background to system theme changes (`WM_THEMECHANGED` / `WM_SETTINGCHANGE`) without restarting.
* **Friendly Empty State Guide**: If no devices are paired, a clean guide card with a `[ ＋ Pair New Device ]` button lets you jump directly into Windows Bluetooth Settings (`ms-settings:bluetooth`).

### 🌐 Instant Multi-Language Hot-Reload
* **6 Supported Languages**:
  * 🌐 **System Default (跟随系统)**
  * 🇨🇳 **Simplified Chinese (简体中文)**
  * 🇭🇰 **Traditional Chinese (繁體中文)**
  * 🇺🇸 **English (en-US)**
  * 🇯🇵 **Japanese (日本語)**
  * 🇰🇷 **Korean (한국어)**
* **Zero-Restart Switcher**: Select any language from the tray menu and watch the entire UI refresh instantly.

### ⚡️ CLI & Automation
* **Rich Command-Line Interface**: Automate connections or integrate with Stream Deck, PowerShell, or AutoHotkey via commands like `/Status`, `/Toggle`, `/Connect`, `/Disconnect`, `/Show`, and `/Exit`.
* **JSON State Output**: Execute `AudioPlaybackConnector64.exe /Status` to retrieve instant JSON diagnostics.

### 🛡️ System & Power Options
* **Auto-Reconnect Nearby Devices**: Automatically re-establishes connection when your devices come back into Bluetooth range.
* **Run on Startup**: Easily toggles Windows startup registry integration.
* **Prevent Sleep While Streaming**: Keeps your PC awake while audio is actively streaming.

---

## 📥 Installation & Usage

### 1. Download
Download the latest pre-compiled binary from [GitHub Releases](https://github.com/Souitou-iop/AudioPlaybackConnector/releases):
* **`AudioPlaybackConnector64.exe`** — for 64-bit Windows 10 / 11 (x64)
* **`AudioPlaybackConnectorArm64.exe`** — for ARM64 devices (Surface Pro, Snapdragon X Elite, etc.)

*(No installation required. Portable single executable!)*

### 2. Quick Start
1. **Pair your device**: Open Windows **Settings > Bluetooth & devices** and pair your phone, tablet, or audio source.
2. **Launch the App**: Run `AudioPlaybackConnector64.exe`. An icon will appear in your system notification tray.
3. **Connect**: Left-click the tray icon and click **Connect** on your device card.
4. **Enjoy**: Play audio on your phone and listen to it seamlessly through your PC!

---

## 💻 CLI Automation Reference

AudioPlaybackConnector supports rich command-line parameters for background control and scripting:

| Command Argument | Description |
| :--- | :--- |
| `AudioPlaybackConnector64.exe /Show` | Shows the device management flyout panel. |
| `AudioPlaybackConnector64.exe /Toggle` | Connects or disconnects the preferred / starred device. |
| `AudioPlaybackConnector64.exe /Connect [Name]` | Connects to a specific device by name (or first available). |
| `AudioPlaybackConnector64.exe /Disconnect [Name]` | Disconnects a specific device (or all devices). |
| `AudioPlaybackConnector64.exe /Status` | Outputs current connection and device status in structured **JSON** to stdout. |
| `AudioPlaybackConnector64.exe /Exit` | Closes the running application instance. |

#### Example `/Status` JSON Output:
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

## ⚙️ Configuration (`AudioPlaybackConnector.json`)

The application automatically creates and manages `AudioPlaybackConnector.json` next to the executable:

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

## 🛠️ Building from Source

### Prerequisites
* **Windows 10 / 11** (Build 19041 or newer)
* **Visual Studio 2022** with:
  * Desktop development with C++
  * C++20 standard support (MSVC v143 toolset)
  * Windows 10/11 SDK (10.0.19041.0 or newer)
  * ARM64 build tools (optional, for ARM64 builds)
* **Python 3.x** (for generating binary translation tables via `translate/po2ymo.py`)

### Build Steps
1. Clone the repository:
   ```bash
   git clone https://github.com/Souitou-iop/AudioPlaybackConnector.git
   cd AudioPlaybackConnector
   ```
2. Generate binary translation resources:
   ```bash
   python translate/po2ymo.py
   ```
3. Open `AudioPlaybackConnector.sln` in Visual Studio 2022, select `Release` configuration and `x64` or `ARM64`, then press **Build Solution** (`Ctrl+Shift+B`).

---

## 🙏 Acknowledgments & Credits

This project builds upon the tremendous work of the open-source community:

* **Special Thanks to [@ysc3839 (Richard Yu)](https://github.com/ysc3839)**: Original creator of [ysc3839/AudioPlaybackConnector](https://github.com/ysc3839/AudioPlaybackConnector). Without his pioneering work on Windows A2DP Sink Win32 implementation and XAML Islands hosting, this modernized version would not have been possible.
* **[Microsoft Windows Implementation Libraries (WIL)](https://github.com/microsoft/wil)**: For robust, exception-safe Windows C++ wrappers.
* **Direct2D & Windows XAML Islands**: For modern hardware-accelerated rendering and Fluent UI controls on Win32.
* **All Community Contributors & Translators**: Thank you to everyone who contributed suggestions, localization, and feedback!

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).
