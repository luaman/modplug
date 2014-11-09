/*
 * unrar.cpp
 * ---------
 * Purpose: Implementation file for extracting modules from .rar archives
 * Notes  : Based on modified UnRAR library to support in-memory files.
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */

#include "stdafx.h"
#include "unrar.h"
#include "../include/unrar/openmpt-callback.hpp"

#if MPT_COMPILER_MSVC
// Disable "unreferenced formal parameter" and type conversion warnings
#pragma warning(disable:4100; disable:4244)
#endif
#include "../include/unrar/rar.hpp"
#if MPT_COMPILER_MSVC
#pragma warning(default:4100; default:4244)
#endif


OPENMPT_NAMESPACE_BEGIN


struct RARData
{
	CommandData Cmd;
	Archive Arc;
	RARFileCallbacks callbacks;
	int64 firstBlock;

	RARData(FileReader &file) : Arc(&Cmd), callbacks(ReadRaw, Seek, GetPosition, GetLength, &file) { }

	// FileReader callbacks
	static size_t CALLBACK ReadRaw(void *file, char *data, size_t size) { return static_cast<FileReader *>(file)->ReadRaw(data, size); };
	static bool CALLBACK Seek(void *file, size_t offset) { return static_cast<FileReader *>(file)->Seek(offset); };
	static size_t CALLBACK GetPosition(void *file) { return static_cast<FileReader *>(file)->GetPosition(); };
	static size_t CALLBACK GetLength(void *file) { return static_cast<FileReader *>(file)->GetLength(); };

	static int CALLBACK RARCallback(unsigned int msg, LPARAM userData, LPARAM p1, LPARAM p2)
	{
		if(msg == UCM_PROCESSDATA)
		{
			// Receive extracted data
			CRarArchive *that = reinterpret_cast<CRarArchive *>(userData);
			const char *data = reinterpret_cast<const char *>(p1);
			that->data.insert(that->data.end(), data, data + p2);
			return 1;
		}
		// No support for passwords or volumes
		return 0;
	}
};


CRarArchive::CRarArchive(FileReader &file) : ArchiveBase(file)
//------------------------------------------------------------
{
	rarData = new (std::nothrow) RARData(inFile);
	if(rarData == nullptr)
	{
		return;
	}

	try
	{
		rarData->Cmd.FileArgs.AddString(MASKALL);
		rarData->Cmd.VersionControl = 1;

		rarData->Arc.Open(reinterpret_cast<wchar *>(&rarData->callbacks), 0);
		if(!rarData->Arc.IsArchive(false))
		{
			return;
		}

		// Read comment
		Array<wchar> rarComment;
		if(rarData->Arc.GetComment(&rarComment))
		{
			comment = std::wstring(&rarComment[0], rarComment.Size());
		}

		// Scan all files
		rarData->firstBlock = rarData->Arc.Tell();
		while(rarData->Arc.SearchBlock(HEAD_FILE) > 0)
		{
			ArchiveFileInfo fileInfo;
			fileInfo.name = mpt::PathString::FromWide(std::wstring(rarData->Arc.FileHead.FileName));
			fileInfo.type = ArchiveFileNormal;
			fileInfo.size = rarData->Arc.FileHead.UnpSize;
			contents.push_back(fileInfo);

			rarData->Arc.SeekToNext();
		}
	} catch(RAR_EXIT)
	{
	}
}


CRarArchive::~CRarArchive()
//-------------------------
{
	delete rarData;
}


bool CRarArchive::ExtractFile(std::size_t index)
//----------------------------------------------
{
	if(index >= contents.size())
	{
		return false;
	}

	try
	{
		// Rewind...
		rarData->Arc.Seek(rarData->firstBlock, SEEK_SET);
		CmdExtract Extract(&rarData->Cmd);
		Extract.ExtractArchiveInit(rarData->Arc);

		// Don't extract, only test (and use the callback function for writing our data)
		rarData->Cmd.Test = true;
		rarData->Cmd.DllOpMode = RAR_TEST;
		rarData->Cmd.Callback = nullptr;
		rarData->Cmd.UserData = reinterpret_cast<LPARAM>(this);

		bool repeat = false;
		size_t headerSize;
		do
		{
			headerSize = rarData->Arc.SearchBlock(HEAD_FILE);
			ASSERT(headerSize);
			if(rarData->Arc.Solid && index)
			{
				// Skip solid files
				Extract.ExtractCurrentFile(rarData->Arc, headerSize, repeat);
			}
			rarData->Arc.SeekToNext();
		} while(index--);

		// Extract real file
		data.clear();
		data.reserve(static_cast<size_t>(rarData->Arc.FileHead.UnpSize));
		rarData->Cmd.Callback = RARData::RARCallback;
		Extract.ExtractCurrentFile(rarData->Arc, headerSize, repeat);
		return !data.empty();
	} catch(...)
	{
		return false;
	}
}


OPENMPT_NAMESPACE_END
