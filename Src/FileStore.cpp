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
//
// Memory map:
//
// &0000-&FBFF  RAM. The ROM images are loaded directly into RAM, see
//              BeebReadRoms()
// &FC00        146818 RTC address register (write only)
// &FC04        146818 RTC data register (when enabled by &FC08 bit 3)
// &FC08        Control latch (write), EPROM/RAM latch (read)
// &FC0C-&FC0F  WD2793 FDC registers
// &FC10-&FC1F  System VIA (emulated using BeebEm's user VIA)
// &FC2C        Front panel switches (read)
// &FC30-&FC33  SCSI interface
// &FD00-&FFFF  RAM

#include <windows.h>

#include "FileStore.h"
#include "6502core.h"
#include "BeebMem.h"
#include "Disc2793.h"
#include "Rtc.h"
#include "Scsi.h"
#include "UserVia.h"
#include "Video.h"
#include "Main.h"

/****************************************************************************/

bool FileStoreDoorOpen = false;
bool FileStoreCommandMode = false;
bool FileStoreModeLED = false;

// Control latch at &FC08 (write only)
constexpr unsigned char FS_CONTROL_FLOPPY_0     = 0x01; // Select left floppy drive
constexpr unsigned char FS_CONTROL_FLOPPY_1     = 0x02; // Select right floppy drive
constexpr unsigned char FS_CONTROL_FLOPPY_SIDE  = 0x04; // Floppy side select
constexpr unsigned char FS_CONTROL_CMOS_ENABLE  = 0x08; // Access to CMOS RAM possible if set
constexpr unsigned char FS_CONTROL_FDC_DENSITY  = 0x10; // FDC density (active low)
constexpr unsigned char FS_CONTROL_FDC_RESET    = 0x20; // FDC master reset (active low)
constexpr unsigned char FS_CONTROL_FDC_TEST     = 0x40; // FDC test (active low)
constexpr unsigned char FS_CONTROL_MODE_LED     = 0x80; // Front panel MODE LED

// Front panel switches at &FC2C (read only)
constexpr unsigned char FS_SWITCH_DOOR_OPEN     = 0x40; // Drive door open
constexpr unsigned char FS_SWITCH_COMMAND_MODE  = 0x80; // Command mode (E01 only)

// The FileStore has no video, but the CPU speed is regulated by counting
// video frames, so a frame is signalled every 20ms (40000 2MHz cycles).
constexpr int FS_FRAME_CYCLES = 40000;

static int FrameCycles = FS_FRAME_CYCLES;

// FDC master reset line from the control latch: false = held in reset
static bool FDCResetLine = false;

/****************************************************************************/

void FileStoreReset()
{
	FileStoreModeLED = false;
	FrameCycles = FS_FRAME_CYCLES;
	FDCResetLine = false;

	RTCChipEnable(false);
}

/****************************************************************************/

unsigned char FileStoreReadMem(int Address)
{
	if (Address < 0xfc00 || Address >= 0xfd00)
	{
		return WholeRam[Address];
	}

	// EPROM/RAM latch. This is only read once by a BIT instruction at the
	// start of the MOS initialisation (&EB94), which switches from reading
	// EPROM to reading RAM. The ROMs are already loaded into RAM.
	if (Address == 0xfc08)
	{
		return 0xff;
	}

	// FDC registers
	if (Address >= 0xfc0c && Address <= 0xfc0f)
	{
		return Read2793Register(static_cast<unsigned char>(Address - 0xfc0c));
	}

	// System VIA
	if ((Address & ~0xf) == 0xfc10)
	{
		SyncIO();
		unsigned char Value = UserVIARead(Address & 0xf);
		AdjustForIORead();
		return Value;
	}

	// Front panel switches
	if (Address == 0xfc2c)
	{
		unsigned char Value = 0x00;

		if (FileStoreDoorOpen)
		{
			Value |= FS_SWITCH_DOOR_OPEN;
		}

		if (MachineType == Model::FileStoreE01 && FileStoreCommandMode)
		{
			Value |= FS_SWITCH_COMMAND_MODE;
		}

		return Value;
	}

	// SCSI interface
	if ((Address & ~0x3) == 0xfc30 && SCSIDriveEnabled)
	{
		return SCSIRead(Address & 0x3);
	}

	// RTC data register. The RTC address register at &FC00 is write only.
	if (Address == 0xfc04 && RTCIsChipEnable())
	{
		return RTCReadData();
	}

	return 0xff;
}

/****************************************************************************/

void FileStoreWriteMem(int Address, unsigned char Value)
{
	if (Address < 0xfc00 || Address >= 0xfd00)
	{
		WholeRam[Address] = Value;
		return;
	}

	SyncIO();
	AdjustForIOWrite();

	// System VIA
	if ((Address & ~0xf) == 0xfc10)
	{
		if (Address == 0xfc1c)
		{
			// Don't allow the VIA PCR CB2 control bits (b4-b7) to be written.
			// On a FileStore these enable the automatic clock and
			// termination circuit.
			Value &= 0x0f;
		}

		SyncIO();
		AdjustForIOWrite();
		UserVIAWrite(Address & 0xf, Value);
		return;
	}

	// FDC registers
	if (Address >= 0xfc0c && Address <= 0xfc0f)
	{
		Write2793Register(static_cast<unsigned char>(Address - 0xfc0c), Value);
		return;
	}

	// SCSI interface
	if ((Address & ~0x3) == 0xfc30 && SCSIDriveEnabled)
	{
		SCSIWrite(Address & 0x3, Value);
		return;
	}

	// RTC address register
	if (Address == 0xfc00)
	{
		RTCWriteAddress(Value);
		return;
	}

	// RTC data register
	if (Address == 0xfc04)
	{
		if (RTCIsChipEnable())
		{
			RTCWriteData(Value);
		}

		return;
	}

	// Control latch
	if (Address == 0xfc08)
	{
		RTCChipEnable((Value & FS_CONTROL_CMOS_ENABLE) != 0);

		FileStoreModeLED = (Value & FS_CONTROL_MODE_LED) != 0;

		// Releasing the FDC master reset (active low) resets the FDC
		const bool Reset = (Value & FS_CONTROL_FDC_RESET) != 0;

		if (!FDCResetLine && Reset)
		{
			Reset2793();
		}

		FDCResetLine = Reset;

		// The FDC drive control register has the bits in a different order
		if (FDCResetLine)
		{
			unsigned char Control = Value & (FS_CONTROL_FLOPPY_0 | FS_CONTROL_FLOPPY_1);

			if (Value & FS_CONTROL_FLOPPY_SIDE)
			{
				Control |= 0x10;
			}

			if (Value & FS_CONTROL_FDC_DENSITY)
			{
				Control |= 0x20;
			}

			WriteFDC2793ControlReg(Control);
		}

		return;
	}
}

/****************************************************************************/

// Called from PollHardware() in place of the video, A/D, serial, 8271,
// 1770, Music 5000, speech and teletext polling, none of which the
// FileStore has. Also polls the 2793 FDC.

void FileStorePoll(int Cycles)
{
	FrameCycles -= Cycles;

	if (FrameCycles <= 0)
	{
		FrameCycles += FS_FRAME_CYCLES;
		mainWin->StartOfFrame();
		mainWin->UpdateFileStoreLEDs();
	}

	RTCPoll(Cycles);

	Poll2793(Cycles);
}

/****************************************************************************/
