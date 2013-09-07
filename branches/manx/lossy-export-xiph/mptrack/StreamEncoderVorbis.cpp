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
#include "StreamEncoderVorbis.h"

#include "Mptrack.h"

#include <vorbis/vorbisenc.h>


static Encoder::Traits BuildTraits()
{
	Encoder::Traits traits;
	traits.fileExtension = "ogg";
	traits.fileShortDescription = "Vorbis";
	traits.fileDescription = "Ogg Vorbis";
	traits.name = "Ogg Vorbis";
	traits.description += "Version: ";
	traits.description += vorbis_version_string() ? vorbis_version_string() : "unknown";
	traits.description += "\n";
	traits.canTags = true;
	traits.maxChannels = 4;
	traits.samplerates = std::vector<uint32>(vorbis_samplerates, vorbis_samplerates + CountOf(vorbis_samplerates));
	traits.modes = Encoder::ModeVBR | Encoder::ModeQuality;
	traits.bitrates = std::vector<int>(vorbis_bitrates, vorbis_bitrates + CountOf(vorbis_bitrates));
	traits.defaultMode = Encoder::ModeQuality;
	traits.defaultBitrate = 160;
	traits.defaultQuality = 0.5;
	return traits;
}


class VorbisStreamWriter : public StreamWriterBase
{
private:
  ogg_stream_state os;
  ogg_page         og;
  ogg_packet       op;
  vorbis_info      vi;
  vorbis_comment   vc;
  vorbis_dsp_state vd;
  vorbis_block     vb;
	bool inited;
	bool started;
	bool vorbis_cbr;
	int vorbis_channels;
	bool vorbis_tags;
private:
	void StartStream()
	{
		ASSERT(inited && !started);
		ogg_packet header;
		ogg_packet header_comm;
		ogg_packet header_code;
		vorbis_analysis_headerout(&vd, &vc, &header, &header_comm, &header_code);
		ogg_stream_packetin(&os, &header);
		ogg_stream_packetin(&os, &header_comm);
		ogg_stream_packetin(&os, &header_code);
		while(true)
		{
			int gotPage = ogg_stream_flush(&os, &og);
			if(!gotPage) break;
			WritePage();
		}
		started = true;
		ASSERT(inited && started);
	}
	void FinishStream()
	{
		if(inited)
		{
			if(!started)
			{
				StartStream();
			}
			ASSERT(inited && started);
			vorbis_analysis_wrote(&vd, 0);
			while(vorbis_analysis_blockout(&vd, &vb) == 1)
			{
				vorbis_analysis(&vb, NULL);
				vorbis_bitrate_addblock(&vb);
				while(vorbis_bitrate_flushpacket(&vd, &op))
				{
					ogg_stream_packetin(&os, &op);
					while(true)
					{
						int gotPage = ogg_stream_flush(&os, &og);
						if(!gotPage) break;
						WritePage();
						if(ogg_page_eos(&og))
							break;
					}
				}
			}
			ogg_stream_clear(&os);
			vorbis_block_clear(&vb);
			vorbis_dsp_clear(&vd);
			vorbis_comment_clear(&vc);
			vorbis_info_clear(&vi);
			started = false;
			inited = false;
		}
		ASSERT(!inited && !started);
	}
	void WritePage()
	{
		ASSERT(inited);
		buf.resize(og.header_len);
		std::memcpy(&buf[0], og.header, og.header_len);
		WriteBuffer();
		buf.resize(og.body_len);
		std::memcpy(&buf[0], og.body, og.body_len);
		WriteBuffer();
	}
	void AddCommentField(const std::string &field, const std::wstring &data)
	{
		if(!field.empty() && !data.empty())
		{
			vorbis_comment_add_tag(&vc, field.c_str(), mpt::String::Encode(data, mpt::CharsetUTF8).c_str());
		}
	}
public:
	VorbisStreamWriter(std::ostream &stream)
		: StreamWriterBase(stream)
	{
		inited = false;
		started = false;
		vorbis_channels = 0;
		vorbis_tags = true;
	}
	virtual ~VorbisStreamWriter()
	{
		FinishStream();
		ASSERT(!inited && !started);
	}
	virtual void SetFormat(int samplerate, int channels, const Encoder::Settings &settings)
	{

		FinishStream();

		ASSERT(!inited && !started);

		vorbis_channels = channels;
		vorbis_tags = settings.Tags;

		vorbis_info_init(&vi);
		vorbis_comment_init(&vc);

		if(settings.Mode == Encoder::ModeQuality)
		{
			vorbis_encode_init_vbr(&vi, vorbis_channels, samplerate, settings.Quality);
		} else {
			vorbis_encode_init(&vi, vorbis_channels, samplerate, -1, settings.Bitrate * 1000, -1);
		}

		vorbis_analysis_init(&vd, &vi);
		vorbis_block_init(&vd, &vb);
		ogg_stream_init(&os, std::rand());

		inited = true;

		ASSERT(inited && !started);
	}
	virtual void WriteMetatags(const FileTags &tags)
	{
		ASSERT(inited && !started);
		AddCommentField("ENCODER", tags.encoder);
		if(vorbis_tags)
		{
			AddCommentField("SOURCEMEDIA",L"tracked music file");
			AddCommentField("TITLE",       tags.title          );
			AddCommentField("ARTIST",      tags.artist         );
			AddCommentField("ALBUM",       tags.album          );
			AddCommentField("DATE",        tags.year           );
			AddCommentField("COMMENT",     tags.comments       );
			AddCommentField("GENRE",       tags.genre          );
			AddCommentField("CONTACT",     tags.url            );
			AddCommentField("BPM",         tags.bpm            ); // non-standard
			AddCommentField("TRACKNUMBER", tags.trackno        );
		}
	}
	virtual void WriteInterleaved(size_t count, const float *interleaved)
	{
		ASSERT(inited);
		if(!started)
		{
			StartStream();
		}
		ASSERT(inited && started);
		float **buffer = vorbis_analysis_buffer(&vd, count);
		for(size_t frame = 0; frame < count; ++frame)
		{
			for(int channel = 0; channel < vorbis_channels; ++channel)
			{
				buffer[channel][frame] = interleaved[frame*vorbis_channels+channel];
			}
		}
		vorbis_analysis_wrote(&vd, count);
    while(vorbis_analysis_blockout(&vd, &vb) == 1)
		{
      vorbis_analysis(&vb, NULL);
      vorbis_bitrate_addblock(&vb);
      while(vorbis_bitrate_flushpacket(&vd, &op))
			{
        ogg_stream_packetin(&os, &op);
				while(true)
				{
					int gotPage = ogg_stream_pageout(&os, &og);
					if(!gotPage) break;
					WritePage();
				}
      }
    }
	}
	virtual void Finalize()
	{
		ASSERT(inited);
		FinishStream();
		ASSERT(!inited && !started);
	}
};



VorbisEncoder::VorbisEncoder()
//----------------------------
{
	SetTraits(BuildTraits());
}


bool VorbisEncoder::IsAvailable() const
//-------------------------------------
{
	return true;
}


VorbisEncoder::~VorbisEncoder()
//-----------------------------
{
	return;
}


IAudioStreamEncoder *VorbisEncoder::ConstructStreamEncoder(std::ostream &file) const
//----------------------------------------------------------------------------------
{
	if(!IsAvailable())
	{
		return nullptr;
	}
	return new VorbisStreamWriter(file);
}


std::string VorbisEncoder::DescribeQuality(float quality) const
//-------------------------------------------------------------
{
	return mpt::String::Format("Q%3.1f", quality * 10.0f);
}

