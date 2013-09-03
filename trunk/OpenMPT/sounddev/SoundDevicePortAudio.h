/*
 * SoundDevicePortAudio.h
 * ----------------------
 * Purpose: PortAudio sound device driver class.
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#pragma once

#include "SoundDevices.h"

#ifndef NO_PORTAUDIO
#include "portaudio/include/portaudio.h"
#include "portaudio/include/pa_win_wasapi.h"
#endif

////////////////////////////////////////////////////////////////////////////////////
//
// Protaudio device
//

#ifndef NO_PORTAUDIO

//=========================================
class CPortaudioDevice: public ISoundDevice
//=========================================
{
protected:
	PaHostApiIndex m_HostApi;
	PaStreamParameters m_StreamParameters;
	PaWasapiStreamInfo m_WasapiStreamInfo;
	PaStream * m_Stream;
	void * m_CurrentFrameBuffer;
	unsigned long m_CurrentFrameCount;

	float m_CurrentRealLatencyMS;

public:

public:
	CPortaudioDevice(PaHostApiIndex hostapi);
	~CPortaudioDevice();

public:
	UINT GetDeviceType() { return HostApiToSndDevType(m_HostApi); }
	bool InternalOpen(UINT nDevice);
	bool InternalClose();
	void FillAudioBuffer();
	void InternalReset();
	void InternalStart();
	void InternalStop();
	bool IsOpen() const { return m_Stream ? true : false; }
	UINT GetNumBuffers() { return 1; }
	float GetCurrentRealLatencyMS();
	bool HasGetStreamPosition() const { return false; }
	int64 GetStreamPositionSamples() const;
	bool CanSampleRate(UINT nDevice, std::vector<uint32> &samplerates, std::vector<bool> &result);

	int StreamCallback(
		const void *input, void *output,
		unsigned long frameCount,
		const PaStreamCallbackTimeInfo* timeInfo,
		PaStreamCallbackFlags statusFlags
		);

public:
	static int StreamCallbackWrapper(
		const void *input, void *output,
		unsigned long frameCount,
		const PaStreamCallbackTimeInfo* timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userData
		);

	static PaDeviceIndex HostApiOutputIndexToGlobalDeviceIndex(int hostapioutputdeviceindex, PaHostApiIndex hostapi);
	static int HostApiToSndDevType(PaHostApiIndex hostapi);
	static PaHostApiIndex SndDevTypeToHostApi(int snddevtype);

	static BOOL EnumerateDevices(UINT nIndex, LPSTR pszDescription, UINT cbSize, PaHostApiIndex hostapi);

};

void SndDevPortaudioInitialize();
void SndDevPortaudioUnnitialize();

bool SndDevPortaudioIsInitialized();

#endif // NO_PORTAUDIO
