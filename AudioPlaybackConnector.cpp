#include "pch.h"
#include "AudioPlaybackConnector.h"

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void SetupMenu();
winrt::fire_and_forget ConnectDevice(DevicePicker, std::wstring_view);
winrt::fire_and_forget ConnectDevice(DevicePicker, DeviceInformation);
void SetupDevicePicker();
void SetupSvgIcon();
void UpdateNotifyIcon();
void UpdateTrayTooltip();
void ReopenAudioConnections();
void SetupAudioEndpointListener(HWND hWnd);
void TeardownAudioEndpointListener();
void CheckAudioEndpointVolume();
void ToggleLastConnectedDevice();
void UpdateAudioThreadPriority(bool enable);
void UpdatePowerLock(bool hasConnections);
void SetupDeviceWatcher(bool enable);
void ExitApp();

class AudioEndpointNotificationClient : public IMMNotificationClient
{
public:
	AudioEndpointNotificationClient(HWND hWnd) : m_hWnd(hWnd), m_refCount(1) {}

	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
	{
		if (!ppv) return E_POINTER;
		if (riid == __uuidof(::IUnknown) || riid == __uuidof(IMMNotificationClient))
		{
			*ppv = static_cast<IMMNotificationClient*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}

	STDMETHODIMP_(ULONG) AddRef() override
	{
		return InterlockedIncrement(&m_refCount);
	}

	STDMETHODIMP_(ULONG) Release() override
	{
		ULONG count = InterlockedDecrement(&m_refCount);
		if (count == 0)
		{
			delete this;
		}
		return count;
	}

	// IMMNotificationClient methods
	STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR /*pwstrDefaultDeviceId*/) override
	{
		if (flow == eRender && (role == eMultimedia || role == eConsole))
		{
			if (m_hWnd && IsWindow(m_hWnd))
			{
				PostMessageW(m_hWnd, WM_DEFAULT_AUDIO_DEVICE_CHANGED, 0, 0);
			}
		}
		return S_OK;
	}

	STDMETHODIMP OnDeviceStateChanged(LPCWSTR /*pwstrDeviceId*/, DWORD /*dwNewState*/) override { return S_OK; }
	STDMETHODIMP OnDeviceAdded(LPCWSTR /*pwstrDeviceId*/) override { return S_OK; }
	STDMETHODIMP OnDeviceRemoved(LPCWSTR /*pwstrDeviceId*/) override { return S_OK; }
	STDMETHODIMP OnPropertyValueChanged(LPCWSTR /*pwstrDeviceId*/, const PROPERTYKEY /*key*/) override { return S_OK; }

private:
	HWND m_hWnd;
	LONG m_refCount;
};

static wil::com_ptr<IMMDeviceEnumerator> g_deviceEnumerator;
static AudioEndpointNotificationClient* g_audioNotificationClient = nullptr;

void SetupAudioEndpointListener(HWND hWnd)
{
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_deviceEnumerator));
	if (SUCCEEDED(hr) && g_deviceEnumerator)
	{
		g_audioNotificationClient = new (std::nothrow) AudioEndpointNotificationClient(hWnd);
		if (g_audioNotificationClient)
		{
			g_deviceEnumerator->RegisterEndpointNotificationCallback(g_audioNotificationClient);
		}
	}
}

void TeardownAudioEndpointListener()
{
	if (g_deviceEnumerator && g_audioNotificationClient)
	{
		g_deviceEnumerator->UnregisterEndpointNotificationCallback(g_audioNotificationClient);
		g_audioNotificationClient->Release();
		g_audioNotificationClient = nullptr;
		g_deviceEnumerator.reset();
	}
}

void CheckAudioEndpointVolume()
{
	if (!g_deviceEnumerator)
		return;

	wil::com_ptr<IMMDevice> defaultDevice;
	HRESULT hr = g_deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, defaultDevice.put());
	if (SUCCEEDED(hr) && defaultDevice)
	{
		wil::com_ptr<IAudioEndpointVolume> endpointVolume;
		hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, endpointVolume.put_void());
		if (SUCCEEDED(hr) && endpointVolume)
		{
			BOOL isMuted = FALSE;
			if (SUCCEEDED(endpointVolume->GetMute(&isMuted)) && isMuted)
			{
				endpointVolume->SetMute(FALSE, nullptr);
			}
		}
	}
}

typedef HANDLE(WINAPI* PFN_AvSetMmThreadCharacteristicsW)(LPCWSTR, LPDWORD);
typedef BOOL(WINAPI* PFN_AvRevertMmThreadCharacteristics)(HANDLE);

void UpdateAudioThreadPriority(bool enable)
{
	static HMODULE hAvrt = nullptr;
	static PFN_AvSetMmThreadCharacteristicsW pfnSetMm = nullptr;
	static PFN_AvRevertMmThreadCharacteristics pfnRevertMm = nullptr;

	if (!hAvrt)
	{
		hAvrt = LoadLibraryW(L"avrt.dll");
		if (hAvrt)
		{
			pfnSetMm = reinterpret_cast<PFN_AvSetMmThreadCharacteristicsW>(GetProcAddress(hAvrt, "AvSetMmThreadCharacteristicsW"));
			pfnRevertMm = reinterpret_cast<PFN_AvRevertMmThreadCharacteristics>(GetProcAddress(hAvrt, "AvRevertMmThreadCharacteristics"));
		}
	}

	if (enable)
	{
		if (!g_mmcssHandle && pfnSetMm)
		{
			DWORD taskIndex = 0;
			g_mmcssHandle = pfnSetMm(L"Pro Audio", &taskIndex);
		}
	}
	else
	{
		if (g_mmcssHandle && pfnRevertMm)
		{
			pfnRevertMm(g_mmcssHandle);
			g_mmcssHandle = nullptr;
		}
	}
}

void UpdatePowerLock(bool hasConnections)
{
	if (hasConnections && g_preventSleepWhileStreaming)
	{
		SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
	}
	else
	{
		SetThreadExecutionState(ES_CONTINUOUS);
	}
}

void SetupDeviceWatcher(bool enable)
{
	if (enable)
	{
		if (!g_deviceWatcher)
		{
			g_deviceWatcher = DeviceInformation::CreateWatcher(AudioPlaybackConnection::GetDeviceSelector());
			g_deviceWatcher.Added([](const DeviceWatcher&, const DeviceInformation& device) {
				// Only auto-reconnect if this device was previously connected in current session and then dropped
				if (g_autoConnectNearby && !g_lostConnectionsInCurrentSession.empty())
				{
					auto it = g_audioPlaybackConnections.find(std::wstring(device.Id()));
					if (it == g_audioPlaybackConnections.end())
					{
						if (g_lostConnectionsInCurrentSession.find(std::wstring(device.Id())) != g_lostConnectionsInCurrentSession.end())
						{
							ConnectDevice(g_devicePicker, device);
						}
					}
				}
			});
			g_deviceWatcher.Updated([](const DeviceWatcher&, const DeviceInformationUpdate& update) {
				if (g_autoConnectNearby && !g_lostConnectionsInCurrentSession.empty())
				{
					auto it = g_audioPlaybackConnections.find(std::wstring(update.Id()));
					if (it == g_audioPlaybackConnections.end())
					{
						if (g_lostConnectionsInCurrentSession.find(std::wstring(update.Id())) != g_lostConnectionsInCurrentSession.end())
						{
							ConnectDevice(g_devicePicker, update.Id());
						}
					}
				}
			});
			g_deviceWatcher.Start();
		}
	}
	else
	{
		if (g_deviceWatcher)
		{
			try { g_deviceWatcher.Stop(); } catch (...) {}
			g_deviceWatcher = nullptr;
		}
	}
}

void ToggleLastConnectedDevice()
{
	if (!g_audioPlaybackConnections.empty())
	{
		for (const auto& connection : g_audioPlaybackConnections)
		{
			try { connection.second.connection.Close(); } catch (...) {}
			g_devicePicker.SetDisplayStatus(connection.second.device, {}, DevicePickerDisplayStatusOptions::None);
		}
		g_audioPlaybackConnections.clear();
		UpdateTrayTooltip();
		UpdateAudioThreadPriority(false);
		UpdatePowerLock(false);
	}
	else
	{
		for (const auto& id : g_startupReconnectDevices)
		{
			ConnectDevice(g_devicePicker, id);
		}
	}
}

void ExitApp()
{
	// 1. Immediately remove tray icon and hide window (0ms instant visual response)
	Shell_NotifyIconW(NIM_DELETE, &g_nid);
	if (g_hWnd && IsWindow(g_hWnd))
	{
		ShowWindow(g_hWnd, SW_HIDE);
	}

	// 2. Persist settings
	SaveSettings();

	// 3. Cleanly close audio connections and reset picker display status
	for (const auto& connection : g_audioPlaybackConnections)
	{
		try { connection.second.connection.Close(); } catch (...) {}
		if (g_devicePicker)
		{
			try { g_devicePicker.SetDisplayStatus(connection.second.device, {}, DevicePickerDisplayStatusOptions::None); } catch (...) {}
		}
	}

	ExitProcess(0);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);

	g_hInst = hInstance;
	LoadTranslateData();

	winrt::init_apartment();

	bool supported = false;
	try
	{
		using namespace winrt::Windows::Foundation::Metadata;

		supported = ApiInformation::IsTypePresent(winrt::name_of<DesktopWindowXamlSource>()) &&
			ApiInformation::IsTypePresent(winrt::name_of<AudioPlaybackConnection>());
	}
	catch (winrt::hresult_error const&)
	{
		supported = false;
		LOG_CAUGHT_EXCEPTION();
	}
	if (!supported)
	{
		TaskDialog(nullptr, nullptr, _(L"Unsupported Operating System"), nullptr, _(L"AudioPlaybackConnector is not supported on this operating system version."), TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr);
		return EXIT_FAILURE;
	}

	WNDCLASSEXW wcex = {
		.cbSize = sizeof(wcex),
		.lpfnWndProc = WndProc,
		.hInstance = hInstance,
		.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_AUDIOPLAYBACKCONNECTOR)),
		.hCursor = LoadCursorW(nullptr, IDC_ARROW),
		.lpszClassName = L"AudioPlaybackConnector",
		.hIconSm = wcex.hIcon
	};

	RegisterClassExW(&wcex);

	// When parent window size is 0x0 or invisible, the dpi scale of menu is incorrect. Here we set window size to 1x1 and use WS_EX_LAYERED to make window looks like invisible.
	g_hWnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TOPMOST, L"AudioPlaybackConnector", nullptr, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
	FAIL_FAST_LAST_ERROR_IF_NULL(g_hWnd);
	FAIL_FAST_IF_WIN32_BOOL_FALSE(SetLayeredWindowAttributes(g_hWnd, 0, 0, LWA_ALPHA));

	DesktopWindowXamlSource desktopSource;
	auto desktopSourceNative2 = desktopSource.as<IDesktopWindowXamlSourceNative2>();
	winrt::check_hresult(desktopSourceNative2->AttachToWindow(g_hWnd));
	winrt::check_hresult(desktopSourceNative2->get_WindowHandle(&g_hWndXaml));

	g_xamlCanvas = Canvas();
	desktopSource.Content(g_xamlCanvas);

	SetupAudioEndpointListener(g_hWnd);

	LoadSettings();
	SetupDeviceWatcher(g_autoConnectNearby);
	SetupMenu();
	SetupDevicePicker();
	SetupSvgIcon();

	g_nid.hWnd = g_niid.hWnd = g_hWnd;
	wcscpy_s(g_nid.szTip, _(L"AudioPlaybackConnector"));
	UpdateNotifyIcon();

	WM_TASKBAR_CREATED = RegisterWindowMessageW(L"TaskbarCreated");
	LOG_LAST_ERROR_IF(WM_TASKBAR_CREATED == 0);

	PostMessageW(g_hWnd, WM_CONNECTDEVICE, 0, 0);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0))
	{
		BOOL processed = FALSE;
		winrt::check_hresult(desktopSourceNative2->PreTranslateMessage(&msg, &processed));
		if (!processed)
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
		ExitApp();
		break;
	case WM_DEFAULT_AUDIO_DEVICE_CHANGED:
	case WM_REFRESH_AUDIO:
		ReopenAudioConnections();
		break;
	case WM_POWERBROADCAST:
		if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
		{
			PostMessageW(hWnd, WM_REFRESH_AUDIO, 0, 0);
		}
		break;
	case WM_SETTINGCHANGE:
		if (lParam && CompareStringOrdinal(reinterpret_cast<LPCWCH>(lParam), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL)
		{
			UpdateNotifyIcon();
		}
		break;
	case WM_NOTIFYICON:
		switch (LOWORD(lParam))
		{
		case WM_LBUTTONDBLCLK:
			ToggleLastConnectedDevice();
			break;
		case NIN_SELECT:
		case NIN_KEYSELECT:
		{
			using namespace winrt::Windows::UI::Popups;

			RECT iconRect;
			auto hr = Shell_NotifyIconGetRect(&g_niid, &iconRect);
			if (FAILED(hr))
			{
				LOG_HR(hr);
				break;
			}

			auto dpi = GetDpiForWindow(hWnd);
			Rect rect = {
				static_cast<float>(iconRect.left * USER_DEFAULT_SCREEN_DPI / dpi),
				static_cast<float>(iconRect.top * USER_DEFAULT_SCREEN_DPI / dpi),
				static_cast<float>((iconRect.right - iconRect.left) * USER_DEFAULT_SCREEN_DPI / dpi),
				static_cast<float>((iconRect.bottom - iconRect.top) * USER_DEFAULT_SCREEN_DPI / dpi)
			};

			SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_HIDEWINDOW);
			SetForegroundWindow(hWnd);
			g_devicePicker.Show(rect, Placement::Above);
		}
		break;
		case WM_RBUTTONUP: // Menu activated by mouse click
			g_menuFocusState = FocusState::Pointer;
			break;
		case WM_CONTEXTMENU:
		{
			if (g_menuFocusState == FocusState::Unfocused)
				g_menuFocusState = FocusState::Keyboard;

			auto dpi = GetDpiForWindow(hWnd);
			Point point = {
				static_cast<float>(GET_X_LPARAM(wParam) * USER_DEFAULT_SCREEN_DPI / dpi),
				static_cast<float>(GET_Y_LPARAM(wParam) * USER_DEFAULT_SCREEN_DPI / dpi)
			};

			SetWindowPos(g_hWndXaml, 0, 0, 0, 0, 0, SWP_NOZORDER | SWP_SHOWWINDOW);
			SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, 1, 1, SWP_SHOWWINDOW);
			SetForegroundWindow(hWnd);

			g_xamlMenu.ShowAt(g_xamlCanvas, point);
		}
		break;
		}
		break;
	case WM_CONNECTDEVICE:
		if (g_reconnect)
		{
			for (const auto& i : g_startupReconnectDevices)
			{
				ConnectDevice(g_devicePicker, i);
			}
			g_startupReconnectDevices.clear();
		}
		break;
	default:
		if (WM_TASKBAR_CREATED && message == WM_TASKBAR_CREATED)
		{
			UpdateNotifyIcon();
		}
		return DefWindowProcW(hWnd, message, wParam, lParam);
	}
	return 0;
}

void SetupMenu()
{
	// Refresh Audio Connection
	FontIcon refreshIcon;
	refreshIcon.Glyph(L"\xE72C");

	MenuFlyoutItem refreshItem;
	refreshItem.Text(_(L"Refresh Audio Connection"));
	refreshItem.Icon(refreshIcon);
	refreshItem.Click([](const auto&, const auto&) {
		PostMessageW(g_hWnd, WM_REFRESH_AUDIO, 0, 0);
	});

	// Sound Settings
	FontIcon soundIcon;
	soundIcon.Glyph(L"\xE767");

	MenuFlyoutItem soundItem;
	soundItem.Text(_(L"Sound Settings"));
	soundItem.Icon(soundIcon);
	soundItem.Click([](const auto&, const auto&) {
		winrt::Windows::System::Launcher::LaunchUriAsync(Uri(L"ms-settings:sound"));
	});

	// Bluetooth Settings
	FontIcon btIcon;
	btIcon.Glyph(L"\xE702");

	MenuFlyoutItem btItem;
	btItem.Text(_(L"Bluetooth Settings"));
	btItem.Icon(btIcon);
	btItem.Click([](const auto&, const auto&) {
		winrt::Windows::System::Launcher::LaunchUriAsync(Uri(L"ms-settings:bluetooth"));
	});

	// Settings Submenu
	FontIcon configIcon;
	configIcon.Glyph(L"\xE713");

	MenuFlyoutSubItem settingsSubMenu;
	settingsSubMenu.Text(_(L"Settings"));
	settingsSubMenu.Icon(configIcon);

	// 1. Reconnect on next start
	ToggleMenuFlyoutItem reconnectItem;
	reconnectItem.Text(_(L"Reconnect on next start"));
	reconnectItem.IsChecked(g_reconnect);
	reconnectItem.Click([](const auto& sender, const auto&) {
		g_reconnect = sender.as<ToggleMenuFlyoutItem>().IsChecked();
		SaveSettings();
	});
	settingsSubMenu.Items().Append(reconnectItem);

	// 2. Run at Windows startup
	ToggleMenuFlyoutItem startupItem;
	startupItem.Text(_(L"Run at Windows startup"));
	startupItem.IsChecked(g_runAtStartup);
	startupItem.Click([](const auto& sender, const auto&) {
		g_runAtStartup = sender.as<ToggleMenuFlyoutItem>().IsChecked();
		SetRunAtStartup(g_runAtStartup);
		SaveSettings();
	});
	settingsSubMenu.Items().Append(startupItem);

	// 3. Auto-connect nearby devices
	ToggleMenuFlyoutItem autoConnectItem;
	autoConnectItem.Text(_(L"Auto-connect nearby devices"));
	autoConnectItem.IsChecked(g_autoConnectNearby);
	autoConnectItem.Click([](const auto& sender, const auto&) {
		g_autoConnectNearby = sender.as<ToggleMenuFlyoutItem>().IsChecked();
		SetupDeviceWatcher(g_autoConnectNearby);
		SaveSettings();
	});
	settingsSubMenu.Items().Append(autoConnectItem);

	// 4. Prevent sleep while streaming
	ToggleMenuFlyoutItem preventSleepItem;
	preventSleepItem.Text(_(L"Prevent sleep while streaming"));
	preventSleepItem.IsChecked(g_preventSleepWhileStreaming);
	preventSleepItem.Click([](const auto& sender, const auto&) {
		g_preventSleepWhileStreaming = sender.as<ToggleMenuFlyoutItem>().IsChecked();
		UpdatePowerLock(!g_audioPlaybackConnections.empty());
		SaveSettings();
	});
	settingsSubMenu.Items().Append(preventSleepItem);

	// Main Separator
	MenuFlyoutSeparator mainSeparator;

	// Exit (instant 0ms direct exit without blocking popups)
	FontIcon closeIcon;
	closeIcon.Glyph(L"\xE8BB");

	MenuFlyoutItem exitItem;
	exitItem.Text(_(L"Exit"));
	exitItem.Icon(closeIcon);
	exitItem.Click([](const auto&, const auto&) {
		ExitApp();
	});

	MenuFlyout menu;
	menu.Items().Append(refreshItem);
	menu.Items().Append(soundItem);
	menu.Items().Append(btItem);
	menu.Items().Append(settingsSubMenu);
	menu.Items().Append(mainSeparator);
	menu.Items().Append(exitItem);

	menu.Opened([](const auto& sender, const auto&) {
		auto menuItems = sender.as<MenuFlyout>().Items();
		auto itemsCount = menuItems.Size();
		if (itemsCount > 0)
		{
			menuItems.GetAt(itemsCount - 1).Focus(g_menuFocusState);
		}
		g_menuFocusState = FocusState::Unfocused;
	});
	menu.Closed([](const auto&, const auto&) {
		ShowWindow(g_hWnd, SW_HIDE);
	});

	g_xamlMenu = menu;
}

winrt::fire_and_forget ConnectDevice(DevicePicker picker, DeviceInformation device)
{
	picker.SetDisplayStatus(device, _(L"Connecting"), DevicePickerDisplayStatusOptions::ShowProgress | DevicePickerDisplayStatusOptions::ShowDisconnectButton);

	bool success = false;
	std::wstring errorMessage;
	std::wstring deviceName = device.Name().c_str();

	try
	{
		// Clean up existing connection for this device if present
		auto existingIt = g_audioPlaybackConnections.find(std::wstring(device.Id()));
		if (existingIt != g_audioPlaybackConnections.end())
		{
			try { existingIt->second.connection.Close(); } catch (...) {}
			g_audioPlaybackConnections.erase(existingIt);
		}

		CheckAudioEndpointVolume();

		auto connection = AudioPlaybackConnection::TryCreateFromId(device.Id());
		if (connection)
		{
			g_audioPlaybackConnections.insert_or_assign(std::wstring(device.Id()), ConnectedDeviceInfo{ device, connection, deviceName });

			connection.StateChanged([](const auto& sender, const auto&) {
				if (sender.State() == AudioPlaybackConnectionState::Closed)
				{
					auto it = g_audioPlaybackConnections.find(std::wstring(sender.DeviceId()));
					if (it != g_audioPlaybackConnections.end())
					{
						// Remember in session's lost list so that if user walks back, DeviceWatcher auto-reconnects
						g_lostConnectionsInCurrentSession.insert(std::wstring(sender.DeviceId()));
						g_devicePicker.SetDisplayStatus(it->second.device, {}, DevicePickerDisplayStatusOptions::None);
						g_audioPlaybackConnections.erase(it);
						UpdateTrayTooltip();
						UpdateAudioThreadPriority(!g_audioPlaybackConnections.empty());
						UpdatePowerLock(!g_audioPlaybackConnections.empty());
					}
					try { sender.Close(); } catch (...) {}
				}
			});

			co_await connection.StartAsync();
			auto result = co_await connection.OpenAsync();

			switch (result.Status())
			{
			case AudioPlaybackConnectionOpenResultStatus::Success:
				success = true;
				break;
			case AudioPlaybackConnectionOpenResultStatus::RequestTimedOut:
				success = false;
				errorMessage = _(L"The request timed out");
				break;
			case AudioPlaybackConnectionOpenResultStatus::DeniedBySystem:
				success = false;
				errorMessage = _(L"The operation was denied by the system");
				break;
			case AudioPlaybackConnectionOpenResultStatus::UnknownFailure:
				success = false;
				winrt::throw_hresult(result.ExtendedError());
				break;
			}
		}
		else
		{
			success = false;
			errorMessage = _(L"Unknown error");
		}
	}
	catch (winrt::hresult_error const& ex)
	{
		success = false;
		errorMessage.resize(64);
		while (1)
		{
			auto result = swprintf(errorMessage.data(), errorMessage.size(), L"%s (0x%08X)", ex.message().c_str(), static_cast<uint32_t>(ex.code()));
			if (result < 0)
			{
				errorMessage.resize(errorMessage.size() * 2);
			}
			else
			{
				errorMessage.resize(result);
				break;
			}
		}
		LOG_CAUGHT_EXCEPTION();
	}

	if (success)
	{
		picker.SetDisplayStatus(device, _(L"Connected"), DevicePickerDisplayStatusOptions::ShowDisconnectButton);
		// Connected successfully -> remove from lost list
		g_lostConnectionsInCurrentSession.erase(std::wstring(device.Id()));
		SaveSettings();
		UpdateTrayTooltip();
		UpdateAudioThreadPriority(true);
		UpdatePowerLock(true);
	}
	else
	{
		auto it = g_audioPlaybackConnections.find(std::wstring(device.Id()));
		if (it != g_audioPlaybackConnections.end())
		{
			try { it->second.connection.Close(); } catch (...) {}
			g_audioPlaybackConnections.erase(it);
		}
		picker.SetDisplayStatus(device, errorMessage, DevicePickerDisplayStatusOptions::ShowRetryButton);
		UpdateTrayTooltip();
		UpdateAudioThreadPriority(!g_audioPlaybackConnections.empty());
		UpdatePowerLock(!g_audioPlaybackConnections.empty());
	}
}

winrt::fire_and_forget ConnectDevice(DevicePicker picker, std::wstring_view deviceId)
{
	auto device = co_await DeviceInformation::CreateFromIdAsync(deviceId);
	ConnectDevice(picker, device);
}

void ReopenAudioConnections()
{
	if (g_audioPlaybackConnections.empty())
		return;

	std::vector<DeviceInformation> devices;
	devices.reserve(g_audioPlaybackConnections.size());
	for (const auto& item : g_audioPlaybackConnections)
	{
		devices.push_back(item.second.device);
	}

	for (const auto& dev : devices)
	{
		ConnectDevice(g_devicePicker, dev);
	}
}

void SetupDevicePicker()
{
	g_devicePicker = DevicePicker();
	winrt::check_hresult(g_devicePicker.as<IInitializeWithWindow>()->Initialize(g_hWnd));

	g_devicePicker.Filter().SupportedDeviceSelectors().Append(AudioPlaybackConnection::GetDeviceSelector());
	g_devicePicker.DevicePickerDismissed([](const auto&, const auto&) {
		SetWindowPos(g_hWnd, nullptr, 0, 0, 0, 0, SWP_NOZORDER | SWP_HIDEWINDOW);
	});
	g_devicePicker.DeviceSelected([](const auto& sender, const auto& args) {
		ConnectDevice(sender, args.SelectedDevice());
	});
	g_devicePicker.DisconnectButtonClicked([](const auto& sender, const auto& args) {
		auto device = args.Device();
		auto it = g_audioPlaybackConnections.find(std::wstring(device.Id()));
		if (it != g_audioPlaybackConnections.end())
		{
			try { it->second.connection.Close(); } catch (...) {}
			g_audioPlaybackConnections.erase(it);
		}
		// Explicit manual disconnect by user -> remove from lost list & reset status
		g_lostConnectionsInCurrentSession.erase(std::wstring(device.Id()));
		SaveSettings();
		sender.SetDisplayStatus(device, {}, DevicePickerDisplayStatusOptions::None);
		UpdateTrayTooltip();
		UpdateAudioThreadPriority(!g_audioPlaybackConnections.empty());
		UpdatePowerLock(!g_audioPlaybackConnections.empty());
	});
}

void SetupSvgIcon()
{
	auto hRes = FindResourceW(g_hInst, MAKEINTRESOURCEW(1), L"SVG");
	FAIL_FAST_LAST_ERROR_IF_NULL(hRes);

	auto size = SizeofResource(g_hInst, hRes);
	FAIL_FAST_LAST_ERROR_IF(size == 0);

	auto hResData = LoadResource(g_hInst, hRes);
	FAIL_FAST_LAST_ERROR_IF_NULL(hResData);

	auto svgData = reinterpret_cast<const char*>(LockResource(hResData));
	FAIL_FAST_IF_NULL_ALLOC(svgData);

	const std::string_view svg(svgData, size);
	const int width = GetSystemMetrics(SM_CXSMICON), height = GetSystemMetrics(SM_CYSMICON);

	g_hIconLight = SvgTohIcon(svg, width, height, { 0, 0, 0, 1 });
	g_hIconDark = SvgTohIcon(svg, width, height, { 1, 1, 1, 1 });
}

void UpdateTrayTooltip()
{
	if (g_audioPlaybackConnections.empty())
	{
		wcscpy_s(g_nid.szTip, _(L"AudioPlaybackConnector"));
	}
	else if (g_audioPlaybackConnections.size() == 1)
	{
		auto const& dev = g_audioPlaybackConnections.begin()->second;
		swprintf_s(g_nid.szTip, L"AudioPlaybackConnector - %s", dev.name.c_str());
	}
	else
	{
		swprintf_s(g_nid.szTip, L"AudioPlaybackConnector (%zu %s)", g_audioPlaybackConnections.size(), _(L"Connected"));
	}

	Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void UpdateNotifyIcon()
{
	DWORD value = 0, cbValue = sizeof(value);
	LOG_IF_WIN32_ERROR(RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)", L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &cbValue));
	g_nid.hIcon = value != 0 ? g_hIconLight : g_hIconDark;

	if (!Shell_NotifyIconW(NIM_MODIFY, &g_nid))
	{
		if (Shell_NotifyIconW(NIM_ADD, &g_nid))
		{
			FAIL_FAST_IF_WIN32_BOOL_FALSE(Shell_NotifyIconW(NIM_SETVERSION, &g_nid));
		}
		else
		{
			LOG_LAST_ERROR();
		}
	}
}
