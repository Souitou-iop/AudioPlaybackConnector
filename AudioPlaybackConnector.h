#pragma once

#include "resource.h"

using namespace winrt::Windows::Data::Json;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media;
using namespace winrt::Windows::Media::Audio;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Controls::Primitives;
using namespace winrt::Windows::UI::Xaml::Hosting;
using namespace winrt::Windows::UI::Xaml::Media;
namespace fs = std::filesystem;

inline FontIcon CreateFontIcon(std::wstring_view glyph, double fontSize = 0)
{
	FontIcon icon;
	icon.FontFamily(FontFamily(L"Segoe Fluent Icons, Segoe MDL2 Assets"));
	icon.Glyph(glyph);
	if (fontSize > 0)
	{
		icon.FontSize(fontSize);
	}
	return icon;
}

constexpr UINT WM_NOTIFYICON = WM_APP + 1;
constexpr UINT WM_CONNECTDEVICE = WM_APP + 2;
constexpr UINT WM_DEFAULT_AUDIO_DEVICE_CHANGED = WM_APP + 3;
constexpr UINT WM_REFRESH_AUDIO = WM_APP + 4;
constexpr UINT WM_UPDATE_DEVICE_PANEL = WM_APP + 5;
constexpr UINT WM_CLI_COMMAND = WM_APP + 6;

constexpr WPARAM CLI_CMD_SHOW = 1;
constexpr WPARAM CLI_CMD_CONNECT = 2;
constexpr WPARAM CLI_CMD_DISCONNECT = 3;
constexpr WPARAM CLI_CMD_TOGGLE = 4;
constexpr WPARAM CLI_CMD_EXIT = 5;

constexpr UINT_PTR IDT_AUDIO_METER = 1001;

struct ConnectedDeviceInfo
{
	DeviceInformation device{ nullptr };
	AudioPlaybackConnection connection{ nullptr };
	std::wstring name;
};

inline HINSTANCE g_hInst = nullptr;
inline HWND g_hWnd = nullptr;
inline HWND g_hWndXaml = nullptr;
inline Canvas g_xamlCanvas = nullptr;
inline MenuFlyout g_xamlMenu = nullptr;
inline Flyout g_deviceFlyout = nullptr;
inline StackPanel g_deviceListPanel = nullptr;
inline TextBlock g_panelBadgeText = nullptr;
inline Button g_panelDisconnectAllBtn = nullptr;
inline Button g_panelRefreshAudioBtn = nullptr;
inline winrt::Windows::Foundation::Collections::IVectorView<DeviceInformation> g_cachedDevices = nullptr;
inline FocusState g_menuFocusState = FocusState::Unfocused;

inline std::unordered_map<std::wstring, ConnectedDeviceInfo> g_audioPlaybackConnections;
inline std::unordered_map<std::wstring, double> g_deviceVolumes;
inline std::unordered_map<std::wstring, std::wstring> g_deviceErrorMessages;
inline std::unordered_map<std::wstring, TextBlock> g_deviceStatusTextBlocks;
inline HICON g_hIconLight = nullptr;
inline HICON g_hIconDark = nullptr;
inline NOTIFYICONDATAW g_nid = {
	.cbSize = sizeof(g_nid),
	.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP,
	.uCallbackMessage = WM_NOTIFYICON,
	.uVersion = NOTIFYICON_VERSION_4
};
inline NOTIFYICONIDENTIFIER g_niid = {
	.cbSize = sizeof(g_niid)
};
inline UINT WM_TASKBAR_CREATED = 0;
inline bool g_reconnect = false;
inline bool g_runAtStartup = false;
inline bool g_autoConnectNearby = false;
inline bool g_preventSleepWhileStreaming = true;
inline bool g_multiDeviceMode = false;
inline bool g_enableMediaKeyForwarding = true;
inline bool g_enableConnectionNotifications = true;
inline bool g_isAudioPlaying = false;
inline std::wstring g_preferredDeviceId;
inline std::wstring g_currentDefaultAudioEndpointId;

// SMTC
inline winrt::Windows::Media::SystemMediaTransportControls g_smtc = nullptr;
inline winrt::event_token g_smtcButtonToken;

inline int GetMaxSupportedA2dpStreams()
{
	return g_multiDeviceMode ? 2 : 1;
}

// Startup reconnect device IDs (only if g_reconnect is enabled)
inline std::vector<std::wstring> g_startupReconnectDevices;

// Devices that dropped in the current session (for auto-reconnect on return)
inline std::unordered_set<std::wstring> g_lostConnectionsInCurrentSession;

// Currently connecting device IDs
inline std::unordered_set<std::wstring> g_connectingDeviceIds;

inline HANDLE g_mmcssHandle = nullptr;
inline DeviceWatcher g_deviceWatcher = nullptr;

void UpdateTrayTooltip();
void ReopenAudioConnections();
void DisconnectAllDevices();
void ToggleLastConnectedDevice();
void ConnectPreferredOrLastDevice();
winrt::fire_and_forget ConnectDeviceByNameOrId(std::wstring target);
void UpdateAudioThreadPriority(bool enable);
void UpdatePowerLock(bool hasConnections);
void SetupDeviceWatcher(bool enable);
void ShowDevicePanel();
void UpdateHeaderBadgeUI();
void UpdateDevicePanelUI();
void SetDeviceVolume(std::wstring_view deviceId, float volume);
float GetDeviceVolume(std::wstring_view deviceId);
void CheckAudioMeter();
void ExitApp();

int GetBatteryPercentFromDevice(const DeviceInformation& dev);
void SetupSmtc(HWND hWnd);
void UpdateSmtcState(bool hasConnections, bool isPlaying, std::wstring_view deviceName = L"");
void ShowTrayNotification(std::wstring_view title, std::wstring_view message);
std::wstring GetDeviceCodecName(const DeviceInformation& dev);
std::wstring GetStatusJsonString();
winrt::fire_and_forget CheckForUpdatesAsync(bool manualTrigger);

#include "Util.hpp"
#include "I18n.hpp"
#include "SettingsUtil.hpp"
#include "Direct2DSvg.hpp"
