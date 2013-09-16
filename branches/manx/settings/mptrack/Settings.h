/*
 * Settings.h
 * ----------
 * Purpose: Header file for application setting handling framework.
 * Notes  : (currently none)
 * Authors: Joern Heusipp
 *          OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#pragma once


#include "../common/misc_util.h"

#include <map>
#include <set>


#define MPT_SETTINGS_CACHE
#define MPT_SETTINGS_CACHE_STORE_DEFAULTS
#define MPT_SETTINGS_PANEL
//#define MPT_SETTINGS_IMMEDIATE_FLUSH


enum SettingType
{
	SettingTypeNone,
	SettingTypeBool,
	SettingTypeInt,
	SettingTypeFloat,
	SettingTypeString,
};

class SettingValue
{
private:
	bool valueBool;
	int32 valueInt;
	double valueFloat;
	std::string valueString;
	SettingType type;
	std::string typeTag;
	void Init()
	{
		valueBool = false;
		valueInt = 0;
		valueFloat = 0.0;
		valueString = std::string();
		type = SettingTypeNone;
		typeTag = std::string();
	}
public:
	bool operator == (const SettingValue &other) const
	{
		return type == other.type
			&& typeTag == other.typeTag
			&& valueBool == other.valueBool
			&& valueInt == other.valueInt
			&& valueFloat == other.valueFloat
			&& valueString == other.valueString
			;
	}
	bool operator != (const SettingValue &other) const
	{
		return !(*this == other);
	}
	SettingValue()
	{
		Init();
	}
	SettingValue(const SettingValue &other)
	{
		Init();
		*this = other;
	}
	SettingValue & operator = (const SettingValue &other)
	{
		if(this == &other)
		{
			return *this;
		}
		ASSERT(type == SettingTypeNone || type == other.type);
		type = other.type;
		valueBool = other.valueBool;
		valueInt = other.valueInt;
		valueFloat = other.valueFloat;
		valueString = other.valueString;
		typeTag = other.typeTag;
		return *this;
	}
	SettingValue(bool val)
	{
		Init();
		type = SettingTypeBool;
		valueBool = val;
	}
	SettingValue(int32 val)
	{
		Init();
		type = SettingTypeInt;
		valueInt = val;
	}
	SettingValue(double val)
	{
		Init();
		type = SettingTypeFloat;
		valueFloat = val;
	}
	SettingValue(const char *val)
	{
		Init();
		type = SettingTypeString;
		valueString = val;
	}
	SettingValue(const std::string &val)
	{
		Init();
		type = SettingTypeString;
		valueString = val;
	}
	SettingValue(bool val, const std::string &typeTag_)
	{
		Init();
		type = SettingTypeBool;
		typeTag = typeTag_;
		valueBool = val;
	}
	SettingValue(int32 val, const std::string &typeTag_)
	{
		Init();
		type = SettingTypeInt;
		typeTag = typeTag_;
		valueInt = val;
	}
	SettingValue(double val, const std::string &typeTag_)
	{
		Init();
		type = SettingTypeFloat;
		typeTag = typeTag_;
		valueFloat = val;
	}
	SettingValue(const char *val, const std::string &typeTag_)
	{
		Init();
		type = SettingTypeString;
		typeTag = typeTag_;
		valueString = val;
	}
	SettingValue(const std::string &val, const std::string &typeTag_)
	{
		Init();
		type = SettingTypeString;
		typeTag = typeTag_;
		valueString = val;
	}
	SettingType GetType() const
	{
		return type;
	}
	bool HasTypeTag() const
	{
		return !typeTag.empty();
	}
	std::string GetTypeTag() const
	{
		return typeTag;
	}
	template <typename T>
	T as() const
	{
		return static_cast<T>(*this);
	}
	operator bool () const
	{
		ASSERT(type == SettingTypeBool);
		return valueBool;
	}
	operator int32 () const
	{
		ASSERT(type == SettingTypeInt);
		return valueInt;
	}
	operator double () const
	{
		ASSERT(type == SettingTypeFloat);
		return valueFloat;
	}
	operator std::string () const
	{
		ASSERT(type == SettingTypeString);
		return valueString;
	}
	std::string FormatTypeAsString() const
	{
		if(GetType() == SettingTypeNone)
		{
			return "nil";
		} else if(HasTypeTag() && !GetTypeTag().empty())
		{
			return GetTypeTag();
		}
		switch(GetType())
		{
			case SettingTypeBool:
				return "bool";
				break;
			case SettingTypeInt:
				return "int";
				break;
			case SettingTypeFloat:
				return "float";
				break;
			case SettingTypeString:
				return "string";
				break;
			case SettingTypeNone:
			default:
				return "nil";
				break;
		}
	}
	std::string FormatValueAsString() const
	{
		switch(GetType())
		{
			case SettingTypeBool:
				return Stringify(valueBool);
				break;
			case SettingTypeInt:
				return Stringify(valueInt);
				break;
			case SettingTypeFloat:
				return Stringify(valueFloat);
				break;
			case SettingTypeString:
				return valueString;
				break;
			case SettingTypeNone:
			default:
				return "";
				break;
		}
	}
	std::string FormatAsString() const
	{
		return "(" + FormatTypeAsString() + ")" + FormatValueAsString();
	}
	void SetFromString(const std::string &newVal)
	{
		switch(GetType())
		{
			case SettingTypeBool:
				valueBool = ConvertStrTo<bool>(newVal);
				break;
			case SettingTypeInt:
				valueInt = ConvertStrTo<int32>(newVal);
				break;
			case SettingTypeFloat:
				valueFloat = ConvertStrTo<double>(newVal);
				break;
			case SettingTypeString:
				valueString = newVal;
				break;
			case SettingTypeNone:
			default:
				break;
		}
	}
};



void EncodeBinarySettingRaw(std::string &dst, const void *src, std::size_t size);
void DecodeBinarySettingRaw(void *dst, std::size_t size, const std::string &src);

template<typename T>
std::string EncodeBinarySetting(const T &val)
{
	std::string result;
	EncodeBinarySettingRaw(result, &val, sizeof(T));
	return result;
}
template<typename T>
T DecodeBinarySetting(const std::string &val)
{
	T result = T();
	DecodeBinarySettingRaw(&result, sizeof(T), val);
	return result;
}

template<typename T>
inline SettingValue ToSettingValue(const T &val)
{
	return SettingValue(val);
}

template<typename T>
inline T FromSettingValue(const SettingValue &val)
{
	return val.as<T>();
}

// To support settings.Read<Tcustom> and settings.Write<Tcustom>,
// just provide specializations of ToSettingsValue<Tcustom> and FromSettingValue<Tcustom>.
// You may use the SettingValue(value, typeTag) constructor in ToSettingValue
// and check the typeTag FromSettingsValue to implement runtime type-checking for custom types.

template<> inline SettingValue ToSettingValue(const CString &val) { return SettingValue(std::string(val.GetString())); }
template<> inline CString FromSettingValue(const SettingValue &val) { return CString(val.as<std::string>().c_str()); }

template<> inline SettingValue ToSettingValue(const uint32 &val) { return SettingValue(int32(val)); }
template<> inline uint32 FromSettingValue(const SettingValue &val) { return uint32(val.as<int32>()); }

template<> inline SettingValue ToSettingValue(const uint16 &val) { return SettingValue(int32(val)); }
template<> inline uint16 FromSettingValue(const SettingValue &val) { return uint16(val.as<int32>()); }

template<> inline SettingValue ToSettingValue(const uint8 &val) { return SettingValue(int32(val)); }
template<> inline uint8 FromSettingValue(const SettingValue &val) { return uint8(val.as<int32>()); }

template<> inline SettingValue ToSettingValue(const LONG &val) { return SettingValue(int32(val)); }
template<> inline LONG FromSettingValue(const SettingValue &val) { return LONG(val.as<int32>()); }


#if defined(MPT_SETTINGS_CACHE)


class SettingState
{
private:
	SettingValue value;
#if defined(MPT_SETTINGS_CACHE_STORE_DEFAULTS)
	SettingValue defaultValue;
#endif // MPT_SETTINGS_CACHE_STORE_DEFAULTS
	bool dirty;
public:
	SettingState()
	{
		return;
	}
	SettingState(const SettingValue &def)
		: value(def)
#if defined(MPT_SETTINGS_CACHE_STORE_DEFAULTS)
		, defaultValue(def)
#endif // MPT_SETTINGS_CACHE_STORE_DEFAULTS
		, dirty(false)
	{
		return;
	}
	SettingState & assign(const SettingValue &other, bool setDirty = true)
	{
#if defined(MPT_SETTINGS_CACHE_STORE_DEFAULTS)
		ASSERT(defaultValue.GetType() == other.GetType());
		ASSERT(defaultValue.GetTypeTag() == other.GetTypeTag());
#endif // MPT_SETTINGS_CACHE_STORE_DEFAULTS
		if(setDirty)
		{
			if(value != other)
			{
				value = other;
				dirty = true;
			}
		} else
		{
			value = other;
		}
		return *this;
	}
	SettingState & operator = (const SettingValue &val)
	{
		assign(val);
		return *this;
	}
	bool IsDirty() const
	{
		return dirty;
	}
	void Clean()
	{
		dirty = false;
	}
	operator SettingValue () const
	{
		return value;
	}
};


#endif // MPT_SETTINGS_CACHE


class SettingPath
{
private:
	std::string section;
	std::string key;
	std::string oldSection;
	std::string oldKey;
public:
	SettingPath()
	{
		return;
	}
	SettingPath(const std::string &section_, const std::string &key_, const std::string &oldSection_ = std::string(), const std::string &oldKey_ = std::string())
		: section(section_)
		, key(key_)
		, oldSection(oldSection_)
		, oldKey(oldKey_)
	{
		return;
	}
	std::string GetSection() const
	{
		return section;
	}
	std::string GetKey() const
	{
		return key;
	}
	bool HasOldPaths() const
	{
		return !oldSection.empty() || !oldKey.empty();
	}
	std::string GetOldSection() const
	{
		return HasOldPaths() ? oldSection : section;
	}
	std::string GetOldKey() const
	{
		return HasOldPaths() ? oldKey : key;
	}
	int compare(const SettingPath &other) const
	{
		int cmp_section = section.compare(other.section);
		if(cmp_section)
		{
			return cmp_section;
		}
		int cmp_key = key.compare(other.key);
		return cmp_key;
	}
	std::string FormatAsString() const
	{
		return section + "." + key;
	}
};

inline bool operator < (const SettingPath &left, const SettingPath &right)
{
	return left.compare(right) < 0;
}


class ISettingsBackend
{
public:
	virtual SettingValue ReadSetting(const SettingPath &path, const SettingValue &def) const = 0;
	virtual void WriteSetting(const SettingPath &path, const SettingValue &val) = 0;
	virtual void RemoveSetting(const SettingPath &path) = 0;
};


#if defined(MPT_SETTINGS_CACHE)

struct SettingMetadata
{
	std::string description;
	SettingMetadata() {}
	template<typename T1>
	SettingMetadata(const T1 &description)
		: description(description)
	{
		return;
	}
};

#else // !MPT_SETTINGS_CACHE

typedef bool SettingMetadata;

#endif // MPT_SETTINGS_CACHE

class ISettingChanged
{
public:
	virtual void SettingChanged(const SettingPath &path) = 0;
};

class SettingsContainer
{

	#if defined(MPT_SETTINGS_CACHE)

		public:
			typedef std::map<SettingPath,SettingState> SettingsMap;
			typedef std::map<SettingPath,SettingMetadata> SettingsMetaMap;
			typedef std::map<SettingPath,std::set<ISettingChanged*> > SettingsListenerMap;
		private:
			mutable SettingsMap map;
			mutable SettingsMetaMap mapMetadata;
			mutable SettingsListenerMap mapListeners;
			void WriteSettings();

	#endif // MPT_SETTINGS_CACHE

private:
	ISettingsBackend *backend;
	ISettingsBackend *oldBackend;
	SettingValue BackendsReadSetting(const SettingPath &path, const SettingValue &def) const;
	void BackendsWriteSetting(const SettingPath &path, const SettingValue &val);
	void BackendsRemoveSetting(const SettingPath &path);
	void NotifyListeners(const SettingPath &path);
	SettingValue ReadSetting(const SettingPath &path, const SettingValue &def, const SettingMetadata &metadata) const
	{
		SettingsMap::iterator entry = map.find(path);
		if(entry == map.end())
		{
			entry = map.insert(map.begin(), std::make_pair(path, SettingState(def).assign(BackendsReadSetting(path, def), false)));
			mapMetadata[path] = metadata;
		}
		return entry->second;
	}
	void WriteSetting(const SettingPath &path, const SettingValue &val)
	{
		SettingsMap::iterator entry = map.find(path);
		if(entry == map.end())
		{
			map[path] = val;
			entry = map.find(path);
		} else
		{
			entry->second = val;
		}
		NotifyListeners(path);
#if defined(MPT_SETTINGS_IMMEDIATE_FLUSH)
		BackendsWriteSetting(path, val);
		entry->second.Clean();
#endif
	}
	void RemoveSetting(const SettingPath &path)
	{
		map.erase(path);
		BackendsRemoveSetting(path);
	}
private:
	SettingsContainer(const SettingsContainer &other); // disable
	SettingsContainer& operator = (const SettingsContainer &other); // disable
public:
	SettingsContainer(ISettingsBackend *backend, ISettingsBackend *oldBackend = nullptr);
	template <typename T>
	T Read(const SettingPath &path, const T &def, const SettingMetadata &metadata = SettingMetadata()) const
	{
		return FromSettingValue<T>(ReadSetting(path, ToSettingValue<T>(def), metadata));
	}
	template <typename T>
	T Read(const std::string &section, const std::string &key, const T &def, const SettingMetadata &metadata = SettingMetadata()) const
	{
		return FromSettingValue<T>(ReadSetting(SettingPath(section, key), ToSettingValue<T>(def), metadata));
	}
	template <typename T>
	void Write(const SettingPath &path, const T &val)
	{
		WriteSetting(path, ToSettingValue<T>(val));
	}
	template <typename T>
	void Write(const std::string &section, const std::string &key, const T &val)
	{
		WriteSetting(SettingPath(section, key), ToSettingValue<T>(val));
	}
	void Remove(const SettingPath &path)
	{
		RemoveSetting(path);
	}
	void Remove(const std::string &section, const std::string &key)
	{
		RemoveSetting(SettingPath(section, key));
	}
	void Flush();
	void Register(ISettingChanged *listener, const SettingPath &path);
	void UnRegister(ISettingChanged *listener, const SettingPath &path);
	~SettingsContainer();

	#if defined(MPT_SETTINGS_CACHE)

		public:
			SettingsMap::const_iterator begin() const { return map.begin(); }
			SettingsMap::const_iterator end() const { return map.end(); }
			const SettingsMap &GetMap() const { return map; }

	#endif // MPT_SETTINGS_CACHE

};

template <typename T>
class Setting
{
private:
	SettingsContainer &conf;
	const SettingPath path;
	const T defaultValue;
public:
	Setting(SettingsContainer &conf_, const std::string &section, const std::string &key, const T&def, const SettingMetadata &metadata = SettingMetadata())
		: conf(conf_)
		, path(section, key)
		, defaultValue(def)
	{
		conf.Read(path, defaultValue, metadata); // set default value
	}
	Setting(SettingsContainer &conf_, const SettingPath &path_, const T&def, const SettingMetadata &metadata = SettingMetadata())
		: conf(conf_)
		, path(path_)
		, defaultValue(def)
	{
		conf.Read(path, defaultValue, metadata); // set default value
	}
	SettingPath GetPath() const
	{
		return path;
	}
	Setting & operator = (const T &val)
	{
		conf.Write(path, val);
		return *this;
	}
	operator const T () const
	{
		return conf.Read(path, defaultValue);
	}
	const T Get() const
	{
		return conf.Read(path, defaultValue);
	}
};

template <typename T>
class ConstSetting
{
private:
	const SettingsContainer &conf;
	const SettingPath path;
	const T defaultValue;
public:
	ConstSetting(const SettingsContainer &conf_, const std::string &section, const std::string &key, const T&def, const SettingMetadata &metadata = SettingMetadata())
		: conf(conf_)
		, path(section, key)
		, defaultValue(def)
	{
		conf.Read(path, defaultValue, metadata); // set default value
	}
	ConstSetting(const SettingsContainer &conf_, const SettingPath &path_, const T&def, const SettingMetadata &metadata = SettingMetadata())
		: conf(conf_)
		, path(path_)
		, defaultValue(def)
	{
		conf.Read(path, defaultValue, metadata); // set default value
	}
	SettingPath GetPath() const
	{
		return path;
	}
	operator const T () const
	{
		return conf.Read(path, defaultValue);
	}
	const T Get() const
	{
		return conf.Read(path, defaultValue);
	}
};

template <typename T>
class CachedSetting
	: public ISettingChanged
{
private:
	T value;
	SettingsContainer &conf;
	const SettingPath path;
	const T defaultValue;
public:
	CachedSetting(SettingsContainer &conf_, const std::string &section, const std::string &key, const T&def, const SettingMetadata &metadata = SettingMetadata())
		: value(def)
		, conf(conf_)
		, path(section, key)
		, defaultValue(def)
	{
		value = conf.Read(path, def, metadata);
		conf.Register(this, path);
	}
	CachedSetting(SettingsContainer &conf_, const SettingPath &path_, const T&def, const SettingMetadata &metadata = SettingMetadata())
		: value(def)
		, conf(conf_)
		, path(path_)
		, defaultValue(def)
	{
		value = conf.Read(path, def, metadata);
		conf.Register(this, path);
	}
	~CachedSetting()
	{
		conf.UnRegister(this, path);
	}
	SettingPath GetPath() const
	{
		return path;
	}
	CachedSetting & operator = (const T &val)
	{
		value = val;
		conf.Write(path, val);
		return *this;
	}
	operator const T & () const
	{
		return value;
	}
	const T & Get() const
	{
		return value;
	}
	CachedSetting & Update()
	{
		value = conf.Read(path, defaultValue);
		return *this;
	}
	void SettingChanged(const SettingPath &path)
	{
		UNREFERENCED_PARAMETER(path);
		Update();
	}
};



class IniFileSettingsBackend : public ISettingsBackend
{
private:
	const std::string filename;
private:
	std::string ReadSettingRaw(const SettingPath &path, const std::string &def) const;
	double ReadSettingRaw(const SettingPath &path, double def) const;
	int32 ReadSettingRaw(const SettingPath &path, int32 def) const;
	bool ReadSettingRaw(const SettingPath &path, bool def) const;
	void WriteSettingRaw(const SettingPath &path, const std::string &val);
	void WriteSettingRaw(const SettingPath &path, double val);
	void WriteSettingRaw(const SettingPath &path, int32 val);
	void WriteSettingRaw(const SettingPath &path, bool val);
	void RemoveSettingRaw(const SettingPath &path);
public:
	IniFileSettingsBackend(const std::string &filename_);
	~IniFileSettingsBackend();
	virtual SettingValue ReadSetting(const SettingPath &path, const SettingValue &def) const;
	virtual void WriteSetting(const SettingPath &path, const SettingValue &val);
	virtual void RemoveSetting(const SettingPath &path);
};

class RegistrySettingsBackend : public ISettingsBackend
{
private:
	const HKEY baseKey;
	const std::string basePath;
	const bool oldPaths;
private:
	std::string BuildKeyName(const SettingPath &path) const;
	std::string BuildValueName(const SettingPath &path) const;
	std::string ReadSettingRaw(const SettingPath &path, const std::string &def) const;
	double ReadSettingRaw(const SettingPath &path, double def) const;
	int32 ReadSettingRaw(const SettingPath &path, int32 def) const;
	bool ReadSettingRaw(const SettingPath &path, bool def) const;
public:
	RegistrySettingsBackend(HKEY baseKey, const std::string &basePath, bool oldPaths = false);
	~RegistrySettingsBackend();
	virtual SettingValue ReadSetting(const SettingPath &path, const SettingValue &def) const;
	virtual void WriteSetting(const SettingPath &path, const SettingValue &val);
	virtual void RemoveSetting(const SettingPath &path);
};

class IniFileSettingsContainer : private IniFileSettingsBackend, public SettingsContainer
{
public:
	IniFileSettingsContainer(const std::string &filename);
	~IniFileSettingsContainer();
};

class DefaultSettingsContainer : public IniFileSettingsContainer
{
public:
	DefaultSettingsContainer();
	~DefaultSettingsContainer();
};

