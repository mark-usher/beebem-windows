/************************************************************************
   filesystem.cpp

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

   BeebEm: the Pi1MHz version of this file (https://github.com/dp111/Pi1MHz,
   BeebSCSI/filesystem.c), with the FatFs calls replaced by the C library's
   and only the drives kept: no VFS (LaserDisc) LUNs, jukebox, FAT
   transfer, storage choice, write protection or host-side interlocks.
   The functions keep their Pi1MHz names and behaviour, so that changes
   there can be followed here.

   A drive N is three files in the hard drive folder: scsiN.dat (the
   image), scsiN.cfg (its descriptor: INQUIRY data, mode pages, block
   size) and, for drives made before the .cfg existed, scsiN.dsc (the
   ACB-4000 MODE SELECT list).  A drive without a .cfg is described by
   defscsi.cfg, which is copied to scsiN.cfg the first time the drive's
   descriptor is written.

************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "filesystem.h"
#include "fileparser.h"

/* The longest a vendor mode page (32 to 38) may be, in bytes. */
#define VENDOR_PAGE_MAX 32

static const parserkey scsiattributes[] = {
   { "Title"           , 0 , 39 , STRING},
   { "Description"     , 0 ,255 , STRING },
   { "Inquiry"         , 0 ,101 , NUMSTRING },
   { "ModeParamHeader" , 0 ,  4 , NUMSTRING },
   { "LBADescriptor"   , 0 ,  8 , NUMSTRING },
   { "ModePage0"       , 0 ,  10 , NUMSTRING},
   { "ModePage1"       , 0 ,  5 , NUMSTRING},
   { "WritPage1"       , 0 ,  5 , NUMSTRING },
   { "ModePage3"       , 0 , 23 , NUMSTRING },
   { "WritPage3"       , 0 , 23 , NUMSTRING },
   { "ModePage4"       , 0 ,  6 , NUMSTRING},
   /* The vendor pages are whatever the drive's own .cfg says, and real
      drives differ: some drives' pages 35, 37 and 38 are 4, 8 and 8
      bytes.  The page's own length is what is stored and sent, so a
      larger limit costs only the zero-padded allocation. */
   { "ModePage32"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "ModePage33"      , 0 , VENDOR_PAGE_MAX , NUMSTRING },
   { "ModePage35"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "ModePage36"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "ModePage37"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "ModePage38"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   /* Each page's changeable mask (MaskPageN: a set bit may be changed by MODE SELECT; WritPage1
      and WritPage3 are the old names, read when there is no MaskPage line) and factory defaults
      (FDefPageN), and how a host asks for them: PageControl=CCS for a drive with no page control
      field (an early CCS drive answers pages 3D/3E/3F with every page's factory, changeable
      and current values), SCSI (or absent) for page control 1-3. */
   { "MaskPage1"       , 0 , 5 , NUMSTRING},
   { "MaskPage3"       , 0 , 23 , NUMSTRING},
   { "MaskPage4"       , 0 , 6 , NUMSTRING},
   { "MaskPage32"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "MaskPage33"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "MaskPage35"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "MaskPage36"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "MaskPage37"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "MaskPage38"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "FDefPage1"       , 0 , 5 , NUMSTRING},
   { "FDefPage3"       , 0 , 23 , NUMSTRING},
   { "FDefPage4"       , 0 , 6 , NUMSTRING},
   { "FDefPage32"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "FDefPage33"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "FDefPage35"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "FDefPage36"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "FDefPage37"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "FDefPage38"      , 0 , VENDOR_PAGE_MAX , NUMSTRING},
   { "PageControl"     , 0 ,  4 , STRING },
   /* A SCSI drive's track layout, TrackFormat=T,O,S in decimal, used by
      FORMAT under DriveController=SCSI (an example drive: 20384,70,1):
        physical sectors per track = T / (bytes per sector + O), rounded
        down; sectors per track = physical - S. */
   { "TrackFormat"     , 0 , 24 , STRING },
   /* The VFS (LaserDisc) keys: not used by BeebEm, but kept so a .cfg
      from Pi1MHz is read and written back unchanged. */
   { "LDUserCode"      , 0 ,  5 , STRING },
   { "LDVideoXoffset"  , -768 , 768 , INTEGER },
   { "LDVideoYoffset"  , -288 , 288 , INTEGER },
   { NULL , 0 ,0, NUMSTRING} // end of list
};

#define NUM_KEYS (sizeof(scsiattributes)/sizeof(parserkey))

/* The public enum (filesystem.h) indexes this table: pin the ordering */
static_assert(LDVIDEOYOFFSET + 2 == NUM_KEYS,
              "parserkeyvalueenum out of step with scsiattributes[]");

static struct filesystemStateStruct
{
   FILE *fileObject[MAX_LUNS];         // LUN image files, open while the LUN is started
   bool fsLunStatus[MAX_LUNS];         // LUN image availability flags (true = started, false = stopped)
   bool fsLunReadOnly[MAX_LUNS];       // Image is read-only: the Beeb's writes to it are swallowed
   struct HDGeometry fsLunGeometry[MAX_LUNS];   // Keep the geometry details for each LUN
   parserkeyvalue keyvalues[MAX_LUNS][NUM_KEYS];   // keys from .cfg file for each LUN
   uint32_t driveGeneration;           // changes when LUNs 0-7 are no longer the same drives
} filesystemState;

static char hardDriveFolder[260];      // where the drives are
static char userDataFolder[260];       // the user data folder, for defscsi.cfg

static char fileName[300];             // String for storing a filename

static uint8_t sectorBuffer[SECTOR_BUFFER_SIZE];   // Buffer for reading sectors

// Globals for multi-sector reading
static uint32_t sectorsInBuffer = 0;
static uint32_t currentBufferSector = 0;
static uint32_t sectorsRemaining = 0;

// C library file functions, with 64-bit offsets ----------------------------

static bool fsSeek(FILE *file, uint64_t offset)
{
#ifdef _WIN32
   return _fseeki64(file, (__int64)offset, SEEK_SET) == 0;
#else
   return fseeko(file, (off_t)offset, SEEK_SET) == 0;
#endif
}

static uint64_t fsSize(FILE *file)
{
#ifdef _WIN32
   __int64 position = _ftelli64(file);
   _fseeki64(file, 0, SEEK_END);
   __int64 size = _ftelli64(file);
   _fseeki64(file, position, SEEK_SET);
#else
   off_t position = ftello(file);
   fseeko(file, 0, SEEK_END);
   off_t size = ftello(file);
   fseeko(file, position, SEEK_SET);
#endif
   return size < 0 ? 0 : (uint64_t)size;
}

static bool fsStat(const char *name, uint64_t *size)
{
#ifdef _WIN32
   struct _stat64 info;
   if (_stat64(name, &info) != 0)
      return false;
#else
   struct stat info;
   if (stat(name, &info) != 0)
      return false;
#endif
   if (size != NULL)
      *size = (uint64_t)info.st_size;
   return true;
}

static bool fsRename(const char *from, const char *to)
{
#ifdef _WIN32
   return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
#else
   return rename(from, to) == 0;
#endif
}

static void fsPath(char *buf, size_t size, const char *folder, const char *name)
{
   size_t length = strlen(folder);
   if ((length == 0) || (folder[length - 1] == '\\') || (folder[length - 1] == '/'))
      snprintf(buf, size, "%s%s", folder, name);
   else
#ifdef _WIN32
      snprintf(buf, size, "%s\\%s", folder, name);
#else
      snprintf(buf, size, "%s/%s", folder, name);
#endif
}

// A LUN's file: scsi<lun>.<ext> in the hard drive folder
static void fsLunFilePath(uint8_t lunNumber, const char *ext, char *buf, size_t size)
{
   char name[32];
   snprintf(name, sizeof name, "scsi%d.%s", lunNumber, ext);
   fsPath(buf, size, hardDriveFolder, name);
}

// The default descriptor: defscsi.cfg in the hard drive folder, else the
// one in the user data folder
static const char *fsDefaultDescriptor(void)
{
   static char name[300];
   fsPath(name, sizeof name, hardDriveFolder, "defscsi.cfg");
   if (!fsStat(name, NULL))
      fsPath(name, sizeof name, userDataFolder, "defscsi.cfg");
   return name;
}

void filesystemSetPaths(const char *hardDrivePath, const char *userDataPath)
{
   snprintf(hardDriveFolder, sizeof hardDriveFolder, "%s", hardDrivePath);
   snprintf(userDataFolder, sizeof userDataFolder, "%s", userDataPath);
}

// Function to initialise the file system control functions
void filesystemInitialise(void)
{
   memset(&filesystemState, 0, sizeof filesystemState);
}

// Stop every LUN: the drives may have changed (a reset, or another hard
// drive folder), so anything kept for one is stale
void filesystemReset(void)
{
   for (uint8_t i = 0; i < MAX_LUNS; i++)
   {
      filesystemSetLunStatus(i, false);
      parse_releasekeyvalues(filesystemState.keyvalues[i], NUM_KEYS);
   }
   filesystemState.driveGeneration++;
}

// Function to set the status of a LUN image
bool filesystemSetLunStatus(uint8_t lunNumber, bool lunStatus)
{
   // Is the requested status the same as the current status?
   if (filesystemState.fsLunStatus[lunNumber] == lunStatus) {
      return true;
   }

   // Transitioning from stopped to started?
   if (lunStatus == true) {
      // If the LUN image is starting the file system needs to recheck the LUN and LUN
      // descriptor to ensure everything is up to date

      // Check that the LUN image exists
      if (!filesystemCheckLunImage(lunNumber)) {
         return false;
      }

      // Exit with success
      filesystemState.fsLunStatus[lunNumber] = true;
      return true;
   }

   // Transitioning from started to stopped
   if (filesystemState.fileObject[lunNumber] != NULL) {
      fclose(filesystemState.fileObject[lunNumber]);
      filesystemState.fileObject[lunNumber] = NULL;
   }
   parse_releasekeyvalues(filesystemState.keyvalues[lunNumber], NUM_KEYS);
   filesystemState.fsLunStatus[lunNumber] = false;

   // Exit with success
   return true;
}

// Function to read the status of a LUN image
bool filesystemReadLunStatus(uint8_t lunNumber)
{
   return filesystemState.fsLunStatus[lunNumber];
}

// Function to scan for SCSI LUN image file on the file system and check
// the image is valid.
bool filesystemCheckLunImage(uint8_t lunNumber)
{
   if (filesystemState.fsLunStatus[lunNumber])
   {
      return true;
   }

   // Attempt to open the LUN image
   fsLunFilePath(lunNumber, "dat", fileName, sizeof(fileName));

   if (filesystemState.fileObject[lunNumber] != NULL) {
      fclose(filesystemState.fileObject[lunNumber]);
      filesystemState.fileObject[lunNumber] = NULL;
   }

   filesystemState.fileObject[lunNumber] = fopen(fileName, "rb+");

   /* An image marked read-only: start it read-only rather than hand the
      host BAD_FORMAT.  Its writes are then swallowed and reported as done
      (fsLunReadOnly). */
   filesystemState.fsLunReadOnly[lunNumber] = false;
   if ((filesystemState.fileObject[lunNumber] == NULL) && (errno == EACCES)) {
      filesystemState.fileObject[lunNumber] = fopen(fileName, "rb");
      filesystemState.fsLunReadOnly[lunNumber] = (filesystemState.fileObject[lunNumber] != NULL);
   }

   if (filesystemState.fileObject[lunNumber] == NULL) {
      // Exit with error
      return false;
   }

   // Get the size of the LUN image in bytes
   uint64_t lunFileSize = fsSize(filesystemState.fileObject[lunNumber]);

   // The started flag itself belongs to filesystemSetLunStatus(), the one
   // owner of the stopped<->started transition; this function only opens
   // the image and loads its descriptor on that owner's behalf.
   if (!filesystemReadLunDescriptor( lunNumber)) {
      filesytemdattoconfigGeometry(lunNumber);
   }

   // The file size and the descriptor may disagree (an image not yet grown
   // to full size): the descriptor wins
   (void)lunFileSize;

   // Exit with success
   return true;
}

// Function to return the LUN image Block size in bytes from the stored geometry
//
uint32_t filesystemGetLunBlockSize( uint8_t lunNumber)
{
   return filesystemState.fsLunGeometry[lunNumber].BlockSize;
}

// The size of the blocks READ and WRITE move for a LUN: its block size, as
// FORMAT laid it down.  filesystemConfigToLunGeometry has already made it
// one the data path can take, so it is the size READ CAPACITY reports too.
uint32_t filesystemGetLunDataBlockSize( uint8_t lunNumber)
{
   return filesystemState.fsLunGeometry[lunNumber].BlockSize;
}

// Function to return the LUN image heads per cylinder
//
uint32_t filesystemGetheadspercylinder( uint8_t lunNumber)
{
   return filesystemState.fsLunGeometry[lunNumber].Heads;
}

// Function to return the LUN image cylinders
//
uint32_t filesystemGetLunCylinders( uint8_t lunNumber)
{
   return filesystemState.fsLunGeometry[lunNumber].Cylinders;
}

// Changes whenever LUNs 0-7 stop being the same drives (a reset).
// Anything kept for a drive (scsi.cpp's pending pages) is stale after it.
uint32_t filesystemGetDriveGeneration(void)
{
   return filesystemState.driveGeneration;
}

// Function to return the LUN image sectors per track size in bytes from the stored geometry
//
uint32_t filesystemGetLunSPTSize( uint8_t lunNumber)
{
   return filesystemState.fsLunGeometry[lunNumber].SectorsPerTrack;
}

// Function to return the LUN image size in bytes from the stored geometry
//
uint32_t filesystemGetLunTotalBytes( uint8_t lunNumber)
{
   // pointer to the geometry data of the LUN
   const struct HDGeometry* ptr = &filesystemState.fsLunGeometry[lunNumber];

   // Tracks = Cylinders * Heads
   // Total Sectors = Tracks * Sectors per Track
   // Total Bytes = Total Sectors * Block Size (block size is normally 256 bytes)
   return (((*ptr).Heads * (*ptr).Cylinders) * (*ptr).SectorsPerTrack) * (*ptr).BlockSize;
}

// Function to return the LUN image size in sectors from the stored geometry
//
uint32_t filesystemGetLunTotalSectors( uint8_t lunNumber)
{
   // pointer to the geometry data of the LUN
   const struct HDGeometry* ptr = &filesystemState.fsLunGeometry[lunNumber];

   // Tracks = Cylinders * Heads
   // Total Sectors = Tracks * Sectors per Track
   return (((*ptr).Cylinders * (*ptr).Heads) * (*ptr).SectorsPerTrack);
}

// Functions for creating LUNs and LUN descriptors ---------------------------------------------------------------------------

// Function to create a new LUN image (makes an empty .dat file)
bool filesystemCreateLunImage(uint8_t lunNumber)
{
   // Assemble the .dat file name
   fsLunFilePath(lunNumber, "dat", fileName, sizeof(fileName));

   // Existence is a question for stat: filesystemCheckLunImage would
   // open the image and load its descriptor for a LUN that stays stopped
   if (fsStat(fileName, NULL)) {
      return true;
   }

   // Create a new .dat file
   FILE *file = fopen(fileName, "wb");
   if (file == NULL) {
      // Create .dat file failed
      return false;
   }

   fclose(file);
   // LUN .dat file created successfully
   return true;
}

// Function to create a new LUN descriptor (makes an default .cfg file)
bool filesystemCreateLunDescriptor(uint8_t lunNumber)
{
   // Assemble the .cfg file name
   fsLunFilePath(lunNumber, "cfg", fileName, sizeof(fileName));
   // release any values from a previous parse so re-parsing doesn't leak them
   parse_releasekeyvalues(filesystemState.keyvalues[lunNumber], NUM_KEYS);
   if (parse_readfile(fileName, 0, scsiattributes, filesystemState.keyvalues[lunNumber]))
      return true;

   if (parse_readfile(fsDefaultDescriptor(), fileName, scsiattributes, filesystemState.keyvalues[lunNumber]))
   {
      // now parse the new file
      parse_releasekeyvalues(filesystemState.keyvalues[lunNumber], NUM_KEYS);
      parse_readfile(fileName, 0, scsiattributes, filesystemState.keyvalues[lunNumber]);
      return true;
   }
   return false;
}

// Function to read a LUN descriptor
bool filesystemReadLunDescriptor(uint8_t lunNumber)
{
   if (!filesystemCheckExtAttributes(lunNumber))
   {
      // Check if the LUN descriptor file (.dsc) is present
      fsLunFilePath(lunNumber, "dsc", fileName, sizeof(fileName));

      FILE *file = fopen(fileName, "rb");

      if (file == NULL) {
         // LUN descriptor file is not found
         return false;
      } else {
         // LUN descriptor file is present

         // pointer to the geometry data of the currently selected LUN
         struct HDGeometry* ptr = &filesystemState.fsLunGeometry[lunNumber];

         uint8_t Buffer[22];
         // Read the DSC data
         size_t fsCounter = fread(Buffer, 1, 22, file);

         // Check that the file was read OK and is the correct length
         if (fsCounter != 22) {
            // Something went wrong
            fclose(file);
            return false;
         }

         // read the parameters into the cache
         (*ptr).BlockSize 	= (((uint32_t)Buffer[9] << 16) |
                              ((uint32_t)Buffer[10] << 8) |
                                 (uint32_t)Buffer[11]);

         (*ptr).Cylinders 	= (((uint32_t)Buffer[13] << 8) |
                                 (uint32_t)Buffer[14]);

         (*ptr).Heads 		=  (uint8_t)Buffer[15];

         (*ptr).SectorsPerTrack = DEFAULT_SECTORS_PER_TRACK;			// .dsc files don't contain sectors per track, always assume the default

         filesystemLunToconfigGeometry(lunNumber);
         fclose(file);
      }
      return true;
   }
   return true;
}

// Function to write a LUN descriptor
bool filesystemWriteAttributes(uint8_t lunNumber)
{
   // Assemble the .cfg file name
   fsLunFilePath(lunNumber, "cfg", fileName, sizeof(fileName));

   if (parse_readfile(fileName, fileName, scsiattributes, filesystemState.keyvalues[lunNumber]))
   {
      return true;
   }

   // assume no cfg file so create one
   if (parse_readfile(fsDefaultDescriptor(), fileName, scsiattributes, filesystemState.keyvalues[lunNumber]))
   {
      return true;
   }

   return false;
}

// FORMAT's geometry (the controller's rules are in scsi.cpp).  Start the LUN
// for its current .cfg first; false: leave it alone (it cannot start -
// FORMAT then says so).
bool filesystemStartLunForFormat(uint8_t lunNumber)
{
   return filesystemSetLunStatus(lunNumber, true);
}

// Save the geometry FORMAT lays down into the LUN's .cfg, before
// filesystemFormatLun sizes the image from it: sectors per track (ModePage3
// bytes 10-11; a .cfg without a ModePage3 line keeps the default 33 at its
// next start), and a block size other than 0 (LBADescriptor).  An
// interleave other than 0 also goes into page 3 with the block size (bytes
// 12-15), as a drive with its own controller keeps them.  A read-only
// image is left alone.  Returns false if the .cfg cannot be saved.
bool filesystemSaveFormatGeometry(uint8_t lunNumber, uint16_t sectorsPerTrack, uint32_t blockSize,
                                  uint16_t interleave)
{
   parserkeyvalue *page3 = &filesystemState.keyvalues[lunNumber][MODEPAGE3];

   if (filesystemState.fsLunReadOnly[lunNumber])
      return true;
   if ((page3->v.string != NULL) && (page3->length >= 12)) {
      page3->v.string[10] = (char)(sectorsPerTrack >> 8);
      page3->v.string[11] = (char)sectorsPerTrack;
      if ((interleave != 0) && (page3->length >= 16)) {
         page3->v.string[12] = (char)(blockSize >> 8);
         page3->v.string[13] = (char)blockSize;
         page3->v.string[14] = (char)(interleave >> 8);
         page3->v.string[15] = (char)interleave;
      }
   }
   if (blockSize != 0)
      filesystemSetLunBlockSize(lunNumber, blockSize);
   filesystemState.fsLunGeometry[lunNumber].SectorsPerTrack = sectorsPerTrack;
   return filesystemWriteAttributes(lunNumber);
}

// Function to format a LUN image
bool filesystemFormatLun(uint8_t lunNumber, uint8_t dataPattern)
{
   // The size below comes from fsLunGeometry, which is only current for a
   // started LUN (loaded at START, updated by MODE SELECT).  Starting the
   // LUN loads its geometry exactly as the START after this FORMAT will;
   // if it cannot start there is no size to format to, so refuse before
   // anything is truncated.
   if (!filesystemSetLunStatus(lunNumber, true)) {
      return false;
   }
   uint64_t totalBytes = filesystemGetLunTotalBytes(lunNumber);
   filesystemSetLunStatus(lunNumber, false );

   // A read-only image (just found so by that start): ignore FORMAT and
   // report success
   if (filesystemState.fsLunReadOnly[lunNumber]) return true;

   // Assemble the .dat file name
   fsLunFilePath(lunNumber, "dat", fileName, sizeof(fileName));

   // Create the .dat file (the old .dat file, if present, is replaced),
   // filled with the data pattern
   FILE *file = fopen(fileName, "wb");
   if (file == NULL) {
      return false;
   }

   static uint8_t pattern[SECTOR_BUFFER_SIZE];
   memset(pattern, dataPattern, sizeof pattern);

   bool ok = true;
   for (uint64_t done = 0; ok && (done < totalBytes); ) {
      size_t chunk = (totalBytes - done) > sizeof pattern ? sizeof pattern : (size_t)(totalBytes - done);
      ok = fwrite(pattern, 1, chunk, file) == chunk;
      done += chunk;
   }

   if (fclose(file) != 0)
      ok = false;

   return ok;
}

// Check an extended attributes file is available for that LUN
// and register it
bool filesystemCheckExtAttributes( uint8_t lunNumber)
{
   char extAttributes_fileName[300];
   fsLunFilePath(lunNumber, "cfg", extAttributes_fileName, sizeof(extAttributes_fileName));

   // release any values from a previous parse: this runs on every MODE SENSE
   // and TRANSLATE, and re-parsing without freeing leaked the whole key set
   parse_releasekeyvalues(filesystemState.keyvalues[lunNumber], NUM_KEYS);
   if (parse_readfile(extAttributes_fileName, 0, scsiattributes, filesystemState.keyvalues[lunNumber]))
   {
      // LUN extended attributes file is present
      filesystemConfigToLunGeometry(lunNumber);
      return true;
   }
   else
   {
      parse_readfile(fsDefaultDescriptor(), 0, scsiattributes, filesystemState.keyvalues[lunNumber]);

      // LUN extended attributes file is not present
      filesystemConfigToLunGeometry(lunNumber);
   }

   return false;
}

/* ModePage0 is the Adaptec's drive parameter list on an ACB or SASI board,
   and carries the geometry.  On a drive with its own controller page 0 is
   an ordinary mode page (a CCS drive's Unit Attention page: some drives
   hold 80 02 00 00, others have none) and the geometry is page 4 alone. */
static bool page0IsDriveList = true;

/* DriveGeometry=CFG (true): a .cfg without its geometry page gets none
   made up; Auto (false, the default) deduces it.  Set by scsi.cpp. */
static bool geometryFromCfgOnly = false;

void filesystemSetGeometryFromCfgOnly(bool cfgOnly)
{
   geometryFromCfgOnly = cfgOnly;
}

void filesystemSetPage0IsDriveList(bool driveList)
{
   page0IsDriveList = driveList;
}

static bool filesystemPage0IsDriveList(void)
{
   return page0IsDriveList;
}

// A key's value, allocated to the key's maximum length if it has none
static char *filesystemKeyValue(uint8_t lunNumber, int index, size_t length)
{
   parserkeyvalue *kv = &filesystemState.keyvalues[lunNumber][index];
   if (!kv->v.string)
   {
      size_t alloc = ((size_t)scsiattributes[index].max > length) ? (size_t)scsiattributes[index].max : length;
      kv->v.string = (char *)malloc(alloc);
      if (!kv->v.string)
         return NULL;
      memset(kv->v.string, 0, alloc);
      kv->length = length;
   }
   return kv->v.string;
}

// takes lun geometry and updates the keyvalues
void filesystemLunToconfigGeometry(uint8_t lunNumber)
{
   int index;
   const struct HDGeometry *g = &filesystemState.fsLunGeometry[lunNumber];
   // Update the extended attributes from the cached geometry for this LUN
   index = MODEPAGE0;
   if (filesystemPage0IsDriveList()) {
      char *page0 = filesystemKeyValue(lunNumber, index, 10);
      if (!page0)
         return;   /* alloc failed: do not dereference NULL below */

      page0[0] = 1 ;
      page0[1] = (char)(g->Cylinders>>8); // cylinder count MSB
      page0[2] = (char)g->Cylinders; // cylinder count LSB
      page0[3] = (char)g->Heads; // Heads
      page0[4] = 0x00; // Reduced write current MSB
      page0[5] = (char)0x80; // Reduced write current LSB
      page0[6] = 0x00; // Write precompensation MSB
      page0[7] = (char)0x80; // Write precompensation LSB
      page0[8] = 0x00; // Landing zone
      page0[9] = 0x01; // Step pulse count
      if (filesystemState.keyvalues[lunNumber][index].length < 10)
         filesystemState.keyvalues[lunNumber][index].length = 10;
   }

   index = MODEPAGE4;
   char *page4 = filesystemKeyValue(lunNumber, index, 6);
   if (!page4)
      return;   /* alloc failed: do not dereference NULL below */
   page4[0] = 04 ;
   page4[1] = 04 ;
   page4[2] = 0 ;
   page4[3] = (char)(g->Cylinders>>8); // cylinder count MSB
   page4[4] = (char)g->Cylinders; // cylinder count LSB
   page4[5] = (char)g->Heads; // Heads
   if (filesystemState.keyvalues[lunNumber][index].length < 6)
      filesystemState.keyvalues[lunNumber][index].length = 6;
}

/* The geometry from the image's size alone: sectors per track from page 3
   (or the default), heads the largest divisor up to 16, cylinders the rest.
   A guess, not the drive's: only for a .cfg without its geometry page. */
static void filesystemGeometryFromImage(uint8_t lunNumber)
{
   uint64_t lunFileSize = 0;
   if (filesystemState.fsLunStatus[lunNumber] && filesystemState.fileObject[lunNumber]) {
      lunFileSize = fsSize(filesystemState.fileObject[lunNumber]);
   } else {
      char datName[300];
      fsLunFilePath(lunNumber, "dat", datName, sizeof(datName));
      if (!fsStat(datName, &lunFileSize))
         lunFileSize = 0;
   }

   struct HDGeometry *g = &filesystemState.fsLunGeometry[lunNumber];
   uint32_t tracks = (uint32_t)(lunFileSize / ((uint64_t)g->SectorsPerTrack * g->BlockSize));
   uint8_t heads = 16;

   while ((tracks % heads != 0) && heads > 1) heads--;
   uint32_t cylinders = tracks / heads;

   g->Cylinders = cylinders;
   g->Heads = heads;
}

void filesytemdattoconfigGeometry(uint8_t lunNumber)
{
   filesystemGeometryFromImage(lunNumber);
   filesystemLunToconfigGeometry(lunNumber);
}

//
// Transfer key Values from attributes to the LUN geometry
//
void filesystemConfigToLunGeometry(uint8_t lunNumber)
{
   struct HDGeometry *g = &filesystemState.fsLunGeometry[lunNumber];
   parserkeyvalue *kv = filesystemState.keyvalues[lunNumber];

   int index = MODEPARAMHEADER;
   if  (!kv[index].v.string)
   {
      // now recreate the ModeParamHeader
      char *header = filesystemKeyValue(lunNumber, index, 4);
      if (!header)
         return;   /* alloc failed: do not dereference NULL below */
      header[0] = 0x00; // Reserved
      header[1] = 0x00; // Reserved
      header[2] = 0x00; // Reserved
      header[3] = 0x08; // Block descriptor length
      kv[index].length = 4;
   }

   // Update the cached geometry for this LUN from the extended attributes
   index = LBADESCRIPTOR;
   if  (kv[index].v.string)
   {
      g->BlockSize = (uint32_t)(((uint8_t)kv[index].v.string[5] << 16) +
                                ((uint8_t)kv[index].v.string[6] << 8) +
                                 (uint8_t)kv[index].v.string[7]);
   }
   else
   {
      g->BlockSize = DEFAULT_BLOCK_SIZE;
      //now recreate the LBADescriptor
      char *lba = filesystemKeyValue(lunNumber, index, 8);
      if (!lba)
         return;   /* alloc failed: do not dereference NULL below */
      lba[5] = (char)(DEFAULT_BLOCK_SIZE >>16); // block size MSB
      lba[6] = (char)(DEFAULT_BLOCK_SIZE >>8); // block size
      lba[7] = (char)(DEFAULT_BLOCK_SIZE); // block size LSB
      kv[index].length = 8;
   }

   // A short or hand-edited cfg value can decode as zero (a divide by zero
   // in the geometry calculations) or as a size the data path cannot move.
   // Use 256 then, and say so in the LBA descriptor too, so READ CAPACITY,
   // MODE SENSE, FORMAT and the data path all agree.
   uint32_t blockSize = g->BlockSize;
   if ((blockSize < MIN_BLOCK_SIZE) || (blockSize > MAX_BLOCK_SIZE))
   {
      g->BlockSize = DEFAULT_BLOCK_SIZE;
      if (kv[index].length >= 8)
      {
         kv[index].v.string[5] = (char)(DEFAULT_BLOCK_SIZE >>16);
         kv[index].v.string[6] = (char)(DEFAULT_BLOCK_SIZE >>8);
         kv[index].v.string[7] = (char)(DEFAULT_BLOCK_SIZE);
      }
   }

   index = MODEPAGE3;
   if  (kv[index].v.string)
   {
      g->SectorsPerTrack = (uint16_t)((((uint8_t)kv[index].v.string[10] << 8) +
                                        (uint8_t)kv[index].v.string[11]));
   }
   else
   {
      g->SectorsPerTrack = DEFAULT_SECTORS_PER_TRACK;
   }
   if (g->SectorsPerTrack == 0)
      g->SectorsPerTrack = DEFAULT_SECTORS_PER_TRACK;

   index = MODEPAGE0;
   if  (filesystemPage0IsDriveList() && kv[index].v.string)
   {
      g->Cylinders = (uint32_t)((((uint8_t)kv[index].v.string[1] << 8) +
                                  (uint8_t)kv[index].v.string[2]));
      g->Heads =     (uint8_t)   (kv[index].v.string[3]);
   }
   else
   {
      // check if page4 is present
      index = MODEPAGE4;
      if  (kv[index].v.string)
      {
         g->Cylinders = (uint32_t)((((uint8_t)kv[index].v.string[3] << 8) +
                                     (uint8_t)kv[index].v.string[4]));
         g->Heads =     (uint8_t)   (kv[index].v.string[5]);

         filesystemCopyPage4toPage0(lunNumber);
      } else
      {
         // The .cfg has no geometry page (page 4 on a drive with its own
         // controller, page 0 or 4 otherwise).  DriveGeometry=Auto: deduce
         // it from the image size and publish it as the page; CFG: invent
         // nothing, a host asking for the page is refused as by a drive
         // without it.
         filesystemGeometryFromImage(lunNumber);
         if (!geometryFromCfgOnly)
            filesystemLunToconfigGeometry(lunNumber);
      }
   }
}

// The LUN's block size, into LBADescriptor (bytes 5-7) and the geometry
void filesystemSetLunBlockSize(uint8_t lunNumber, uint32_t blockSize)
{
   char *lba = filesystemState.keyvalues[lunNumber][LBADESCRIPTOR].v.string;
   if (lba == NULL) {
      filesystemConfigToLunGeometry(lunNumber);   // makes a default one
      lba = filesystemState.keyvalues[lunNumber][LBADESCRIPTOR].v.string;
      if (lba == NULL)
         return;
   }
   lba[5] = (char)(blockSize >> 16);
   lba[6] = (char)(blockSize >> 8);
   lba[7] = (char)blockSize;
   filesystemState.fsLunGeometry[lunNumber].BlockSize = blockSize;
}

void filesystemCopyPage0toPage4(uint8_t lunNumber)
{
   if (!filesystemPage0IsDriveList())
      return;
   struct HDGeometry *g = &filesystemState.fsLunGeometry[lunNumber];
   parserkeyvalue *kv = filesystemState.keyvalues[lunNumber];
   int index = MODEPAGE0;
   if (kv[index].v.string)
   {
      g->Cylinders = (uint32_t)((((uint8_t)kv[index].v.string[1] << 8) +
                                  (uint8_t)kv[index].v.string[2]));
      g->Heads =     (uint8_t)   (kv[index].v.string[3]);
      // now recreate Page4
      index = MODEPAGE4;
      if  (kv[index].v.string)
      {
         kv[index].v.string[2] = 0 ;
         kv[index].v.string[3] = (char)(g->Cylinders>>8); // cylinder count MSB
         kv[index].v.string[4] = (char)g->Cylinders; // cylinder count LSB
         kv[index].v.string[5] = (char)g->Heads; // Heads
         if (kv[index].length < 6)
            kv[index].length = 6;
      }
   }
}

void filesystemCopyPage4toPage0(uint8_t lunNumber)
{
   if (!filesystemPage0IsDriveList())
      return;
   struct HDGeometry *g = &filesystemState.fsLunGeometry[lunNumber];
   parserkeyvalue *kv = filesystemState.keyvalues[lunNumber];
   int index = MODEPAGE4;
   if (kv[index].v.string)
   {
      g->Cylinders = (uint32_t)((((uint8_t)kv[index].v.string[3] << 8) +
                                  (uint8_t)kv[index].v.string[4]));
      g->Heads =     (uint8_t)   (kv[index].v.string[5]);
      // now recreate Page0
      index = MODEPAGE0;
      if  (kv[index].v.string)
      {
         kv[index].v.string[1] = (char)(g->Cylinders>>8); // cylinder count MSB
         kv[index].v.string[2] = (char)g->Cylinders; // cylinder count LSB
         kv[index].v.string[3] = (char)g->Heads; // Heads
         if (kv[index].length < 4)
            kv[index].length = 4;
      }
   }
}

// Functions for reading and writing LUN images --------------------------------------------------------------------

// Function to open a LUN ready for reading
// Note: The read functions use a multi-sector buffer to lower the number of required
// reads from the physical media.
bool filesystemOpenLunForRead(uint8_t lunNumber, uint32_t startSector, uint32_t requiredNumberOfSectors)
{
   FILE *file = filesystemState.fileObject[lunNumber];
   if (file == NULL)
      return false;

   // Reads past the end of a growing image return zeros for the unwritten
   // sectors: clamp the seek to the file size
   uint64_t seekPoint = (uint64_t)startSector * filesystemGetLunDataBlockSize(lunNumber);
   uint64_t size = fsSize(file);
   if (seekPoint > size)
      seekPoint = size;

   if (!fsSeek(file, seekPoint)) {
      // Something went wrong with seeking, do not retry
      return false;
   }
   sectorsRemaining = requiredNumberOfSectors;
   sectorsInBuffer = 0;

   // Exit with success
   filesystemState.fsLunStatus[lunNumber] = true;
   return true;
}

// Function to read next sector from a LUN
bool filesystemReadNextSector(uint8_t lunNumber, uint8_t **buffer)
{
   // The LUN's block size: as many whole blocks as the buffer holds
   uint32_t blockSize = filesystemGetLunDataBlockSize(lunNumber);

   if (sectorsInBuffer == 0)
   {
      uint32_t sectorsToRead = sectorsRemaining;
      if (sectorsToRead > SECTOR_BUFFER_SIZE / blockSize) sectorsToRead = SECTOR_BUFFER_SIZE / blockSize;

      // Read the required data into the sector buffer
      size_t fsCounter = fread(sectorBuffer, 1, sectorsToRead * blockSize, filesystemState.fileObject[lunNumber]);

      // Check that the file was read OK
      if (ferror(filesystemState.fileObject[lunNumber])) {
         // Something went wrong
         clearerr(filesystemState.fileObject[lunNumber]);
         return false;
      }

      // A short read means the host is reading past the end of an image that
      // has not been grown to full geometry yet - unwritten sectors read as
      // zeros rather than whatever the buffer last held
      if (fsCounter < sectorsToRead * blockSize) {
         memset(sectorBuffer + fsCounter, 0, (sectorsToRead * blockSize) - fsCounter);
      }

      sectorsInBuffer = sectorsToRead;
      currentBufferSector = 0;
      sectorsRemaining = sectorsRemaining - sectorsInBuffer;
   }
   // return pointer to buffer with sector
   *buffer = sectorBuffer + (currentBufferSector * blockSize);
   currentBufferSector++;
   sectorsInBuffer--;

   // Exit with success
   return true;
}

// Function to close a LUN for reading
bool filesystemCloseLunForRead(uint8_t lunNumber)
{
   filesystemSetLunStatus(lunNumber, false);
   return true;
}

// Function to open a LUN ready for writing
bool filesystemOpenLunForWrite(uint8_t lunNumber, uint32_t startSector, uint32_t requiredNumberOfSectors)
{
   // A read-only image: nothing to seek or grow - filesystemWriteNextSector()
   // swallows the data
   if (filesystemState.fsLunReadOnly[lunNumber])
      return true;

   FILE *file = filesystemState.fileObject[lunNumber];
   if (file == NULL)
      return false;

   uint64_t seekPoint = (uint64_t)startSector * filesystemGetLunDataBlockSize(lunNumber);

   // Move to the correct point in the DAT file (a write past the end of a
   // growing image extends it)
   if (!fsSeek(file, seekPoint)) {
      // Something went wrong with seeking, do not retry
      return false;
   }

   sectorsRemaining = requiredNumberOfSectors;
   currentBufferSector = 0;

   // Exit with success
   return true;
}

// Function to write next sector to a LUN
bool filesystemWriteNextSector(uint8_t lunNumber, uint8_t const buffer[])
{
   // A read-only image: swallow the sector, report success, write nothing.
   // Returning true (never false) keeps a mounted ADFS from seeing an error.
   if (filesystemState.fsLunReadOnly[lunNumber]) return true;

   // The LUN's block size: as many whole blocks as the buffer holds
   uint32_t blockSize = filesystemGetLunDataBlockSize(lunNumber);
   uint32_t bufferSectors = SECTOR_BUFFER_SIZE / blockSize;

   memcpy(sectorBuffer + (currentBufferSector * blockSize), buffer , blockSize );
   currentBufferSector++;

   if ( (currentBufferSector == bufferSectors) || ( currentBufferSector == sectorsRemaining)) {
      uint32_t sectorsToWrite;

      if (currentBufferSector == bufferSectors)
         sectorsToWrite = bufferSectors;
      else
         sectorsToWrite = sectorsRemaining;

      // Write the required data
      size_t fsCounter = fwrite(sectorBuffer, 1, sectorsToWrite * blockSize, filesystemState.fileObject[lunNumber]);

      // Check that the file was written OK and in full (a short transfer
      // here means the disc is full)
      if (fsCounter != sectorsToWrite * blockSize) {
         // Something went wrong
         return false;
      }

      currentBufferSector = 0;
      sectorsRemaining -= sectorsToWrite;

      if (sectorsRemaining == 0)
         fflush(filesystemState.fileObject[lunNumber]);
   }
   // Exit with success
   return true;
}

// Function to close a LUN for writing
bool filesystemCloseLunForWrite(uint8_t lunNumber)
{
   filesystemSetLunStatus(lunNumber, false);
   return true;
}

// Functions for reading SCSI attributes -------------------------------------------------------------

char * filesystemGetInquiryData(uint8_t lunNumber, size_t * length)
{
   int index = INQUIRY;
   *length = filesystemState.keyvalues[lunNumber][index].length;
   return filesystemState.keyvalues[lunNumber][index].v.string;
}

char * filesystemGetModeParamHeaderData(uint8_t lunNumber, size_t * length)
{
   int index = MODEPARAMHEADER;
   *length= filesystemState.keyvalues[lunNumber][index].length;
   return filesystemState.keyvalues[lunNumber][index].v.string;
}

char * filesystemGetLBADescriptorData(uint8_t lunNumber, size_t * length)
{
   int index = LBADESCRIPTOR;
   *length= filesystemState.keyvalues[lunNumber][index].length;
   return filesystemState.keyvalues[lunNumber][index].v.string;
}

/* The index of the key PREFIX + page number (e.g. "MaskPage3"), or -1. */
static int filesystemPageKeyIndex(const char * prefix, uint8_t page)
{
   char key[16];
   snprintf(key, sizeof key, "%s%u", prefix, (unsigned)(page & 0x3f));
   return parse_findindex(key, scsiattributes);
}

// takes a page number and returns the index of the mode page
static int filesystemPagetoIndex( uint8_t page)
{
   return filesystemPageKeyIndex("ModePage", page);
}

/* A page's changeable mask: MaskPageN, or the old name WritPageN when the
   .cfg has no MaskPage line for it; -1 if there is neither key. */
static int filesystemMaskIndex(uint8_t lunNumber, uint8_t page)
{
   int index = filesystemPageKeyIndex("MaskPage", page);
   if (index != -1 && filesystemState.keyvalues[lunNumber][index].v.string != NULL)
      return index;
   int old = filesystemPageKeyIndex("WritPage", page);
   return (old != -1) ? old : index;
}

// returns the mode page data for a given page
char * filesystemGetModePageData(uint8_t lunNumber, uint8_t page, size_t * length)
{
   int index = filesystemPagetoIndex(page);
   if (index == -1)
      return 0;
   *length= filesystemState.keyvalues[lunNumber][index].length;
   return filesystemState.keyvalues[lunNumber][index].v.string;
}

/* A page's changeable mask (MaskPageN, the page as a drive returns it for
   "changeable values": page code, length, then 1 = changeable), or NULL. */
char * filesystemGetMaskPageData(uint8_t lunNumber, uint8_t page, size_t * length)
{
   int index = filesystemMaskIndex(lunNumber, page);
   if (index == -1)
      return 0;
   *length = filesystemState.keyvalues[lunNumber][index].length;
   return filesystemState.keyvalues[lunNumber][index].v.string;
}

/* A page's factory defaults (FDefPageN), or NULL. */
char * filesystemGetFDefPageData(uint8_t lunNumber, uint8_t page, size_t * length)
{
   int index = filesystemPageKeyIndex("FDefPage", page);
   if (index == -1)
      return 0;
   *length = filesystemState.keyvalues[lunNumber][index].length;
   return filesystemState.keyvalues[lunNumber][index].v.string;
}

/* PageControl=CCS: the drive has no page control field (MODE SENSE pages
   3D/3E/3F give factory, changeable and current values). */
bool filesystemPageControlCCS(uint8_t lunNumber)
{
   const char * s = filesystemState.keyvalues[lunNumber][PAGECONTROL].v.string;
   return (s != NULL) &&
          ((s[0] | 0x20) == 'c') && ((s[1] | 0x20) == 'c') && ((s[2] | 0x20) == 's');
}

// Writes the mode page data for a given page
int filesystemWriteModePageData(uint8_t lunNumber, uint8_t page, uint8_t len, const uint8_t * Buffer)
{
   int index = filesystemPagetoIndex(page);
   if (index == -1)
      return 0;

   parserkeyvalue *kv = filesystemState.keyvalues[lunNumber];
   int windex = filesystemMaskIndex(lunNumber, page);
   if (windex != -1 && kv[index].v.string != NULL && kv[windex].v.string != NULL)
   {
      unsigned int i;
      for ( i=0; (i< len) && (i < kv[windex].length ); i++)
      {
         kv[index].v.string[i] = (char) ( (Buffer[i] & kv[windex].v.string[i]) |
                   ((kv[index].v.string[i] & ~kv[windex].v.string[i])));
      }
      if (kv[index].length < i)
         kv[index].length = i;
      return 1;
   }

   // Allocate at least the key's declared maximum (zero filled) so the
   // fixed-offset geometry consumers and the masked write above can never
   // overrun a page the host supplied short
   size_t alloc = len;
   if ((size_t)scsiattributes[index].max > alloc)
      alloc = (size_t)scsiattributes[index].max;
   kv[index].length = len;
   if (kv[index].v.string)
      free(kv[index].v.string);
   kv[index].v.string = (char *)malloc(alloc);
   if (kv[index].v.string == NULL) {
      kv[index].length = 0;
      return 0; /* allocation failed */
   }
   memset(kv[index].v.string, 0, alloc);
   memcpy(kv[index].v.string, Buffer, len);
   return 1;
}

/* A numeric field of a mode page: where it starts (counting the page code and
   length bytes) and how many bytes it takes. */

/* Page 3 (format parameters) and page 4 (rigid drive geometry).  The Acorn
   FileStore formatter zeroes most of page 3 before FORMAT, as a drive's
   specification asks for read-only fields.  With a mask the drive simply
   ignores those bytes; without one a zero in these fields
   is taken as "the drive's own value". */
static const modePageField_t modePage3Fields[] = {
   {2, 2}, {4, 2}, {6, 2}, {8, 2}, {10, 2}, {12, 2}, {14, 2}, {16, 2}, {18, 2}
};
static const modePageField_t modePage4Fields[] = { {2, 3}, {5, 1} };

const modePageField_t * filesystemModePageFields(uint8_t page, size_t * count)
{
   if (page == 3) {
      *count = sizeof modePage3Fields / sizeof modePage3Fields[0];
      return modePage3Fields;
   }
   if (page == 4) {
      *count = sizeof modePage4Fields / sizeof modePage4Fields[0];
      return modePage4Fields;
   }
   *count = 0;
   return NULL;
}

/* Store a mode page a host has sent, which may have been cut short.

   Buffer is the page as received: the page code, the length byte, then the
   data.  `declared` is the size the length byte promises (counting those two
   bytes) and `received` how much of it actually arrived.  A drive takes the
   bytes it is given and leaves the rest of the page as it was (as a drive's
   own firmware does: a page may be sent shorter than stored, never longer);
   the Acorn formatter relies on it, sending len+4 bytes where a whole page
   needs len+6 (FServFmt: page 3 always, pages 1 and 4 on some drives).
   zeroKeeps: a zero in a numeric field of page 3 or 4 keeps the stored
   value, so formatting cannot wipe the sectors per track, cylinders or
   heads.  keepCode: a page already held keeps its own code byte. */
static size_t filesystemMergeOnto(uint8_t page, const char * old, size_t oldLength, uint8_t declared,
                                  uint8_t received, const uint8_t * Buffer, uint8_t merged[256],
                                  bool zeroKeeps, bool keepCode)
{
   size_t total = declared;
   size_t fieldCount = 0;
   const modePageField_t * fields = zeroKeeps ? filesystemModePageFields(page, &fieldCount) : NULL;

   memset(merged, 0, (size_t)256u);
   if (old != NULL) {
      if (oldLength > (size_t)256u)
         oldLength = (size_t)256u;
      memcpy(merged, old, oldLength);
      if (oldLength > total)
         total = oldLength;
   }
   if (total > (size_t)256u)
      total = (size_t)256u;
   if (received > total)
      received = (uint8_t)total;

   /* The page code, unless the drive already has the page: then its own
      code byte stays, PS bit included (a host's MODE SELECT sends the page
      code without PS; the drive still reports it).  The length byte covers
      the whole stored page. */
   if (received > 0 && (!keepCode || oldLength < 1))
      merged[0] = Buffer[0];
   if (total >= 2)
      merged[1] = (uint8_t)(total - 2);

   for (uint8_t i = 2; i < received; i++) {
      bool inField = false;
      for (size_t f = 0; f < fieldCount; f++)
         if (i >= fields[f].start && i < fields[f].start + fields[f].size)
            inField = true;
      if (!inField)
         merged[i] = Buffer[i];
   }

   for (size_t f = 0; f < fieldCount; f++) {
      if (fields[f].start >= received)
         continue;
      unsigned int have = (unsigned int)received - fields[f].start;
      if (have > fields[f].size)
         have = fields[f].size;
      bool allZero = true;
      for (unsigned int b = 0; b < have; b++)
         if (Buffer[fields[f].start + b] != 0)
            allZero = false;
      if (!allZero)
         memcpy(&merged[fields[f].start], &Buffer[fields[f].start], have);
   }

   return total;
}

/* driveRules (a drive with its own controller): the stored code byte stays
   (PS bit included), and a page with a mask is decided by the mask alone.
   Without them a zero field of page 3 or 4 always keeps the stored value
   and the code byte is the host's, as Pi1MHz has always done. */
int filesystemMergeModePageData(uint8_t lunNumber, uint8_t page, uint8_t declared, uint8_t received,
                                const uint8_t * Buffer, bool driveRules)
{
   uint8_t merged[256];
   size_t oldLength = 0;
   const char * old = filesystemGetModePageData(lunNumber, page, &oldLength);
   size_t maskLength = 0;
   bool zeroKeeps = !driveRules || (filesystemGetMaskPageData(lunNumber, page, &maskLength) == NULL);
   size_t total = filesystemMergeOnto(page, old, oldLength, declared, received, Buffer, merged, zeroKeeps,
                                      driveRules);

   return filesystemWriteModePageData(lunNumber, page, (uint8_t)total, merged);
}

/* What a page becomes when a host sends Buffer onto `base` (the pending
   page): the merge above, then the page's mask as filesystemWriteModePageData
   applies it.  Returns its length; out holds 256 bytes. */
size_t filesystemModePageAfterSelect(uint8_t lunNumber, uint8_t page, const char * base, size_t baseLength,
                                     uint8_t declared, uint8_t received, const uint8_t * Buffer, uint8_t out[256])
{
   uint8_t merged[256];
   size_t maskLength = 0;
   const char * mask = filesystemGetMaskPageData(lunNumber, page, &maskLength);
   size_t total = filesystemMergeOnto(page, base, baseLength, declared, received, Buffer, merged, mask == NULL, true);

   memcpy(out, merged, 256);
   if ((mask != NULL) && (base != NULL))
      for (size_t i = 0; (i < total) && (i < maskLength); i++)
         out[i] = (uint8_t)((merged[i] & (uint8_t)mask[i]) | ((i < baseLength ? (uint8_t)base[i] : 0u) & (uint8_t)~mask[i]));
   return total;
}

/* Store a whole page as given, no mask (FORMAT making the pending page
   current, or a reset to the factory values). */
void filesystemSetModePageRaw(uint8_t lunNumber, uint8_t page, const uint8_t * data, size_t length)
{
   int index = filesystemPagetoIndex(page);
   if ((index == -1) || (length == 0))
      return;
   parserkeyvalue *kv = &filesystemState.keyvalues[lunNumber][index];
   size_t alloc = ((size_t)scsiattributes[index].max > length) ? (size_t)scsiattributes[index].max : length;
   if ((kv->v.string == NULL) || (kv->length < length)) {
      char *grown = (char *)malloc(alloc);
      if (grown == NULL)
         return;
      memset(grown, 0, alloc);
      if (kv->v.string != NULL) {
         memcpy(grown, kv->v.string, kv->length);
         free(kv->v.string);
      }
      kv->v.string = grown;
   }
   memcpy(kv->v.string, data, length);
   kv->length = length;                 // a shorter page leaves no old tail behind
}

/* TrackFormat=T,O,S of a LUN; false if it has none (or a broken one) */
bool filesystemGetLunTrackFormat(uint8_t lunNumber, uint32_t *track, uint32_t *overhead, uint32_t *spare)
{
   const char *s = filesystemState.keyvalues[lunNumber][TRACKFORMAT].v.string;
   char *end;
   if (s == NULL)
      return false;
   *track = (uint32_t)strtoul(s, &end, 10);
   if (*end != ',')
      return false;
   *overhead = (uint32_t)strtoul(end + 1, &end, 10);
   if (*end != ',')
      return false;
   *spare = (uint32_t)strtoul(end + 1, &end, 10);
   return *track != 0;
}

// Read a whole file (address NULL: allocated here, with a NUL after it)
uint32_t filesystemReadFile(const char * filename, uint8_t **address, unsigned int max_size)
{
   FILE *file = fopen(filename, "rb");
   if (file == NULL) {
      return 0;
   }

   uint64_t fileSize = fsSize(file);
   size_t read_len;

   if (*address == NULL) {
      /* allocate buffer to hold entire file, plus a NUL terminator so
         text consumers (fileparser key match / strtol) can never read
         past the end of the allocation */
      *address = (uint8_t *)malloc((size_t)fileSize + 1u);
      if (*address == NULL) {
         fclose(file);
         return 0;
      }
      (*address)[fileSize] = 0;
      read_len = (size_t)fileSize;
   } else {
      /* caller provided a buffer length in max_size; clamp to file size */
      read_len = (size_t)max_size;
      if (read_len > fileSize) read_len = (size_t)fileSize;
   }

   size_t byteCounter = fread(*address, 1, read_len, file);
   fclose(file);

   return (uint32_t)byteCounter;
}

// Write a file
static uint32_t filesystemWriteFile(const char * filename, const uint8_t *address, uint32_t max_size)
{
   FILE *file = fopen(filename, "wb");
   if (file == NULL)
      return 0;

   size_t byteCounter = fwrite(address, 1, max_size, file);
   if (fclose(file) != 0)
      return 0;
   return (uint32_t)byteCounter;
}

/* Read a file back and compare it with what was meant to be written. */
static bool filesystemFileMatches(const char * filename, const uint8_t *address,
                                  uint32_t length)
{
   uint8_t *buffer = NULL;
   uint32_t got = filesystemReadFile(filename, &buffer, 0);
   bool ok = (buffer != NULL) && (got == length) && (memcmp(buffer, address, length) == 0);
   free(buffer);
   return ok;
}

/* Replace a file without risking the copy already there: write
   "<name>.new", read it back and compare it byte for byte, and only then
   swap it in.  On any failure the original is left exactly as it was. */
bool filesystemWriteFileSafe(const char * filename, const uint8_t *address, uint32_t length)
{
   char newname[310];

   if (filename == NULL || address == NULL)
      return false;
   if (snprintf(newname, sizeof newname, "%s.new", filename) >= (int)sizeof newname)
      return false;

   /* Verify before anything irreversible happens to the original. */
   if (filesystemWriteFile(newname, address, length) != length
       || !filesystemFileMatches(newname, address, length)
       || !fsRename(newname, filename))
   {
      remove(newname);
      return false;
   }
   return true;
}
