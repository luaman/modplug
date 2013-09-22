/*
 * Settings.cpp
 * ------------
 * Purpose: Application setting handling framework.
 * Notes  : (currently none)
 * Authors: Joern Heusipp
 *          OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"

#include "Settings.h"

#include "../common/misc_util.h"
#include "../common/StringFixer.h"
#include "Mptrack.h"



static const char EncodeNibble[16] = { '0', '1', '2', '3', '4', '5' ,'6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

static inline bool DecodeByte(uint8 &byte, char c1, char c2)
{
	byte = 0;
	if('0' <= c1 && c1 <= '9')
	{
		byte += (c1 - '0') << 4;
	} else if('A' <= c1 && c1 <= 'F')
	{
		byte += (c1 - 'A' + 10) << 4;
	} else
	{
		return false;
	}
	if('0' <= c2 && c2 <= '9')
	{
		byte += c2 - '0';
	} else if('A' <= c2 && c2 <= 'F')
	{
		byte += c2 - 'A' + 10;
	} else
	{
		return false;
	}
	return true;
}


std::string SettingBinToHex(const std::vector<char> &src)
{
	std::string result;
	for(std::size_t i = 0; i < src.size(); ++i)
	{
		uint8 byte = src[i];
		result.push_back(EncodeNibble[(byte&0xf0)>>4]);
		result.push_back(EncodeNibble[byte&0x0f]);
	}
	return result;
}

std::vector<char> SettingHexToBin(const std::string &src)
{
	std::vector<char> result;
	for(std::size_t i = 0; i+1 < src.size(); i += 2)
	{
		uint8 byte = 0;
		if(!DecodeByte(byte, src[i*2+0], src[i*2+1]))
		{
			return result;
		}
		result.push_back(byte);
	}
	return result;
}


#if defined(MPT_SETTINGS_CACHE)


SettingValue SettingsContainer::BackendsReadSetting(const SettingPath &path, const SettingValue &def) const
{
	SettingValue result = def;
	if(oldBackend)
	{
		result = oldBackend->ReadSetting(path, result);
	}
	result = backend->ReadSetting(path, result);
	return result;
}

void SettingsContainer::BackendsWriteSetting(const SettingPath &path, const SettingValue &val)
{
	backend->WriteSetting(path, val);
}

void SettingsContainer::BackendsRemoveSetting(const SettingPath &path)
{
	backend->RemoveSetting(path);
}

void SettingsContainer::NotifyListeners(const SettingPath &path)
{
	const SettingsListenerMap::iterator entry = mapListeners.find(path);
	if(entry != mapListeners.end())
	{
		const std::set<ISettingChanged*>::const_iterator beg = entry->second.begin();
		const std::set<ISettingChanged*>::const_iterator end = entry->second.end();
		for(std::set<ISettingChanged*>::const_iterator it = beg; it != end; ++it)
		{
			(*it)->SettingChanged(path);
		}
	}
}

void SettingsContainer::WriteSettings()
{
	for(SettingsMap::iterator i = map.begin(); i != map.end(); ++i)
	{
		if(i->second.IsDirty())
		{
#if !defined(MPT_SETTINGS_IMMEDIATE_FLUSH)
			BackendsWriteSetting(i->first, i->second);
			i->second.Clean();
#endif
		}
	}
}

void SettingsContainer::Flush()
{
	WriteSettings();
}

void SettingsContainer::Register(ISettingChanged *listener, const SettingPath &path)
{
	mapListeners[path].insert(listener);
}

void SettingsContainer::UnRegister(ISettingChanged *listener, const SettingPath &path)
{
	mapListeners[path].erase(listener);
}

SettingsContainer::~SettingsContainer()
{
	WriteSettings();
}

#else // !MPT_SETTINGS_CACHE

SettingValue SettingsContainer::ReadSetting(const SettingPath &path, const SettingValue &def, const SettingMetadata & /*metadata*/ ) const
{
	return backend->ReadSetting(path, def);
}

void SettingsContainer::WriteSetting(const SettingPath &path, const SettingValue &val)
{
	backend->WriteSetting(path, val);
}

void SettingsContainer::RemoveSetting(const SettingPath &path)
{
	backend->RemoveSetting(path);
}

SettingsContainer::SettingsContainer(ISettingsBackend *backend)
	: backend(backend)
	, oldBackend(nullptr)
{
	ASSERT(backend);
}

void SettingsContainer::Flush()
{
	return;
}

SettingsContainer::~SettingsContainer()
{
	return;
}

#endif // MPT_SETTINGS_CACHE


SettingsContainer::SettingsContainer(ISettingsBackend *backend, ISettingsBackend *oldBackend)
	: backend(backend)
	, oldBackend(oldBackend)
{
	ASSERT(backend);
}





std::vector<char> IniFileSettingsBackend::ReadSettingRaw(const SettingPath &path, const std::vector<char> &def) const
{
	std::vector<char> result = def;
	::GetPrivateProfileStruct(path.GetSection().c_str(), path.GetKey().c_str(), &result[0], result.size(), filename.c_str());
	return result;
}

std::string IniFileSettingsBackend::ReadSettingRaw(const SettingPath &path, const std::string &def) const
{
	std::vector<CHAR> buf(128);
	while(::GetPrivateProfileString(path.GetSection().c_str(), path.GetKey().c_str(), def.c_str(), &buf[0], buf.size(), filename.c_str()) == buf.size() - 1)
	{
		buf.resize(buf.size() * 2);
	}
	return &buf[0];
}

double IniFileSettingsBackend::ReadSettingRaw(const SettingPath &path, double def) const
{
	std::vector<CHAR> buf(128);
	while(::GetPrivateProfileString(path.GetSection().c_str(), path.GetKey().c_str(), Stringify(def).c_str(), &buf[0], buf.size(), filename.c_str()) == buf.size() - 1)
	{
		buf.resize(buf.size() * 2);
	}
	return ConvertStrTo<double>(std::string(&buf[0]));
}

int32 IniFileSettingsBackend::ReadSettingRaw(const SettingPath &path, int32 def) const
{
	return (int32)::GetPrivateProfileInt(path.GetSection().c_str(), path.GetKey().c_str(), (UINT)def, filename.c_str());
}

bool IniFileSettingsBackend::ReadSettingRaw(const SettingPath &path, bool def) const
{
	return ::GetPrivateProfileInt(path.GetSection().c_str(), path.GetKey().c_str(), def?1:0, filename.c_str()) ? true : false;
}


void IniFileSettingsBackend::WriteSettingRaw(const SettingPath &path, const std::vector<char> &val)
{
	::WritePrivateProfileStruct(path.GetSection().c_str(), path.GetKey().c_str(), (LPVOID)&val[0], val.size(), filename.c_str());
}

void IniFileSettingsBackend::WriteSettingRaw(const SettingPath &path, const std::string &val)
{
	::WritePrivateProfileString(path.GetSection().c_str(), path.GetKey().c_str(), val.c_str(), filename.c_str());
}

void IniFileSettingsBackend::WriteSettingRaw(const SettingPath &path, double val)
{
	::WritePrivateProfileString(path.GetSection().c_str(), path.GetKey().c_str(), Stringify(val).c_str(), filename.c_str());
}

void IniFileSettingsBackend::WriteSettingRaw(const SettingPath &path, int32 val)
{
	::WritePrivateProfileString(path.GetSection().c_str(), path.GetKey().c_str(), Stringify(val).c_str(), filename.c_str());
}

void IniFileSettingsBackend::WriteSettingRaw(const SettingPath &path, bool val)
{
	::WritePrivateProfileString(path.GetSection().c_str(), path.GetKey().c_str(), Stringify(val?1:0).c_str(), filename.c_str());
}

void IniFileSettingsBackend::RemoveSettingRaw(const SettingPath &path)
{
	::WritePrivateProfileString(path.GetSection().c_str(), path.GetKey().c_str(), NULL, filename.c_str());
}



IniFileSettingsBackend::IniFileSettingsBackend(const std::string &filename_)
	: filename(filename_)
{
	return;
}

IniFileSettingsBackend::~IniFileSettingsBackend()
{
	return;
}

SettingValue IniFileSettingsBackend::ReadSetting(const SettingPath &path, const SettingValue &def) const
{
	switch(def.GetType())
	{
	case SettingTypeBool: return SettingValue(ReadSettingRaw(path, def.as<bool>()), def.GetTypeTag()); break;
	case SettingTypeInt: return SettingValue(ReadSettingRaw(path, def.as<int32>()), def.GetTypeTag()); break;
	case SettingTypeFloat: return SettingValue(ReadSettingRaw(path, def.as<double>()), def.GetTypeTag()); break;
	case SettingTypeString: return SettingValue(ReadSettingRaw(path, def.as<std::string>()), def.GetTypeTag()); break;
	case SettingTypeBinary: return SettingValue(ReadSettingRaw(path, def.as<std::vector<char> >()), def.GetTypeTag()); break;
	default: return SettingValue(); break;
	}
}

void IniFileSettingsBackend::WriteSetting(const SettingPath &path, const SettingValue &val)
{
	ASSERT(val.GetType() != SettingTypeNone);
	switch(val.GetType())
	{
	case SettingTypeBool: WriteSettingRaw(path, val.as<bool>()); break;
	case SettingTypeInt: WriteSettingRaw(path, val.as<int32>()); break;
	case SettingTypeFloat: WriteSettingRaw(path, val.as<double>()); break;
	case SettingTypeString: WriteSettingRaw(path, val.as<std::string>()); break;
	case SettingTypeBinary: WriteSettingRaw(path, val.as<std::vector<char> >()); break;
	default: break;
	}
}

void IniFileSettingsBackend::RemoveSetting(const SettingPath &path)
{
	RemoveSettingRaw(path);
}





std::string RegistrySettingsBackend::BuildKeyName(const SettingPath &path) const
{
	if((oldPaths?path.GetOldSection():path.GetSection()).empty())
	{
		return basePath;
	} else
	{
		return basePath + "\\" + (oldPaths?path.GetOldSection():path.GetSection());
	}
}

std::string RegistrySettingsBackend::BuildValueName(const SettingPath &path) const
{
	return oldPaths?path.GetOldKey():path.GetKey();
}

std::vector<char> RegistrySettingsBackend::ReadSettingRaw(const SettingPath &path, const std::vector<char> &def) const
{
	std::vector<char> val = def;
	HKEY regKey = HKEY();
	if(RegOpenKeyEx(baseKey, BuildKeyName(path).c_str(), 0, KEY_READ, &regKey) == ERROR_SUCCESS)
	{
		char v[4096];
		MemsetZero(v);
		DWORD type = REG_BINARY;
		DWORD typesize = sizeof(v);
		if(RegQueryValueEx(regKey, BuildValueName(path).c_str(), NULL, &type, (BYTE *)&v, &typesize) == ERROR_SUCCESS)
		{
			val.assign(v, v + typesize);
		}
		RegCloseKey(regKey);
		regKey = HKEY();
	}
	return val;
}

std::string RegistrySettingsBackend::ReadSettingRaw(const SettingPath &path, const std::string &def) const
{
	std::string val = def;
	HKEY regKey = HKEY();
	if(RegOpenKeyEx(baseKey, BuildKeyName(path).c_str(), 0, KEY_READ, &regKey) == ERROR_SUCCESS)
	{
		CHAR v[1024];
		MemsetZero(v);
		DWORD type = REG_SZ;
		DWORD typesize = sizeof(v);
		if(RegQueryValueEx(regKey, BuildValueName(path).c_str(), NULL, &type, (BYTE *)&v, &typesize) == ERROR_SUCCESS)
		{
			mpt::String::Copy(val, v);
		}
		RegCloseKey(regKey);
		regKey = HKEY();
	}
	return val;
}

double RegistrySettingsBackend::ReadSettingRaw(const SettingPath &path, double def) const
{
	return ConvertStrTo<double>(ReadSettingRaw(path, Stringify(def)));
}

int32 RegistrySettingsBackend::ReadSettingRaw(const SettingPath &path, int32 def) const
{
	int32 val = def;
	HKEY regKey = HKEY();
	if(RegOpenKeyEx(baseKey, BuildKeyName(path).c_str(), 0, KEY_READ, &regKey) == ERROR_SUCCESS)
	{
		DWORD v = val;
		DWORD type = REG_DWORD;
		DWORD typesize = sizeof(v);
		if(RegQueryValueEx(regKey, BuildValueName(path).c_str(), NULL, &type, (BYTE *)&v, &typesize) == ERROR_SUCCESS)
		{
			val = v;
		}
		RegCloseKey(regKey);
		regKey = HKEY();
	}
	return val;
}

bool RegistrySettingsBackend::ReadSettingRaw(const SettingPath &path, bool def) const
{
	return ReadSettingRaw(path, def ? 1 : 0) ? true : false;
}



RegistrySettingsBackend::RegistrySettingsBackend(HKEY baseKey, const std::string &basePath, bool oldPaths)
	: baseKey(baseKey)
	, basePath(basePath)
	, oldPaths(oldPaths)
{
	return;
}

RegistrySettingsBackend::~RegistrySettingsBackend()
{
	return;
}

SettingValue RegistrySettingsBackend::ReadSetting(const SettingPath &path, const SettingValue &def) const
{
	switch(def.GetType())
	{
	case SettingTypeBool: return SettingValue(ReadSettingRaw(path, def.as<bool>()), def.GetTypeTag()); break;
	case SettingTypeInt: return SettingValue(ReadSettingRaw(path, def.as<int32>()), def.GetTypeTag()); break;
	case SettingTypeFloat: return SettingValue(ReadSettingRaw(path, def.as<double>()), def.GetTypeTag()); break;
	case SettingTypeString: return SettingValue(ReadSettingRaw(path, def.as<std::string>()), def.GetTypeTag()); break;
	case SettingTypeBinary: return SettingValue(ReadSettingRaw(path, def.as<std::vector<char> >()), def.GetTypeTag()); break;
	default: return SettingValue(); break;
	}
}

void RegistrySettingsBackend::WriteSetting(const SettingPath &path, const SettingValue &val)
{
	UNREFERENCED_PARAMETER(path);
	UNREFERENCED_PARAMETER(val);
	// not needed in OpenMPT
}

void RegistrySettingsBackend::RemoveSetting(const SettingPath &path)
{
	UNREFERENCED_PARAMETER(path);
	// not needed in OpenMPT
}



IniFileSettingsContainer::IniFileSettingsContainer(const std::string &filename)
	: IniFileSettingsBackend(filename)
	, SettingsContainer(this)
{
	return;
}

IniFileSettingsContainer::~IniFileSettingsContainer()
{
	return;
}



DefaultSettingsContainer::DefaultSettingsContainer()
	: IniFileSettingsContainer(theApp.GetConfigFileName())
{
	return;
}

DefaultSettingsContainer::~DefaultSettingsContainer()
{
	return;
}


