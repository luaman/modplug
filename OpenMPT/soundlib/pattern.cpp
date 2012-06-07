/*
 * Pattern.cpp
 * -----------
 * Purpose: Module Pattern header class
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "pattern.h"
#include "patternContainer.h"
#include "../mptrack/mainfrm.h"
#include "../mptrack/serialization_utils.h"
#include "../mptrack/version.h"
#include "ITTools.h"


CSoundFile& CPattern::GetSoundFile() { return m_rPatternContainer.GetSoundFile(); }
const CSoundFile& CPattern::GetSoundFile() const { return m_rPatternContainer.GetSoundFile(); }


CHANNELINDEX CPattern::GetNumChannels() const
//-------------------------------------------
{
	return GetSoundFile().GetNumChannels();
}


// Check if there is any note data on a given row.
bool CPattern::IsEmptyRow(ROWINDEX row) const
//-------------------------------------------
{
	if(m_ModCommands == nullptr || IsValidRow(row))
	{
		return true;
	}

	PatternRow data = GetRow(row);
	for(CHANNELINDEX chn = 0; chn < GetNumChannels(); chn++, data++)
	{
		if(!data->IsEmpty())
		{
			return false;
		}
	}
	return true;
}


bool CPattern::SetSignature(const ROWINDEX rowsPerBeat, const ROWINDEX rowsPerMeasure)
//------------------------------------------------------------------------------------
{
	if(rowsPerBeat < GetSoundFile().GetModSpecifications().patternRowsMin
		|| rowsPerBeat > GetSoundFile().GetModSpecifications().patternRowsMax
		|| rowsPerMeasure < rowsPerBeat
		|| rowsPerMeasure > GetSoundFile().GetModSpecifications().patternRowsMax)
	{
		return false;
	}
	m_RowsPerBeat = rowsPerBeat;
	m_RowsPerMeasure = rowsPerMeasure;
	return true;
}


// Add or remove rows from the pattern.
bool CPattern::Resize(const ROWINDEX newRowCount)
//-----------------------------------------------
{
	CSoundFile &sndFile = GetSoundFile();
	const CModSpecifications& specs = sndFile.GetModSpecifications();
	ModCommand *newPattern;

	if(m_ModCommands == nullptr
		|| newRowCount == m_Rows
		|| newRowCount > specs.patternRowsMax
		|| newRowCount < specs.patternRowsMin
		|| (newPattern = AllocatePattern(newRowCount, GetNumChannels())) == nullptr)
	{
		return false;
	}

	// Copy over pattern data
	memcpy(newPattern, m_ModCommands, GetNumChannels() * Util::Min(m_Rows, newRowCount) * sizeof(ModCommand));

	CriticalSection cs;
	FreePattern(m_ModCommands);
	m_ModCommands = newPattern;
	m_Rows = newRowCount;

	return true;
}


void CPattern::ClearCommands()
//----------------------------
{
	if (m_ModCommands != nullptr)
		memset(m_ModCommands, 0, GetNumRows() * GetNumChannels() * sizeof(ModCommand));
}


bool CPattern::AllocatePattern(ROWINDEX rows)
//-------------------------------------------
{
	ModCommand *m = AllocatePattern(rows, GetNumChannels());
	if(m != nullptr)
	{
		Deallocate();
		m_ModCommands = m;
		m_Rows = rows;
		m_RowsPerBeat = m_RowsPerMeasure = 0;
		return true;
	} else
	{
		return false;
	}

}


void CPattern::Deallocate()
//-------------------------
{
	m_Rows = m_RowsPerBeat = m_RowsPerMeasure = 0;
	FreePattern(m_ModCommands);
	m_ModCommands = nullptr;
	m_PatternName.Empty();
}


bool CPattern::Expand()
//---------------------
{
	const ROWINDEX newRows = m_Rows * 2;
	const CHANNELINDEX nChns = GetNumChannels();
	ModCommand *newPattern;

	if(!m_ModCommands
		|| newRows > GetSoundFile().GetModSpecifications().patternRowsMax
		|| (newPattern = AllocatePattern(newRows, nChns)) == nullptr)
	{
		return false;
	}

	for(ROWINDEX y = 0; y < m_Rows; y++)
	{
		memcpy(newPattern + y * 2 * nChns, m_ModCommands + y * nChns, nChns * sizeof(ModCommand));
	}

	CriticalSection cs;
	FreePattern(m_ModCommands);
	m_ModCommands = newPattern;
	m_Rows = newRows;

	return true;
}


bool CPattern::Shrink()
//---------------------
{
	if (!m_ModCommands
		|| m_Rows < GetSoundFile().GetModSpecifications().patternRowsMin * 2)
	{
		return false;
	}

	m_Rows /= 2;
	const CHANNELINDEX nChns = GetNumChannels();

	for(ROWINDEX y = 0; y < m_Rows; y++)
	{
		const PatternRow srcRow = GetRow(y * 2);
		const PatternRow nextSrcRow = GetRow(y * 2 + 1);
		PatternRow destRow = GetRow(y);

		for(CHANNELINDEX x = 0; x < nChns; x++)
		{
			const ModCommand &src = srcRow[x];
			const ModCommand &srcNext = nextSrcRow[x];
			ModCommand &dest = destRow[x];
			dest = src;

			if(dest.note == NOTE_NONE && !dest.instr)
			{
				// Fill in data from next row if field is empty
				dest.note = srcNext.note;
				dest.instr = srcNext.instr;
				if(srcNext.volcmd != VOLCMD_NONE)
				{
					dest.volcmd = srcNext.volcmd;
					dest.vol = srcNext.vol;
				}
				if(dest.command == CMD_NONE)
				{
					dest.command = srcNext.command;
					dest.param = srcNext.param;
				}
			}
		}
	}

	return true;
}


bool CPattern::SetName(const char *newName, size_t maxChars)
//----------------------------------------------------------
{
	if(newName == nullptr || maxChars == 0)
	{
		return false;
	}
	m_PatternName.SetString(newName, strnlen(newName, maxChars));
	return true;
}


bool CPattern::GetName(char *buffer, size_t maxChars) const
//---------------------------------------------------------
{
	if(buffer == nullptr || maxChars == 0)
	{
		return false;
	}
	strncpy(buffer, m_PatternName, maxChars - 1);
	buffer[maxChars - 1] = '\0';
	return true;
}


////////////////////////////////////////////////////////////////////////
//
//	Static allocation / deallocation methods
//
////////////////////////////////////////////////////////////////////////


ModCommand *CPattern::AllocatePattern(ROWINDEX rows, CHANNELINDEX nchns)
//----------------------------------------------------------------------
{
	try
	{
		ModCommand *p = new ModCommand[rows * nchns];
		memset(p, 0, rows * nchns * sizeof(ModCommand));
		return p;
	} catch(MPTMemoryException)
	{
		return nullptr;
	}
}


void CPattern::FreePattern(ModCommand *pat)
//-----------------------------------------
{
	if (pat) delete[] pat;
}


////////////////////////////////////////////////////////////////////////
//
//	ITP functions
//
////////////////////////////////////////////////////////////////////////


bool CPattern::WriteITPdata(FILE* f) const
//----------------------------------------
{
	for(ROWINDEX r = 0; r < GetNumRows(); r++)
	{
		for(CHANNELINDEX c = 0; c < GetNumChannels(); c++)
		{
			ModCommand mc = GetModCommand(r,c);
			fwrite(&mc, sizeof(ModCommand), 1, f);
		}
	}
    return false;
}

bool CPattern::ReadITPdata(const BYTE* const lpStream, DWORD& streamPos, const DWORD datasize, const DWORD dwMemLength)
//-----------------------------------------------------------------------------------------------
{
	if(streamPos > dwMemLength || datasize >= dwMemLength - streamPos || datasize < sizeof(MODCOMMAND_ORIGINAL))
		return true;

	const DWORD startPos = streamPos;
	size_t counter = 0;
	while(streamPos - startPos + sizeof(MODCOMMAND_ORIGINAL) <= datasize)
	{
		MODCOMMAND_ORIGINAL temp;
		memcpy(&temp, lpStream + streamPos, sizeof(MODCOMMAND_ORIGINAL));
		ModCommand& mc = GetModCommand(counter);
		mc.command = temp.command;
		mc.instr = temp.instr;
		mc.note = temp.note;
		mc.param = temp.param;
		mc.vol = temp.vol;
		mc.volcmd = temp.volcmd;
		streamPos += sizeof(MODCOMMAND_ORIGINAL);
		counter++;
	}
	if(streamPos != startPos + datasize)
	{
		ASSERT(false);
		return true;
	}
	else
		return false;
}




////////////////////////////////////////////////////////////////////////
//
//	Pattern serialization functions
//
////////////////////////////////////////////////////////////////////////


static enum maskbits
{
	noteBit			= (1 << 0),
	instrBit		= (1 << 1),
	volcmdBit		= (1 << 2),
	volBit			= (1 << 3),
	commandBit		= (1 << 4),
	effectParamBit	= (1 << 5),
	extraData		= (1 << 6)
};

void WriteData(std::ostream& oStrm, const CPattern& pat);
void ReadData(std::istream& iStrm, CPattern& pat, const size_t nSize = 0);

void WriteModPattern(std::ostream& oStrm, const CPattern& pat)
//------------------------------------------------------------
{
	srlztn::Ssb ssb(oStrm);
	ssb.BeginWrite(FileIdPattern, MptVersion::num);
	ssb.WriteItem(pat, "data", strlen("data"), &WriteData);
	// pattern time signature
	if(pat.GetOverrideSignature())
	{
		ssb.WriteItem<uint32>(pat.GetRowsPerBeat(), "RPB.");
		ssb.WriteItem<uint32>(pat.GetRowsPerMeasure(), "RPM.");
	}
	ssb.FinishWrite();
}


void ReadModPattern(std::istream& iStrm, CPattern& pat, const size_t)
//-------------------------------------------------------------------
{
	srlztn::Ssb ssb(iStrm);
	ssb.BeginRead(FileIdPattern, MptVersion::num);
	if ((ssb.m_Status & srlztn::SNT_FAILURE) != 0)
		return;
	ssb.ReadItem(pat, "data", strlen("data"), &ReadData);
	// pattern time signature
	uint32 nRPB = 0, nRPM = 0;
	ssb.ReadItem<uint32>(nRPB, "RPB.");
	ssb.ReadItem<uint32>(nRPM, "RPM.");
	pat.SetSignature(nRPB, nRPM);
}


uint8 CreateDiffMask(ModCommand chnMC, ModCommand newMC)
//------------------------------------------------------
{
	uint8 mask = 0;
	if(chnMC.note != newMC.note)
		mask |= noteBit;
	if(chnMC.instr != newMC.instr)
		mask |= instrBit;
	if(chnMC.volcmd != newMC.volcmd)
		mask |= volcmdBit;
	if(chnMC.vol != newMC.vol)
		mask |= volBit;
	if(chnMC.command != newMC.command)
		mask |= commandBit;
	if(chnMC.param != newMC.param)
		mask |= effectParamBit;
	return mask;
}

using srlztn::Binarywrite;
using srlztn::Binaryread;

// Writes pattern data. Adapted from SaveIT.
void WriteData(std::ostream& oStrm, const CPattern& pat)
//------------------------------------------------------
{
	if(!pat)
		return;

	const ROWINDEX rows = pat.GetNumRows();
	const CHANNELINDEX chns = pat.GetNumChannels();
	vector<ModCommand> lastChnMC(chns);

	for(ROWINDEX r = 0; r<rows; r++)
	{
		for(CHANNELINDEX c = 0; c<chns; c++)
		{
			const ModCommand m = *pat.GetpModCommand(r, c);
			// Writing only commands not written in IT-pattern writing:
			// For now this means only NOTE_PC and NOTE_PCS.
			if(!m.IsPcNote())
				continue;
			uint8 diffmask = CreateDiffMask(lastChnMC[c], m);
			uint8 chval = static_cast<uint8>(c+1);
			if(diffmask != 0)
				chval |= IT_bitmask_patternChanEnabled_c;

			Binarywrite<uint8>(oStrm, chval);
				
			if(diffmask)
			{
				lastChnMC[c] = m;
				Binarywrite<uint8>(oStrm, diffmask);
				if(diffmask & noteBit) Binarywrite<uint8>(oStrm, m.note);
				if(diffmask & instrBit) Binarywrite<uint8>(oStrm, m.instr);
				if(diffmask & volcmdBit) Binarywrite<uint8>(oStrm, m.volcmd);
				if(diffmask & volBit) Binarywrite<uint8>(oStrm, m.vol);
				if(diffmask & commandBit) Binarywrite<uint8>(oStrm, m.command);
				if(diffmask & effectParamBit) Binarywrite<uint8>(oStrm, m.param);
			}
		}
		Binarywrite<uint8>(oStrm, 0); // Write end of row marker.
	}
}


#define READITEM(itembit,id)		\
if(diffmask & itembit)				\
{									\
	Binaryread<uint8>(iStrm, temp);	\
	if(ch < chns)					\
		lastChnMC[ch].id = temp;	\
}									\
if(ch < chns)						\
	m.id = lastChnMC[ch].id;


void ReadData(std::istream& iStrm, CPattern& pat, const size_t)
//-------------------------------------------------------------
{
	if (!pat) // Expecting patterns to be allocated and resized properly.
		return;

	const CHANNELINDEX chns = pat.GetNumChannels();
	const ROWINDEX rows = pat.GetNumRows();

	vector<ModCommand> lastChnMC(chns);

	ROWINDEX row = 0;
	while(row < rows && iStrm.good())
	{
		uint8 t = 0;
		Binaryread<uint8>(iStrm, t);
		if(t == 0)
		{
			row++;
			continue;
		}

		CHANNELINDEX ch = (t & IT_bitmask_patternChanField_c); 
		if(ch > 0)
			ch--;

		uint8 diffmask = 0;
		if((t & IT_bitmask_patternChanEnabled_c) != 0)
			Binaryread<uint8>(iStrm, diffmask);
		uint8 temp = 0;

		ModCommand dummy;
		ModCommand& m = (ch < chns) ? *pat.GetpModCommand(row, ch) : dummy;

		READITEM(noteBit, note);
		READITEM(instrBit, instr);
		READITEM(volcmdBit, volcmd);
		READITEM(volBit, vol);
		READITEM(commandBit, command);
		READITEM(effectParamBit, param);
		if(diffmask & extraData)
		{
			//Ignore additional data.
			uint8 temp;
			Binaryread<uint8>(iStrm, temp);
			iStrm.ignore(temp);
		}
	}
}

#undef READITEM
