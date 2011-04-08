/*
** file_vswap.cpp
**
**---------------------------------------------------------------------------
** Copyright 2011 Braden Obrzut
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
**
*/

#include "wl_def.h"
#include "m_swap.h"
#include "resourcefile.h"
#include "w_wad.h"
#include "lumpremap.h"
#include "zstring.h"

// Some sounds in the VSwap file are multiparty so we need mean to concationate
// them.
struct FVSwapSound : public FResourceLump
{
	protected:
		struct Chunk
		{
			public:
				int	offset;
				int	length;
		};

		Chunk *chunks;
		unsigned short numChunks;

	public:
		FVSwapSound(int maxNumChunks) : FResourceLump(), numChunks(0)
		{
			if(maxNumChunks < 0)
				maxNumChunks = 0;
			chunks = new Chunk[maxNumChunks];
			LumpSize = 0;
		}
		~FVSwapSound()
		{
			delete[] chunks;
		}

		int AddChunk(int offset, int length)
		{
			LumpSize += length;

			chunks[numChunks].offset = offset;
			chunks[numChunks].length = length;
			numChunks++;
		}

		int FillCache()
		{
			Cache = new char[LumpSize];
			unsigned int pos = 0;
			for(unsigned int i = 0;i < numChunks;i++)
			{
				Owner->Reader->Seek(chunks[i].offset, SEEK_SET);
				Owner->Reader->Read(Cache+pos, chunks[i].length);
				pos += chunks[i].length;
			}
			return 1;
		}
};

class FVSwap : public FResourceFile
{
	public:
		FVSwap(const char* filename, FileReader *file) : FResourceFile(filename, file), spriteStart(0), soundStart(0), Lumps(NULL), SoundLumps(NULL), vswapFile(filename)
		{
			int lastSlash = vswapFile.LastIndexOfAny("/\\");
			extension = vswapFile.Mid(lastSlash+7);
		}
		~FVSwap()
		{
			if(Lumps != NULL)
				delete[] Lumps;
			if(SoundLumps != NULL)
			{
				for(unsigned int i = 0;i < NumLumps-soundStart;i++)
					delete SoundLumps[i];
				delete[] SoundLumps;
			}
		}

		bool Open(bool quiet)
		{
			FileReader vswapReader;
			if(!vswapReader.Open(vswapFile))
				return false;

			BYTE header[6];
			vswapReader.Read(header, 6);
			int numChunks = ReadLittleShort(&header[0]);

			spriteStart = ReadLittleShort(&header[2]);
			soundStart = ReadLittleShort(&header[4]);

			Lumps = new FUncompressedLump[soundStart];


			BYTE* data = new BYTE[6*numChunks];
			vswapReader.Read(data, 6*numChunks);

			for(unsigned int i = 0;i < soundStart;i++)
			{
				char lumpname[9];
				sprintf(lumpname, "VSP%05d", i);

				Lumps[i].Owner = this;
				Lumps[i].LumpNameSetup(lumpname);
				Lumps[i].Namespace = i >= soundStart ? ns_sounds : (i >= spriteStart ? ns_sprites : ns_flats);
				if(Lumps[i].Namespace == ns_flats)
					Lumps[i].Flags |= LUMPF_DONTFLIPFLAT;
				Lumps[i].Position = ReadLittleLong(&data[i*4]);
				Lumps[i].LumpSize = ReadLittleShort(&data[i*2 + 4*numChunks]);
			}

			// Now for sounds we need to get the last Chunk and read the sound information.
			int soundMapOffset = ReadLittleLong(&data[(numChunks-1)*4]);
			int soundMapSize = ReadLittleShort(&data[(numChunks-1)*2 + 4*numChunks]);
			int numDigi = soundMapSize/4 - 1;
			byte* soundMap = new byte[soundMapSize];
			SoundLumps = new FVSwapSound*[numDigi];
			vswapReader.Seek(soundMapOffset, SEEK_SET);
			vswapReader.Read(soundMap, soundMapSize);
			for(unsigned int i = 0;i < numDigi;i++)
			{
				int start = ReadLittleShort(&soundMap[i*4]);
				int end = ReadLittleShort(&soundMap[i*4 + 4]);

				if(start + soundStart > numChunks - 1)
				{ // Read past end of chunks.
					numDigi = i;
					break;
				}

				char lumpname[9];
				sprintf(lumpname, "VSP%05d", i+soundStart);
				SoundLumps[i] = new FVSwapSound(end-start);
				SoundLumps[i]->Owner = this;
				SoundLumps[i]->LumpNameSetup(lumpname);
				SoundLumps[i]->Namespace = ns_sounds;
				for(unsigned int j = start;j < end && end + soundStart < numChunks;j++)
					SoundLumps[i]->AddChunk(ReadLittleLong(&data[(soundStart+j)*4]), ReadLittleShort(&data[(soundStart+j)*2 + numChunks*4]));
			}

			// Number of lumps is not the number of chunks, but the number of
			// chunks up to sounds + how many sounds are formed from the chunks.
			NumLumps = soundStart + numDigi;

			delete[] data;
			if(!quiet) Printf(", %d lumps\n", NumLumps);

			LumpRemaper::AddFile(extension, this, LumpRemaper::VSWAP);
			return true;
		}

		FResourceLump *GetLump(int no)
		{
			if(no < soundStart)
				return &Lumps[no];
			return SoundLumps[no-soundStart];
		}

	private:
		unsigned short spriteStart;
		unsigned short soundStart;

		FUncompressedLump* Lumps;
		FVSwapSound* *SoundLumps;

		FString	extension;
		FString	vswapFile;
};

FResourceFile *CheckVSwap(const char *filename, FileReader *file, bool quiet)
{
	FString fname(filename);
	int lastSlash = fname.LastIndexOfAny("/\\");
	if(lastSlash != -1)
		fname = fname.Mid(lastSlash+1, 5);
	else
		fname = fname.Left(5);

	if(fname.Len() == 5 && fname.CompareNoCase("vswap") == 0) // file must be vswap.something
	{
		FResourceFile *rf = new FVSwap(filename, file);
		if(rf->Open(quiet)) return rf;
		delete rf;
	}
	return NULL;
}