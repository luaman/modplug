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


#define MPT_SETTINGS_CACHE
#define MPT_SETTINGS_CACHE_STORE_DEFAULTS
#define MPT_SETTINGS_PANEL


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
	float valueFloat;
	std::string valueString;
	SettingType type;
	std::string typeTag;
	void Init()
	{
		valueBool = false;
		valueInt = 0;
		valueFloat = 0.0f;
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
	SettingValue(float val)
	{
		Init();
		type = SettingTypeFloat;
		valueFloat = val;
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
	SettingValue(float val, const std::string &typeTag_)
	{
		Init();
		type = SettingTypeFloat;
		typeTag = typeTag_;
		valueFloat = val;
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
	operator float () const
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
				valueFloat = ConvertStrTo<float>(newVal);
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
public:
	SettingPath()
	{
		return;
	}
	SettingPath(const std::string &section_, const std::string &key_)
		: section(section_)
		, key(key_)
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

class SettingsContainer
{

	#if defined(MPT_SETTINGS_CACHE)

		public:
			typedef std::map<SettingPath,SettingState> SettingsMap;
			typedef std::map<SettingPath,SettingMetadata> SettingsMetaMap;
		private:
			mutable SettingsMap map;
			mutable SettingsMetaMap mapMetadata;
			void WriteSettings();

	#endif // MPT_SETTINGS_CACHE

private:
	ISettingsBackend &backend;
	SettingValue ReadSetting(const SettingPath &path, const SettingValue &def, const SettingMetadata &metadata) const;
	void WriteSetting(const SettingPath &path, const SettingValue &val);
	void RemoveSetting(const SettingPath &path);
private:
	SettingsContainer(const SettingsContainer &other); // disable
	SettingsContainer& operator = (const SettingsContainer &other); // disable
public:
	SettingsContainer(ISettingsBackend &backend);
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
	~SettingsContainer();

	#if defined(MPT_SETTINGS_CACHE)

		public:
			SettingsMap::const_iterator begin() const { return map.begin(); }
			SettingsMap::const_iterator end() const { return map.end(); }
			const SettingsMap &GetMap() const { return map; }

	#endif // MPT_SETTINGS_CACHE

};

template <typename T>
class DeferredSettingsContainer
{
private:
	T *impl;
public:
	DeferredSettingsContainer()
		: impl(nullptr)
	{
		return;
	}
	~DeferredSettingsContainer()
	{
		if(impl)
		{
			delete impl;
			impl = nullptr;
		}
	}
	void Construct() { ASSERT(!impl); impl = new T(); }
	template<typename Ta> void Construct(const Ta &a) { ASSERT(!impl); impl = new T(a); }
	template<typename Ta, typename Tb> void Construct(const Ta &a, const Tb &b) { ASSERT(!impl); impl = new T(a, b); }
	template<typename Ta, typename Tb, typename Tc> void Construct(const Ta &a, const Tb &b, const Tc &c) { ASSERT(!impl); impl = new T(a, b, c); }
	void Destruct() { ASSERT(impl); delete impl; impl = 0; }
	template<typename T>
	T Read(const SettingPath &path, const T &def, const SettingMetadata &metadata = SettingMetadata()) const
	{
		ASSERT(impl);
		return impl->Read(path, def, metadata);
	}
	template<typename T>
	T Read(const std::string &section, const std::string &key, const T &def, const SettingMetadata &metadata = SettingMetadata()) const
	{
		ASSERT(impl);
		return impl->Read(section, key, def, metadata);
	}
	template <typename T>
	void Write(const SettingPath &path, const T &val)
	{
		ASSERT(impl);
		impl->Write(path, val);
	}
	template <typename T>
	void Write(const std::string &section, const std::string &key, const T &val)
	{
		ASSERT(impl);
		impl->Write(section, key, val);
	}
	void Remove(const SettingPath &path)
	{
		ASSERT(impl);
		impl->Remove(path);
	}
	void Remove(const std::string &section, const std::string &key)
	{
		ASSERT(impl);
		impl->Remove(section, key);
	}
	void Flush()
	{
		ASSERT(impl);
		impl->Flush();
	}
	operator const T& () const
	{
		ASSERT(impl);
		return *impl;
	}
	operator T& ()
	{
		ASSERT(impl);
		return *impl;
	}
};

template <typename T>
class Setting
{
private:
	SettingsContainer &conf;
	SettingPath path;
	T defaultValue;
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
	Setting & operator = (const T &val)
	{
		conf.Write(path, val);
		return *this;
	}
	operator T () const
	{
		return conf.Read(path, defaultValue);
	}
};

template <typename T>
class ConstSetting
{
private:
	const SettingsContainer &conf;
	SettingPath path;
	T defaultValue;
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
	operator T () const
	{
		return conf.Read(path, defaultValue);
	}
};


class IniFileSettingsBackend : public ISettingsBackend
{
private:
	const std::string filename;
private:
	std::string ReadSettingRaw(const SettingPath &path, const std::string &def) const;
	float ReadSettingRaw(const SettingPath &path, float def) const;
	int32 ReadSettingRaw(const SettingPath &path, int32 def) const;
	bool ReadSettingRaw(const SettingPath &path, bool def) const;
	void WriteSettingRaw(const SettingPath &path, const std::string &val);
	void WriteSettingRaw(const SettingPath &path, float val);
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

