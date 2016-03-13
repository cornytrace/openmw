/*
  OpenOht - The completely unofficial fork of OpenMW
  Copyright (C) 2008-2010  Nicolay Korslund
  Email: < korslund@gmail.com >
  WWW: http://openmw.sourceforge.net/

  This file (bsa_file.cpp) is part of the OpenMW package.

  OpenMW is distributed as free software: you can redistribute it
  and/or modify it under the terms of the GNU General Public License
  version 3, as published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  version 3 along with this program. If not, see
  http://www.gnu.org/licenses/ .

 */

//TODO: Decompress compressed files.

#include "bsa_file.hpp"

#include <stdexcept>
#include <string>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/copy.hpp>

using namespace std;
using namespace Bsa;


struct bzstring {
	unsigned char len;
	char* str;
	void read(boost::filesystem::ifstream &input) {
		input.read(reinterpret_cast<char*>(&len), 1);
		str = new char[len];
		input.read(str, len);
	}
};

/// Error handling
void BSAFile::fail(const string &msg)
{
    throw std::runtime_error("BSA Error: " + msg + "\nArchive: " + filename);
}

/// Read header information from the input source
void BSAFile::readHeader()
{
    /*
     * The layout of a BSA archive is as follows:
     *
     * - 12 bytes header, contains 3 ints:
     *         id number - equal to 0x100
     *         dirsize - size of the directory block (see below)
     *         numfiles - number of files
     *
     * ---------- start of directory block -----------
     *
     * - 8 bytes*numfiles, each record contains:
     *         fileSize
     *         offset into data buffer (see below)
     *
     * - 4 bytes*numfiles, each record is an offset into the following name buffer
     *
     * - name buffer, indexed by the previous table, each string is
     *   null-terminated. Size is (dirsize - 12*numfiles).
     *
     * ---------- end of directory block -------------
     *
     * - 8*filenum - hash table block, we currently ignore this
     *
     * ----------- start of data buffer --------------
     *
     * - The rest of the archive is file data, indexed by the
     *   offsets in the directory block. The offsets start at 0 at
     *   the beginning of this buffer.
     *
     */
    assert(!isLoaded);

    namespace bfs = boost::filesystem;
    bfs::ifstream input(bfs::path(filename), std::ios_base::binary);

    // Total archive size
    std::streamoff fsize = 0;
    if(input.seekg(0, std::ios_base::end))
    {
        fsize = input.tellg();
        input.seekg(0);
    }

    if(fsize < 12)
        fail("File too small to be a valid BSA archive");

    // Get essential header numbers
    unsigned int dirnum, filenum, totfilenamelen, compressed;
    {
        // First 12 bytes
        char temp[4];

		//fileID, must always be BSA
		input.read(temp,4);
        if(strcmp(temp,"BSA"))
            fail("Unrecognized BSA header");

		//version, we only understand 103 (0x67)
		input.read(temp,4);
		if (temp[0] != 103)
			fail("Unknown BSA version");

		input.read(temp,4);
		if (temp[0] != 36)
			fail("Strange BSA header size");

		//skip archiveflags
		int archiveflags;
		input.read(reinterpret_cast<char*>(&archiveflags),4);
		compressed = (archiveflags >> 2) & 1;

		//folderCount
		input.read(temp,4);
		dirnum = reinterpret_cast<unsigned int&>(temp);

		//fileCount
		input.read(temp,4);
		filenum = reinterpret_cast<unsigned int&>(temp);

		//totalFolderNameLength
		input.read(temp,4);

		//totalFileNameLength
		input.read(temp, 4);
		totfilenamelen = reinterpret_cast<unsigned int&>(temp);

		//fileFlags
		input.read(temp, 4);
    }

	//Header is done, set stream to reading folder records
	//input.seekg(36);
	int arraycount = 0;
	int oldoff = input.tellg();
	for (size_t i = 0; i < dirnum; i++)
	{
		char hash[8];
		unsigned int count;
		unsigned int offset;

		input.seekg(oldoff);

		input.read(hash, 8);
		input.read(reinterpret_cast<char*>(&count), 4);
		input.read(reinterpret_cast<char*>(&offset), 4);
		offset = offset - totfilenamelen;

		oldoff = input.tellg();
		input.seekg(offset);
		bzstring dirname;
		dirname.read(input);

		files.resize(files.size()+count);

		//Read file records
		for (unsigned j = 0; j < count;j++)
		{
			char fileHash[8];
			unsigned int filesize;
			unsigned int fileoffset;
			input.read(fileHash, 8);
			input.read(reinterpret_cast<char*>(&filesize), 4);
			input.read(reinterpret_cast<char*>(&fileoffset), 4);

			FileStruct &fs = files[arraycount++];
			fs.fileSize = filesize;
			int filecompressed = (filesize >> 29) & 1;
			filecompressed = compressed ^ filecompressed;
			fs.compressed = filecompressed;
			fs.offset = fileoffset;
			fs.name = dirname.str; // we add the actual file name in the next section.
		}
	}

	//File Name blocks
	for (int i = 0; i < arraycount; i++)
	{
		string buf;
		std::getline(input, buf, '\0');
		
		files[i].name += "\\";
		files[i].name += buf;

		lookup[files[i].name.c_str()] = i;
	}

    isLoaded = true;
}

/// Get the index of a given file name, or -1 if not found
int BSAFile::getIndex(const char *str) const
{
    Lookup::const_iterator it = lookup.find(str);
    if(it == lookup.end())
        return -1;

    int res = it->second;
    assert(res >= 0 && (size_t)res < files.size());
    return res;
}

/// Open an archive file.
void BSAFile::open(const string &file)
{
    filename = file;
    readHeader();
}

Files::IStreamPtr BSAFile::getFile(const char *file)
{
    assert(file);
    int i = getIndex(file);
    if(i == -1)
        fail("File not found: " + string(file));

    const FileStruct &fs = files[i];
	auto filestream = Files::openConstrainedFileStream (filename.c_str (), fs.offset, fs.fileSize);

	if (fs.compressed)
	{
		boost::iostreams::filtering_istreambuf buf;
		buf.push(boost::iostreams::zlib_decompressor());
		buf.push(*filestream);
		Files::IStreamPtr out(new std::istream(&buf));
		return out;
	}
	
	return filestream;
}

Files::IStreamPtr BSAFile::getFile(const FileStruct *file)
{
    return Files::openConstrainedFileStream (filename.c_str (), file->offset, file->fileSize);
}

unsigned long long BSAFile::genHash(const char *sPath, bool isFolder)
{
	unsigned long long hash = 0;
	unsigned long hash2 = 0;

	char s[255];    // copy into a local array so we don't modify the data that was passed
	strcpy_s(s, 255, sPath);

	// This is a pointer to the file extension, otherwise NULL
	char *pExt = (isFolder) ? 0 : strrchr(s, '.');

	// Length of the filename or folder path
	unsigned int iLen = strnlen_s(s, 0xFF);

	// pointer to the end of the filename or folder path
	char *pEnd = s + iLen;

	// A single loop converts / to \ and anything else to lower case
	for (int iLoop = 0; iLoop < iLen; iLoop++)
		s[iLoop] = (s[iLoop] == '/') ? '\\' : tolower(s[iLoop]);


	// Hash 1
	// If this is a file with an extension
	if (pExt)
	{
		for (char *x = pExt; x < pEnd; x++)
			hash = (hash * 0x1003f) + *x;
		// From here on, iLen and pEnd must NOT include the file extension.
		iLen = pExt - s;
		pEnd = pExt;
	}

	for (char *x = s + 1; x < (pEnd - 2); x++)
		hash2 = (hash2 * 0x1003f) + *x;
	hash += hash2;
	hash2 = 0;

	// Move Hash 1 to the upper bits
	hash <<= 32;


	//Hash 2
	hash2 = s[iLen - 1];
	hash2 |= ((iLen > 2) ? s[iLen - 2] << 8 : 0);
	hash2 |= (iLen << 16);
	hash2 |= (s[0] << 24);

	if (pExt) {
		switch (*(unsigned long*)(void*)pExt)   // load these 4 bytes as a long integer
		{
		case 0x00666B2E:  // 2E 6B 66 00 == .kf\0
			hash2 |= 0x80;
			break;
		case 0x66696E2E:  // .nif
			hash2 |= 0x8000;
			break;
		case 0x7364642E:  // .dds
			hash2 |= 0x8080;
			break;
		case 0x7661772E:  // .wav
			hash2 |= 0x80000000;
		}
	}

	return hash + hash2;
}