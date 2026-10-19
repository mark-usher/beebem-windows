/****************************************************************
BeebEm - BBC Micro and Master 128 Emulator
Copyright (C) 2021  Mark Usher
Copyright (C) 2021  Chris Needham

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public
License along with this program; if not, write to the Free
Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA  02110-1301, USA.
****************************************************************/

// Acorn FileStore E01/E01S hardware: I/O page, front panel and control latch

#ifndef FILESTORE_HEADER
#define FILESTORE_HEADER

extern bool FileStoreDoorOpen;    // Front panel door switch
extern bool FileStoreModeLED;     // Front panel MODE LED

// Hard drive unit attached as each of SCSI drives 0-3. Auto attaches the
// drive if its image file exists, and labels it from the image size.
enum class FileStoreDriveType
{
	None,
	Auto,
	E20,
	E40S,
	E60S,
	Custom
};

constexpr int FILESTORE_MAX_DRIVES = 4;

// Longest custom drive label, and the characters it can contain (those
// in the front panel lettering)
constexpr int FILESTORE_DRIVE_LABEL_LENGTH = 4;
constexpr const char* FILESTORE_DRIVE_LABEL_CHARS = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-./";

extern FileStoreDriveType FileStoreDrive[FILESTORE_MAX_DRIVES];

void FileStoreReset();
unsigned char FileStoreReadMem(int Address);
void FileStoreWriteMem(int Address, unsigned char Value);
void FileStorePoll(int Cycles);

#endif
