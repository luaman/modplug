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
#include "Mptrack.h"



// WINAPI compatible binary structure encoding

static const char EncodeNibble[16] = { '0', '1', '2', '3', '4', '5' ,'6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

void EncodeBinarySettingRaw(std::string &dst, const void *src, std::size_t size)
{
	dst.clear();
	const uint8 *source = static_cast<const uint8*>(src);
	uint8 checksum = 0;
	for(std::size_t i = 0; i < size; ++i)
	{
		uint8 byte = *source;
		dst.push_back(EncodeNibble[(byte&0xf0)>>4]);
		dst.push_back(EncodeNibble[byte&0x0f]);
		checksum += byte;
		source++;
	}
	dst.push_back(EncodeNibble[(checksum&0xf0)>>4]);
	dst.push_back(EncodeNibble[checksum&0x0f]);
}

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

void DecodeBinarySettingRaw(void *dst, std::size_t size, const std::string &src)
{
	if(src.length() != size * 2 + 2)
	{
		return;
	}
	uint8 checksum = 0;
	for(std::size_t i = 0; i < size; ++i)
	{
		uint8 byte = 0;
		if(!DecodeByte(byte, src[i*2+0], src[i*2+1]))
		{
			return;
		}
		checksum += byte;
	}
	{
		uint8 byte = 0;
		if(!DecodeByte(byte, src[size*2+0], src[size*2+1]))
		{
			return;
		}
		if(checksum != byte)
		{
			return;
		}
	}
	uint8 *dest = static_cast<uint8*>(dst);
	for(std::size_t i = 0; i < size; ++i)
	{
		uint8 byte = 0;
		DecodeByte(byte, src[i*2+0], src[i*2+1]);
		*dest = byte;
		dest++;
	}
}


#if defined(MPT_SETTINGS_CACHE)


SettingValue SettingsContainer::ReadSetting(const SettingPath &path, const SettingValue &def, const SettingMetadata &metadata) const
{
	if(map.find(path) == map.end())
	{
		map[path] = SettingState(def).assign(backend.ReadSetting(path, def), false);
		mapMetadata[path] = metadata;
	}
	return map[path];
}

void SettingsContainer::WriteSetting(const SettingPath &path, const SettingValue &val)
{
	map[path] = val;
	//backend.WriteSetting(path, val);
}

void SettingsContainer::RemoveSetting(const SettingPath &path)
{
	map.erase(path);
	backend.RemoveSetting(path);
}

void SettingsContainer::WriteSettings()
{
	for(SettingsMap::iterator i = map.begin(); i != map.end(); ++i)
	{
		if(i->second.IsDirty())
		{
			backend.WriteSetting(i->first, i->second);
			i->second.Clean();
		}
	}
}

SettingsContainer::SettingsContainer(ISettingsBackend &backend_)
	: backend(backend_)
{
	return;
}

void SettingsContainer::Flush()
{
	WriteSettings();
}

SettingsContainer::~SettingsContainer()
{
	WriteSettings();
}


#else // !MPT_SETTINGS_CACHE


SettingValue SettingsContainer::ReadSetting(const SettingPath &path, const SettingValue &def, const SettingMetadata & /*metadata*/ ) const
{
	return backend.ReadSetting(path, def);
}

void SettingsContainer::WriteSetting(const SettingPath &path, const SettingValue &val)
{
	backend.WriteSetting(path, val);
}

void SettingsContainer::RemoveSetting(const SettingPath &path)
{
	backend.RemoveSetting(path);
}

SettingsContainer::SettingsContainer(ISettingsBackend &backend_)
	: backend(backend_)
{
	return;
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


std::string IniFileSettingsBackend::ReadSettingRaw(const SettingPath &path, const std::string &def) const
{
	std::vector<CHAR> buf(128);
	while(::GetPrivateProfileString(path.GetSection().c_str(), path.GetKey().c_str(), def.c_str(), &buf[0], buf.size(), filename.c_str()) == buf.size() - 1)
	{
		buf.resize(buf.size() * 2);
	}
	return &buf[0];
}

float IniFileSettingsBackend::ReadSettingRaw(const SettingPath &path, float def) const
{
	std::vector<CHAR> buf(128);
	while(::GetPrivateProfileString(path.GetSection().c_str(), path.GetKey().c_str(), Stringify(def).c_str(), &buf[0], buf.size(), filename.c_str()) == buf.size() - 1)
	{
		buf.resize(buf.size() * 2);
	}
	return ConvertStrTo<float>(std::string(&buf[0]));
}

int32 IniFileSettingsBackend::ReadSettingRaw(const SettingPath &path, int32 def) const
{
	return (int32)::GetPrivateProfileInt(path.GetSection().c_str(), path.GetKey().c_str(), (UINT)def, filename.c_str());
}

bool IniFileSettingsBackend::ReadSettingRaw(const SettingPath &path, bool def) const
{
	return ::GetPrivateProfileInt(path.GetSection().c_str(), path.GetKey().c_str(), def?1:0, filename.c_str()) ? true : false;
}


void IniFileSettingsBackend::WriteSettingRaw(const SettingPath &path, const std::string &val)
{
	::WritePrivateProfileString(path.GetSection().c_str(), path.GetKey().c_str(), val.c_str(), filename.c_str());
}

void IniFileSettingsBackend::WriteSettingRaw(const SettingPath &path, float val)
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
	case SettingTypeFloat: return SettingValue(ReadSettingRaw(path, def.as<float>()), def.GetTypeTag()); break;
	case SettingTypeString: return SettingValue(ReadSettingRaw(path, def.as<std::string>()), def.GetTypeTag()); break;
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
	case SettingTypeFloat: WriteSettingRaw(path, val.as<float>()); break;
	case SettingTypeString: WriteSettingRaw(path, val.as<std::string>()); break;
	default: break;
	}
}

void IniFileSettingsBackend::RemoveSetting(const SettingPath &path)
{
	RemoveSettingRaw(path);
}



IniFileSettingsContainer::IniFileSettingsContainer(const std::string &filename)
	: IniFileSettingsBackend(filename)
	, SettingsContainer(*static_cast<ISettingsBackend*>(this))
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


