/*
 * StreamEncoder.cpp
 * -----------------
 * Purpose: Exporting streamed music files.
 * Notes  : none
 * Authors: Joern Heusipp
 *          OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */

#include "stdafx.h"

#include "StreamEncoder.h"

#include "Mptrack.h"
#include "TrackerSettings.h"

#include <ostream>


OPENMPT_NAMESPACE_BEGIN


StreamEncoderSettings &StreamEncoderSettings::Instance()
//------------------------------------------------------
{
	return TrackerSettings::Instance().ExportStreamEncoderSettings;
}


StreamEncoderSettings::StreamEncoderSettings(SettingsContainer &conf, const mpt::ustring &section)
//------------------------------------------------------------------------------------------------
	: FLACCompressionLevel(conf, section, "FLACCompressionLevel", 5)
	, MP3LameQuality(conf, section, "MP3LameQuality", 3)
	, MP3ACMFast(conf, section, "MP3ACMFast", false)
	, OpusComplexity(conf, section, "OpusComplexity", -1)
{
	return;
}


StreamWriterBase::StreamWriterBase(std::ostream &stream)
//------------------------------------------------------
	: f(stream)
	, fStart(f.tellp())
{
	return;
}

StreamWriterBase::~StreamWriterBase()
//-----------------------------------
{
	return;
}


void StreamWriterBase::WriteMetatags(const FileTags &tags)
//--------------------------------------------------------
{
	MPT_UNREFERENCED_PARAMETER(tags);
}


void StreamWriterBase::WriteInterleavedConverted(size_t frameCount, const char *data)
//-----------------------------------------------------------------------------------
{
	MPT_UNREFERENCED_PARAMETER(frameCount);
	MPT_UNREFERENCED_PARAMETER(data);
}


void StreamWriterBase::WriteCues(const std::vector<uint64> &cues)
//---------------------------------------------------------------
{
	MPT_UNREFERENCED_PARAMETER(cues);
}


void StreamWriterBase::WriteBuffer()
//----------------------------------
{
	if(!f)
	{
		return;
	}
	if(buf.empty())
	{
		return;
	}
	f.write(&buf[0], buf.size());
	buf.resize(0);
}


void EncoderFactoryBase::SetTraits(const Encoder::Traits &traits_)
//----------------------------------------------------------------
{
	traits = traits_;
}


std::string EncoderFactoryBase::DescribeQuality(float quality) const
//------------------------------------------------------------------
{
	return mpt::String::Print("VBR %1%%", static_cast<int>(quality * 100.0f));
}

std::string EncoderFactoryBase::DescribeBitrateVBR(int bitrate) const
//-------------------------------------------------------------------
{
	return mpt::String::Print("VBR %1 kbit", bitrate);
}

std::string EncoderFactoryBase::DescribeBitrateABR(int bitrate) const
//-------------------------------------------------------------------
{
	return mpt::String::Print("ABR %1 kbit", bitrate);
}

std::string EncoderFactoryBase::DescribeBitrateCBR(int bitrate) const
//-------------------------------------------------------------------
{
	return mpt::String::Print("CBR %1 kbit", bitrate);
}


OPENMPT_NAMESPACE_END
