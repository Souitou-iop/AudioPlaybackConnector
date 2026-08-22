#pragma once

#include "resource.h"

using namespace winrt::Windows::Data::Json;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media::Audio;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Controls::Primitives;
using namespace winrt::Windows::UI::Xaml::Hosting;
using namespace winrt::Windows::UI::Xaml::Media;
namespace fs = std::filesystem;

constexpr UINT WM_NOTIFYICON = WM_APP + 1;
constexpr UINT WM_CONNECTDEVICE = WM_APP + 2;
constexpr UINT WM_DEFAULT_AUDIO_DEVICE_CHANGED = WM_APP + 3;
constexpr UINT WM_REFRESH_AUDIO = WM_APP + 4;
constexpr UINT WM_UPDATE_DEVICE_PANEL = WM_APP + 5;

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
inline FocusState g_menuFocusState = FocusState::Unfocused;
inline std::unordered_map<std::wstring, ConnectedDeviceInfo> g_audioPlaybackConnections;
inline std::unordered_map<std::wstring, double> g_deviceVolumes;
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
inline std::wstring g_currentDefaultAudioEndpointId;

// Devices to restore on cold start (only if g_reconnect is enabled)
inline std::vector<std::wstring> g_startupReconnectDevices;

// Devices that were active in the current running session and got dropped/lost
inline std::unordered_set<std::wstring> g_lostConnectionsInCurrentSession;

// Connecting in progress set
inline std::unordered_set<std::wstring> g_connectingDeviceIds;

inline HANDLE g_mmcssHandle = nullptr;
inline DeviceWatcher g_deviceWatcher = nullptr;

void UpdateTrayTooltip();
void ReopenAudioConnections();
void ToggleLastConnectedDevice();
void UpdateAudioThreadPriority(bool enable);
void UpdatePowerLock(bool hasConnections);
void SetupDeviceWatcher(bool enable);
void ShowDevicePanel();
void UpdateDeviceListUI(const winrt::Windows::Foundation::Collections::IVectorView<DeviceInformation>& devices);
void SetDeviceVolume(std::wstring_view deviceId, float volume);
float GetDeviceVolume(std::wstring_view deviceId);
void ExitApp();

#include "Util.hpp"
#include "I18n.hpp"
#include "SettingsUtil.hpp"
#include "Direct2DSvg.hpp"
