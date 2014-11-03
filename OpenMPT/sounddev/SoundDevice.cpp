/*
 * SoundDevice.cpp
 * ---------------
 * Purpose: Actual sound device driver classes.
 * Notes  : (currently none)
 * Authors: Olivier Lapicque
 *          OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"

#include "SoundDevice.h"

#include "../common/misc_util.h"
#include "../common/StringFixer.h"

#include "SoundDeviceASIO.h"
#include "SoundDeviceDirectSound.h"
#include "SoundDevicePortAudio.h"
#include "SoundDeviceWaveout.h"

#include <algorithm>
#include <iterator>

#include <mmreg.h>


OPENMPT_NAMESPACE_BEGIN


namespace SoundDevice {


mpt::ustring TypeToString(SoundDevice::Type type)
//-----------------------------------------------
{
	switch(type)
	{
	case TypeWAVEOUT: return MPT_USTRING("WaveOut"); break;
	case TypeDSOUND: return MPT_USTRING("DirectSound"); break;
	case TypeASIO: return MPT_USTRING("ASIO"); break;
	case TypePORTAUDIO_WASAPI: return MPT_USTRING("WASAPI"); break;
	case TypePORTAUDIO_WDMKS: return MPT_USTRING("WDM-KS"); break;
	case TypePORTAUDIO_WMME: return MPT_USTRING("MME"); break;
	case TypePORTAUDIO_DS: return MPT_USTRING("DS"); break;
	case TypePORTAUDIO_ASIO: return MPT_USTRING("ASIO"); break;
	}
	return mpt::ustring();
}


ChannelMapping::ChannelMapping()
//------------------------------
{
	return;
}


ChannelMapping::ChannelMapping(uint32 numHostChannels)
//----------------------------------------------------
{
	ChannelToDeviceChannel.resize(numHostChannels);
	for(uint32 channel = 0; channel < numHostChannels; ++channel)
	{
		ChannelToDeviceChannel[channel] = channel;
	}
}


ChannelMapping::ChannelMapping(const std::vector<int32> &mapping)
//---------------------------------------------------------------
{
	if(IsValid(mapping))
	{
		ChannelToDeviceChannel = mapping;
	}
}


ChannelMapping ChannelMapping::BaseChannel(uint32 channels, int32 baseChannel)
//----------------------------------------------------------------------------
{
	SoundDevice::ChannelMapping result;
	result.ChannelToDeviceChannel.resize(channels);
	for(uint32 channel = 0; channel < channels; ++channel)
	{
		result.ChannelToDeviceChannel[channel] = channel + baseChannel;
	}
	return result;
}


bool ChannelMapping::IsValid(const std::vector<int32> &mapping)
//-------------------------------------------------------------
{
	if(mapping.empty())
	{
		return true;
	}
	std::map<int32, uint32> inverseMapping;
	for(uint32 hostChannel = 0; hostChannel < mapping.size(); ++hostChannel)
	{
		int32 deviceChannel = mapping[hostChannel];
		if(deviceChannel < 0)
		{
			return false;
		}
		if(deviceChannel > MaxDeviceChannel)
		{
			return false;
		}
		inverseMapping[deviceChannel] = hostChannel;
	}
	if(inverseMapping.size() != mapping.size())
	{
		return false;
	}
	return true;
}


std::string ChannelMapping::ToString() const
//------------------------------------------
{
	return mpt::String::Combine<int32>(ChannelToDeviceChannel);
}


ChannelMapping ChannelMapping::FromString(const std::string &str)
//---------------------------------------------------------------
{
	return SoundDevice::ChannelMapping(mpt::String::Split<int32>(str));
}


///////////////////////////////////////////////////////////////////////////////////////
//
// SoundDevice::Base base class
//

Base::Base(SoundDevice::Info info)
//--------------------------------
	: m_Source(nullptr)
	, m_MessageReceiver(nullptr)
	, m_Info(info)
{

	m_DeviceUnavailableOnOpen = false;

	m_BufferAttributes.Latency = m_Settings.Latency;
	m_BufferAttributes.UpdateInterval = m_Settings.UpdateInterval;
	m_BufferAttributes.NumBuffers = 0;

	m_IsPlaying = false;
	m_CurrentUpdateInterval = 0.0;
	m_StreamPositionRenderFrames = 0;
	m_StreamPositionOutputFrames = 0;

	m_RequestFlags.store(0);
}


Base::~Base()
//-----------
{
	return;
}


SoundDevice::DynamicCaps Base::GetDeviceDynamicCaps(const std::vector<uint32> &baseSampleRates)
//---------------------------------------------------------------------------------------------
{
	SoundDevice::DynamicCaps result;
	result.supportedSampleRates = baseSampleRates;
	return result;
}


bool FillWaveFormatExtensible(WAVEFORMATEXTENSIBLE &WaveFormat, const SoundDevice::Settings &m_Settings)
//------------------------------------------------------------------------------------------------------
{
	MemsetZero(WaveFormat);
	if(!m_Settings.sampleFormat.IsValid()) return false;
	WaveFormat.Format.wFormatTag = m_Settings.sampleFormat.IsFloat() ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
	WaveFormat.Format.nChannels = (WORD)m_Settings.Channels;
	WaveFormat.Format.nSamplesPerSec = m_Settings.Samplerate;
	WaveFormat.Format.nAvgBytesPerSec = (DWORD)m_Settings.GetBytesPerSecond();
	WaveFormat.Format.nBlockAlign = (WORD)m_Settings.GetBytesPerFrame();
	WaveFormat.Format.wBitsPerSample = (WORD)m_Settings.sampleFormat.GetBitsPerSample();
	WaveFormat.Format.cbSize = 0;
	if((WaveFormat.Format.wBitsPerSample > 16 && m_Settings.sampleFormat.IsInt()) || (WaveFormat.Format.nChannels > 2))
	{
		WaveFormat.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		WaveFormat.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
		WaveFormat.Samples.wValidBitsPerSample = WaveFormat.Format.wBitsPerSample;
		switch(WaveFormat.Format.nChannels)
		{
		case 1:  WaveFormat.dwChannelMask = SPEAKER_FRONT_CENTER; break;
		case 2:  WaveFormat.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT; break;
		case 3:  WaveFormat.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_CENTER; break;
		case 4:  WaveFormat.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT; break;
		default: WaveFormat.dwChannelMask = 0; return false; break;
		}
		const GUID guid_MEDIASUBTYPE_PCM = {0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x0, 0xAA, 0x0, 0x38, 0x9B, 0x71};
		const GUID guid_MEDIASUBTYPE_IEEE_FLOAT = {0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
		WaveFormat.SubFormat = m_Settings.sampleFormat.IsFloat() ? guid_MEDIASUBTYPE_IEEE_FLOAT : guid_MEDIASUBTYPE_PCM;
	}
	return true;
}


void Base::UpdateBufferAttributes(SoundDevice::BufferAttributes attributes)
//-------------------------------------------------------------------------
{
	m_BufferAttributes = attributes;
}


void Base::UpdateTimeInfo(SoundDevice::TimeInfo timeInfo)
//-------------------------------------------------------
{
	m_TimeInfo = timeInfo;
}


bool Base::Init()
//---------------
{
	if(IsInited())
	{
		return true;
	}
	m_Caps = InternalGetDeviceCaps();
	return m_Caps.Available;
}


bool Base::Open(const SoundDevice::Settings &settings)
//----------------------------------------------------
{
	if(IsOpen())
	{
		Close();
	}
	m_Settings = settings;
	if(m_Settings.Latency == 0.0) m_Settings.Latency = m_Caps.DefaultSettings.Latency;
	if(m_Settings.UpdateInterval == 0.0) m_Settings.UpdateInterval = m_Caps.DefaultSettings.UpdateInterval;
	m_Settings.Latency = Clamp(m_Settings.Latency, m_Caps.LatencyMin, m_Caps.LatencyMax);
	m_Settings.UpdateInterval = Clamp(m_Settings.UpdateInterval, m_Caps.UpdateIntervalMin, m_Caps.UpdateIntervalMax);
	if(m_Caps.CanChannelMapping)
	{
		if(m_Settings.ChannelMapping.GetNumHostChannels() == 0)
		{
			// default mapping
			m_Settings.ChannelMapping = SoundDevice::ChannelMapping(m_Settings.Channels);
		}
		if(m_Settings.ChannelMapping.GetNumHostChannels() != m_Settings.Channels)
		{
			return false;
		}
	} else
	{
		m_Settings.ChannelMapping = SoundDevice::ChannelMapping(m_Settings.Channels);
	}
	m_Flags = SoundDevice::Flags();
	m_DeviceUnavailableOnOpen = false;
	m_BufferAttributes.Latency = m_Settings.Latency;
	m_BufferAttributes.UpdateInterval = m_Settings.UpdateInterval;
	m_BufferAttributes.NumBuffers = 0;
	m_RequestFlags.store(0);
	return InternalOpen();
}


bool Base::Close()
//----------------
{
	if(!IsOpen()) return true;
	Stop();
	bool result = InternalClose();
	m_RequestFlags.store(0);
	return result;
}


void Base::FillAudioBuffer()
//--------------------------
{
	InternalFillAudioBuffer();
}


void Base::SourceFillAudioBufferLocked()
//--------------------------------------
{
	if(m_Source)
	{
		m_Source->FillAudioBufferLocked(*this);
	}
}


void Base::SourceAudioPreRead(std::size_t numFrames)
//--------------------------------------------------
{
	if(!InternalHasTimeInfo())
	{
		if(InternalHasGetStreamPosition())
		{
			SoundDevice::TimeInfo timeInfo;
			timeInfo.StreamFrames = InternalHasGetStreamPosition();
			timeInfo.SystemTimestamp = Clock().NowNanoseconds();
			timeInfo.Speed = 1.0;
			UpdateTimeInfo(timeInfo);
		} else
		{
			SoundDevice::TimeInfo timeInfo;
			{
				Util::lock_guard<Util::mutex> lock(m_StreamPositionMutex);
				timeInfo.StreamFrames = m_StreamPositionRenderFrames + numFrames;
			}
			timeInfo.SystemTimestamp = Clock().NowNanoseconds() + Util::Round<int64>(m_BufferAttributes.Latency * 1000000000.0);
			timeInfo.Speed = 1.0;
			UpdateTimeInfo(timeInfo);
		}
	}
}


void Base::SourceAudioRead(void *buffer, std::size_t numFrames)
//-------------------------------------------------------------
{
	if(numFrames <= 0)
	{
		return;
	}
	m_Source->AudioRead(m_Settings, m_Flags, m_BufferAttributes, m_TimeInfo, numFrames, buffer);
}


void Base::SourceAudioDone(std::size_t numFrames, int32 framesLatency)
//--------------------------------------------------------------------
{
	if(numFrames <= 0)
	{
		return;
	}
	int64 framesRendered = 0;
	{
		Util::lock_guard<Util::mutex> lock(m_StreamPositionMutex);
		m_CurrentUpdateInterval = (double)numFrames / (double)m_Settings.Samplerate;
		m_StreamPositionRenderFrames += numFrames;
		m_StreamPositionOutputFrames = m_StreamPositionRenderFrames - framesLatency;
		framesRendered = m_StreamPositionRenderFrames;
	}
	m_Source->AudioDone(m_Settings, m_Flags, m_BufferAttributes, m_TimeInfo, numFrames, framesRendered);
}


void Base::AudioSendMessage(const std::string &str)
//-------------------------------------------------
{
	if(m_MessageReceiver)
	{
		m_MessageReceiver->AudioMessage(str);
	}
}


bool Base::Start()
//----------------
{
	if(!IsOpen()) return false; 
	if(!IsPlaying())
	{
		{
			Util::lock_guard<Util::mutex> lock(m_StreamPositionMutex);
			m_CurrentUpdateInterval = 0.0;
			m_StreamPositionRenderFrames = 0;
			m_StreamPositionOutputFrames = 0;
		}
		m_Clock.SetResolution(1);
		m_RequestFlags.fetch_and(~RequestFlagRestart);
		if(!InternalStart())
		{
			m_Clock.SetResolution(0);
			return false;
		}
		m_IsPlaying = true;
	}
	return true;
}


void Base::Stop(bool force)
//-------------------------
{
	if(!IsOpen()) return;
	if(IsPlaying())
	{
		if(force)
		{
			InternalStopForce();
		} else
		{
			InternalStop();
		}
		m_RequestFlags.fetch_and(~RequestFlagRestart);
		m_Clock.SetResolution(0);
		m_IsPlaying = false;
		{
			Util::lock_guard<Util::mutex> lock(m_StreamPositionMutex);
			m_CurrentUpdateInterval = 0.0;
			m_StreamPositionRenderFrames = 0;
			m_StreamPositionOutputFrames = 0;
		}
	}
}


double Base::GetCurrentUpdateInterval() const
//-------------------------------------------
{
	Util::lock_guard<Util::mutex> lock(m_StreamPositionMutex);
	return m_CurrentUpdateInterval;
}


int64 Base::GetStreamPositionFrames() const
//-----------------------------------------
{
	if(!IsOpen()) return 0;
	if(InternalHasGetStreamPosition())
	{
		return InternalGetStreamPositionFrames();
	} else
	{
		Util::lock_guard<Util::mutex> lock(m_StreamPositionMutex);
		return m_StreamPositionOutputFrames;
	}
}


///////////////////////////////////////////////////////////////////////////////////////
//
// Global Functions
//


void Manager::ReEnumerate()
//-------------------------
{
	m_SoundDevices.clear();
	m_DeviceUnavailable.clear();
	m_DeviceCaps.clear();
	m_DeviceDynamicCaps.clear();

#ifndef NO_PORTAUDIO
	SndDevPortaudioUnnitialize();
	SndDevPortaudioInitialize();
#endif // NO_PORTAUDIO

	{
		const std::vector<SoundDevice::Info> infos = CWaveDevice::EnumerateDevices();
		std::copy(infos.begin(), infos.end(), std::back_inserter(m_SoundDevices));
	}
#ifndef NO_ASIO
	{
		const std::vector<SoundDevice::Info> infos = CASIODevice::EnumerateDevices();
		std::copy(infos.begin(), infos.end(), std::back_inserter(m_SoundDevices));
	}
#endif // NO_ASIO
#ifndef NO_PORTAUDIO
	{
		const std::vector<SoundDevice::Info> infos = CPortaudioDevice::EnumerateDevices(TypePORTAUDIO_WASAPI);
		std::copy(infos.begin(), infos.end(), std::back_inserter(m_SoundDevices));
	}
	{
		const std::vector<SoundDevice::Info> infos = CPortaudioDevice::EnumerateDevices(TypePORTAUDIO_WDMKS);
		std::copy(infos.begin(), infos.end(), std::back_inserter(m_SoundDevices));
	}
#endif // NO_PORTAUDIO

	// kind of deprecated by now
#ifndef NO_DSOUND
	{
		const std::vector<SoundDevice::Info> infos = CDSoundDevice::EnumerateDevices();
		std::copy(infos.begin(), infos.end(), std::back_inserter(m_SoundDevices));
	}
#endif // NO_DSOUND

	// duplicate devices, only used if [Sound Settings]MorePortaudio=1
#ifndef NO_PORTAUDIO
	{
		const std::vector<SoundDevice::Info> infos = CPortaudioDevice::EnumerateDevices(TypePORTAUDIO_WMME);
		std::copy(infos.begin(), infos.end(), std::back_inserter(m_SoundDevices));
	}
	{
		const std::vector<SoundDevice::Info> infos = CPortaudioDevice::EnumerateDevices(TypePORTAUDIO_ASIO);
		std::copy(infos.begin(), infos.end(), std::back_inserter(m_SoundDevices));
	}
	{
		const std::vector<SoundDevice::Info> infos = CPortaudioDevice::EnumerateDevices(TypePORTAUDIO_DS);
		std::copy(infos.begin(), infos.end(), std::back_inserter(m_SoundDevices));
	}
#endif // NO_PORTAUDIO

}


SoundDevice::Info Manager::FindDeviceInfo(SoundDevice::ID id) const
//-----------------------------------------------------------------
{
	for(std::vector<SoundDevice::Info>::const_iterator it = begin(); it != end(); ++it)
	{
		if(it->id == id)
		{
			return *it;
		}
	}
	return SoundDevice::Info();
}


SoundDevice::Info Manager::FindDeviceInfo(SoundDevice::Identifier identifier) const
//---------------------------------------------------------------------------------
{
	if(m_SoundDevices.empty())
	{
		return SoundDevice::Info();
	}
	if(identifier.empty())
	{
		return m_SoundDevices[0];
	}
	for(std::vector<SoundDevice::Info>::const_iterator it = begin(); it != end(); ++it)
	{
		if(it->GetIdentifier() == identifier)
		{
			return *it;
		}
	}
	return SoundDevice::Info();
}


SoundDevice::Type ParseType(const SoundDevice::Identifier &identifier)
//--------------------------------------------------------------------
{
	for(int i = 0; i < TypeNUM_DEVTYPES; ++i)
	{
		const mpt::ustring api = SoundDevice::TypeToString(static_cast<SoundDevice::Type>(i));
		if(identifier.find(api) == 0)
		{
			return static_cast<SoundDevice::Type>(i);
		}
	}
	return TypeINVALID;
}


SoundDevice::Info Manager::FindDeviceInfoBestMatch(SoundDevice::Identifier identifier, bool preferSameType)
//---------------------------------------------------------------------------------------------------------
{
	if(m_SoundDevices.empty())
	{
		return SoundDevice::Info();
	}
	if(identifier.empty())
	{
			return *begin();
	}
	for(std::vector<SoundDevice::Info>::const_iterator it = begin(); it != end(); ++it)
	{
		if((it->GetIdentifier() == identifier) && !IsDeviceUnavailable(it->GetIdentifier()))
		{ // exact match
			return *it;
		}
	}
	const SoundDevice::Type type = ParseType(identifier);
	switch(type)
	{
		case TypePORTAUDIO_WASAPI:
			// WASAPI devices might change names if a different connector jack is used.
			// In order to avoid defaulting to wave mapper in that case,
			// just find the first WASAPI device.
			for(std::vector<SoundDevice::Info>::const_iterator it = begin(); it != end(); ++it)
			{
				if((it->id.GetType() == TypePORTAUDIO_WASAPI) && !IsDeviceUnavailable(it->GetIdentifier()))
				{
					return *it;
				}
			}
			// default to first device
			return *begin();
			break;
		case TypeWAVEOUT:
		case TypeDSOUND:
		case TypePORTAUDIO_WMME:
		case TypePORTAUDIO_DS:
		case TypeASIO:
		case TypePORTAUDIO_WDMKS:
		case TypePORTAUDIO_ASIO:
			if(preferSameType)
			{
				for(std::vector<SoundDevice::Info>::const_iterator it = begin(); it != end(); ++it)
				{
					if((it->id.GetType() == type) && !IsDeviceUnavailable(it->GetIdentifier()))
					{
						return *it;
					}
				}
			} else
			{
				// default to first device
				return *begin();
			}
			break;
	}
	// invalid
	return SoundDevice::Info();
}


bool Manager::OpenDriverSettings(SoundDevice::Identifier identifier, SoundDevice::IMessageReceiver *messageReceiver, SoundDevice::IBase *currentSoundDevice)
//----------------------------------------------------------------------------------------------------------------------------------------------------------
{
	bool result = false;
	if(currentSoundDevice && FindDeviceInfo(identifier).IsValid() && (currentSoundDevice->GetDeviceInfo().GetIdentifier() == identifier))
	{
		result = currentSoundDevice->OpenDriverSettings();
	} else
	{
		SoundDevice::IBase *dummy = CreateSoundDevice(identifier);
		if(dummy)
		{
			dummy->SetMessageReceiver(messageReceiver);
			result = dummy->OpenDriverSettings();
		}
		delete dummy;
	}
	return result;
}


SoundDevice::Caps Manager::GetDeviceCaps(SoundDevice::Identifier identifier, SoundDevice::IBase *currentSoundDevice)
//------------------------------------------------------------------------------------------------------------------
{
	if(m_DeviceCaps.find(identifier) == m_DeviceCaps.end())
	{
		if(currentSoundDevice && FindDeviceInfo(identifier).IsValid() && (currentSoundDevice->GetDeviceInfo().GetIdentifier() == identifier))
		{
			m_DeviceCaps[identifier] = currentSoundDevice->GetDeviceCaps();
		} else
		{
			SoundDevice::IBase *dummy = CreateSoundDevice(identifier);
			if(dummy)
			{
				m_DeviceCaps[identifier] = dummy->GetDeviceCaps();
			} else
			{
				SetDeviceUnavailable(identifier);
			}
			delete dummy;
		}
	}
	return m_DeviceCaps[identifier];
}


SoundDevice::DynamicCaps Manager::GetDeviceDynamicCaps(SoundDevice::Identifier identifier, const std::vector<uint32> &baseSampleRates, SoundDevice::IMessageReceiver *messageReceiver, SoundDevice::IBase *currentSoundDevice, bool update)
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
{
	if((m_DeviceDynamicCaps.find(identifier) == m_DeviceDynamicCaps.end()) || update)
	{
		if(currentSoundDevice && FindDeviceInfo(identifier).IsValid() && (currentSoundDevice->GetDeviceInfo().GetIdentifier() == identifier))
		{
			m_DeviceDynamicCaps[identifier] = currentSoundDevice->GetDeviceDynamicCaps(baseSampleRates);
			if(!currentSoundDevice->IsAvailable())
			{
				SetDeviceUnavailable(identifier);
			}
		} else
		{
			SoundDevice::IBase *dummy = CreateSoundDevice(identifier);
			if(dummy)
			{
				dummy->SetMessageReceiver(messageReceiver);
				m_DeviceDynamicCaps[identifier] = dummy->GetDeviceDynamicCaps(baseSampleRates);
				if(!dummy->IsAvailable())
				{
					SetDeviceUnavailable(identifier);
				}
			} else
			{
				SetDeviceUnavailable(identifier);
			}
			delete dummy;
		}
	}
	return m_DeviceDynamicCaps[identifier];
}


SoundDevice::IBase * Manager::CreateSoundDevice(SoundDevice::Identifier identifier)
//---------------------------------------------------------------------------------
{
	const SoundDevice::Info info = FindDeviceInfo(identifier);
	if(!info.IsValid())
	{
		return nullptr;
	}
	SoundDevice::IBase *result = nullptr;
	switch(info.id.GetType())
	{
	case TypeWAVEOUT: result = new CWaveDevice(info); break;
#ifndef NO_DSOUND
	case TypeDSOUND: result = new CDSoundDevice(info); break;
#endif // NO_DSOUND
#ifndef NO_ASIO
	case TypeASIO: result = new CASIODevice(info); break;
#endif // NO_ASIO
#ifndef NO_PORTAUDIO
	case TypePORTAUDIO_WASAPI:
	case TypePORTAUDIO_WDMKS:
	case TypePORTAUDIO_WMME:
	case TypePORTAUDIO_DS:
	case TypePORTAUDIO_ASIO:
		result = SndDevPortaudioIsInitialized() ? new CPortaudioDevice(info) : nullptr;
		break;
#endif // NO_PORTAUDIO
	}
	if(!result)
	{
		return nullptr;
	}
	if(!result->Init())
	{
		delete result;
		result = nullptr;
		return nullptr;
	}
	m_DeviceCaps[identifier] = result->GetDeviceCaps(); // update cached caps
	return result;
}


Manager::Manager()
//----------------
{
	ReEnumerate();
}


Manager::~Manager()
//-----------------
{
#ifndef NO_PORTAUDIO
	SndDevPortaudioUnnitialize();
#endif // NO_PORTAUDIO
}


} // namespace SoundDevice


OPENMPT_NAMESPACE_END
