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
void ConnectPreferredOrLastDevice();
winrt::fire_and_forget ConnectDeviceByNameOrId(std::wstring target);
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
void CheckAudioMeter();
void ExitApp();

// System Media Transport Controls (SMTC) & Windows Integration
void SetupSmtc(HWND hWnd)
{
	try
	{
		auto interop = winrt::get_activation_factory<winrt::Windows::Media::SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
		if (interop)
		{
			winrt::check_hresult(interop->GetForWindow(hWnd, winrt::guid_of<winrt::Windows::Media::SystemMediaTransportControls>(), winrt::put_abi(g_smtc)));
			if (g_smtc)
			{
				g_smtc.IsPlayEnabled(true);
				g_smtc.IsPauseEnabled(true);
				g_smtc.IsNextEnabled(true);
				g_smtc.IsPreviousEnabled(true);
				g_smtc.IsEnabled(false);

				g_smtcButtonToken = g_smtc.ButtonPressed([](const auto&, const winrt::Windows::Media::SystemMediaTransportControlsButtonPressedEventArgs& args) {
					if (!g_enableMediaKeyForwarding)
						return;
					switch (args.Button())
					{
					case winrt::Windows::Media::SystemMediaTransportControlsButton::Play:
					case winrt::Windows::Media::SystemMediaTransportControlsButton::Pause:
						SendMediaKey(VK_MEDIA_PLAY_PAUSE);
						break;
					case winrt::Windows::Media::SystemMediaTransportControlsButton::Next:
						SendMediaKey(VK_MEDIA_NEXT_TRACK);
						break;
					case winrt::Windows::Media::SystemMediaTransportControlsButton::Previous:
						SendMediaKey(VK_MEDIA_PREV_TRACK);
						break;
					default:
						break;
					}
				});
			}
		}
	}
	CATCH_LOG();
}

void UpdateSmtcState(bool hasConnections, bool isPlaying, std::wstring_view deviceName)
{
	if (!g_smtc)
		return;

	try
	{
		if (!hasConnections)
		{
			g_smtc.PlaybackStatus(winrt::Windows::Media::MediaPlaybackStatus::Closed);
			g_smtc.IsEnabled(false);
			return;
		}

		g_smtc.IsEnabled(true);
		g_smtc.PlaybackStatus(isPlaying ? winrt::Windows::Media::MediaPlaybackStatus::Playing : winrt::Windows::Media::MediaPlaybackStatus::Paused);

		auto updater = g_smtc.DisplayUpdater();
		updater.Type(winrt::Windows::Media::MediaPlaybackType::Music);
		if (!deviceName.empty())
		{
			updater.MusicProperties().Title(winrt::hstring(deviceName));
		}
		else if (!g_audioPlaybackConnections.empty())
		{
			updater.MusicProperties().Title(winrt::hstring(g_audioPlaybackConnections.begin()->second.name));
		}
		else
		{
			updater.MusicProperties().Title(L"Bluetooth Audio");
		}
		updater.MusicProperties().Artist(L"Bluetooth Audio Receiver");
		updater.Update();
	}
	CATCH_LOG();
}

void ShowTrayNotification(std::wstring_view title, std::wstring_view message)
{
	NOTIFYICONDATAW nid = g_nid;
	nid.uFlags |= NIF_INFO;
	nid.dwInfoFlags = NIIF_INFO | NIIF_LARGE_ICON;
	wcsncpy_s(nid.szInfoTitle, title.data(), _TRUNCATE);
	wcsncpy_s(nid.szInfo, message.data(), _TRUNCATE);
	Shell_NotifyIconW(NIM_MODIFY, &nid);
}

std::wstring GetDeviceCodecName(const DeviceInformation& dev)
{
	std::wstring name = dev.Name().c_str();
	for (auto& c : name) c = towlower(c);

	if (name.find(L"iphone") != std::wstring::npos ||
		name.find(L"ipad") != std::wstring::npos ||
		name.find(L"macbook") != std::wstring::npos ||
		name.find(L"mac") != std::wstring::npos ||
		name.find(L"airpods") != std::wstring::npos ||
		name.find(L"apple") != std::wstring::npos)
	{
		return L"AAC";
	}

	return L"AAC";
}

std::wstring GetStatusJsonString()
{
	JsonObject root;
	root.Insert(L"status", JsonValue::CreateStringValue(L"running"));
	root.Insert(L"version", JsonValue::CreateStringValue(L"1.0-beta2"));
	root.Insert(L"connectedCount", JsonValue::CreateNumberValue(static_cast<double>(g_audioPlaybackConnections.size())));
	root.Insert(L"isAudioPlaying", JsonValue::CreateBooleanValue(g_isAudioPlaying));
	root.Insert(L"multiDeviceMode", JsonValue::CreateBooleanValue(g_multiDeviceMode));
	root.Insert(L"enableMediaKeyForwarding", JsonValue::CreateBooleanValue(g_enableMediaKeyForwarding));
	root.Insert(L"enableConnectionNotifications", JsonValue::CreateBooleanValue(g_enableConnectionNotifications));
	root.Insert(L"language", JsonValue::CreateStringValue(g_language));

	if (!g_preferredDeviceId.empty())
	{
		root.Insert(L"preferredDeviceId", JsonValue::CreateStringValue(g_preferredDeviceId));
	}

	JsonArray devArray;
	for (const auto& pair : g_audioPlaybackConnections)
	{
		JsonObject devObj;
		devObj.Insert(L"id", JsonValue::CreateStringValue(pair.first));
		devObj.Insert(L"name", JsonValue::CreateStringValue(pair.second.name));
		devObj.Insert(L"connected", JsonValue::CreateBooleanValue(true));
		devObj.Insert(L"isPreferred", JsonValue::CreateBooleanValue(pair.first == g_preferredDeviceId));
		if (pair.second.device)
		{
			int bat = GetBatteryPercentFromDevice(pair.second.device);
			if (bat >= 0)
			{
				devObj.Insert(L"batteryPercent", JsonValue::CreateNumberValue(bat));
			}
			devObj.Insert(L"codec", JsonValue::CreateStringValue(GetDeviceCodecName(pair.second.device)));
		}
		devArray.Append(devObj);
	}
	root.Insert(L"connectedDevices", devArray);

	return root.Stringify().c_str();
}

winrt::fire_and_forget CheckForUpdatesAsync(bool manualTrigger)
{
	try
	{
		winrt::Windows::Web::Http::HttpClient client;
		client.DefaultRequestHeaders().UserAgent().TryParseAdd(L"AudioPlaybackConnector/1.0");

		auto uri = winrt::Windows::Foundation::Uri(L"https://api.github.com/repos/Souitou-iop/AudioPlaybackConnector/releases/latest");
		auto response = co_await client.GetAsync(uri);
		if (response.IsSuccessStatusCode())
		{
			auto jsonStr = co_await response.Content().ReadAsStringAsync();
			auto jsonObj = JsonObject::Parse(jsonStr);
			if (jsonObj.HasKey(L"tag_name"))
			{
				auto latestTag = std::wstring(jsonObj.Lookup(L"tag_name").GetString());
				auto htmlUrl = jsonObj.HasKey(L"html_url") ? std::wstring(jsonObj.Lookup(L"html_url").GetString()) : L"https://github.com/Souitou-iop/AudioPlaybackConnector/releases/latest";

				if (latestTag != L"v1.0-beta2" && !latestTag.empty())
				{
					std::wstring msg = std::wstring(_(L"A new version is available:")) + L" " + latestTag + L"\n" + std::wstring(_(L"Would you like to open GitHub to download it?"));
					int ret = MessageBoxW(g_hWnd, msg.c_str(), _(L"AudioPlaybackConnector Update"), MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST);
					if (ret == IDYES)
					{
						ShellExecuteW(nullptr, L"open", htmlUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					}
					co_return;
				}
				else if (manualTrigger)
				{
					MessageBoxW(g_hWnd, _(L"You are using the latest version (v1.0 Beta 2)."), _(L"Check for Updates"), MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
					co_return;
				}
			}
		}
		else if (manualTrigger)
		{
			MessageBoxW(g_hWnd, _(L"Failed to check for updates. Please check your internet connection."), _(L"Check for Updates"), MB_OK | MB_ICONWARNING | MB_TOPMOST);
		}
	}
	catch (...)
	{
		if (manualTrigger)
		{
			MessageBoxW(g_hWnd, _(L"Failed to check for updates. Please check your internet connection."), _(L"Check for Updates"), MB_OK | MB_ICONWARNING | MB_TOPMOST);
		}
	}
}

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
static wil::com_ptr<IAudioMeterInformation> g_audioMeterInfo;

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
			defaultDevice->Activate(__uuidof(IAudioMeterInformation), CLSCTX_INPROC_SERVER, nullptr, g_audioMeterInfo.put_void());
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
		g_audioMeterInfo.reset();
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

void CheckAudioMeter()
{
	if (g_audioPlaybackConnections.empty() || !g_deviceListPanel)
	{
		if (g_isAudioPlaying)
		{
			g_isAudioPlaying = false;
			UpdateSmtcState(!g_audioPlaybackConnections.empty(), false);
			for (auto& pair : g_deviceStatusTextBlocks)
			{
				if (pair.second)
				{
					pair.second.Text(_(L"Connected"));
					pair.second.Foreground(SolidColorBrush(winrt::Windows::UI::Color{ 0xB0, 0xFF, 0xFF, 0xFF }));
				}
			}
		}
		return;
	}

	float peak = 0.0f;
	if (!g_audioMeterInfo && g_deviceEnumerator)
	{
		wil::com_ptr<IMMDevice> defaultDevice;
		if (SUCCEEDED(g_deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, defaultDevice.put())) && defaultDevice)
		{
			defaultDevice->Activate(__uuidof(IAudioMeterInformation), CLSCTX_INPROC_SERVER, nullptr, g_audioMeterInfo.put_void());
		}
	}

	if (g_audioMeterInfo)
	{
		if (FAILED(g_audioMeterInfo->GetPeakValue(&peak)))
		{
			g_audioMeterInfo.reset();
		}
	}

	bool nowPlaying = (peak > 0.001f);
	if (nowPlaying != g_isAudioPlaying)
	{
		g_isAudioPlaying = nowPlaying;
		UpdateSmtcState(!g_audioPlaybackConnections.empty(), g_isAudioPlaying);
		for (auto& pair : g_deviceStatusTextBlocks)
		{
			if (pair.second)
			{
				if (g_isAudioPlaying)
				{
					std::wstring playingStr = L"● ";
					playingStr += _(L"Playing");
					pair.second.Text(playingStr);
					pair.second.Foreground(SolidColorBrush(winrt::Windows::UI::Color{ 0xFF, 0x5C, 0xD6, 0x5C })); // Fresh Green
				}
				else
				{
					pair.second.Text(_(L"Connected"));
					pair.second.Foreground(SolidColorBrush(winrt::Windows::UI::Color{ 0xB0, 0xFF, 0xFF, 0xFF }));
				}
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
	if (FAILED(g_deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, defaultDevice.put())) || !defaultDevice)
		return;

	// 1. Direct Master Endpoint Volume (Instantly scales physical speaker/headphone volume for all Bluetooth & System audio)
	wil::com_ptr<IAudioEndpointVolume> endpointVolume;
	if (SUCCEEDED(defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, endpointVolume.put_void())) && endpointVolume)
	{
		if (volume <= 0.001f)
		{
			endpointVolume->SetMute(TRUE, nullptr);
		}
		else
		{
			BOOL isMuted = FALSE;
			if (SUCCEEDED(endpointVolume->GetMute(&isMuted)) && isMuted)
			{
				endpointVolume->SetMute(FALSE, nullptr);
			}
			endpointVolume->SetMasterVolumeLevelScalar(volume, nullptr);
		}
	}

	// 2. Adjust active audio sessions
	wil::com_ptr<IAudioSessionManager2> sessionManager;
	if (SUCCEEDED(defaultDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, nullptr, sessionManager.put_void())) && sessionManager)
	{
		wil::com_ptr<IAudioSessionEnumerator> sessionEnumerator;
		if (SUCCEEDED(sessionManager->GetSessionEnumerator(sessionEnumerator.put())) && sessionEnumerator)
		{
			int count = 0;
			sessionEnumerator->GetCount(&count);
			for (int i = 0; i < count; ++i)
			{
				wil::com_ptr<IAudioSessionControl> control;
				if (FAILED(sessionEnumerator->GetSession(i, control.put())) || !control)
					continue;

				wil::com_ptr<IAudioSessionControl2> control2;
				if (SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2), control2.put_void())) && control2)
				{
					wil::com_ptr<ISimpleAudioVolume> simpleVol;
					if (SUCCEEDED(control->QueryInterface(__uuidof(ISimpleAudioVolume), simpleVol.put_void())) && simpleVol)
					{
						AudioSessionState state;
						if (SUCCEEDED(control2->GetState(&state)) && state == AudioSessionStateActive)
						{
							simpleVol->SetMasterVolume(volume, nullptr);
						}
					}
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

	if (g_deviceEnumerator)
	{
		wil::com_ptr<IMMDevice> defaultDevice;
		if (SUCCEEDED(g_deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, defaultDevice.put())) && defaultDevice)
		{
			wil::com_ptr<IAudioEndpointVolume> endpointVolume;
			if (SUCCEEDED(defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, endpointVolume.put_void())) && endpointVolume)
			{
				float curVol = 1.0f;
				if (SUCCEEDED(endpointVolume->GetMasterVolumeLevelScalar(&curVol)))
				{
					return curVol;
				}
			}
		}
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
				PostMessageW(g_hWnd, WM_UPDATE_DEVICE_PANEL, 0, 0);
			});
			g_deviceWatcher.Removed([](const DeviceWatcher&, const DeviceInformationUpdate&) {
				PostMessageW(g_hWnd, WM_UPDATE_DEVICE_PANEL, 0, 0);
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
				PostMessageW(g_hWnd, WM_UPDATE_DEVICE_PANEL, 0, 0);
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
		if (g_enableConnectionNotifications)
		{
			ShowTrayNotification(_(L"Bluetooth Audio Disconnected"), conn.second.name);
		}
	}
	g_audioPlaybackConnections.clear();
	g_lostConnectionsInCurrentSession.clear();
	g_deviceErrorMessages.clear();
	SaveSettings();
	UpdateTrayTooltip();
	UpdateAudioThreadPriority(false);
	UpdatePowerLock(false);
	UpdateSmtcState(false, false);
	UpdateDevicePanelUI();
}

winrt::fire_and_forget ConnectPreferredOrLastDeviceAsync()
{
	try
	{
		if (!g_preferredDeviceId.empty())
		{
			ConnectDevice(g_preferredDeviceId);
			co_return;
		}

		for (const auto& id : g_startupReconnectDevices)
		{
			if (!id.empty())
			{
				ConnectDevice(id);
				co_return;
			}
		}

		auto devices = co_await DeviceInformation::FindAllAsync(AudioPlaybackConnection::GetDeviceSelector());
		if (devices.Size() > 0)
		{
			ConnectDevice(devices.GetAt(0));
		}
		else
		{
			ShowDevicePanel();
		}
	}
	catch (...)
	{
		LOG_CAUGHT_EXCEPTION();
	}
}

void ConnectPreferredOrLastDevice()
{
	ConnectPreferredOrLastDeviceAsync();
}

void ToggleLastConnectedDevice()
{
	if (!g_audioPlaybackConnections.empty())
	{
		DisconnectAllDevices();
	}
	else
	{
		ConnectPreferredOrLastDevice();
	}
}

winrt::fire_and_forget ConnectDeviceByNameOrId(std::wstring target)
{
	if (target.empty())
	{
		ConnectPreferredOrLastDevice();
		co_return;
	}

	try
	{
		auto devices = co_await DeviceInformation::FindAllAsync(AudioPlaybackConnection::GetDeviceSelector());
		for (const auto& dev : devices)
		{
			std::wstring id = dev.Id().c_str();
			std::wstring name = dev.Name().c_str();
			if (id == target || name == target || id.find(target) != std::wstring::npos || name.find(target) != std::wstring::npos)
			{
				ConnectDevice(dev);
				co_return;
			}
		}
	}
	catch (...) {}

	// Fallback to direct ID
	ConnectDevice(target);
}

void ExitApp()
{
	UpdateSmtcState(false, false);
	Shell_NotifyIconW(NIM_DELETE, &g_nid);
	if (g_hWnd && IsWindow(g_hWnd))
	{
		ShowWindow(g_hWnd, SW_HIDE);
	}
	SaveSettings();
	ExitProcess(0);
}

void SetLanguage(std::wstring_view langCode)
{
	g_language = langCode;
	SaveSettings();
	LoadTranslateData(g_language);
	SetupMenu();
	UpdateTrayTooltip();
	UpdateDevicePanelUI();
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
	if (g_panelRefreshAudioBtn)
	{
		g_panelRefreshAudioBtn.Visibility(!g_audioPlaybackConnections.empty() ? Visibility::Visible : Visibility::Collapsed);
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

int GetBatteryPercentFromDevice(const DeviceInformation& dev)
{
	try
	{
		auto props = dev.Properties();
		const wchar_t* keys[] = {
			L"System.Devices.Aep.Bluetooth.Le.BatteryLevel",
			L"System.Devices.BatteryLifePercent"
		};
		for (const auto& key : keys)
		{
			if (props.HasKey(key))
			{
				auto obj = props.Lookup(key);
				if (obj)
				{
					auto propVal = obj.try_as<winrt::Windows::Foundation::IPropertyValue>();
					if (propVal)
					{
						int pct = -1;
						switch (propVal.Type())
						{
						case winrt::Windows::Foundation::PropertyType::UInt8:
							pct = static_cast<int>(propVal.GetUInt8());
							break;
						case winrt::Windows::Foundation::PropertyType::Int32:
							pct = propVal.GetInt32();
							break;
						case winrt::Windows::Foundation::PropertyType::UInt32:
							pct = static_cast<int>(propVal.GetUInt32());
							break;
						case winrt::Windows::Foundation::PropertyType::Int16:
							pct = static_cast<int>(propVal.GetInt16());
							break;
						case winrt::Windows::Foundation::PropertyType::UInt16:
							pct = static_cast<int>(propVal.GetUInt16());
							break;
						default:
							break;
						}
						if (pct >= 0 && pct <= 100)
						{
							return pct;
						}
					}
				}
			}
		}
	}
	catch (...) {}
	return -1;
}

void PopulateDeviceList(StackPanel targetPanel, const winrt::Windows::Foundation::Collections::IVectorView<DeviceInformation>& devices)
{
	targetPanel.Children().Clear();
	g_deviceStatusTextBlocks.clear();

	if (devices.Size() == 0)
	{
		StackPanel emptyPanel;
		emptyPanel.HorizontalAlignment(HorizontalAlignment::Center);
		emptyPanel.Margin({ 0, 16, 0, 16 });

		FontIcon emptyIcon = CreateFontIcon(L"\uE702", 28);
		emptyIcon.HorizontalAlignment(HorizontalAlignment::Center);
		emptyIcon.Opacity(0.5);
		emptyPanel.Children().Append(emptyIcon);

		TextBlock emptyText;
		emptyText.Text(_(L"No paired Bluetooth devices"));
		emptyText.FontSize(13);
		emptyText.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
		emptyText.Margin({ 0, 8, 0, 2 });
		emptyText.HorizontalAlignment(HorizontalAlignment::Center);
		emptyPanel.Children().Append(emptyText);

		TextBlock emptySubtext;
		emptySubtext.Text(_(L"Pair your phone in Windows Settings first"));
		emptySubtext.FontSize(11);
		emptySubtext.Opacity(0.6);
		emptySubtext.HorizontalAlignment(HorizontalAlignment::Center);
		emptyPanel.Children().Append(emptySubtext);

		Button pairBtn;
		pairBtn.Content(winrt::box_value(_(L"＋ Pair New Device")));
		pairBtn.FontSize(11);
		pairBtn.HorizontalAlignment(HorizontalAlignment::Center);
		pairBtn.Margin({ 0, 10, 0, 0 });
		pairBtn.Click([](const auto&, const auto&) {
			winrt::Windows::System::Launcher::LaunchUriAsync(Uri(L"ms-settings:bluetooth"));
		});
		emptyPanel.Children().Append(pairBtn);

		targetPanel.Children().Append(emptyPanel);
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
		bool isPreferred = (!g_preferredDeviceId.empty() && g_preferredDeviceId == devId);
		int batteryPct = GetBatteryPercentFromDevice(dev);

		// Unified Card Border with fixed MinHeight for consistent visual banner size
		Border cardBorder;
		cardBorder.CornerRadius(CornerRadius{ 8, 8, 8, 8 });
		cardBorder.Padding({ 12, 8, 12, 8 });
		cardBorder.Margin({ 0, 3, 0, 3 });
		cardBorder.MinHeight(54);
		cardBorder.Background(SolidColorBrush(winrt::Windows::UI::Color{ 0x18, 0x80, 0x80, 0x80 }));

		// Top Row: Star (24px) + Icon (24px) + Name & Status (1 Star) + Action (60px)
		Grid topGrid;
		topGrid.VerticalAlignment(VerticalAlignment::Center);

		ColumnDefinition colStar, colIcon, colName, colAction;
		colStar.Width(GridLength{ 24, GridUnitType::Pixel });
		colIcon.Width(GridLength{ 24, GridUnitType::Pixel });
		colName.Width(GridLength{ 1, GridUnitType::Star });
		colAction.Width(GridLength{ 60, GridUnitType::Pixel });
		topGrid.ColumnDefinitions().Append(colStar);
		topGrid.ColumnDefinitions().Append(colIcon);
		topGrid.ColumnDefinitions().Append(colName);
		topGrid.ColumnDefinitions().Append(colAction);

		// Star Preferred Button
		Button starBtn;
		starBtn.Background(SolidColorBrush(winrt::Windows::UI::Color{ 0, 0, 0, 0 }));
		starBtn.BorderThickness({ 0, 0, 0, 0 });
		starBtn.Padding({ 0, 0, 0, 0 });
		starBtn.Width(24);
		starBtn.Height(24);
		starBtn.HorizontalAlignment(HorizontalAlignment::Center);
		starBtn.VerticalAlignment(VerticalAlignment::Center);

		FontIcon starIcon = CreateFontIcon(isPreferred ? L"\uE735" : L"\uE734", 13);
		if (isPreferred)
		{
			starIcon.Foreground(SolidColorBrush(winrt::Windows::UI::Color{ 0xFF, 0xFF, 0xC1, 0x07 })); // Amber Gold
			starIcon.Opacity(1.0);
		}
		else
		{
			starIcon.Foreground(SolidColorBrush(winrt::Windows::UI::Color{ 0xFF, 0xFF, 0xFF, 0xFF }));
			starIcon.Opacity(0.35);
		}
		starBtn.Content(starIcon);
		starBtn.Click([devId, isPreferred](const auto&, const auto&) {
			if (isPreferred)
			{
				g_preferredDeviceId.clear();
			}
			else
			{
				g_preferredDeviceId = devId;
			}
			SaveSettings();
			UpdateDevicePanelUI();
		});
		Grid::SetColumn(starBtn, 0);
		topGrid.Children().Append(starBtn);

		// Phone Icon
		FontIcon phoneIcon = CreateFontIcon(L"\uE8EA", 16);
		phoneIcon.HorizontalAlignment(HorizontalAlignment::Center);
		phoneIcon.VerticalAlignment(VerticalAlignment::Center);
		phoneIcon.Opacity(0.85);
		Grid::SetColumn(phoneIcon, 1);
		topGrid.Children().Append(phoneIcon);

		// Name & Status Panel
		StackPanel namePanel;
		namePanel.VerticalAlignment(VerticalAlignment::Center);
		namePanel.Margin({ 8, 0, 6, 0 });

		// Name + Battery Row
		StackPanel titleRow;
		titleRow.Orientation(Orientation::Horizontal);
		titleRow.VerticalAlignment(VerticalAlignment::Center);

		TextBlock nameText;
		nameText.Text(devName);
		nameText.FontSize(13);
		nameText.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
		nameText.TextTrimming(TextTrimming::CharacterEllipsis);
		nameText.MaxWidth(140);
		titleRow.Children().Append(nameText);

		std::wstring codec = GetDeviceCodecName(dev);
		if (!codec.empty())
		{
			Border codecBadge;
			codecBadge.CornerRadius(CornerRadius{ 3, 3, 3, 3 });
			codecBadge.Padding({ 4, 1, 4, 1 });
			codecBadge.Margin({ 5, 0, 0, 0 });
			codecBadge.VerticalAlignment(VerticalAlignment::Center);
			if (codec == L"AAC")
			{
				codecBadge.Background(SolidColorBrush(winrt::Windows::UI::Color{ 0x30, 0x00, 0x78, 0xD4 }));
				TextBlock codecText;
				codecText.Text(L"AAC");
				codecText.FontSize(9.5);
				codecText.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
				codecText.Foreground(SolidColorBrush(winrt::Windows::UI::Color{ 0xFF, 0x60, 0xB0, 0xFF }));
				codecBadge.Child(codecText);
			}
			else
			{
				codecBadge.Background(SolidColorBrush(winrt::Windows::UI::Color{ 0x25, 0x80, 0x80, 0x80 }));
				TextBlock codecText;
				codecText.Text(codec);
				codecText.FontSize(9.5);
				codecText.FontWeight(winrt::Windows::UI::Text::FontWeights::Medium());
				codecText.Foreground(SolidColorBrush(winrt::Windows::UI::Color{ 0xCC, 0xFF, 0xFF, 0xFF }));
				codecBadge.Child(codecText);
			}
			titleRow.Children().Append(codecBadge);
		}

		if (batteryPct >= 0)
		{
			StackPanel batPanel;
			batPanel.Orientation(Orientation::Horizontal);
			batPanel.VerticalAlignment(VerticalAlignment::Center);
			batPanel.Margin({ 6, 0, 0, 0 });
			batPanel.Opacity(0.7);

			const wchar_t* batGlyph = L"\uE859";
			if (batteryPct <= 5) batGlyph = L"\uE83F";
			else if (batteryPct <= 15) batGlyph = L"\uE850";
			else if (batteryPct <= 25) batGlyph = L"\uE851";
			else if (batteryPct <= 35) batGlyph = L"\uE852";
			else if (batteryPct <= 45) batGlyph = L"\uE853";
			else if (batteryPct <= 55) batGlyph = L"\uE854";
			else if (batteryPct <= 65) batGlyph = L"\uE855";
			else if (batteryPct <= 75) batGlyph = L"\uE856";
			else if (batteryPct <= 85) batGlyph = L"\uE857";
			else if (batteryPct <= 95) batGlyph = L"\uE858";

			FontIcon batIcon = CreateFontIcon(batGlyph, 11);
			batIcon.VerticalAlignment(VerticalAlignment::Center);
			batPanel.Children().Append(batIcon);

			TextBlock batValText;
			wchar_t batBuf[16];
			swprintf_s(batBuf, L" %d%%", batteryPct);
			batValText.Text(batBuf);
			batValText.FontSize(11);
			batValText.VerticalAlignment(VerticalAlignment::Center);
			batPanel.Children().Append(batValText);

			titleRow.Children().Append(batPanel);
		}

		namePanel.Children().Append(titleRow);

		if (isConnected)
		{
			TextBlock statusText;
			statusText.Margin({ 0, 2, 0, 0 });
			if (g_isAudioPlaying)
			{
				std::wstring playingStr = L"● ";
				playingStr += _(L"Playing");
				statusText.Text(playingStr);
				statusText.Foreground(SolidColorBrush(winrt::Windows::UI::Color{ 0xFF, 0x5C, 0xD6, 0x5C }));
			}
			else
			{
				statusText.Text(_(L"Connected"));
				statusText.Opacity(0.7);
			}
			statusText.FontSize(11);
			g_deviceStatusTextBlocks[devId] = statusText;
			namePanel.Children().Append(statusText);
		}
		else if (hasError)
		{
			TextBlock errorText;
			errorText.Margin({ 0, 2, 0, 0 });
			errorText.Text(g_deviceErrorMessages[devId]);
			errorText.FontSize(11);
			errorText.Foreground(SolidColorBrush(winrt::Windows::UI::Color{ 0xFF, 0xE7, 0x48, 0x56 }));
			errorText.TextWrapping(TextWrapping::Wrap);
			namePanel.Children().Append(errorText);
		}

		Grid::SetColumn(namePanel, 2);
		topGrid.Children().Append(namePanel);

		// Action Button / Progress - Unified 60x28 dimensions for perfect alignment
		if (isConnecting)
		{
			Grid ringBox;
			ringBox.Width(60);
			ringBox.Height(28);
			ringBox.HorizontalAlignment(HorizontalAlignment::Center);
			ringBox.VerticalAlignment(VerticalAlignment::Center);

			ProgressRing ring;
			ring.IsActive(true);
			ring.Width(18);
			ring.Height(18);
			ring.HorizontalAlignment(HorizontalAlignment::Center);
			ring.VerticalAlignment(VerticalAlignment::Center);
			ringBox.Children().Append(ring);

			Grid::SetColumn(ringBox, 3);
			topGrid.Children().Append(ringBox);
		}
		else if (isConnected)
		{
			Button disconnectBtn;
			disconnectBtn.Content(winrt::box_value(_(L"Disconnect")));
			disconnectBtn.Width(60);
			disconnectBtn.Height(28);
			disconnectBtn.Padding({ 0, 0, 0, 0 });
			disconnectBtn.FontSize(11);
			disconnectBtn.HorizontalAlignment(HorizontalAlignment::Center);
			disconnectBtn.VerticalAlignment(VerticalAlignment::Center);
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
			Grid::SetColumn(disconnectBtn, 3);
			topGrid.Children().Append(disconnectBtn);
		}
		else
		{
			Button connectBtn;
			connectBtn.Content(winrt::box_value(_(L"Connect")));
			connectBtn.Width(60);
			connectBtn.Height(28);
			connectBtn.Padding({ 0, 0, 0, 0 });
			connectBtn.FontSize(11);
			connectBtn.HorizontalAlignment(HorizontalAlignment::Center);
			connectBtn.VerticalAlignment(VerticalAlignment::Center);
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
			Grid::SetColumn(connectBtn, 3);
			topGrid.Children().Append(connectBtn);
		}

		cardBorder.Child(topGrid);
		targetPanel.Children().Append(cardBorder);
	}

	UpdateHeaderBadgeUI();
}

winrt::fire_and_forget RefreshDevicePanelAsync(bool forceReopen)
{
	try
	{
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

		RECT iconRect = {};
		auto hr = Shell_NotifyIconGetRect(&g_niid, &iconRect);
		if (FAILED(hr))
		{
			POINT pt;
			GetCursorPos(&pt);
			iconRect = { pt.x - 12, pt.y - 12, pt.x + 12, pt.y + 12 };
		}

		auto dpi = GetDpiForWindow(g_hWnd);
		int iconW = iconRect.right - iconRect.left;
		int iconH = iconRect.bottom - iconRect.top;
		if (iconW <= 0) iconW = 24;
		if (iconH <= 0) iconH = 24;

		SetWindowPos(g_hWnd, HWND_TOPMOST, iconRect.left, iconRect.top, iconW, iconH, SWP_SHOWWINDOW);
		SetWindowPos(g_hWndXaml, nullptr, 0, 0, iconW, iconH, SWP_NOZORDER | SWP_SHOWWINDOW);
		SetForegroundWindow(g_hWnd);

		g_xamlCanvas.Width(static_cast<float>(iconW * USER_DEFAULT_SCREEN_DPI / dpi));
		g_xamlCanvas.Height(static_cast<float>(iconH * USER_DEFAULT_SCREEN_DPI / dpi));

		// Root Container Border
		Border rootBorder;
		rootBorder.Width(350);
		rootBorder.Padding({ 16, 14, 16, 14 });

		StackPanel rootPanel;

		// Header: Title + Capacity Badge + Disconnect All + Refresh Audio + Refresh List + Close Button
		Grid headerGrid;
		ColumnDefinition colTitle;
		ColumnDefinition colBadge;
		ColumnDefinition colDisconnectAll;
		ColumnDefinition colRefreshAudio;
		ColumnDefinition colRefresh;
		ColumnDefinition colClose;
		colTitle.Width(GridLength{ 1, GridUnitType::Star });
		colBadge.Width(GridLength{ 0, GridUnitType::Auto });
		colDisconnectAll.Width(GridLength{ 0, GridUnitType::Auto });
		colRefreshAudio.Width(GridLength{ 0, GridUnitType::Auto });
		colRefresh.Width(GridLength{ 0, GridUnitType::Auto });
		colClose.Width(GridLength{ 0, GridUnitType::Auto });
		headerGrid.ColumnDefinitions().Append(colTitle);
		headerGrid.ColumnDefinitions().Append(colBadge);
		headerGrid.ColumnDefinitions().Append(colDisconnectAll);
		headerGrid.ColumnDefinitions().Append(colRefreshAudio);
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

		// 1. Audio Connection Reconnect / Refresh Button (Streaming Wave Icon:  - Fix no sound)
		Button refreshAudioBtn;
		g_panelRefreshAudioBtn = refreshAudioBtn;
		FontIcon audioIcon = CreateFontIcon(L"\uE93E", 13); // Streaming (( · ))
		refreshAudioBtn.Content(audioIcon);
		refreshAudioBtn.Padding({ 6, 6, 6, 6 });
		refreshAudioBtn.Margin({ 0, 0, 4, 0 });
		ToolTipService::SetToolTip(refreshAudioBtn, winrt::box_value(_(L"Refresh Audio Connection (Fix no sound)")));
		refreshAudioBtn.Click([](const auto&, const auto&) {
			ReopenAudioConnections();
		});
		Grid::SetColumn(refreshAudioBtn, 3);
		headerGrid.Children().Append(refreshAudioBtn);

		// 2. Device List Scan / Refresh Button (Device Discovery Radar Icon: )
		Button refreshBtn;
		FontIcon refreshIcon = CreateFontIcon(L"\uEBDE", 13); // DeviceDiscovery
		refreshBtn.Content(refreshIcon);
		refreshBtn.Padding({ 6, 6, 6, 6 });
		refreshBtn.Margin({ 0, 0, 4, 0 });
		ToolTipService::SetToolTip(refreshBtn, winrt::box_value(_(L"Scan Bluetooth devices / Refresh list")));
		refreshBtn.Click([](const auto&, const auto&) {
			g_deviceErrorMessages.clear();
			RefreshDevicePanelAsync(false);
		});
		Grid::SetColumn(refreshBtn, 4);
		headerGrid.Children().Append(refreshBtn);

		// 3. Close Button (Close Icon: )
		Button closeBtn;
		FontIcon closeIcon = CreateFontIcon(L"\uE8BB", 11);
		closeBtn.Content(closeIcon);
		closeBtn.Padding({ 6, 6, 6, 6 });
		ToolTipService::SetToolTip(closeBtn, winrt::box_value(_(L"Close")));
		closeBtn.Click([](const auto&, const auto&) {
			if (g_deviceFlyout)
			{
				g_deviceFlyout.Hide();
			}
		});
		Grid::SetColumn(closeBtn, 5);
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
		flyout.Placement(FlyoutPlacementMode::Top);
		flyout.ShouldConstrainToRootBounds(false);
		flyout.Closed([](const auto&, const auto&) {
			KillTimer(g_hWnd, IDT_AUDIO_METER);
			g_deviceListPanel = nullptr;
			g_panelBadgeText = nullptr;
			g_panelDisconnectAllBtn = nullptr;
			g_panelRefreshAudioBtn = nullptr;
			g_deviceStatusTextBlocks.clear();
			g_deviceFlyout = nullptr;
			ShowWindow(g_hWnd, SW_HIDE);
		});

		g_deviceFlyout = flyout;
		g_deviceFlyout.ShowAt(g_xamlCanvas);
		SetTimer(g_hWnd, IDT_AUDIO_METER, 200, nullptr);
	}
	catch (...)
	{
		LOG_CAUGHT_EXCEPTION();
	}
}

void ShowDevicePanel()
{
	if (g_xamlMenu)
	{
		try { g_xamlMenu.Hide(); } catch (...) {}
	}
	if (g_deviceFlyout && g_deviceListPanel)
	{
		g_deviceFlyout.Hide();
		return;
	}
	RefreshDevicePanelAsync(true);
}

static void HandleCliCommand(const std::wstring& cmdLine)
{
	if (cmdLine.find(L"/Status") != std::wstring::npos || cmdLine.find(L"-status") != std::wstring::npos || cmdLine.find(L"/status") != std::wstring::npos || cmdLine.find(L"/json") != std::wstring::npos)
	{
		std::wstring json = GetStatusJsonString();
		if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole())
		{
			HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
			if (hStdOut && hStdOut != INVALID_HANDLE_VALUE)
			{
				std::string utf8 = Utf16ToUtf8(json + L"\r\n");
				DWORD written = 0;
				WriteFile(hStdOut, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
			}
			FreeConsole();
		}
		return;
	}

	if (cmdLine.empty() || cmdLine.find(L"/Show") != std::wstring::npos || cmdLine.find(L"/s") != std::wstring::npos)
	{
		ShowDevicePanel();
		return;
	}

	if (cmdLine.find(L"/Disconnect") != std::wstring::npos || cmdLine.find(L"/d") != std::wstring::npos)
	{
		DisconnectAllDevices();
		return;
	}

	if (cmdLine.find(L"/Toggle") != std::wstring::npos || cmdLine.find(L"/t") != std::wstring::npos)
	{
		ToggleLastConnectedDevice();
		return;
	}

	if (cmdLine.find(L"/Exit") != std::wstring::npos || cmdLine.find(L"/q") != std::wstring::npos)
	{
		ExitApp();
		return;
	}

	auto connectPos = cmdLine.find(L"/Connect");
	if (connectPos == std::wstring::npos) connectPos = cmdLine.find(L"/c");
	if (connectPos != std::wstring::npos)
	{
		std::wstring arg;
		auto spacePos = cmdLine.find_first_of(L" 	", connectPos);
		if (spacePos != std::wstring::npos)
		{
			arg = cmdLine.substr(spacePos + 1);
			while (!arg.empty() && (arg.front() == L' ' || arg.front() == L'	' || arg.front() == L'"')) arg.erase(0, 1);
			while (!arg.empty() && (arg.back() == L' ' || arg.back() == L'	' || arg.back() == L'"')) arg.pop_back();
		}
		ConnectDeviceByNameOrId(arg);
		return;
	}
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);

	// Single Instance Mutex
	HANDLE hMutex = CreateMutexW(nullptr, FALSE, L"Global\\AudioPlaybackConnector_Instance_Mutex");
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		std::wstring cmd = (lpCmdLine && wcslen(lpCmdLine) > 0) ? lpCmdLine : L"/Show";
		if (cmd.find(L"/Status") != std::wstring::npos || cmd.find(L"-status") != std::wstring::npos || cmd.find(L"/status") != std::wstring::npos || cmd.find(L"/json") != std::wstring::npos)
		{
			LoadSettings();
			std::wstring json = GetStatusJsonString();
			if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole())
			{
				HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
				if (hStdOut && hStdOut != INVALID_HANDLE_VALUE)
				{
					std::string utf8 = Utf16ToUtf8(json + L"\r\n");
					DWORD written = 0;
					WriteFile(hStdOut, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
				}
				FreeConsole();
			}
			if (hMutex) CloseHandle(hMutex);
			ExitProcess(0);
		}

		HWND hExisting = FindWindowW(L"AudioPlaybackConnector", nullptr);
		if (hExisting)
		{
			COPYDATASTRUCT cds = {
				.dwData = 1,
				.cbData = static_cast<DWORD>((cmd.length() + 1) * sizeof(wchar_t)),
				.lpData = const_cast<wchar_t*>(cmd.c_str())
			};
			SendMessageW(hExisting, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
		}
		if (hMutex) CloseHandle(hMutex);
		ExitProcess(0);
	}

	g_hInst = hInstance;
	LoadSettings();
	LoadTranslateData(g_language);

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
	SetupSmtc(g_hWnd);

	LoadSettings();
	SetupDeviceWatcher(g_autoConnectNearby);
	SetupMenu();
	SetupSvgIcon();

	g_nid.hWnd = g_niid.hWnd = g_hWnd;
	wcscpy_s(g_nid.szTip, _(L"AudioPlaybackConnector"));
	UpdateNotifyIcon();

	WM_TASKBAR_CREATED = RegisterWindowMessageW(L"TaskbarCreated");
	LOG_LAST_ERROR_IF(WM_TASKBAR_CREATED == 0);

	// Process initial command line if any
	if (lpCmdLine && wcslen(lpCmdLine) > 0)
	{
		HandleCliCommand(lpCmdLine);
	}
	else
	{
		PostMessageW(g_hWnd, WM_CONNECTDEVICE, 0, 0);
	}

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
	case WM_TIMER:
		if (wParam == IDT_AUDIO_METER)
		{
			CheckAudioMeter();
		}
		break;
	case WM_COPYDATA:
	{
		auto cds = reinterpret_cast<PCOPYDATASTRUCT>(lParam);
		if (cds && cds->lpData)
		{
			std::wstring cmd = reinterpret_cast<const wchar_t*>(cds->lpData);
			HandleCliCommand(cmd);
			return TRUE;
		}
		break;
	}
	case WM_APPCOMMAND:
	{
		if (g_enableMediaKeyForwarding && !g_audioPlaybackConnections.empty())
		{
			// Let system media transport controls handle or forward
			return DefWindowProcW(hWnd, message, wParam, lParam);
		}
		break;
	}
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
			if (g_deviceFlyout)
			{
				try { g_deviceFlyout.Hide(); } catch (...) {}
				g_deviceFlyout = nullptr;
			}

			if (g_menuFocusState == FocusState::Unfocused)
				g_menuFocusState = FocusState::Keyboard;

			POINT pt = {};
			if (!GetCursorPos(&pt) || (pt.x == 0 && pt.y == 0))
			{
				RECT iconRect = {};
				if (SUCCEEDED(Shell_NotifyIconGetRect(&g_niid, &iconRect)))
				{
					pt.x = (iconRect.left + iconRect.right) / 2;
					pt.y = (iconRect.top + iconRect.bottom) / 2;
				}
				else
				{
					pt.x = GET_X_LPARAM(wParam);
					pt.y = GET_Y_LPARAM(wParam);
				}
			}

			auto dpi = GetDpiForWindow(hWnd);
			Point point = {
				static_cast<float>(pt.x * USER_DEFAULT_SCREEN_DPI / dpi),
				static_cast<float>(pt.y * USER_DEFAULT_SCREEN_DPI / dpi)
			};

			SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, 1, 1, SWP_SHOWWINDOW);
			SetWindowPos(g_hWndXaml, nullptr, 0, 0, 1, 1, SWP_NOZORDER | SWP_SHOWWINDOW);
			SetForegroundWindow(hWnd);

			g_xamlMenu.ShowAt(g_xamlCanvas, point);
		}
		break;
		}
		break;
	case WM_CONNECTDEVICE:
		// Startup auto-reconnect only if g_reconnect is true
		if (g_reconnect && !g_startupReconnectDevices.empty())
		{
			for (const auto& id : g_startupReconnectDevices)
			{
				ConnectDevice(id);
			}
		}
		break;
	default:
		if (message == WM_TASKBAR_CREATED)
		{
			UpdateNotifyIcon();
			return 0;
		}
		return DefWindowProcW(hWnd, message, wParam, lParam);
	}
	return 0;
}

void SetupMenu()
{
	// Refresh Audio Connection
	FontIcon refreshIcon = CreateFontIcon(L"\uE72C");

	MenuFlyoutItem refreshItem;
	refreshItem.Text(_(L"Refresh Audio Connection"));
	refreshItem.Icon(refreshIcon);
	refreshItem.Click([](const auto&, const auto&) {
		PostMessageW(g_hWnd, WM_REFRESH_AUDIO, 0, 0);
	});

	// Sound Settings
	FontIcon soundIcon = CreateFontIcon(L"\uE767");

	MenuFlyoutItem soundItem;
	soundItem.Text(_(L"Sound Settings"));
	soundItem.Icon(soundIcon);
	soundItem.Click([](const auto&, const auto&) {
		winrt::Windows::System::Launcher::LaunchUriAsync(Uri(L"ms-settings:sound"));
	});

	// Bluetooth Settings
	FontIcon btIcon = CreateFontIcon(L"\uE702");

	MenuFlyoutItem btItem;
	btItem.Text(_(L"Bluetooth Settings"));
	btItem.Icon(btIcon);
	btItem.Click([](const auto&, const auto&) {
		winrt::Windows::System::Launcher::LaunchUriAsync(Uri(L"ms-settings:bluetooth"));
	});

	// Settings Submenu
	FontIcon configIcon = CreateFontIcon(L"\uE713");

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

	// 5. Media key forwarding
	ToggleMenuFlyoutItem mediaKeyItem;
	mediaKeyItem.Text(_(L"Enable media key forwarding"));
	mediaKeyItem.IsChecked(g_enableMediaKeyForwarding);
	mediaKeyItem.Click([](const auto& sender, const auto&) {
		g_enableMediaKeyForwarding = sender.as<ToggleMenuFlyoutItem>().IsChecked();
		SaveSettings();
	});
	settingsSubMenu.Items().Append(mediaKeyItem);

	// 6. Connection notifications
	ToggleMenuFlyoutItem notificationItem;
	notificationItem.Text(_(L"Show connection notifications"));
	notificationItem.IsChecked(g_enableConnectionNotifications);
	notificationItem.Click([](const auto& sender, const auto&) {
		g_enableConnectionNotifications = sender.as<ToggleMenuFlyoutItem>().IsChecked();
		SaveSettings();
	});
	settingsSubMenu.Items().Append(notificationItem);

	// 7. Language Submenu
	FontIcon langIcon = CreateFontIcon(L"\uE774");
	MenuFlyoutSubItem langSubMenu;
	langSubMenu.Text(_(L"Language"));
	langSubMenu.Icon(langIcon);

	struct LangOption {
		std::wstring code;
		std::wstring label;
	};

	std::vector<LangOption> langOptions = {
		{ L"auto", _(L"System Default") },
		{ L"zh-CN", L"简体中文" },
		{ L"zh-TW", L"繁體中文" },
		{ L"en-US", L"English" },
		{ L"ja-JP", L"日本語" },
		{ L"ko-KR", L"한국어" }
	};

	for (const auto& opt : langOptions)
	{
		ToggleMenuFlyoutItem item;
		item.Text(opt.label);
		item.IsChecked(g_language == opt.code);
		item.Click([code = opt.code](const auto&, const auto&) {
			SetLanguage(code);
		});
		langSubMenu.Items().Append(item);
	}
	settingsSubMenu.Items().Append(langSubMenu);

	// Separator
	MenuFlyoutSeparator subSeparator;
	settingsSubMenu.Items().Append(subSeparator);

	// 6. Multi-device concurrent mode
	ToggleMenuFlyoutItem multiDeviceItem;
	multiDeviceItem.Text(_(L"Multi-device concurrent mode"));
	multiDeviceItem.IsChecked(g_multiDeviceMode);
	multiDeviceItem.Click([](const auto& sender, const auto&) {
		g_multiDeviceMode = sender.as<ToggleMenuFlyoutItem>().IsChecked();
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

	// Check for updates
	FontIcon updateIcon = CreateFontIcon(L"\uE895");
	MenuFlyoutItem updateItem;
	updateItem.Text(_(L"Check for Updates"));
	updateItem.Icon(updateIcon);
	updateItem.Click([](const auto&, const auto&) {
		CheckForUpdatesAsync(true);
	});

	// Main Separator
	MenuFlyoutSeparator mainSeparator;

	// Exit
	FontIcon closeIcon = CreateFontIcon(L"\uE8BB");

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
	menu.Items().Append(updateItem);
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
						std::wstring dName = it->second.name;
						g_lostConnectionsInCurrentSession.insert(deviceId);
						g_audioPlaybackConnections.erase(it);
						if (g_enableConnectionNotifications)
						{
							ShowTrayNotification(_(L"Bluetooth Audio Disconnected"), dName);
						}
						UpdateSmtcState(!g_audioPlaybackConnections.empty(), g_isAudioPlaying);
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

		if (g_enableConnectionNotifications)
		{
			ShowTrayNotification(_(L"Bluetooth Audio Connected"), deviceName + L" " + _(L"is ready to stream audio"));
		}
		UpdateSmtcState(true, g_isAudioPlaying, deviceName);

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
	try
	{
		if (deviceId.empty()) co_return;
		auto device = co_await DeviceInformation::CreateFromIdAsync(deviceId);
		if (device)
		{
			ConnectDevice(device);
		}
	}
	catch (...)
	{
		LOG_CAUGHT_EXCEPTION();
	}
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
	auto dpi = GetDpiForWindow(g_hWnd);
	auto iconSize = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
	auto res = GetModuleFsPath(g_hInst).remove_filename() / L"AudioPlaybackConnector.svg";
	auto resLegacy = GetModuleFsPath(g_hInst).remove_filename() / L"BluetoothAudioReceiver.svg";

	std::filesystem::path svgPath;
	if (fs::exists(res))
	{
		svgPath = res;
	}
	else if (fs::exists(resLegacy))
	{
		svgPath = resLegacy;
	}

	if (!svgPath.empty())
	{
		wil::unique_hfile hFile(CreateFileW(svgPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (hFile)
		{
			std::string svg;
			while (1)
			{
				size_t size = svg.size();
				svg.resize(size + BUFFER_SIZE);
				DWORD read = 0;
				THROW_IF_WIN32_BOOL_FALSE(ReadFile(hFile.get(), svg.data() + size, BUFFER_SIZE, &read, nullptr));
				svg.resize(size + read);
				if (read == 0)
					break;
			}

			if (!svg.empty())
			{
				g_hIconLight = SvgTohIcon(svg, iconSize, iconSize, D2D1::ColorF(D2D1::ColorF::Black));
				g_hIconDark = SvgTohIcon(svg, iconSize, iconSize, D2D1::ColorF(D2D1::ColorF::White));
			}
		}
	}
}

void UpdateNotifyIcon()
{
	DWORD value = 0, cbValue = sizeof(value);
	LOG_IF_WIN32_ERROR(RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)", L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &cbValue));
	g_nid.hIcon = value != 0 ? g_hIconLight : g_hIconDark;
	if (!g_nid.hIcon)
	{
		g_nid.hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_AUDIOPLAYBACKCONNECTOR));
	}

	if (!Shell_NotifyIconW(NIM_MODIFY, &g_nid))
	{
		if (Shell_NotifyIconW(NIM_ADD, &g_nid))
		{
			Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
		}
	}
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

