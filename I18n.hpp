#pragma once
#include "FnvHash.hpp"

inline std::unordered_map<uint32_t, const wchar_t*> hashToStrMap;
inline std::unordered_map<const wchar_t*, const wchar_t*> ptrToStrMap;

#pragma pack(push, 1)
struct YMOData
{
	uint16_t len;
	struct
	{
		uint32_t hash;
		uint16_t offset;
	} table[1];
};
#pragma pack(pop)

inline void LoadTranslateData(std::wstring_view langOverride = L"")
{
	hashToStrMap.clear();
	ptrToStrMap.clear();

	HRSRC hRes = nullptr;
	LANGID targetLangId = 0;

	if (!langOverride.empty() && langOverride != L"auto")
	{
		if (langOverride == L"zh-CN" || langOverride == L"zh_CN" || langOverride == L"zh-Hans")
			targetLangId = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
		else if (langOverride == L"zh-TW" || langOverride == L"zh_TW" || langOverride == L"zh-Hant" || langOverride == L"zh-HK")
			targetLangId = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL);
		else if (langOverride == L"ja-JP" || langOverride == L"ja_JP" || langOverride == L"ja")
			targetLangId = MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT);
		else if (langOverride == L"ko-KR" || langOverride == L"ko_KR" || langOverride == L"ko")
			targetLangId = MAKELANGID(LANG_KOREAN, SUBLANG_DEFAULT);
		else if (langOverride == L"en-US" || langOverride == L"en_US" || langOverride == L"en")
			targetLangId = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
	}

	if (targetLangId != 0)
	{
		if (PRIMARYLANGID(targetLangId) == LANG_ENGLISH)
		{
			// English returns raw source strings directly
			return;
		}
		hRes = FindResourceExW(g_hInst, L"YMO", MAKEINTRESOURCEW(1), targetLangId);
	}
	else
	{
		const LANGID uiLangs[] = {
			GetThreadUILanguage(),
			GetUserDefaultUILanguage(),
			GetSystemDefaultUILanguage()
		};

		for (auto langId : uiLangs)
		{
			hRes = FindResourceExW(g_hInst, L"YMO", MAKEINTRESOURCEW(1), langId);
			if (hRes)
				break;

			if (PRIMARYLANGID(langId) == LANG_CHINESE)
			{
				if (SUBLANGID(langId) == SUBLANG_CHINESE_TRADITIONAL ||
					SUBLANGID(langId) == SUBLANG_CHINESE_HONGKONG ||
					SUBLANGID(langId) == SUBLANG_CHINESE_MACAU)
				{
					hRes = FindResourceExW(g_hInst, L"YMO", MAKEINTRESOURCEW(1), MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL));
				}
				else
				{
					hRes = FindResourceExW(g_hInst, L"YMO", MAKEINTRESOURCEW(1), MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED));
				}
				if (hRes)
					break;
			}
			else if (PRIMARYLANGID(langId) == LANG_JAPANESE)
			{
				hRes = FindResourceExW(g_hInst, L"YMO", MAKEINTRESOURCEW(1), MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT));
				if (hRes)
					break;
			}
			else if (PRIMARYLANGID(langId) == LANG_KOREAN)
			{
				hRes = FindResourceExW(g_hInst, L"YMO", MAKEINTRESOURCEW(1), MAKELANGID(LANG_KOREAN, SUBLANG_DEFAULT));
				if (hRes)
					break;
			}
		}
	}

	if (!hRes && targetLangId == 0)
	{
		hRes = FindResourceW(g_hInst, MAKEINTRESOURCEW(1), L"YMO");
	}

	if (hRes)
	{
		auto hResData = LoadResource(g_hInst, hRes);
		if (hResData)
		{
			auto ymo = reinterpret_cast<const YMOData*>(LockResource(hResData));
			if (ymo)
			{
				hashToStrMap.reserve(ymo->len);

				for (int i = 0; i < ymo->len; ++i)
				{
					auto hash = ymo->table[i].hash;
					auto offset = ymo->table[i].offset;
					auto str = reinterpret_cast<const wchar_t*>(reinterpret_cast<const uint8_t*>(hResData) + offset);
					hashToStrMap.emplace(hash, str);
				}
			}
		}
	}
}

inline const wchar_t* Translate(const wchar_t* str)
{
	auto translation = str;

	auto i = ptrToStrMap.find(str);
	if (i == ptrToStrMap.end())
	{
		auto hash = fnv1a_32(str, wcslen(str) * sizeof(wchar_t));
		auto j = hashToStrMap.find(hash);
		if (j != hashToStrMap.end())
			translation = j->second;

		ptrToStrMap.emplace(str, translation);
	}
	else
		translation = i->second;

	return translation;
}

inline const wchar_t* TranslateContext(const wchar_t* str, const wchar_t* ctxtStr)
{
	auto translation = Translate(ctxtStr);
	if (translation == ctxtStr)
		return str;
	return translation;
}

#define _(str) Translate(str)
#define C_(ctxt, str) TranslateContext(str, ctxt L"\004" str)
