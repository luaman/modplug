/*
 * ModConvert.cpp
 * --------------
 * Purpose: Converting between various module formats.
 * Notes  : Incomplete list of MPTm-only features and extensions in the old formats:
 *          Features only available for MPTm:
 *           - User definable tunings.
 *           - Extended pattern range
 *           - Extended sequence
 *           - Multiple sequences ("songs")
 *           - Pattern-specific time signatures
 *           - Pattern effects :xy, S7D, S7E
 *           - Long instrument envelopes
 *           - Envelope release node (this was previously also usable in the IT format, but is now deprecated in that format)
 *
 *          Extended features in IT/XM/S3M (not all listed below are available in all of those formats):
 *           - Plugins
 *           - Extended ranges for
 *              - Sample count
 *              - Instrument count
 *              - Pattern count
 *              - Sequence size
 *              - Row count
 *              - Channel count
 *              - Tempo limits
 *           - Extended sample/instrument properties.
 *           - MIDI mapping directives
 *           - Version info
 *           - Channel names
 *           - Pattern names
 *           - Alternative tempo modes
 *           - For more info, see e.g. SaveExtendedSongProperties(), SaveExtendedInstrumentProperties()
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "Stdafx.h"
#include "Moddoc.h"
#include "Mainfrm.h"
#include "InputHandler.h"
#include "modsmp_ctrl.h"
#include "ModConvert.h"


OPENMPT_NAMESPACE_BEGIN


#define CHANGEMODTYPE_WARNING(x)	warnings.set(x);
#define CHANGEMODTYPE_CHECK(x, s)	if(warnings[x]) AddToLog(_T(s));


// Trim envelopes and remove release nodes.
void UpdateEnvelopes(InstrumentEnvelope &mptEnv, CSoundFile &sndFile, std::bitset<wNumWarnings> &warnings)
//--------------------------------------------------------------------------------------------------------
{
	// shorten instrument envelope if necessary (for mod conversion)
	const uint8 envMax = sndFile.GetModSpecifications().envelopePointsMax;

#define TRIMENV(envLen) if(envLen > envMax) { envLen = envMax; CHANGEMODTYPE_WARNING(wTrimmedEnvelopes); }

	TRIMENV(mptEnv.nNodes);
	TRIMENV(mptEnv.nLoopStart);
	TRIMENV(mptEnv.nLoopEnd);
	TRIMENV(mptEnv.nSustainStart);
	TRIMENV(mptEnv.nSustainEnd);
	if(mptEnv.nReleaseNode != ENV_RELEASE_NODE_UNSET)
	{
		if(sndFile.GetModSpecifications().hasReleaseNode)
		{
			TRIMENV(mptEnv.nReleaseNode);
		} else
		{
			mptEnv.nReleaseNode = ENV_RELEASE_NODE_UNSET;
			CHANGEMODTYPE_WARNING(wReleaseNode);
		}
	}

	#undef TRIMENV
}


bool CModDoc::ChangeModType(MODTYPE nNewType)
//-------------------------------------------
{
	std::bitset<wNumWarnings> warnings;
	warnings.reset();
	PATTERNINDEX nResizedPatterns = 0;

	const MODTYPE nOldType = m_SndFile.GetType();
	
	if(nNewType == nOldType && nNewType == MOD_TYPE_IT)
	{
		// Even if m_nType doesn't change, we might need to change extension in itp<->it case.
		// This is because ITP is a HACK and doesn't genuinely change m_nType,
		// but uses flags instead.
		ChangeFileExtension(nNewType);
		return true;
	}

	if(nNewType == nOldType)
		return true;

	const bool oldTypeIsXM = (nOldType == MOD_TYPE_XM),
		oldTypeIsS3M = (nOldType == MOD_TYPE_S3M), oldTypeIsIT = (nOldType == MOD_TYPE_IT),
		oldTypeIsMPT = (nOldType == MOD_TYPE_MPT),
		oldTypeIsS3M_IT_MPT = (oldTypeIsS3M || oldTypeIsIT || oldTypeIsMPT),
		oldTypeIsIT_MPT = (oldTypeIsIT || oldTypeIsMPT);

	const bool newTypeIsMOD = (nNewType == MOD_TYPE_MOD), newTypeIsXM =  (nNewType == MOD_TYPE_XM), 
		newTypeIsS3M = (nNewType == MOD_TYPE_S3M), newTypeIsIT = (nNewType == MOD_TYPE_IT),
		newTypeIsMPT = (nNewType == MOD_TYPE_MPT), newTypeIsMOD_XM = (newTypeIsMOD || newTypeIsXM), 
		newTypeIsIT_MPT = (newTypeIsIT || newTypeIsMPT);

	const CModSpecifications& specs = m_SndFile.GetModSpecifications(nNewType);

	// Check if conversion to 64 rows is necessary
	for(PATTERNINDEX pat = 0; pat < m_SndFile.Patterns.Size(); pat++)
	{
		if(m_SndFile.Patterns.IsValidPat(pat) && (m_SndFile.Patterns[pat].GetNumRows() != 64))
			nResizedPatterns++;
	}

	if((m_SndFile.GetNumInstruments() || nResizedPatterns) && (nNewType & (MOD_TYPE_MOD|MOD_TYPE_S3M)))
	{
		if(Reporting::Confirm(
			"This operation will convert all instruments to samples,\n"
			"and resize all patterns to 64 rows.\n"
			"Do you want to continue?", "Warning") != cnfYes) return false;
		BeginWaitCursor();
		CriticalSection cs;

		// Converting instruments to samples
		if(m_SndFile.GetNumInstruments())
		{
			ConvertInstrumentsToSamples();
			CHANGEMODTYPE_WARNING(wInstrumentsToSamples);
		}

		// Resizing all patterns to 64 rows
		for(PATTERNINDEX pat = 0; pat < m_SndFile.Patterns.Size(); pat++) if(m_SndFile.Patterns.IsValidPat(pat) && m_SndFile.Patterns[pat].GetNumRows() != 64)
		{
			ROWINDEX origRows = m_SndFile.Patterns[pat].GetNumRows();
			m_SndFile.Patterns[pat].Resize(64);

			if(origRows < 64)
			{
				// Try to save short patterns by inserting a pattern break.
				m_SndFile.Patterns[pat].WriteEffect(EffectWriter(CMD_PATTERNBREAK, 0).Row(origRows - 1).Retry(EffectWriter::rmTryNextRow));
			}

			CHANGEMODTYPE_WARNING(wResizedPatterns);
		}

		// Removing all instrument headers from channels
		for(CHANNELINDEX nChn = 0; nChn < MAX_CHANNELS; nChn++)
		{
			m_SndFile.m_PlayState.Chn[nChn].pModInstrument = nullptr;
		}

		for(INSTRUMENTINDEX nIns = 0; nIns <= m_SndFile.GetNumInstruments(); nIns++) if (m_SndFile.Instruments[nIns])
		{
			delete m_SndFile.Instruments[nIns];
			m_SndFile.Instruments[nIns] = nullptr;
		}
		m_SndFile.m_nInstruments = 0;

		EndWaitCursor();
	} //End if (((m_SndFile.m_nInstruments) || (b64)) && (nNewType & (MOD_TYPE_MOD|MOD_TYPE_S3M)))
	BeginWaitCursor();


	/////////////////////////////
	// Converting pattern data

	// When converting to MOD, get the new sample transpose setting right here so that we can compensate notes in the pattern.
	if(newTypeIsMOD && !oldTypeIsXM)
	{
		for(SAMPLEINDEX smp = 1; smp <= m_SndFile.GetNumSamples(); smp++)
		{
			m_SndFile.GetSample(smp).FrequencyToTranspose();
		}
	}

	for(PATTERNINDEX pat = 0; pat < m_SndFile.Patterns.Size(); pat++) if (m_SndFile.Patterns[pat])
	{
		ModCommand *m = m_SndFile.Patterns[pat];

		// This is used for -> MOD/XM conversion
		std::vector<std::vector<ModCommand::PARAM> > effMemory(GetNumChannels());
		std::vector<ModCommand::VOL> volMemory(GetNumChannels(), 0);
		std::vector<ModCommand::INSTR> instrMemory(GetNumChannels(), 0);
		for(size_t i = 0; i < GetNumChannels(); i++)
		{
			effMemory[i].resize(MAX_EFFECTS, 0);
		}

		bool addBreak = false;	// When converting to XM, avoid the E60 bug.
		CHANNELINDEX chn = 0;
		ROWINDEX row = 0;

		for(size_t len = m_SndFile.Patterns[pat].GetNumRows() * m_SndFile.GetNumChannels(); len; m++, len--, chn++)
		{
			if(chn >= GetNumChannels())
			{
				chn = 0;
				row++;
			}

			ModCommand::INSTR instr = m->instr;
			if(m->instr) instrMemory[chn] = instr;
			else instr = instrMemory[chn];

			// Deal with volume column slide memory (it's not shared with the effect column)
			if(oldTypeIsIT_MPT && (newTypeIsMOD_XM || newTypeIsS3M))
			{
				switch(m->volcmd)
				{
				case VOLCMD_VOLSLIDEUP:
				case VOLCMD_VOLSLIDEDOWN:
				case VOLCMD_FINEVOLUP:
				case VOLCMD_FINEVOLDOWN:
					if(m->vol == 0)
						m->vol = volMemory[chn];
					else
						volMemory[chn] = m->vol;
					break;
				}
			}

			// Deal with effect memory for MOD/XM arpeggio
			if(oldTypeIsS3M_IT_MPT && newTypeIsMOD_XM)
			{
				switch(m->command)
				{
				case CMD_ARPEGGIO:
				case CMD_S3MCMDEX:
				case CMD_MODCMDEX:
					// No effect memory in XM / MOD
					if(m->param == 0)
						m->param = effMemory[chn][m->command];
					else
						effMemory[chn][m->command] = m->param;
					break;
				}
			}

			// Adjust effect memory for MOD files
			if(newTypeIsMOD)
			{
				switch(m->command)
				{
				case CMD_PORTAMENTOUP:
				case CMD_PORTAMENTODOWN:
				case CMD_TONEPORTAVOL:
				case CMD_VIBRATOVOL:
				case CMD_VOLUMESLIDE:
					// ProTracker doesn't have effect memory for these commands, so let's try to fix them
					if(m->param == 0)
						m->param = effMemory[chn][m->command];
					else
						effMemory[chn][m->command] = m->param;
					break;

				}

				// Compensate for loss of transpose information
				if(m->IsNote() && instr && instr <= GetNumSamples())
				{
					const int newNote = m->note + m_SndFile.GetSample(instr).RelativeTone;
					m->note = static_cast<uint8>(Clamp(newNote, specs.noteMin, specs.noteMax));
				}
			}

			m->Convert(nOldType, nNewType);

			// When converting to XM, avoid the E60 bug.
			if(newTypeIsXM)
			{
				switch(m->command)
				{
				case CMD_MODCMDEX:
					if(m->param == 0x60 && row > 0)
					{
						addBreak = true;
					}
					break;
				case CMD_POSITIONJUMP:
				case CMD_PATTERNBREAK:
					addBreak = false;
					break;
				}
			}

			// Fix Row Delay commands when converting between MOD/XM and S3M/IT.
			// FT2 only considers the rightmost command, ST3/IT only the leftmost...
			if((nOldType & (MOD_TYPE_S3M | MOD_TYPE_IT | MOD_TYPE_MPT)) && (nNewType & (MOD_TYPE_MOD | MOD_TYPE_XM))
				&& m->command == CMD_MODCMDEX && (m->param & 0xF0) == 0xE0)
			{
				if(oldTypeIsIT_MPT || m->param != 0xE0)
				{
					// If the leftmost row delay command is SE0, ST3 ignores it, IT doesn't.

					// Delete all commands right of the first command
					ModCommand *p = m + 1;
					for(CHANNELINDEX c = chn + 1; c < m_SndFile.GetNumChannels(); c++, p++)
					{
						if(p->command == CMD_S3MCMDEX && (p->param & 0xF0) == 0xE0)
						{
							p->command = CMD_NONE;
						}
					}
				}
			} else if((nOldType & (MOD_TYPE_MOD | MOD_TYPE_XM)) && (nNewType & (MOD_TYPE_S3M | MOD_TYPE_IT | MOD_TYPE_MPT))
				&& m->command == CMD_S3MCMDEX && (m->param & 0xF0) == 0xE0)
			{
				// Delete all commands left of the last command
				ModCommand *p = m - 1;
				for(CHANNELINDEX c = 0; c < chn; c++, p--)
				{
					if(p->command == CMD_S3MCMDEX && (p->param & 0xF0) == 0xE0)
					{
						p->command = CMD_NONE;
					}
				}
			}

		}
		if(addBreak)
		{
			m_SndFile.Patterns[pat].WriteEffect(EffectWriter(CMD_PATTERNBREAK, 0).Row(m_SndFile.Patterns[pat].GetNumRows() - 1));
		}
	}

	////////////////////////////////////////////////
	// Converting instrument / sample / etc. data


	// Do some sample conversion
	for(SAMPLEINDEX smp = 1; smp <= m_SndFile.GetNumSamples(); smp++)
	{
		ModSample &sample = m_SndFile.GetSample(smp);
		GetSampleUndo().PrepareUndo(smp, sundo_none, "Song Conversion");

		// Too many samples? Only 31 samples allowed in MOD format...
		if(newTypeIsMOD && smp > 31 && sample.nLength > 0)
		{
			CHANGEMODTYPE_WARNING(wMOD31Samples);
		}

		// No Bidi and Autovibrato for MOD/S3M
		if(newTypeIsMOD || newTypeIsS3M)
		{
			// Bidi loops
			if((sample.uFlags[CHN_PINGPONGLOOP]) != 0)
			{
				CHANGEMODTYPE_WARNING(wSampleBidiLoops);
			}

			// Autovibrato
			if((sample.nVibDepth | sample.nVibRate | sample.nVibSweep) != 0)
			{
				CHANGEMODTYPE_WARNING(wSampleAutoVibrato);
			}
		}

		// No sustain loops for MOD/S3M/XM
		if(newTypeIsMOD_XM || newTypeIsS3M)
		{
			// Sustain loops - convert to normal loops
			if((sample.uFlags[CHN_SUSTAINLOOP]) != 0)
			{
				CHANGEMODTYPE_WARNING(wSampleSustainLoops);
			}
		}

		// TODO: Pattern notes could be transposed based on the previous relative tone?
		if(newTypeIsMOD && sample.RelativeTone != 0)
		{
			CHANGEMODTYPE_WARNING(wMODSampleFrequency);
		}

		sample.Convert(nOldType, nNewType);
	}

	for(INSTRUMENTINDEX ins = 1; ins <= m_SndFile.GetNumInstruments(); ins++)
	{
		ModInstrument *pIns = m_SndFile.Instruments[ins];
		if(pIns == nullptr)
		{
			continue;
		}

		// Convert IT/MPT to XM (fix instruments)
		if(newTypeIsXM)
		{
			for(size_t i = 0; i < CountOf(pIns->NoteMap); i++)
			{
				if (pIns->NoteMap[i] && pIns->NoteMap[i] != static_cast<uint8>(i + 1))
				{
					CHANGEMODTYPE_WARNING(wBrokenNoteMap);
					break;
				}
			}
			// Convert sustain loops to sustain "points"
			if(pIns->VolEnv.nSustainStart != pIns->VolEnv.nSustainEnd)
			{
				CHANGEMODTYPE_WARNING(wInstrumentSustainLoops);
			}
			if(pIns->PanEnv.nSustainStart != pIns->PanEnv.nSustainEnd)
			{
				CHANGEMODTYPE_WARNING(wInstrumentSustainLoops);
			}
		}


		// Convert MPT to anything - remove instrument tunings, Pitch/Tempo Lock, filter variation
		if(oldTypeIsMPT)
		{
			if(pIns->pTuning != nullptr)
			{
				CHANGEMODTYPE_WARNING(wInstrumentTuning);
			}

			if(pIns->wPitchToTempoLock != 0)
			{
				CHANGEMODTYPE_WARNING(wPitchToTempoLock);
			}

			if((pIns->nCutSwing | pIns->nResSwing) != 0)
			{
				CHANGEMODTYPE_WARNING(wFilterVariation);
			}
		}

		pIns->Convert(nOldType, nNewType);
	}

	if(newTypeIsMOD)
	{
		// Not supported in MOD format
		if(m_SndFile.m_nDefaultSpeed != 6)
		{
			if(m_SndFile.Order.size() > 0)
			{
				m_SndFile.Patterns[m_SndFile.Order[0]].WriteEffect(EffectWriter(CMD_SPEED, ModCommand::PARAM(m_SndFile.m_nDefaultSpeed)).Retry(EffectWriter::rmTryNextRow));
			}
			m_SndFile.m_nDefaultSpeed = 6;
		}
		if(m_SndFile.m_nDefaultTempo != 125)
		{
			if(m_SndFile.Order.size() > 0)
			{
				m_SndFile.Patterns[m_SndFile.Order[0]].WriteEffect(EffectWriter(CMD_TEMPO, ModCommand::PARAM(m_SndFile.m_nDefaultTempo)).Retry(EffectWriter::rmTryNextRow));
			}
			m_SndFile.m_nDefaultTempo = 125;
		}
		m_SndFile.m_nDefaultGlobalVolume = MAX_GLOBAL_VOLUME;
		m_SndFile.m_nSamplePreAmp = 48;
		m_SndFile.m_nVSTiVolume = 48;
		CHANGEMODTYPE_WARNING(wMODGlobalVars);
	}

	// Is the "restart position" value allowed in this format?
	if(m_SndFile.m_nRestartPos > 0 && !CSoundFile::GetModSpecifications(nNewType).hasRestartPos)
	{
		// Try to fix it by placing a pattern jump command in the pattern.
		if(!RestartPosToPattern())
		{
			// Couldn't fix it! :(
			CHANGEMODTYPE_WARNING(wRestartPos);
		}
	}

	// Fix channel settings (pan/vol)
	for(CHANNELINDEX nChn = 0; nChn < GetNumChannels(); nChn++)
	{
		if(newTypeIsMOD_XM || newTypeIsS3M)
		{
			if(m_SndFile.ChnSettings[nChn].nVolume != 64 || m_SndFile.ChnSettings[nChn].dwFlags[CHN_SURROUND])
			{
				m_SndFile.ChnSettings[nChn].nVolume = 64;
				m_SndFile.ChnSettings[nChn].dwFlags.reset(CHN_SURROUND);
				CHANGEMODTYPE_WARNING(wChannelVolSurround);
			}
		}
		if(newTypeIsXM)
		{
			if(m_SndFile.ChnSettings[nChn].nPan != 128)
			{
				m_SndFile.ChnSettings[nChn].nPan = 128;
				CHANGEMODTYPE_WARNING(wChannelPanning);
			}
		}
	}

	// Check for patterns with custom time signatures (fixing will be applied in the pattern container)
	if(!CSoundFile::GetModSpecifications(nNewType).hasPatternSignatures)
	{
		for(PATTERNINDEX nPat = 0; nPat < m_SndFile.Patterns.Size(); nPat++)
		{
			if(m_SndFile.Patterns[nPat].GetOverrideSignature())
			{
				CHANGEMODTYPE_WARNING(wPatternSignatures);
				break;
			}
		}
	}

	// Check whether the new format supports embedding the edit history in the file.
	if(oldTypeIsIT_MPT && !newTypeIsIT_MPT && GetrSoundFile().GetFileHistory().size() > 0)
	{
		CHANGEMODTYPE_WARNING(wEditHistory);
	}

	if((nOldType & MOD_TYPE_XM) && m_SndFile.GetModFlag(MSF_VOLRAMP))
	{
		CHANGEMODTYPE_WARNING(wVolRamp);
	}

	CriticalSection cs;
	m_SndFile.ChangeModTypeTo(nNewType);

	// In case we need to update IT bidi loop handling pre-computation or loops got changed...
	m_SndFile.PrecomputeSampleLoops(false);

	// Song flags
	if(!(CSoundFile::GetModSpecifications(nNewType).songFlags & SONG_LINEARSLIDES) && m_SndFile.m_SongFlags[SONG_LINEARSLIDES])
	{
		CHANGEMODTYPE_WARNING(wLinearSlides);
	}
	if(oldTypeIsXM && newTypeIsIT_MPT) m_SndFile.m_SongFlags.set(SONG_ITCOMPATGXX);
	m_SndFile.m_SongFlags &= (CSoundFile::GetModSpecifications(nNewType).songFlags | SONG_PLAY_FLAGS);

	// Adjust mix levels
	if(newTypeIsMOD || newTypeIsS3M)
	{
		m_SndFile.SetMixLevels(mixLevels_compatible);
	}
	if(oldTypeIsMPT && m_SndFile.GetMixLevels() != mixLevels_compatible && m_SndFile.GetMixLevels() != mixLevels_compatible_FT2)
	{
		CHANGEMODTYPE_WARNING(wMixmode);
	}

	// Automatically enable compatible mode when converting from MOD and S3M, since it's automatically enabled in those formats.
	if((nOldType & (MOD_TYPE_MOD | MOD_TYPE_S3M)) && (nNewType & (MOD_TYPE_XM | MOD_TYPE_IT)))
	{
		m_SndFile.SetModFlag(MSF_COMPATIBLE_PLAY, true);
	}
	if((nNewType & (MOD_TYPE_XM | MOD_TYPE_IT)) && !m_SndFile.GetModFlag(MSF_COMPATIBLE_PLAY))
	{
		CHANGEMODTYPE_WARNING(wCompatibilityMode);
	}

	cs.Leave();
	ChangeFileExtension(nNewType);

	// Check mod specifications
	Limit(m_SndFile.m_nDefaultTempo, specs.tempoMin, specs.tempoMax);
	Limit(m_SndFile.m_nDefaultSpeed, specs.speedMin, specs.speedMax);

	for(INSTRUMENTINDEX i = 1; i <= m_SndFile.GetNumInstruments(); i++) if(m_SndFile.Instruments[i] != nullptr)
	{
		UpdateEnvelopes(m_SndFile.Instruments[i]->VolEnv, m_SndFile, warnings);
		UpdateEnvelopes(m_SndFile.Instruments[i]->PanEnv, m_SndFile, warnings);
		UpdateEnvelopes(m_SndFile.Instruments[i]->PitchEnv, m_SndFile, warnings);
	}

	// XM requires instruments, so we create them right away.
	if(newTypeIsXM && GetNumInstruments() == 0)
	{
		ConvertSamplesToInstruments();
	}

	// XM has no global volume
	if(newTypeIsXM && m_SndFile.m_nDefaultGlobalVolume != MAX_GLOBAL_VOLUME)
	{
		if(!GlobalVolumeToPattern())
		{
			CHANGEMODTYPE_WARNING(wGlobalVolumeNotSupported);
		}
	}
		
	// Pattern warnings
	CHAR s[64];
	wsprintf(s, "%d patterns have been resized to 64 rows", nResizedPatterns);
	CHANGEMODTYPE_CHECK(wResizedPatterns, s);
	CHANGEMODTYPE_CHECK(wRestartPos, "Restart position is not supported by the new format.");
	CHANGEMODTYPE_CHECK(wPatternSignatures, "Pattern-specific time signatures are not supported by the new format.");
	CHANGEMODTYPE_CHECK(wChannelVolSurround, "Channel volume and surround are not supported by the new format.");
	CHANGEMODTYPE_CHECK(wChannelPanning, "Channel panning is not supported by the new format.");

	// Sample warnings
	CHANGEMODTYPE_CHECK(wSampleBidiLoops, "Sample bidi loops are not supported by the new format.");
	CHANGEMODTYPE_CHECK(wSampleSustainLoops, "New format doesn't support sample sustain loops.");
	CHANGEMODTYPE_CHECK(wSampleAutoVibrato, "New format doesn't support sample autovibrato.");
	CHANGEMODTYPE_CHECK(wMODSampleFrequency, "Sample C-5 frequencies will be lost.");
	CHANGEMODTYPE_CHECK(wMOD31Samples, "Samples above 31 will be lost when saving as MOD. Consider rearranging samples if there are unused slots available.");

	// Instrument warnings
	CHANGEMODTYPE_CHECK(wInstrumentsToSamples, "All instruments have been converted to samples.");
	CHANGEMODTYPE_CHECK(wTrimmedEnvelopes, "Instrument envelopes have been shortened.");
	CHANGEMODTYPE_CHECK(wInstrumentSustainLoops, "Sustain loops were converted to sustain points.");
	CHANGEMODTYPE_CHECK(wInstrumentTuning, "Instrument tunings will be lost.");
	CHANGEMODTYPE_CHECK(wPitchToTempoLock, "Pitch / Tempo Lock instrument property is not supported by the new format.");
	CHANGEMODTYPE_CHECK(wBrokenNoteMap, "Instrument Note Mapping is not supported by the new format.");
	CHANGEMODTYPE_CHECK(wReleaseNode, "Instrument envelope release nodes are not supported by the new format.");
	CHANGEMODTYPE_CHECK(wFilterVariation, "Random filter variation is not supported by the new format.");

	// General warnings
	CHANGEMODTYPE_CHECK(wMODGlobalVars, "Default speed, tempo and global volume will be lost.");
	CHANGEMODTYPE_CHECK(wLinearSlides, "Linear Frequency Slides not supported by the new format.");
	CHANGEMODTYPE_CHECK(wEditHistory, "Edit history will not be saved in the new format.");
	CHANGEMODTYPE_CHECK(wMixmode, "Consider setting the mix levels to \"Compatible\" in the song properties when working with legacy formats.");
	CHANGEMODTYPE_CHECK(wVolRamp, "Fasttracker 2 compatible super soft volume ramping gets lost when converting XM files to another type.");
	CHANGEMODTYPE_CHECK(wCompatibilityMode, "Consider enabling the \"compatible playback\" option in the song properties to increase compatiblity with other players.");
	CHANGEMODTYPE_CHECK(wGlobalVolumeNotSupported, "Default global volume is not supported by the new format.");

	SetModified();
	GetPatternUndo().ClearUndo();
	UpdateAllViews(NULL, HINT_MODTYPE | HINT_MODGENERAL);
	EndWaitCursor();

	//rewbs.customKeys: update effect key commands
	CInputHandler *ih = CMainFrame::GetMainFrame()->GetInputHandler();
	ih->SetEffectLetters(m_SndFile.GetModSpecifications());
	//end rewbs.customKeys

	return true;
}


#undef CHANGEMODTYPE_WARNING
#undef CHANGEMODTYPE_CHECK


OPENMPT_NAMESPACE_END
