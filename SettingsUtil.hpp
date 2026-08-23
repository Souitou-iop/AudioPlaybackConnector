#pragma once

constexpr auto CONFIG_NAME = L"AudioPlaybackConnector.json";
constexpr auto BUFFER_SIZE = 4096;

inline bool IsRunAtStartupEnabled()
{
	HKEY hKey = nullptr;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Run)", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
	{
		DWORD type = 0;
		DWORD size = 0;
		LONG res = RegQueryValueExW(hKey, L"AudioPlaybackConnector", nullptr, &type, nullptr, &size);
		RegCloseKey(hKey);
		return (res == ERROR_SUCCESS && size > 0);
	}
	return false;
}

inline void SetRunAtStartup(bool enable)
{
	HKEY hKey = nullptr;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Run)", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
	{
		if (enable)
		{
			auto exePath = GetModuleFsPath(g_hInst).wstring();
			std::wstring value = L"\"" + exePath + L"\"";
			RegSetValueExW(hKey, L"AudioPlaybackConnector", 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.length() + 1) * sizeof(wchar_t)));
		}
		else
		{
			RegDeleteValueW(hKey, L"AudioPlaybackConnector");
		}
		RegCloseKey(hKey);
	}
}

inline void DefaultSettings()
{
	g_reconnect = false;
	g_startupReconnectDevices.clear();
	g_lostConnectionsInCurrentSession.clear();
	g_connectingDeviceIds.clear();
	g_runAtStartup = IsRunAtStartupEnabled();
	g_autoConnectNearby = false;
	g_preventSleepWhileStreaming = true;
	g_multiDeviceMode = false;
	g_enableConnectionNotifications = true;
	g_language = L"auto";
	g_preferredDeviceId.clear();
	g_deviceVolumes.clear();
}

inline void LoadSettings()
{
	try
	{
		DefaultSettings();

		wil::unique_hfile hFile(CreateFileW((GetModuleFsPath(g_hInst).remove_filename() / CONFIG_NAME).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (!hFile)
			return;

		std::string string;
		while (1)
		{
			size_t size = string.size();
			string.resize(size + BUFFER_SIZE);
			DWORD read = 0;
			THROW_IF_WIN32_BOOL_FALSE(ReadFile(hFile.get(), string.data() + size, BUFFER_SIZE, &read, nullptr));
			string.resize(size + read);
			if (read == 0)
				break;
		}

		if (string.empty())
			return;

		std::wstring utf16 = Utf8ToUtf16(string);
		auto jsonObj = JsonObject::Parse(utf16);
		if (jsonObj.HasKey(L"reconnect"))
			g_reconnect = jsonObj.Lookup(L"reconnect").GetBoolean();

		if (jsonObj.HasKey(L"runAtStartup"))
			g_runAtStartup = jsonObj.Lookup(L"runAtStartup").GetBoolean();
		else
			g_runAtStartup = IsRunAtStartupEnabled();

		if (jsonObj.HasKey(L"autoConnectNearby"))
			g_autoConnectNearby = jsonObj.Lookup(L"autoConnectNearby").GetBoolean();

		if (jsonObj.HasKey(L"preventSleepWhileStreaming"))
			g_preventSleepWhileStreaming = jsonObj.Lookup(L"preventSleepWhileStreaming").GetBoolean();

		if (jsonObj.HasKey(L"multiDeviceMode"))
			g_multiDeviceMode = jsonObj.Lookup(L"multiDeviceMode").GetBoolean();

		if (jsonObj.HasKey(L"enableConnectionNotifications"))
			g_enableConnectionNotifications = jsonObj.Lookup(L"enableConnectionNotifications").GetBoolean();

		if (jsonObj.HasKey(L"language"))
			g_language = jsonObj.Lookup(L"language").GetString().c_str();

		if (jsonObj.HasKey(L"preferredDeviceId"))
			g_preferredDeviceId = jsonObj.Lookup(L"preferredDeviceId").GetString().c_str();

		if (jsonObj.HasKey(L"deviceVolumes"))
		{
			auto devVols = jsonObj.Lookup(L"deviceVolumes").GetObject();
			for (const auto& pair : devVols)
			{
				g_deviceVolumes[std::wstring(pair.Key())] = pair.Value().GetNumber();
			}
		}

		// Only load startup devices if g_reconnect is enabled by the user
		if (g_reconnect && jsonObj.HasKey(L"lastDevices"))
		{
			auto lastDevices = jsonObj.Lookup(L"lastDevices").GetArray();
			g_startupReconnectDevices.reserve(lastDevices.Size());
			for (const auto& i : lastDevices)
			{
				if (i.ValueType() == JsonValueType::String)
					g_startupReconnectDevices.push_back(std::wstring(i.GetString()));
			}
		}
		else
		{
			g_startupReconnectDevices.clear();
		}
	}
	CATCH_LOG();
}

inline void SaveSettings()
{
	try
	{
		JsonObject jsonObj;
		jsonObj.Insert(L"reconnect", JsonValue::CreateBooleanValue(g_reconnect));
		jsonObj.Insert(L"runAtStartup", JsonValue::CreateBooleanValue(g_runAtStartup));
		jsonObj.Insert(L"autoConnectNearby", JsonValue::CreateBooleanValue(g_autoConnectNearby));
		jsonObj.Insert(L"preventSleepWhileStreaming", JsonValue::CreateBooleanValue(g_preventSleepWhileStreaming));
		jsonObj.Insert(L"multiDeviceMode", JsonValue::CreateBooleanValue(g_multiDeviceMode));
		jsonObj.Insert(L"enableConnectionNotifications", JsonValue::CreateBooleanValue(g_enableConnectionNotifications));
		jsonObj.Insert(L"language", JsonValue::CreateStringValue(g_language));

		if (!g_preferredDeviceId.empty())
		{
			jsonObj.Insert(L"preferredDeviceId", JsonValue::CreateStringValue(g_preferredDeviceId));
		}

		JsonObject devVols;
		for (const auto& pair : g_deviceVolumes)
		{
			devVols.Insert(pair.first, JsonValue::CreateNumberValue(pair.second));
		}
		jsonObj.Insert(L"deviceVolumes", devVols);

		JsonArray lastDevices;
		if (g_reconnect)
		{
			for (const auto& i : g_audioPlaybackConnections)
			{
				lastDevices.Append(JsonValue::CreateStringValue(i.first));
			}
			if (g_audioPlaybackConnections.empty())
			{
				for (const auto& id : g_startupReconnectDevices)
				{
					lastDevices.Append(JsonValue::CreateStringValue(id));
				}
			}
		}
		jsonObj.Insert(L"lastDevices", lastDevices);

		wil::unique_hfile hFile(CreateFileW((GetModuleFsPath(g_hInst).remove_filename() / CONFIG_NAME).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
		THROW_LAST_ERROR_IF(!hFile);

		std::string utf8 = Utf16ToUtf8(jsonObj.Stringify());
		DWORD written = 0;
		THROW_IF_WIN32_BOOL_FALSE(WriteFile(hFile.get(), utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr));
		THROW_HR_IF(E_FAIL, written != utf8.size());
	}
	CATCH_LOG();
}
