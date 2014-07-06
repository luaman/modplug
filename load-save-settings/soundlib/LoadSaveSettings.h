/*
 * LoadSaveSettings.h
 * ------------------
 * Purpose: Interface for sepcifying loading and saving settings for CSoundFile for some file formats.
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */

#pragma once


OPENMPT_NAMESPACE_BEGIN


//=====================
class ILoadSaveSettings
//=====================
{
public:
	virtual ~ILoadSaveSettings() {}

	virtual bool LoadXMApplySmoothFT2VolumeRamping() = 0;
	virtual bool LoadMODMaxPanning() = 0;
	virtual int32 LoadMIDISpeed() = 0;
	virtual int32 LoadMIDIPatternLength() = 0;

#ifndef MODPLUG_NO_FILESAVE

	virtual int SaveFLACCompressionLevel() = 0;
	virtual bool SaveITCompatibleCompressStereoSamples() = 0;
	virtual bool SaveITCompatibleCompressMonoSamples() = 0;
	virtual bool SaveITCompressStereoSamples() = 0;
	virtual bool SaveITCompressMonoSamples() = 0;
	virtual bool SaveMPTMCompressStereoSamples() = 0;
	virtual bool SaveMPTMCompressMonoSamples() = 0;

	static bool GetITCompatibleCompressionFromMask(uint32 mask)
	{
		return (mask & (1<<1)) ? true : false;
	}
	static bool GetITCompressionFromMask(uint32 mask)
	{
		return (mask & (1<<0)) ? true : false;
	}
	static bool GetMPTMCompressionFromMask(uint32 mask)
	{
		return (mask & (1<<2)) ? true : false;
	}
	uint32 GetITCompressionStereoMask()
	{
		uint32 mask = 0;
		mask |= SaveITCompatibleCompressStereoSamples() ? (1<<1) : 0;
		mask |= SaveITCompressStereoSamples() ? (1<<0) : 0;
		mask |= SaveMPTMCompressStereoSamples() ? (1<<2) : 0;
		return mask;
	}
	uint32 GetITCompressionMonoMask()
	{
		uint32 mask = 0;
		mask |= SaveITCompatibleCompressMonoSamples() ? (1<<1) : 0;
		mask |= SaveITCompressMonoSamples() ? (1<<0) : 0;
		mask |= SaveMPTMCompressMonoSamples() ? (1<<2) : 0;
		return mask;
	}

#endif // !MODPLUG_NO_FILESAVE

};


//============================
class LoadSaveSettingsDefaults
//============================
	: public ILoadSaveSettings
{
public:
	virtual ~LoadSaveSettingsDefaults() {}

	virtual bool LoadXMApplySmoothFT2VolumeRamping() { return false; }
	virtual bool LoadMODMaxPanning() { return false; }
	virtual int32 LoadMIDISpeed() { return 3; }
	virtual int32 LoadMIDIPatternLength() { return 128; }

#ifndef MODPLUG_NO_FILESAVE
	virtual int SaveFLACCompressionLevel() { return 5; }
	virtual bool SaveITCompatibleCompressStereoSamples() { return false; }
	virtual bool SaveITCompatibleCompressMonoSamples() { return false; }
	virtual bool SaveITCompressStereoSamples() { return false; }
	virtual bool SaveITCompressMonoSamples() { return false; }
	virtual bool SaveMPTMCompressStereoSamples() { return false; }
	virtual bool SaveMPTMCompressMonoSamples() { return false; }
#endif // !MODPLUG_NO_FILESAVE

};


OPENMPT_NAMESPACE_END
