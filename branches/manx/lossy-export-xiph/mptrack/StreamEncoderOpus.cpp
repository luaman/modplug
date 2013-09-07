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
#include "StreamEncoderOpus.h"

#ifdef MODPLUG_TRACKER
#include "../mptrack/Mainfrm.h"
#include "../mptrack/Mptrack.h"
#include "../mptrack/Reporting.h"
#endif //MODPLUG_TRACKER

#include <deque>

#include <opus.h>
#include <opus_multistream.h>

#if MPT_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable: 4244)
#endif // MPT_COMPILER_MSVC

#include <opus-tools/src/opus_header.h>
#include <opus-tools/src/opus_header.c>

#if MPT_COMPILER_MSVC
#pragma warning(pop)
#endif // MPT_COMPILER_MSVC


static Encoder::Traits BuildTraits()
{
	Encoder::Traits traits;
	traits.fileExtension = "opus";
	traits.fileShortDescription = "Opus";
	traits.fileDescription = "Opus";
	traits.name = "Opus";
	traits.description += "Version: ";
	traits.description += opus_get_version_string() ? opus_get_version_string() : "";
	traits.description += "\n";
	traits.canTags = true;
	traits.maxChannels = 4;
	traits.samplerates = std::vector<uint32>(opus_samplerates, opus_samplerates + CountOf(opus_samplerates));
	traits.modes = Encoder::ModeCBR | Encoder::ModeVBR;
	traits.bitrates = std::vector<int>(opus_bitrates, opus_bitrates + CountOf(opus_bitrates));
	traits.defaultMode = Encoder::ModeVBR;
	traits.defaultBitrate = 128;
	return traits;
}


class OpusStreamWriter : public StreamWriterBase
{
private:
  ogg_stream_state os;
  ogg_page         og;
  ogg_packet       op;
	OpusMSEncoder*   st;
	bool inited;
	bool started;
	int opus_bitrate;
	int opus_samplerate;
	int opus_channels;
	bool opus_tags;

	std::vector<std::string> opus_comments;

	int opus_extrasamples;

	ogg_int64_t packetno;
	ogg_int64_t last_granulepos;
	ogg_int64_t enc_granulepos;
	ogg_int64_t original_samples;

	std::deque<float> opus_sampleBuf;
	std::vector<float> opus_frameBuf;
	std::vector<unsigned char> opus_frameData;
private:
	static void PushUint32LE(std::vector<unsigned char> &buf, uint32 val)
	{
		buf.push_back((val>> 0)&0xff);
		buf.push_back((val>> 8)&0xff);
		buf.push_back((val>>16)&0xff);
		buf.push_back((val>>24)&0xff);
	}
	void StartStream()
	{
		ASSERT(inited && !started);

		std::vector<unsigned char> opus_comments_buf;
		for(const char *it = "OpusTags"; *it; ++it)
		{
			opus_comments_buf.push_back(*it);
		}
		const char *version_string = opus_get_version_string();
		if(version_string)
		{
			PushUint32LE(opus_comments_buf, std::strlen(version_string));
			for(/*nothing*/; *version_string; ++version_string)
			{
				opus_comments_buf.push_back(*version_string);
			}
		} else
		{
			PushUint32LE(opus_comments_buf, 0);
		}
		PushUint32LE(opus_comments_buf, opus_comments.size());
		for(std::vector<std::string>::const_iterator it = opus_comments.begin(); it != opus_comments.end(); ++it)
		{
			PushUint32LE(opus_comments_buf, it->length());
			for(std::size_t i = 0; i < it->length(); ++i)
			{
				opus_comments_buf.push_back((*it)[i]);
			}
		}
		op.packet = &opus_comments_buf[0];
		op.bytes = opus_comments_buf.size();
		op.b_o_s = 0;
		op.e_o_s = 0;
		op.granulepos = 0;
		op.packetno = 1;
		ogg_stream_packetin(&os, &op);
		while(ogg_stream_flush(&os, &og))
		{
			WritePage();
		}
		packetno = 2;

		last_granulepos = 0;
		enc_granulepos = 0;
		original_samples = 0;
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

			std::vector<float> extraBuf(opus_extrasamples * opus_channels);
			WriteInterleaved(opus_extrasamples, &extraBuf[0]);

			int cur_frame_size = 960 * opus_samplerate / 48000;
			int last_frame_size = (opus_sampleBuf.size() / opus_channels) * opus_samplerate / 48000;

			opus_frameBuf.resize(opus_channels * cur_frame_size);
			for(size_t sample = 0; sample < opus_frameBuf.size(); ++sample)
			{
				opus_frameBuf[sample] = 0.0f;
			}

			for(size_t sample = 0; sample < opus_sampleBuf.size(); ++sample)
			{
				opus_frameBuf[sample] = opus_sampleBuf[sample];
			}
			opus_sampleBuf.clear();

			opus_frameData.resize(65536);
			opus_frameData.resize(opus_multistream_encode_float(st, &opus_frameBuf[0], cur_frame_size, &opus_frameData[0], opus_frameData.size()));
			enc_granulepos += last_frame_size * 48000 / opus_samplerate;

			op.b_o_s = 0;
			op.e_o_s = 1;
			op.granulepos = enc_granulepos;
			op.packetno = packetno;
			op.packet = &opus_frameData[0];
			op.bytes = opus_frameData.size();
			ogg_stream_packetin(&os, &op);

			packetno++;

			while(ogg_stream_flush(&os, &og))
			{
				WritePage();
			}

			ogg_stream_clear(&os);

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
			opus_comments.push_back(field + "=" + mpt::String::Encode(data, mpt::CharsetUTF8));
		}
	}
public:
	OpusStreamWriter(std::ostream &stream)
		: StreamWriterBase(stream)
	{
		inited = false;
		started = false;
		opus_channels = 0;
		opus_tags = true;
		opus_comments.clear();
		opus_extrasamples = 0;
	}
	virtual ~OpusStreamWriter()
	{
		FinishStream();
		ASSERT(!inited && !started);
	}
	virtual void SetFormat(int samplerate, int channels, const Encoder::Settings &settings)
	{

		FinishStream();

		ASSERT(!inited && !started);

		opus_bitrate = settings.Bitrate * 1000;
		opus_samplerate = samplerate;
		opus_channels = channels;
		opus_tags = settings.Tags;

		int opus_error = 0;

		int num_streams = 0;
		int num_coupled = 0;
		unsigned char mapping[4] = { 0, 0, 0, 0 };

		st = opus_multistream_surround_encoder_create(samplerate, opus_channels, opus_channels > 2 ? 1 : 0, &num_streams, &num_coupled, mapping, OPUS_APPLICATION_AUDIO, &opus_error);

		opus_int32 ctl_lookahead = 0;
		opus_multistream_encoder_ctl(st, OPUS_GET_LOOKAHEAD(&ctl_lookahead));

		opus_int32 ctl_bitrate = opus_bitrate;
		opus_multistream_encoder_ctl(st, OPUS_SET_BITRATE(ctl_bitrate));

		if(settings.Mode == Encoder::ModeCBR)
		{
			opus_int32 ctl_vbr = 0;
			opus_multistream_encoder_ctl(st, OPUS_SET_VBR(ctl_vbr));
		} else
		{
			opus_int32 ctl_vbr = 1;
			opus_multistream_encoder_ctl(st, OPUS_SET_VBR(ctl_vbr));
			opus_int32 ctl_vbrcontraint = 0;
			opus_multistream_encoder_ctl(st, OPUS_SET_VBR_CONSTRAINT(ctl_vbrcontraint));
		}

#ifdef MODPLUG_TRACKER
		opus_int32 complexity = 0;
		opus_multistream_encoder_ctl(st, OPUS_GET_COMPLEXITY(&complexity));
		complexity = CMainFrame::GetPrivateProfileLong("Export", "OpusComplexity", complexity, theApp.GetConfigFileName());
		opus_multistream_encoder_ctl(st, OPUS_SET_COMPLEXITY(complexity));
#endif // MODPLUG_TRACKER

		OpusHeader header;
		MemsetZero(header);
		header.version = 0;
		header.channels = opus_channels;
		header.preskip = ctl_lookahead * (48000/samplerate);
		header.input_sample_rate = samplerate;
		header.gain = 0;
		header.channel_mapping = (opus_channels > 2) ? 1 : 0;
		if(header.channel_mapping == 0)
		{
			header.nb_streams = 0;
			header.nb_coupled = 0;
			MemsetZero(header.stream_map);
		} else if(header.channel_mapping == 1)
		{
			header.nb_streams = num_streams;
			header.nb_coupled = num_coupled;
			MemsetZero(header.stream_map);
			for(int channel=0; channel<opus_channels; ++channel)
			{
				header.stream_map[channel] = mapping[channel];
			}
		}

		opus_extrasamples = ctl_lookahead;

		opus_comments.clear();

		ogg_stream_init(&os, std::rand());

		inited = true;

		unsigned char header_data[1024];
		int packet_size = opus_header_to_packet(&header, header_data, 1024);
		op.packet = header_data;
		op.bytes = packet_size;
		op.b_o_s = 1;
		op.e_o_s = 0;
		op.granulepos = 0;
		op.packetno = 0;
		ogg_stream_packetin(&os, &op);

		while(ogg_stream_flush(&os, &og))
		{
			WritePage();
		}

		ASSERT(inited && !started);
	}
	virtual void WriteMetatags(const FileTags &tags)
	{
		ASSERT(inited && !started);
		AddCommentField("ENCODER", tags.encoder);
		if(opus_tags)
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
		original_samples += count;
		for(size_t frame = 0; frame < count; ++frame)
		{
			for(int channel = 0; channel < opus_channels; ++channel)
			{
				opus_sampleBuf.push_back(interleaved[frame*opus_channels+channel]);
			}
		}
		int cur_frame_size = 960 * opus_samplerate / 48000;
		opus_frameBuf.resize(opus_channels * cur_frame_size);
		while(opus_sampleBuf.size() > opus_frameBuf.size())
		{
			for(size_t sample = 0; sample < opus_frameBuf.size(); ++sample)
			{
				opus_frameBuf[sample] = opus_sampleBuf.front();
				opus_sampleBuf.pop_front();
			}

			opus_frameData.resize(65536);
			opus_frameData.resize(opus_multistream_encode_float(st, &opus_frameBuf[0], cur_frame_size, &opus_frameData[0], opus_frameData.size()));
			enc_granulepos += cur_frame_size * 48000 / opus_samplerate;

			op.b_o_s = 0;
			op.e_o_s = 0;
			op.granulepos = enc_granulepos;
			op.packetno = packetno;
			op.packet = &opus_frameData[0];
			op.bytes = opus_frameData.size();
			ogg_stream_packetin(&os, &op);

			packetno++;

			while(ogg_stream_pageout(&os, &og))
			{
				WritePage();
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



OggOpusEncoder::OggOpusEncoder()
//------------------------------
{
	SetTraits(BuildTraits());
}


bool OggOpusEncoder::IsAvailable() const
//--------------------------------------
{
	return true;
}


OggOpusEncoder::~OggOpusEncoder()
//-------------------------------
{
	return;
}


IAudioStreamEncoder *OggOpusEncoder::ConstructStreamEncoder(std::ostream &file) const
//-----------------------------------------------------------------------------------
{
	if(!IsAvailable())
	{
		return nullptr;
	}
	return new OpusStreamWriter(file);
}
