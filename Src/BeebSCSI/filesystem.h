/************************************************************************
	filesystem.h

	BeebSCSI filing system functions
    BeebSCSI - BBC Micro SCSI Drive Emulator
    Copyright (C) 2018 Simon Inns

	This file is part of BeebSCSI.

    BeebSCSI is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

	Email: simon.inns@gmail.com

	BeebEm: the Pi1MHz version (https://github.com/dp111/Pi1MHz), cut
	down to the drives: no VFS (LaserDisc) LUNs, jukebox, FAT transfer or
	storage choice.  The images and their .cfg files are in the hard
	drive folder (HardDrivePath), the default descriptor defscsi.cfg in
	the hard drive folder or the user data folder.

************************************************************************/

#ifndef FILESYSTEM_H_
#define FILESYSTEM_H_

#include <stddef.h>
#include <stdint.h>

// Read/Write sector buffer (must be 256 bytes minimum)
#define SECTOR_BUFFER_SIZE	 (16*1024)

// The drives (LUNs) 0-7
#define MAX_LUNS 8

// Calculate the length of the sector buffer in 256 byte sectors
#define SECTOR_BUFFER_LENGTH	(SECTOR_BUFFER_SIZE / 256)

// The block sizes the data path takes: an Adaptec board has 256, 512 or
// 1024, a drive with its own controller 128 to 2048
// (filesystemGetLunDataBlockSize)
#define MIN_BLOCK_SIZE 128
#define MAX_BLOCK_SIZE 2048

// The default '33' is from the ACB-4000 which uses MFM encoding. The later
// ACB-4070 which Acorn used in the FileStore product uses RLL encoding and
// allows for a higher density of sectors per track.
#define DEFAULT_SECTORS_PER_TRACK 33
#define DEFAULT_BLOCK_SIZE 256

// Minimum geometry details needed to operate the LUN
// This saves looking them up again in the future
struct HDGeometry
{
	uint32_t BlockSize;				// This will almost always be 256
	uint32_t Cylinders;
	uint8_t  Heads;
	uint16_t SectorsPerTrack;		// default is 33
	uint16_t Interleave;				// doesn't really matter for BeebSCSI
};

// External prototypes
void filesystemInitialise(void);
void filesystemReset(void);

// The folders BeebEm keeps the drives in, and the default descriptor in
void filesystemSetPaths(const char *hardDrivePath, const char *userDataPath);

// Indexes into the scsiattributes key table (and each LUN's parsed
// keyvalues) - must match the table order in filesystem.cpp
enum parserkeyvalueenum {
    TITLE,
    DESCRIPTION,
    INQUIRY,
    MODEPARAMHEADER,
    LBADESCRIPTOR,
    MODEPAGE0,
    MODEPAGE1,
    WRITPAGE1,
    MODEPAGE3,
    WRITPAGE3,
    MODEPAGE4,
    MODEPAGE32,
    MODEPAGE33,
    MODEPAGE35,
    MODEPAGE36,
    MODEPAGE37,
    MODEPAGE38,
    MASKPAGE1,
    MASKPAGE3,
    MASKPAGE4,
    MASKPAGE32,
    MASKPAGE33,
    MASKPAGE35,
    MASKPAGE36,
    MASKPAGE37,
    MASKPAGE38,
    FDEFPAGE1,
    FDEFPAGE3,
    FDEFPAGE4,
    FDEFPAGE32,
    FDEFPAGE33,
    FDEFPAGE35,
    FDEFPAGE36,
    FDEFPAGE37,
    FDEFPAGE38,
    PAGECONTROL,
    TRACKFORMAT,
    LDUSERCODE,
    LDVIDEOXOFFSET,
    LDVIDEOYOFFSET
};

bool filesystemSetLunStatus(uint8_t lunNumber, bool lunStatus);
bool filesystemReadLunStatus(uint8_t lunNumber);
bool filesystemCheckLunImage(uint8_t lunNumber);
bool filesystemCheckExtAttributes( uint8_t lunNumber);
void filesystemConfigToLunGeometry(uint8_t lunNumber);
void filesytemdattoconfigGeometry(uint8_t lunNumber);
void filesystemLunToconfigGeometry(uint8_t lunNumber);
void filesystemSetLunBlockSize(uint8_t lunNumber, uint32_t blockSize);
void filesystemCopyPage0toPage4(uint8_t lunNumber);
void filesystemCopyPage4toPage0(uint8_t lunNumber);
uint32_t filesystemGetLunBlockSize(uint8_t lunNumber);
uint32_t filesystemGetLunDataBlockSize(uint8_t lunNumber);
uint32_t filesystemGetheadspercylinder(uint8_t lunNumber);
uint32_t filesystemGetLunCylinders(uint8_t lunNumber);
uint32_t filesystemGetDriveGeneration(void);
uint32_t filesystemGetLunSPTSize( uint8_t lunNumber);
uint32_t filesystemGetLunTotalSectors(uint8_t lunNumber);
uint32_t filesystemGetLunTotalBytes(uint8_t lunNumber);

bool filesystemCreateLunImage(uint8_t lunNumber);
bool filesystemCreateLunDescriptor(uint8_t lunNumber);
bool filesystemReadLunDescriptor(uint8_t lunNumber);
bool filesystemWriteAttributes(uint8_t lunNumber);
bool filesystemStartLunForFormat(uint8_t lunNumber);
bool filesystemSaveFormatGeometry(uint8_t lunNumber, uint16_t sectorsPerTrack, uint32_t blockSize,
                                  uint16_t interleave);
bool filesystemGetLunTrackFormat(uint8_t lunNumber, uint32_t *track, uint32_t *overhead, uint32_t *spare);
bool filesystemFormatLun(uint8_t lunNumber, uint8_t dataPattern);

bool filesystemOpenLunForRead(uint8_t lunNumber, uint32_t startSector, uint32_t requiredNumberOfSectors);
bool filesystemReadNextSector(uint8_t lunNumber, uint8_t **buffer);
bool filesystemCloseLunForRead(uint8_t lunNumber);
bool filesystemOpenLunForWrite(uint8_t lunNumber, uint32_t startSector, uint32_t requiredNumberOfSectors);
bool filesystemWriteNextSector(uint8_t lunNumber, uint8_t const buffer[]);
bool filesystemCloseLunForWrite(uint8_t lunNumber);

char * filesystemGetInquiryData(uint8_t lunNumber, size_t * length);
char * filesystemGetModeParamHeaderData(uint8_t lunNumber, size_t * length);
char * filesystemGetLBADescriptorData(uint8_t lunNumber, size_t * length);
char * filesystemGetModePageData(uint8_t lunNumber, uint8_t page, size_t * length);
char * filesystemGetMaskPageData(uint8_t lunNumber, uint8_t page, size_t * length);
char * filesystemGetFDefPageData(uint8_t lunNumber, uint8_t page, size_t * length);
bool filesystemPageControlCCS(uint8_t lunNumber);
int filesystemWriteModePageData(uint8_t lunNumber, uint8_t page, uint8_t len, const uint8_t * Buffer);
int filesystemMergeModePageData(uint8_t lunNumber, uint8_t page, uint8_t declared, uint8_t received,
                                const uint8_t * Buffer, bool driveRules);
size_t filesystemModePageAfterSelect(uint8_t lunNumber, uint8_t page, const char * base, size_t baseLength,
                                     uint8_t declared, uint8_t received, const uint8_t * Buffer, uint8_t out[256]);
void filesystemSetModePageRaw(uint8_t lunNumber, uint8_t page, const uint8_t * data, size_t length);

/* The numeric fields of pages 3 and 4 (offsets into the page, counting the
   code and length bytes); NULL and 0 for any other page. */
typedef struct { uint8_t start; uint8_t size; } modePageField_t;
const modePageField_t * filesystemModePageFields(uint8_t page, size_t * count);

/* ModePage0 is the Adaptec's drive parameter list (true, the default), or an
   ordinary page with the geometry in page 4 alone (a drive with its own
   controller); set by scsi.cpp from the drive controller. */
void filesystemSetPage0IsDriveList(bool driveList);

/* DriveGeometry=CFG (true) or Auto: set by scsi.cpp */
void filesystemSetGeometryFromCfgOnly(bool cfgOnly);

uint32_t filesystemReadFile(const char * filename, uint8_t **address, unsigned int max_size);

/* Verified write-and-swap for files whose existing copy matters (.cfg and
   the like): writes <name>.new, reads it back to check it, then renames. */
bool filesystemWriteFileSafe(const char * filename, const uint8_t *address, uint32_t length);

#endif /* FILESYSTEM_H_ */
