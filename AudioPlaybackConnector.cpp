#include "pch.h"
#include "AudioPlaybackConnector.h"

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void SetupMenu();
winrt::fire_and_forget ConnectDevice(std::wstring deviceId);
winrt::fire_and_forget ConnectDevice(DeviceInformation device);
void SetupSvgIcon();
void UpdateNotifyIcon();
void UpdateTrayTooltip();
void ReopenAudioConnections();
void DisconnectAllDevices();
void SetupAudioEndpointListener(HWND hWnd);
void TeardownAudioEndpointListener();
void CheckAudioEndpointVolume();
void ToggleLastConnectedDevice();
void UpdateAudioThreadPriority(bool enable);
void UpdatePowerLock(bool hasConnections);
void SetupDeviceWatcher(bool enable);
winrt::fire_and_forget RefreshDevicePanelAsync(bool forceReopen);
void ShowDevicePanel();
void UpdateHeaderBadgeUI();
void UpdateDevicePanelUI();
void PopulateDeviceList(StackPanel targetPanel, const winrt::Windows::Foundation::Collections::IVectorView<DeviceInformation>& devices);
void SetDeviceVolume(std::wstring_view deviceId, float volume);
float GetDeviceVolume(std::wstring_view deviceId);
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
	STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override
	{
		if (flow == eRender && (role == eMultimedia || role == eConsole))
		{
			if (pwstrDefaultDeviceId)
			{
				std::wstring newId = pwstrDefaultDeviceId;
				if (!g_currentDefaultAudioEndpointId.empty() && newId != g_currentDefaultAudioEndpointId)
				{
					g_currentDefaultAudioEndpointId = newId;
					if (m_hWnd && IsWindow(m_hWnd))
					{
						PostMessageW(m_hWnd, WM_DEFAULT_AUDIO_DEVICE_CHANGED, 0, 0);
					}
				}
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
		wil::com_ptr<IMMDevice> defaultDevice;
		if (SUCCEEDED(g_deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, defaultDevice.put())) && defaultDevice)
		{
			wil::unique_cotaskmem_string id;
			if (SUCCEEDED(defaultDevice->GetId(&id)) && id)
			{
				g_currentDefaultAudioEndpointId = id.get();
			}
		}

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

void SetDeviceVolume(std::wstring_view deviceId, float volume)
{
	if (volume < 0.0f) volume = 0.0f;
	if (volume > 1.0f) volume = 1.0f;
	g_deviceVolumes[std::wstring(deviceId)] = volume;
	SaveSettings();

	if (!g_deviceEnumerator)
		return;

	wil::com_ptr<IMMDevice> defaultDevice;
	if (FAILED(g_deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, defaultDevice.put())))
		return;

	wil::com_ptr<IAudioSessionManager2> sessionManager;
	if (FAILED(defaultDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, nullptr, sessionManager.put_void())))
		return;

	wil::com_ptr<IAudioSessionEnumerator> sessionEnumerator;
	if (FAILED(sessionManager->GetSessionEnumerator(sessionEnumerator.put())))
		return;

	int count = 0;
	sessionEnumerator->GetCount(&count);

	for (int i = 0; i < count; ++i)
	{
		wil::com_ptr<IAudioSessionControl> control;
		if (FAILED(sessionEnumerator->GetSession(i, control.put())))
			continue;

		wil::com_ptr<IAudioSessionControl2> control2;
		if (SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2), control2.put_void())))
		{
			DWORD pid = 0;
			control2->GetProcessId(&pid);

			if (pid == GetCurrentProcessId() || pid == 0)
			{
				wil::com_ptr<ISimpleAudioVolume> simpleVol;
				if (SUCCEEDED(control->QueryInterface(__uuidof(ISimpleAudioVolume), simpleVol.put_void())))
				{
					simpleVol->SetMasterVolume(volume, nullptr);
				}
			}
		}
	}
}

float GetDeviceVolume(std::wstring_view deviceId)
{
	auto it = g_deviceVolumes.find(std::wstring(deviceId));
	if (it != g_deviceVolumes.end())
	{
		return static_cast<float>(it->second);
	}
	return 1.0f; // Default 100%
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
				if (g_autoConnectNearby && !g_lostConnectionsInCurrentSession.empty())
				{
					auto it = g_audioPlaybackConnections.find(std::wstring(device.Id()));
					if (it == g_audioPlaybackConnections.end())
					{
						if (g_lostConnectionsInCurrentSession.find(std::wstring(device.Id())) != g_lostConnectionsInCurrentSession.end())
						{
							ConnectDevice(device);
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
							ConnectDevice(std::wstring(update.Id()));
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

void DisconnectAllDevices()
{
	for (const auto& conn : g_audioPlaybackConnections)
	{
		try { conn.second.connection.Close(); } catch (...) {}
	}
	g_audioPlaybackConnections.clear();
	g_lostConnectionsInCurrentSession.clear();
	g_deviceErrorMessages.clear();
	SaveSettings();
	UpdateTrayTooltip();
	UpdateAudioThreadPriority(false);
	UpdatePowerLock(false);
	UpdateDevicePanelUI();
}

void ToggleLastConnectedDevice()
{
	if (!g_audioPlaybackConnections.empty())
	{
		DisconnectAllDevices();
		PostMessageW(g_hWnd, WM_UPDATE_DEVICE_PANEL, 0, 0);
	}
	else
	{
		for (const auto& id : g_startupReconnectDevices)
		{
			ConnectDevice(id);
		}
	}
}

void ExitApp()
{
	Shell_NotifyIconW(NIM_DELETE, &g_nid);
	if (g_hWnd && IsWindow(g_hWnd))
	{
		ShowWindow(g_hWnd, SW_HIDE);
	}
	SaveSettings();
	ExitProcess(0);
}

void UpdateHeaderBadgeUI()
{
	if (!g_panelBadgeText)
		return;

	int maxCap = GetMaxSupportedA2dpStreams();
	size_t curConn = g_audioPlaybackConnections.size();
	wchar_t badgeBuf[32];
	if (curConn >= static_cast<size_t>(maxCap) && maxCap > 1)
	{
		swprintf_s(badgeBuf, L"[%zu/%d %s]", curConn, maxCap, _(L"Full"));
		g_panelBadgeText.Foreground(SolidColorBrush(winrt::Windows::UI::Color{ 0xFF, 0xFF, 0x99, 0x00 }));
		g_panelBadgeText.Opacity(1.0);
	}
	else
	{
		swprintf_s(badgeBuf, L"[%zu/%d]", curConn, maxCap);
		g_panelBadgeText.Foreground(SolidColorBrush(winrt::Windows::UI::Color{ 0xDD, 0xFF, 0xFF, 0xFF }));
		g_panelBadgeText.Opacity(0.65);
	}
	g_panelBadgeText.Text(badgeBuf);

	if (g_panelDisconnectAllBtn)
	{
		g_panelDisconnectAllBtn.Visibility((g_multiDeviceMode && g_audioPlaybackConnections.size() > 1) ? Visibility::Visible : Visibility::Collapsed);
	}
}

void UpdateDevicePanelUI()
{
	UpdateHeaderBadgeUI();
	if (g_deviceListPanel && g_cachedDevices)
	{
		PopulateDeviceList(g_deviceListPanel, g_cachedDevices);
	}
	else if (g_deviceListPanel)
	{
		RefreshDevicePanelAsync(false);
	}
}

void PopulateDeviceList(StackPanel targetPanel, const winrt::Windows::Foundation::Collections::IVectorView<DeviceInformation>& devices)
{
	targetPanel.Children().Clear();

	if (devices.Size() == 0)
	{
		TextBlock emptyText;
		emptyText.Text(_(L"No Bluetooth audio devices found."));
		emptyText.Opacity(0.6);
		emptyText.Margin({ 0, 16, 0, 16 });
		emptyText.HorizontalAlignment(HorizontalAlignment::Center);
		targetPanel.Children().Append(emptyText);
		UpdateHeaderBadgeUI();
		return;
	}

	for (const auto& dev : devices)
	{
		std::wstring devId = dev.Id().c_str();
		std::wstring devName = dev.Name().c_str();
		bool isConnected = (g_audioPlaybackConnections.find(devId) != g_audioPlaybackConnections.end());
		bool isConnecting = (g_connectingDeviceIds.find(devId) != g_connectingDeviceIds.end());
		bool hasError = (g_deviceErrorMessages.find(devId) != g_deviceErrorMessages.end());

		// Card Border
		Border cardBorder;
		cardBorder.CornerRadius(CornerRadius{ 8, 8, 8, 8 });
		cardBorder.Padding({ 14, 12, 14, 12 });
		cardBorder.Margin({ 0, 4, 0, 4 });
		cardBorder.Background(SolidColorBrush(winrt::Windows::UI::Color{ 0x18, 0x80, 0x80, 0x80 }));

		StackPanel cardStack;

		// Card Top Row: Icon + Name + Action Button
		Grid topGrid;
		ColumnDefinition colIcon, colName, colAction;
		colIcon.Width(GridLength{ 30, GridUnitType::Pixel });
		colAction.Width(GridLength{ 0, GridUnitType::Auto });
		topGrid.ColumnDefinitions().Append(colIcon);
		topGrid.ColumnDefinitions().Append(colName);
		topGrid.ColumnDefinitions().Append(colAction);

		FontIcon phoneIcon;
		phoneIcon.Glyph(L"\xE8EA");
		phoneIcon.FontSize(18);
		phoneIcon.VerticalAlignment(VerticalAlignment::Center);
		Grid::SetColumn(phoneIcon, 0);
		topGrid.Children().Append(phoneIcon);

		StackPanel namePanel;
		namePanel.VerticalAlignment(VerticalAlignment::Center);
		namePanel.Margin({ 4, 0, 8, 0 });

		TextBlock nameText;
		nameText.Text(devName);
		nameText.FontSize(14);
		nameText.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
		nameText.TextTrimming(TextTrimming::CharacterEllipsis);
		namePanel.Children().Append(nameText);

		if (isConnected)
		{
			TextBlock statusText;
			statusText.Text(_(L"Connected"));
			statusText.FontSize(12);
			statusText.Opacity(0.7);
			namePanel.Children().Append(statusText);
		}
		else if (hasError)
		{
			TextBlock errorText;
			errorText.Text(g_deviceErrorMessages[devId]);
			errorText.FontSize(11);
			errorText.Foreground(SolidColorBrush(winrt::Windows::UI::Color{ 0xFF, 0xE7, 0x48, 0x56 }));
			errorText.TextWrapping(TextWrapping::Wrap);
			namePanel.Children().Append(errorText);
		}

		Grid::SetColumn(namePanel, 1);
		topGrid.Children().Append(namePanel);

		// Action Button / Progress
		if (isConnecting)
		{
			ProgressRing ring;
			ring.IsActive(true);
			ring.Width(22);
			ring.Height(22);
			Grid::SetColumn(ring, 2);
			topGrid.Children().Append(ring);
		}
		else if (isConnected)
		{
			Button disconnectBtn;
			disconnectBtn.Content(winrt::box_value(_(L"Disconnect")));
			disconnectBtn.Padding({ 12, 5, 12, 5 });
			disconnectBtn.Click([devId](const auto&, const auto&) {
				if (g_multiDeviceMode && g_audioPlaybackConnections.size() > 1)
				{
					DisconnectAllDevices();
				}
				else
				{
					auto it = g_audioPlaybackConnections.find(devId);
					if (it != g_audioPlaybackConnections.end())
					{
						try { it->second.connection.Close(); } catch (...) {}
						g_audioPlaybackConnections.erase(it);
					}
					g_lostConnectionsInCurrentSession.erase(devId);
					g_deviceErrorMessages.erase(devId);
					SaveSettings();
					UpdateTrayTooltip();
					UpdateAudioThreadPriority(!g_audioPlaybackConnections.empty());
					UpdatePowerLock(!g_audioPlaybackConnections.empty());
					UpdateDevicePanelUI();
				}
			});
			Grid::SetColumn(disconnectBtn, 2);
			topGrid.Children().Append(disconnectBtn);
		}
		else
		{
			Button connectBtn;
			connectBtn.Content(winrt::box_value(_(L"Connect")));
			connectBtn.Padding({ 14, 5, 14, 5 });
			connectBtn.Click([dev, devId](const auto&, const auto&) {
				// Hardware capacity check for Multi-device mode
				if (g_multiDeviceMode && g_audioPlaybackConnections.size() >= 2 && g_audioPlaybackConnections.find(devId) == g_audioPlaybackConnections.end())
				{
					g_deviceErrorMessages[devId] = _(L"Hardware limit reached (Max 2 devices). Please disconnect one first.");
					UpdateDevicePanelUI();
					return;
				}
				g_deviceErrorMessages.erase(devId);
				ConnectDevice(dev);
			});
			Grid::SetColumn(connectBtn, 2);
			topGrid.Children().Append(connectBtn);
		}

		cardStack.Children().Append(topGrid);

		// Card Bottom Row: Volume Slider (Only visible when connected!)
		if (isConnected)
		{
			Grid volGrid;
			volGrid.Margin({ 0, 10, 0, 0 });

			ColumnDefinition colVolIcon, colSlider, colVolVal;
			colVolIcon.Width(GridLength{ 30, GridUnitType::Pixel });
			colVolVal.Width(GridLength{ 42, GridUnitType::Pixel });
			volGrid.ColumnDefinitions().Append(colVolIcon);
			volGrid.ColumnDefinitions().Append(colSlider);
			volGrid.ColumnDefinitions().Append(colVolVal);

			float currentVol = GetDeviceVolume(devId);
			int volPercent = static_cast<int>(currentVol * 100.0f + 0.5f);

			FontIcon volIcon;
			volIcon.Glyph(volPercent == 0 ? L"\xE74F" : L"\xE767");
			volIcon.FontSize(15);
			volIcon.VerticalAlignment(VerticalAlignment::Center);
			Grid::SetColumn(volIcon, 0);
			volGrid.Children().Append(volIcon);

			TextBlock volValText;
			wchar_t volBuf[16];
			swprintf_s(volBuf, L"%d%%", volPercent);
			volValText.Text(volBuf);
			volValText.FontSize(12);
			volValText.FontWeight(winrt::Windows::UI::Text::FontWeights::Medium());
			volValText.VerticalAlignment(VerticalAlignment::Center);
			volValText.HorizontalAlignment(HorizontalAlignment::Right);
			volValText.Opacity(0.85);
			Grid::SetColumn(volValText, 2);
			volGrid.Children().Append(volValText);

			Slider slider;
			slider.Minimum(0);
			slider.Maximum(100);
			slider.Value(volPercent);
			slider.Margin({ 6, 0, 6, 0 });
			slider.VerticalAlignment(VerticalAlignment::Center);

			slider.ValueChanged([devId, volValText, volIcon](const auto& /*sender*/, const auto& args) {
				int newPercent = static_cast<int>(args.NewValue());
				float newVol = newPercent / 100.0f;
				SetDeviceVolume(devId, newVol);

				wchar_t buf[16];
				swprintf_s(buf, L"%d%%", newPercent);
				volValText.Text(buf);
				volIcon.Glyph(newPercent == 0 ? L"\xE74F" : L"\xE767");
			});
			Grid::SetColumn(slider, 1);
			volGrid.Children().Append(slider);

			cardStack.Children().Append(volGrid);
		}

		cardBorder.Child(cardStack);
		targetPanel.Children().Append(cardBorder);
	}

	UpdateHeaderBadgeUI();
}

winrt::fire_and_forget RefreshDevicePanelAsync(bool forceReopen)
{
	try
	{
		// Query devices asynchronously
		auto devices = co_await DeviceInformation::FindAllAsync(AudioPlaybackConnection::GetDeviceSelector());
		g_cachedDevices = devices;

		// Return to UI thread
		co_await winrt::resume_foreground(g_xamlCanvas.Dispatcher());

		// In-place refresh if panel already exists and we are not forcing a reopen
		if (g_deviceListPanel && !forceReopen)
		{
			PopulateDeviceList(g_deviceListPanel, devices);
			co_return;
		}

		if (!forceReopen)
		{
			co_return;
		}

		RECT iconRect;
		auto hr = Shell_NotifyIconGetRect(&g_niid, &iconRect);
		if (FAILED(hr))
		{
			LOG_HR(hr);
			co_return;
		}

		auto dpi = GetDpiForWindow(g_hWnd);
		SetWindowPos(g_hWndXaml, 0, 0, 0, 0, 0, SWP_NOZORDER | SWP_SHOWWINDOW);
		SetWindowPos(g_hWnd, HWND_TOPMOST, iconRect.left, iconRect.top, 0, 0, SWP_SHOWWINDOW);
		SetForegroundWindow(g_hWnd);

		g_xamlCanvas.Width(static_cast<float>((iconRect.right - iconRect.left) * USER_DEFAULT_SCREEN_DPI / dpi));
		g_xamlCanvas.Height(static_cast<float>((iconRect.bottom - iconRect.top) * USER_DEFAULT_SCREEN_DPI / dpi));

		// Root Container Border
		Border rootBorder;
		rootBorder.Width(340);
		rootBorder.Padding({ 16, 14, 16, 14 });

		StackPanel rootPanel;

				// Header: Title + Capacity Badge + Disconnect All (if multi-device) + Refresh Button + Close Button
		Grid headerGrid;
		ColumnDefinition colTitle;
		ColumnDefinition colBadge;
		ColumnDefinition colDisconnectAll;
		ColumnDefinition colRefresh;
		ColumnDefinition colClose;
		colBadge.Width(GridLength{ 0, GridUnitType::Auto });
		colDisconnectAll.Width(GridLength{ 0, GridUnitType::Auto });
		colRefresh.Width(GridLength{ 0, GridUnitType::Auto });
		colClose.Width(GridLength{ 0, GridUnitType::Auto });
		headerGrid.ColumnDefinitions().Append(colTitle);
		headerGrid.ColumnDefinitions().Append(colBadge);
		headerGrid.ColumnDefinitions().Append(colDisconnectAll);
		headerGrid.ColumnDefinitions().Append(colRefresh);
		headerGrid.ColumnDefinitions().Append(colClose);

		TextBlock headerText;
		headerText.Text(_(L"Bluetooth Audio Devices"));
		headerText.FontSize(15);
		headerText.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
		headerText.VerticalAlignment(VerticalAlignment::Center);
		Grid::SetColumn(headerText, 0);
		headerGrid.Children().Append(headerText);

		// Dynamic capacity badge
		TextBlock badgeText;
		g_panelBadgeText = badgeText;
		badgeText.FontSize(12);
		badgeText.FontWeight(winrt::Windows::UI::Text::FontWeights::Medium());
		badgeText.VerticalAlignment(VerticalAlignment::Center);
		badgeText.Margin({ 6, 0, 8, 0 });
		Grid::SetColumn(badgeText, 1);
		headerGrid.Children().Append(badgeText);

		Button discAllBtn;
		g_panelDisconnectAllBtn = discAllBtn;
		discAllBtn.Content(winrt::box_value(_(L"Disconnect all")));
		discAllBtn.FontSize(11);
		discAllBtn.Padding({ 8, 4, 8, 4 });
		discAllBtn.Margin({ 0, 0, 4, 0 });
		discAllBtn.Click([](const auto&, const auto&) {
			DisconnectAllDevices();
		});
		Grid::SetColumn(discAllBtn, 2);
		headerGrid.Children().Append(discAllBtn);

		Button refreshBtn;
		FontIcon refreshIcon;
		refreshIcon.Glyph(L"\xE72C");
		refreshIcon.FontSize(12);
		refreshBtn.Content(refreshIcon);
		refreshBtn.Padding({ 6, 6, 6, 6 });
		refreshBtn.Margin({ 0, 0, 4, 0 });
		refreshBtn.Click([](const auto&, const auto&) {
			g_deviceErrorMessages.clear();
			RefreshDevicePanelAsync(false);
		});
		Grid::SetColumn(refreshBtn, 3);
		headerGrid.Children().Append(refreshBtn);

		Button closeBtn;
		FontIcon closeIcon;
		closeIcon.Glyph(L"\xE8BB");
		closeIcon.FontSize(11);
		closeBtn.Content(closeIcon);
		closeBtn.Padding({ 6, 6, 6, 6 });
		closeBtn.Click([](const auto&, const auto&) {
			if (g_deviceFlyout)
			{
				g_deviceFlyout.Hide();
			}
		});
		Grid::SetColumn(closeBtn, 4);
		headerGrid.Children().Append(closeBtn);

		rootPanel.Children().Append(headerGrid);

		// Subtle Header Separator
		Border separatorTop;
		separatorTop.Height(1);
		separatorTop.Margin({ 0, 10, 0, 6 });
		separatorTop.Background(SolidColorBrush(winrt::Windows::UI::Color{ 0x22, 0x80, 0x80, 0x80 }));
		rootPanel.Children().Append(separatorTop);

		// Device List in ScrollViewer
		ScrollViewer scrollViewer;
		scrollViewer.MaxHeight(360);
		scrollViewer.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);

		StackPanel listPanel;
		g_deviceListPanel = listPanel;
		PopulateDeviceList(listPanel, devices);

		scrollViewer.Content(listPanel);
		rootPanel.Children().Append(scrollViewer);

		rootBorder.Child(rootPanel);

		Flyout flyout;
		flyout.Content(rootBorder);
		flyout.ShouldConstrainToRootBounds(false);
		flyout.Closed([](const auto&, const auto&) {
			g_deviceListPanel = nullptr;
			g_panelBadgeText = nullptr;
			g_panelDisconnectAllBtn = nullptr;
			ShowWindow(g_hWnd, SW_HIDE);
		});

		g_deviceFlyout = flyout;
		g_deviceFlyout.ShowAt(g_xamlCanvas);
	}
	catch (...)
	{
		LOG_CAUGHT_EXCEPTION();
	}
}

void ShowDevicePanel()
{
	RefreshDevicePanelAsync(true);
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
	case WM_UPDATE_DEVICE_PANEL:
		UpdateDevicePanelUI();
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
			ShowDevicePanel();
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
				ConnectDevice(i);
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

	// Separator
	MenuFlyoutSeparator subSeparator;
	settingsSubMenu.Items().Append(subSeparator);

	// 5. Multi-device concurrent mode
	ToggleMenuFlyoutItem multiDeviceItem;
	multiDeviceItem.Text(_(L"Multi-device concurrent mode"));
	multiDeviceItem.IsChecked(g_multiDeviceMode);
	multiDeviceItem.Click([](const auto& sender, const auto&) {
		g_multiDeviceMode = sender.as<ToggleMenuFlyoutItem>().IsChecked();
		// If switching back to single device mode and multiple are connected, retain only first
		if (!g_multiDeviceMode && g_audioPlaybackConnections.size() > 1)
		{
			auto firstId = g_audioPlaybackConnections.begin()->first;
			std::vector<std::wstring> toRemove;
			for (const auto& conn : g_audioPlaybackConnections)
			{
				if (conn.first != firstId)
				{
					toRemove.push_back(conn.first);
				}
			}
			for (const auto& id : toRemove)
			{
				auto it = g_audioPlaybackConnections.find(id);
				if (it != g_audioPlaybackConnections.end())
				{
					try { it->second.connection.Close(); } catch (...) {}
					g_audioPlaybackConnections.erase(it);
				}
			}
		}
		SaveSettings();
		UpdateTrayTooltip();
		UpdateDevicePanelUI();
	});
	settingsSubMenu.Items().Append(multiDeviceItem);

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

winrt::fire_and_forget ConnectDevice(DeviceInformation device)
{
	std::wstring deviceId = device.Id().c_str();
	std::wstring deviceName = device.Name().c_str();

	// In single-device mode, cleanly disconnect any existing connected device first
	if (!g_multiDeviceMode && !g_audioPlaybackConnections.empty())
	{
		auto existingIt = g_audioPlaybackConnections.find(deviceId);
		if (existingIt != g_audioPlaybackConnections.end())
		{
			co_return; // Already connected
		}

		for (const auto& conn : g_audioPlaybackConnections)
		{
			try { conn.second.connection.Close(); } catch (...) {}
		}
		g_audioPlaybackConnections.clear();
	}

	g_connectingDeviceIds.insert(deviceId);
	g_deviceErrorMessages.erase(deviceId);
	UpdateDevicePanelUI();

	bool success = false;
	std::wstring errorMessage;
	AudioPlaybackConnection activeConnection{ nullptr };

	try
	{
		// Clean up existing connection for this specific device if present
		auto existingIt = g_audioPlaybackConnections.find(deviceId);
		if (existingIt != g_audioPlaybackConnections.end())
		{
			try { existingIt->second.connection.Close(); } catch (...) {}
			g_audioPlaybackConnections.erase(existingIt);
		}

		CheckAudioEndpointVolume();

		auto connection = AudioPlaybackConnection::TryCreateFromId(device.Id());
		if (connection)
		{
			connection.StateChanged([deviceId](const auto& sender, const auto&) {
				if (sender.State() == AudioPlaybackConnectionState::Closed)
				{
					auto it = g_audioPlaybackConnections.find(deviceId);
					if (it != g_audioPlaybackConnections.end())
					{
						g_lostConnectionsInCurrentSession.insert(deviceId);
						g_audioPlaybackConnections.erase(it);
						UpdateTrayTooltip();
						UpdateAudioThreadPriority(!g_audioPlaybackConnections.empty());
						UpdatePowerLock(!g_audioPlaybackConnections.empty());
						PostMessageW(g_hWnd, WM_UPDATE_DEVICE_PANEL, 0, 0);
					}
				}
			});

			co_await connection.StartAsync();
			auto result = co_await connection.OpenAsync();

			switch (result.Status())
			{
			case AudioPlaybackConnectionOpenResultStatus::Success:
				success = true;
				activeConnection = connection;
				break;
			case AudioPlaybackConnectionOpenResultStatus::RequestTimedOut:
				success = false;
				errorMessage = _(L"The request timed out");
				break;
			case AudioPlaybackConnectionOpenResultStatus::DeniedBySystem:
				success = false;
				errorMessage = _(L"Hardware limit reached (Max 2 devices). Please disconnect one first.");
				break;
			case AudioPlaybackConnectionOpenResultStatus::UnknownFailure:
				success = false;
				errorMessage = _(L"Hardware limit reached (Max 2 devices). Please disconnect one first.");
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
		errorMessage = ex.message().c_str();
		LOG_CAUGHT_EXCEPTION();
	}

	g_connectingDeviceIds.erase(deviceId);

	if (success && activeConnection)
	{
		g_audioPlaybackConnections.insert_or_assign(deviceId, ConnectedDeviceInfo{ device, activeConnection, deviceName });
		g_lostConnectionsInCurrentSession.erase(deviceId);
		g_deviceErrorMessages.erase(deviceId);
		SaveSettings();
		UpdateTrayTooltip();
		UpdateAudioThreadPriority(true);
		UpdatePowerLock(true);

		// Restore saved volume level
		float savedVol = GetDeviceVolume(deviceId);
		SetDeviceVolume(deviceId, savedVol);
	}
	else
	{
		g_deviceErrorMessages[deviceId] = errorMessage;
		auto it = g_audioPlaybackConnections.find(deviceId);
		if (it != g_audioPlaybackConnections.end())
		{
			try { it->second.connection.Close(); } catch (...) {}
			g_audioPlaybackConnections.erase(it);
		}
		UpdateTrayTooltip();
		UpdateAudioThreadPriority(!g_audioPlaybackConnections.empty());
		UpdatePowerLock(!g_audioPlaybackConnections.empty());
	}

	PostMessageW(g_hWnd, WM_UPDATE_DEVICE_PANEL, 0, 0);
}

winrt::fire_and_forget ConnectDevice(std::wstring deviceId)
{
	auto device = co_await DeviceInformation::CreateFromIdAsync(deviceId);
	ConnectDevice(device);
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
		ConnectDevice(dev);
	}
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
