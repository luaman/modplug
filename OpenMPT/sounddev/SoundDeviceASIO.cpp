/*
 * SoundDeviceASIO.cpp
 * -------------------
 * Purpose: ASIO sound device driver class.
 * Notes  : (currently none)
 * Authors: Olivier Lapicque
 *          OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"

#include "SoundDevice.h"
#include "SoundDevices.h"

#include "SoundDeviceASIO.h"

#include "../common/misc_util.h"
#include "../common/StringFixer.h"
#include "../soundlib/SampleFormatConverters.h"

// DEBUG:
#include "../common/AudioCriticalSection.h"

#include <algorithm>


// Helper class to temporarily open a device for a query.
class TemporaryASIODeviceOpener
{
protected:
	CASIODevice &device;
	const bool wasOpen;

public:
	TemporaryASIODeviceOpener(CASIODevice &d) : device(d), wasOpen(d.IsOpen())
	{
		if(!wasOpen)
		{
			device.OpenDevice();
		}
	}

	~TemporaryASIODeviceOpener()
	{
		if(!wasOpen)
		{
			device.CloseDevice();
		}
	}
};


///////////////////////////////////////////////////////////////////////////////////////
//
// ASIO Device implementation
//

#ifndef NO_ASIO

#define ASIO_MAXDRVNAMELEN	1024

CASIODevice *CASIODevice::gpCurrentAsio = nullptr;

static DWORD g_dwBuffer = 0;

static int g_asio_startcount = 0;


std::vector<SoundDeviceInfo> CASIODevice::EnumerateDevices()
//----------------------------------------------------------
{
	std::vector<SoundDeviceInfo> devices;

	LONG cr;

	HKEY hkEnum = NULL;
	cr = RegOpenKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", &hkEnum);

	for(DWORD index = 0; ; ++index)
	{

		WCHAR keynameBuf[ASIO_MAXDRVNAMELEN];
		if((cr = RegEnumKeyW(hkEnum, index, keynameBuf, ASIO_MAXDRVNAMELEN)) != ERROR_SUCCESS)
		{
			break;
		}
		const std::wstring keyname = keynameBuf;
		#ifdef ASIO_LOG
			Log("ASIO: Found \"%s\":\n", mpt::ToLocale(keyname).c_str());
		#endif

		HKEY hksub = NULL;
		if(RegOpenKeyExW(hkEnum, keynameBuf, 0, KEY_READ, &hksub) != ERROR_SUCCESS)
		{
			continue;
		}

		WCHAR descriptionBuf[ASIO_MAXDRVNAMELEN];
		DWORD datatype = REG_SZ;
		DWORD datasize = sizeof(descriptionBuf);
		std::wstring description;
		if(ERROR_SUCCESS == RegQueryValueExW(hksub, L"Description", 0, &datatype, (LPBYTE)descriptionBuf, &datasize))
		{
		#ifdef ASIO_LOG
			Log("  description =\"%s\":\n", mpt::ToLocale(description).c_str());
		#endif
			description = descriptionBuf;
		} else
		{
			description = keyname;
		}

		WCHAR idBuf[256];
		datatype = REG_SZ;
		datasize = sizeof(idBuf);
		if(ERROR_SUCCESS == RegQueryValueExW(hksub, L"CLSID", 0, &datatype, (LPBYTE)idBuf, &datasize))
		{
			const std::wstring internalID = idBuf;
			if(Util::IsCLSID(internalID))
			{
				#ifdef ASIO_LOG
					Log("  clsid=\"%s\"\n", mpt::ToLocale(internalID).c_str());
				#endif

				if(SoundDeviceIndexIsValid(devices.size()))
				{
					// everything ok
					devices.push_back(SoundDeviceInfo(SoundDeviceID(SNDDEV_ASIO, static_cast<SoundDeviceIndex>(devices.size())), description, L"ASIO", internalID));
				}

			}
		}

		RegCloseKey(hksub);
		hksub = NULL;

	}

	if(hkEnum)
	{
		RegCloseKey(hkEnum);
		hkEnum = NULL;
	}

	return devices;
}


CASIODevice::CASIODevice(SoundDeviceID id, const std::wstring &internalID)
//------------------------------------------------------------------------
	: ISoundDevice(id, internalID)
{
	m_pAsioDrv = NULL;
	m_nAsioBufferLen = 0;
	m_nAsioSampleSize = 0;
	m_Float = false;
	m_Callbacks.bufferSwitch = BufferSwitch;
	m_Callbacks.sampleRateDidChange = SampleRateDidChange;
	m_Callbacks.asioMessage = AsioMessage;
	m_Callbacks.bufferSwitchTimeInfo = BufferSwitchTimeInfo;
	m_bMixRunning = FALSE;
	InterlockedExchange(&m_RenderSilence, 0);
	InterlockedExchange(&m_RenderingSilence, 0);
}


CASIODevice::~CASIODevice()
//-------------------------
{
	Close();
}


bool CASIODevice::InternalOpen()
//------------------------------
{

	bool bOk = false;

#ifdef ASIO_LOG
	Log("CASIODevice::Open(%d:\"%s\"): %d-bit, %d channels, %dHz\n",
		GetDeviceIndex(), mpt::ToLocale(GetDeviceInternalID()).c_str(), (int)m_Settings.sampleFormat.GetBitsPerSample(), m_Settings.Channels, m_Settings.Samplerate);
#endif
	OpenDevice();

	if(IsDeviceOpen())
	{
		long nInputChannels = 0, nOutputChannels = 0;
		long minSize = 0, maxSize = 0, preferredSize = 0, granularity = 0;

		if(m_Settings.Channels > ASIO_MAX_CHANNELS)
		{
			goto abort;
		}
		m_pAsioDrv->getChannels(&nInputChannels, &nOutputChannels);
	#ifdef ASIO_LOG
		Log("  getChannels: %d inputs, %d outputs\n", nInputChannels, nOutputChannels);
	#endif
		if (m_Settings.Channels > nOutputChannels) goto abort;
		if (m_pAsioDrv->setSampleRate(m_Settings.Samplerate) != ASE_OK)
		{
		#ifdef ASIO_LOG
			Log("  setSampleRate(%d) failed (sample rate not supported)!\n", m_Settings.Samplerate);
		#endif
			goto abort;
		}
		for (UINT ich=0; ich<m_Settings.Channels; ich++)
		{
			m_ChannelInfo[ich].channel = ich;
			m_ChannelInfo[ich].isInput = ASIOFalse;
			m_pAsioDrv->getChannelInfo(&m_ChannelInfo[ich]);
		#ifdef ASIO_LOG
			Log("  getChannelInfo(%d): isActive=%d channelGroup=%d type=%d name=\"%s\"\n",
				ich, m_ChannelInfo[ich].isActive, m_ChannelInfo[ich].channelGroup, m_ChannelInfo[ich].type, m_ChannelInfo[ich].name);
		#endif
			m_BufferInfo[ich].isInput = ASIOFalse;
			m_BufferInfo[ich].channelNum = ich + m_Settings.BaseChannel;		// map MPT channel i to ASIO channel i
			m_BufferInfo[ich].buffers[0] = NULL;
			m_BufferInfo[ich].buffers[1] = NULL;
			m_Float = false;
			switch(m_ChannelInfo[ich].type)
			{
				case ASIOSTInt16MSB:
				case ASIOSTInt16LSB:
					m_nAsioSampleSize = 2;
					break;
				case ASIOSTInt24MSB:
				case ASIOSTInt24LSB:
					m_nAsioSampleSize = 3;
					break;
				case ASIOSTInt32MSB:
				case ASIOSTInt32LSB:
					m_nAsioSampleSize = 4;
					break;
				case ASIOSTInt32MSB16:
				case ASIOSTInt32MSB18:
				case ASIOSTInt32MSB20:
				case ASIOSTInt32MSB24:
				case ASIOSTInt32LSB16:
				case ASIOSTInt32LSB18:
				case ASIOSTInt32LSB20:
				case ASIOSTInt32LSB24:
					m_nAsioSampleSize = 4;
					break;
				case ASIOSTFloat32MSB:
				case ASIOSTFloat32LSB:
					m_Float = true;
					m_nAsioSampleSize = 4;
					break;
				case ASIOSTFloat64MSB:
				case ASIOSTFloat64LSB:
					m_Float = true;
					m_nAsioSampleSize = 8;
					break;
				default:
					m_nAsioSampleSize = 0;
					goto abort;
					break;
			}
		}
		m_Settings.sampleFormat = m_Float ? SampleFormatFloat32 : SampleFormatInt32;
		m_pAsioDrv->getBufferSize(&minSize, &maxSize, &preferredSize, &granularity);
	#ifdef ASIO_LOG
		Log("  getBufferSize(): minSize=%d maxSize=%d preferredSize=%d granularity=%d\n",
				minSize, maxSize, preferredSize, granularity);
	#endif
		m_nAsioBufferLen = ((m_Settings.LatencyMS * m_Settings.Samplerate) / 2000);
		if (m_nAsioBufferLen < (UINT)minSize) m_nAsioBufferLen = minSize; else
		if (m_nAsioBufferLen > (UINT)maxSize) m_nAsioBufferLen = maxSize; else
		if (granularity < 0)
		{
			//rewbs.ASIOfix:
			/*UINT n = (minSize < 32) ? 32 : minSize;
			if (n % granularity) n = (n + granularity - 1) - (n % granularity);
			while ((n+(n>>1) < m_nAsioBufferLen) && (n*2 <= (UINT)maxSize))
			{
				n *= 2;
			}
			m_nAsioBufferLen = n;*/
			//end rewbs.ASIOfix
			m_nAsioBufferLen = preferredSize;

		} else
		if (granularity > 0)
		{
			int n = (minSize < 32) ? 32 : minSize;
			n = (n + granularity-1);
			n -= (n % granularity);
			while ((n+(granularity>>1) < (int)m_nAsioBufferLen) && (n+granularity <= maxSize))
			{
				n += granularity;
			}
			m_nAsioBufferLen = n;
		}
	#ifdef ASIO_LOG
		Log("  Using buffersize=%d samples\n", m_nAsioBufferLen);
	#endif
		if (m_pAsioDrv->createBuffers(m_BufferInfo, m_Settings.Channels, m_nAsioBufferLen, &m_Callbacks) == ASE_OK)
		{
			for (UINT iInit=0; iInit<m_Settings.Channels; iInit++)
			{
				if (m_BufferInfo[iInit].buffers[0])
				{
					memset(m_BufferInfo[iInit].buffers[0], 0, m_nAsioBufferLen * m_nAsioSampleSize);
				}
				if (m_BufferInfo[iInit].buffers[1])
				{
					memset(m_BufferInfo[iInit].buffers[1], 0, m_nAsioBufferLen * m_nAsioSampleSize);
				}
			}

			m_CanOutputReady = (m_pAsioDrv->outputReady() == ASE_OK);

			SoundBufferAttributes bufferAttributes;
			long inputLatency = 0;
			long outputLatency = 0;
			if(m_pAsioDrv->getLatencies(&inputLatency, &outputLatency) != ASE_OK)
			{
				inputLatency = 0;
				outputLatency = 0;
			}
			if(outputLatency >= (long)m_nAsioBufferLen)
			{
				bufferAttributes.Latency = (double)(outputLatency + m_nAsioBufferLen) / (double)m_Settings.Samplerate; // ASIO and OpenMPT semantics of 'latency' differ by one chunk/buffer
			} else
			{
				// pointless value returned from asio driver, use a sane estimate
				bufferAttributes.Latency = 2.0 * (double)m_nAsioBufferLen / (double)m_Settings.Samplerate;
			}
			bufferAttributes.UpdateInterval = (double)m_nAsioBufferLen / (double)m_Settings.Samplerate;
			bufferAttributes.NumBuffers = 2;
			UpdateBufferAttributes(bufferAttributes);

			bOk = true;
		}
	#ifdef ASIO_LOG
		else Log("  createBuffers failed!\n");
	#endif
	}
abort:
	if (bOk)
	{
		gpCurrentAsio = this;
	} else
	{
	#ifdef ASIO_LOG
		Log("Error opening ASIO device!\n");
	#endif
		CloseDevice();
	}
	return bOk;
}


void CASIODevice::SetRenderSilence(bool silence, bool wait)
//---------------------------------------------------------
{
	InterlockedExchange(&m_RenderSilence, silence?1:0);
	if(!wait)
	{
		return;
	}
	DWORD pollingstart = GetTickCount();
	while(InterlockedExchangeAdd(&m_RenderingSilence, 0) != (silence?1:0))
	{
		if(GetTickCount() - pollingstart > 250)
		{
			if(silence)
			{
				if(CriticalSection::IsLocked())
				{
					ASSERT_WARN_MESSAGE(false, "AudioCriticalSection locked while stopping ASIO");
				} else
				{
					ASSERT_WARN_MESSAGE(false, "waiting for asio failed in Stop()");
				}
			} else
			{
				if(CriticalSection::IsLocked())
				{
					ASSERT_WARN_MESSAGE(false, "AudioCriticalSection locked while starting ASIO");
				} else
				{
					ASSERT_WARN_MESSAGE(false, "waiting for asio failed in Start()");
				}
			}
			break;
		}
		Sleep(1);
	}
}


void CASIODevice::InternalStart()
//-------------------------------
{
	ALWAYS_ASSERT_WARN_MESSAGE(!CriticalSection::IsLocked(), "AudioCriticalSection locked while starting ASIO");

		ALWAYS_ASSERT(g_asio_startcount==0);
		g_asio_startcount++;

		if(!m_bMixRunning)
		{
			SetRenderSilence(false);
			m_bMixRunning = TRUE;
			try
			{
				m_pAsioDrv->start();
			} catch(...)
			{
				CASIODevice::ReportASIOException("ASIO crash in start()\n");
				m_bMixRunning = FALSE;
			}
		} else
		{
			SetRenderSilence(false, true);
		}
}


void CASIODevice::InternalStop()
//------------------------------
{
	ALWAYS_ASSERT(g_asio_startcount==1);
	ALWAYS_ASSERT_WARN_MESSAGE(!CriticalSection::IsLocked(), "AudioCriticalSection locked while stopping ASIO");

		SetRenderSilence(true, true);
		g_asio_startcount--;
		ALWAYS_ASSERT(g_asio_startcount==0);
}


bool CASIODevice::InternalClose()
//-------------------------------
{
	if (m_bMixRunning)
	{
		m_bMixRunning = FALSE;
		ALWAYS_ASSERT(g_asio_startcount==0);
		try
		{
			m_pAsioDrv->stop();
		} catch(...)
		{
			CASIODevice::ReportASIOException("ASIO crash in stop()\n");
		}
	}
	g_asio_startcount = 0;
	SetRenderSilence(false);
	try
	{
		m_pAsioDrv->disposeBuffers();
	} catch(...)
	{
		CASIODevice::ReportASIOException("ASIO crash in disposeBuffers()\n");
	}
	CloseDevice();
	if(gpCurrentAsio == this)
	{
		gpCurrentAsio = nullptr;
	}
	return true;
}


void CASIODevice::OpenDevice()
//----------------------------
{
	if(IsDeviceOpen())
	{
		return;
	}

	CLSID clsid;
	Util::StringToCLSID(GetDeviceInternalID(), clsid);
	if (CoCreateInstance(clsid,0,CLSCTX_INPROC_SERVER, clsid, (void **)&m_pAsioDrv) == S_OK)
	{
		m_pAsioDrv->init((void *)m_Settings.hWnd);
	} else
	{
#ifdef ASIO_LOG
		Log("  CoCreateInstance failed!\n");
#endif
		m_pAsioDrv = NULL;
	}
}


void CASIODevice::CloseDevice()
//-----------------------------
{
	if(IsDeviceOpen())
	{
		try
		{
			m_pAsioDrv->Release();
		} catch(...)
		{
			CASIODevice::ReportASIOException("ASIO crash in Release()\n");
		}
		m_pAsioDrv = NULL;
	}
}


static void SwapEndian(uint8 *buf, std::size_t itemCount, std::size_t itemSize)
//-----------------------------------------------------------------------------
{
	for(std::size_t i = 0; i < itemCount; ++i)
	{
		std::reverse(buf, buf + itemSize);
		buf += itemSize;
	}
}


void CASIODevice::FillAudioBuffer()
//---------------------------------
{
	bool rendersilence = (InterlockedExchangeAdd(&m_RenderSilence, 0) == 1);

	const int channels = m_Settings.Channels;
	std::size_t sampleFrameSize = channels * sizeof(int32);
	const std::size_t sampleFramesGoal = m_nAsioBufferLen;
	std::size_t sampleFramesToRender = sampleFramesGoal;
	std::size_t sampleFramesRendered = 0;
	const std::size_t countChunkMax = (ASIO_BLOCK_LEN * sizeof(int32)) / sampleFrameSize;
	
	g_dwBuffer &= 1;
	//Log("FillAudioBuffer(%d): dwSampleSize=%d dwSamplesLeft=%d dwFrameLen=%d\n", g_dwBuffer, sampleFrameSize, dwSamplesLeft, dwFrameLen);
	while(sampleFramesToRender > 0)
	{
		const std::size_t countChunk = std::min(sampleFramesToRender, countChunkMax);
		if(rendersilence)
		{
			memset(m_FrameBuffer, 0, countChunk * sampleFrameSize);
		} else
		{
			SourceAudioRead(m_FrameBuffer, countChunk);
		}
		for(int channel = 0; channel < channels; ++channel)
		{
			const int32 *src = m_FrameBuffer;
			const float *srcFloat = reinterpret_cast<const float*>(m_FrameBuffer);
			void *dst = (char*)m_BufferInfo[channel].buffers[g_dwBuffer] + m_nAsioSampleSize * sampleFramesRendered;
			if(m_Float) switch(m_ChannelInfo[channel].type)
			{
				case ASIOSTFloat32MSB:
				case ASIOSTFloat32LSB:
					CopyInterleavedToChannel<SC::Convert<float, float> >(reinterpret_cast<float*>(dst), srcFloat, channels, countChunk, channel);
					break;
				case ASIOSTFloat64MSB:
				case ASIOSTFloat64LSB:
					CopyInterleavedToChannel<SC::Convert<double, float> >(reinterpret_cast<double*>(dst), srcFloat, channels, countChunk, channel);
					break;
				default:
					ASSERT(false);
					break;
			} else switch(m_ChannelInfo[channel].type)
			{
				case ASIOSTInt16MSB:
				case ASIOSTInt16LSB:
					CopyInterleavedToChannel<SC::Convert<int16, int32> >(reinterpret_cast<int16*>(dst), src, channels, countChunk, channel);
					break;
				case ASIOSTInt24MSB:
				case ASIOSTInt24LSB:
					CopyInterleavedToChannel<SC::Convert<int24, int32> >(reinterpret_cast<int24*>(dst), src, channels, countChunk, channel);
					break;
				case ASIOSTInt32MSB:
				case ASIOSTInt32LSB:
					CopyInterleavedToChannel<SC::Convert<int32, int32> >(reinterpret_cast<int32*>(dst), src, channels, countChunk, channel);
					break;
				case ASIOSTFloat32MSB:
				case ASIOSTFloat32LSB:
					CopyInterleavedToChannel<SC::Convert<float, int32> >(reinterpret_cast<float*>(dst), src, channels, countChunk, channel);
					break;
				case ASIOSTFloat64MSB:
				case ASIOSTFloat64LSB:
					CopyInterleavedToChannel<SC::Convert<double, int32> >(reinterpret_cast<double*>(dst), src, channels, countChunk, channel);
					break;
				case ASIOSTInt32MSB16:
				case ASIOSTInt32LSB16:
					CopyInterleavedToChannel<SC::ConvertShift<int32, int32, 16> >(reinterpret_cast<int32*>(dst), src, channels, countChunk, channel);
					break;
				case ASIOSTInt32MSB18:
				case ASIOSTInt32LSB18:
					CopyInterleavedToChannel<SC::ConvertShift<int32, int32, 14> >(reinterpret_cast<int32*>(dst), src, channels, countChunk, channel);
					break;
				case ASIOSTInt32MSB20:
				case ASIOSTInt32LSB20:
					CopyInterleavedToChannel<SC::ConvertShift<int32, int32, 12> >(reinterpret_cast<int32*>(dst), src, channels, countChunk, channel);
					break;
				case ASIOSTInt32MSB24:
				case ASIOSTInt32LSB24:
					CopyInterleavedToChannel<SC::ConvertShift<int32, int32, 8> >(reinterpret_cast<int32*>(dst), src, channels, countChunk, channel);
					break;
				default:
					ASSERT(false);
					break;
			}
			switch(m_ChannelInfo[channel].type)
			{
				case ASIOSTInt16MSB:
				case ASIOSTInt24MSB:
				case ASIOSTInt32MSB:
				case ASIOSTFloat32MSB:
				case ASIOSTFloat64MSB:
				case ASIOSTInt32MSB16:
				case ASIOSTInt32MSB18:
				case ASIOSTInt32MSB20:
				case ASIOSTInt32MSB24:
					SwapEndian(reinterpret_cast<uint8*>(dst), countChunk, m_nAsioSampleSize);
					break;
			}
		}
		sampleFramesToRender -= countChunk;
		sampleFramesRendered += countChunk;
	}
	if(m_CanOutputReady)
	{
		m_pAsioDrv->outputReady();
	}
	if(!rendersilence)
	{
		SourceAudioDone(sampleFramesRendered, m_nAsioBufferLen);
	}
	return;
}


void CASIODevice::BufferSwitch(long doubleBufferIndex)
//----------------------------------------------------
{
	g_dwBuffer = doubleBufferIndex;
	bool rendersilence = (InterlockedExchangeAdd(&m_RenderSilence, 0) == 1);
	InterlockedExchange(&m_RenderingSilence, rendersilence ? 1 : 0 );
	if(rendersilence)
	{
		FillAudioBuffer();
	} else
	{
		SourceFillAudioBufferLocked();
	}
}


void CASIODevice::BufferSwitch(long doubleBufferIndex, ASIOBool directProcess)
//----------------------------------------------------------------------------
{
	MPT_UNREFERENCED_PARAMETER(directProcess);
	if(gpCurrentAsio)
	{
		gpCurrentAsio->BufferSwitch(doubleBufferIndex);
	} else
	{
		ALWAYS_ASSERT(false && "gpCurrentAsio");
	}
}


void CASIODevice::SampleRateDidChange(ASIOSampleRate sRate)
//---------------------------------------------------------
{
	MPT_UNREFERENCED_PARAMETER(sRate);
}


long CASIODevice::AsioMessage(long selector, long value, void* message, double* opt)
//----------------------------------------------------------------------------------
{
	MPT_UNREFERENCED_PARAMETER(value);
	MPT_UNREFERENCED_PARAMETER(message);
	MPT_UNREFERENCED_PARAMETER(opt);
#ifdef ASIO_LOG
	// Log("AsioMessage(%d, %d)\n", selector, value);
#endif
	switch(selector)
	{
	case kAsioEngineVersion: return 2;
	}
	return 0;
}


ASIOTime* CASIODevice::BufferSwitchTimeInfo(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess)
//-----------------------------------------------------------------------------------------------------------
{
	BufferSwitch(doubleBufferIndex, directProcess);
	return params;
}


void CASIODevice::ReportASIOException(const std::string &str)
//-----------------------------------------------------------
{
	AudioSendMessage(str);
	Log("%s", str.c_str());
}


SoundDeviceCaps CASIODevice::GetDeviceCaps(const std::vector<uint32> &baseSampleRates)
//------------------------------------------------------------------------------------
{
	SoundDeviceCaps caps;

	TemporaryASIODeviceOpener opener(*this);
	if(!IsOpen())
	{
		return caps;
	}

	ASIOSampleRate samplerate;
	if(m_pAsioDrv->getSampleRate(&samplerate) != ASE_OK)
	{
		samplerate = 0;
	}
	caps.currentSampleRate = Util::Round<uint32>(samplerate);

	for(size_t i = 0; i < baseSampleRates.size(); i++)
	{
		if(m_pAsioDrv->canSampleRate((ASIOSampleRate)baseSampleRates[i]) == ASE_OK)
		{
			caps.supportedSampleRates.push_back(baseSampleRates[i]);
		}
	}
	long inputChannels = 0;
	long outputChannels = 0;
	if(m_pAsioDrv->getChannels(&inputChannels, &outputChannels) == ASE_OK)
	{
		for(long i = 0; i < outputChannels; ++i)
		{
			ASIOChannelInfo channelInfo;
			MemsetZero(channelInfo);
			channelInfo.channel = i;
			if(m_pAsioDrv->getChannelInfo(&channelInfo) == ASE_OK)
			{
				mpt::String::SetNullTerminator(channelInfo.name);
				caps.channelNames.push_back(mpt::ToWide(mpt::CharsetLocale, channelInfo.name));
			} else
			{
				caps.channelNames.push_back(mpt::ToWString(i));
			}
		}
	}

	return caps;
}


bool CASIODevice::OpenDriverSettings()
//------------------------------------
{
	TemporaryASIODeviceOpener opener(*this);
	if(!IsOpen())
	{
		return false;
	}
	return m_pAsioDrv->controlPanel() == ASE_OK;
}


#endif // NO_ASIO
