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
// &FC00-&FC03  146818 RTC address register (write only)
// &FC04-&FC07  146818 RTC data register (when enabled by &FC08 bit 3)
// &FC08-&FC0B  Control latch (write), EPROM/RAM latch (read)
// &FC0C-&FC0F  WD2793 FDC registers
// &FC10-&FC1F  System VIA (emulated using BeebEm's user VIA)
// &FC20-&FC23  MC68B54 ADLC (Econet)
// &FC24-&FC27  Econet INTOFF and station number (read)
// &FC28-&FC2B  Econet INTON (read)
// &FC2C-&FC2F  Front panel switches (read)
// &FC30-&FC33  SCSI interface
// &FC40-&FCFF  Repeats of &FC00-&FC3F
// &FD00-&FFFF  RAM
//
// Unlike the BBC Micro, the FileStore runs its I/O bus at 2MHz, so I/O
// accesses aren't slowed down to 1MHz.

#include <windows.h>

#include "FileStore.h"
#include "6502core.h"
#include "BeebMem.h"
#include "Disc1770.h"
#include "Econet.h"
#include "Rtc.h"
#include "Scsi.h"
#include "UserVia.h"
#include "Video.h"
#include "DebugTrace.h"
#include "Main.h"

/****************************************************************************/

bool FileStoreDoorOpen = false;
bool FileStoreModeLED = false;

FileStoreDriveType FileStoreDrive[FILESTORE_MAX_DRIVES] = {
	FileStoreDriveType::Auto,
	FileStoreDriveType::Auto,
	FileStoreDriveType::Auto,
	FileStoreDriveType::Auto
};

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
constexpr unsigned char FS_SWITCH_COMMAND_MODE  = 0x80; // Command mode (E01 only), always off

// The FileStore has no video, but the CPU speed is regulated by counting
// video frames, so a frame is signalled every 20ms (40000 2MHz cycles).
constexpr int FS_FRAME_CYCLES = 40000;

static int FrameCycles = FS_FRAME_CYCLES;

// FDC master reset line from the control latch: false = held in reset
static bool FDCResetLine = false;

// DRAM refresh: 30 cycles after the last refresh, the next opcode fetch
// holds the CPU for one cycle (Ready low) while a refresh is done
constexpr unsigned int FS_REFRESH_CYCLES = 30;

static unsigned int RefreshCycles = 0;

/****************************************************************************/

void FileStoreReset()
{
	FileStoreModeLED = false;
	FrameCycles = FS_FRAME_CYCLES;
	FDCResetLine = false;
	RefreshCycles = 0;

	RTCChipEnable(false);
}

/****************************************************************************/

unsigned char FileStoreReadMem(int Address)
{
	if (Address < 0xfc00 || Address >= 0xfd00)
	{
		return WholeRam[Address];
	}

	// Page &FC repeats every &40 bytes, and the RTC, control latch, Econet
	// interrupt and front panel registers also appear at the three
	// addresses after each one
	Address = 0xfc00 | (Address & 0x3f);
	const int Group = Address & ~0x3;

	// EPROM/RAM latch. This is only read once by a BIT instruction at the
	// start of the MOS initialisation (&EB94), which switches from reading
	// EPROM to reading RAM. The ROMs are already loaded into RAM.
	if (Group == 0xfc08)
	{
		return 0xff;
	}

	// FDC registers
	if (Address >= 0xfc0c && Address <= 0xfc0f)
	{
		return Read1770Register(Address - 0xfc0c);
	}

	// System VIA
	if ((Address & ~0xf) == 0xfc10)
	{
		return UserVIARead(Address & 0xf);
	}

	// Econet ADLC
	if (Address >= 0xfc20 && Address <= 0xfc23)
	{
		return EconetEnabled ? EconetRead(Address & 3) : 0xfe;
	}

	// Econet NMI disable (INTOFF) and station number
	if (Group == 0xfc24 && EconetEnabled)
	{
		#ifdef DEBUG_ECONET_INTERRUPTS
		DebugTrace("Econet: INTOFF\n");
		#endif

		EconetNMIEnabled = false;

		return EconetReadStationID();
	}

	// Econet NMI enable (INTON)
	if (Group == 0xfc28 && EconetEnabled)
	{
		#ifdef DEBUG_ECONET_INTERRUPTS
		DebugTrace("Econet: INTON\n");
		#endif

		if (!EconetNMIEnabled)
		{
			EconetNMIEnabled = true;

			// Assert a pending NMI
			if (EconetInterruptRequest())
			{
				NMIStatus |= NMI_ECONET;

				#ifdef DEBUG_ECONET_INTERRUPTS
				DebugTrace("Econet: Delayed NMI asserted\n");
				#endif
			}
		}

		return 0xff;
	}

	// Front panel switches
	if (Group == 0xfc2c)
	{
		unsigned char Value = 0x00;

		if (FileStoreDoorOpen)
		{
			Value |= FS_SWITCH_DOOR_OPEN;
		}

		return Value;
	}

	// SCSI interface
	if ((Address & ~0x3) == 0xfc30 && SCSIDriveEnabled)
	{
		return SCSIRead(Address & 0x3);
	}

	// RTC data register. The RTC address register at &FC00 is write only.
	if (Group == 0xfc04 && RTCIsChipEnable())
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

	// Page &FC repeats every &40 bytes, and the RTC, control latch, Econet
	// interrupt and front panel registers also appear at the three
	// addresses after each one
	Address = 0xfc00 | (Address & 0x3f);
	const int Group = Address & ~0x3;

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

		UserVIAWrite(Address & 0xf, Value);
		return;
	}

	// FDC registers
	if (Address >= 0xfc0c && Address <= 0xfc0f)
	{
		Write1770Register(Address - 0xfc0c, Value);
		return;
	}

	// Econet ADLC
	if (Address >= 0xfc20 && Address <= 0xfc23)
	{
		if (EconetEnabled)
		{
			EconetWrite(Address & 3, Value);
		}

		return;
	}

	// SCSI interface
	if ((Address & ~0x3) == 0xfc30 && SCSIDriveEnabled)
	{
		SCSIWrite(Address & 0x3, Value);
		return;
	}

	// RTC address register
	if (Group == 0xfc00)
	{
		RTCWriteAddress(Value);
		return;
	}

	// RTC data register
	if (Group == 0xfc04)
	{
		if (RTCIsChipEnable())
		{
			RTCWriteData(Value);
		}

		return;
	}

	// Control latch
	if (Group == 0xfc08)
	{
		RTCChipEnable((Value & FS_CONTROL_CMOS_ENABLE) != 0);

		FileStoreModeLED = (Value & FS_CONTROL_MODE_LED) != 0;

		// Releasing the FDC master reset (active low) resets the FDC
		const bool Reset = (Value & FS_CONTROL_FDC_RESET) != 0;

		if (!FDCResetLine && Reset)
		{
			Reset1770();
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

			WriteFDCControlReg(Control);
		}

		return;
	}
}

/****************************************************************************/

// Called after each instruction with its number of cycles. Returns the
// number of extra cycles the CPU is held for by the DRAM refresh.

unsigned int FileStoreRefreshWait(unsigned int Cycles)
{
	RefreshCycles += Cycles;

	if (RefreshCycles >= FS_REFRESH_CYCLES)
	{
		RefreshCycles = 0;
		return 1;
	}

	return 0;
}

/****************************************************************************/

// Called from PollHardware() in place of the video, A/D, serial, 8271,
// 1770, Music 5000, speech and teletext polling, none of which the
// FileStore has. Also polls the WD2793 FDC (emulated by the 1770 code).

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

	Poll1770(Cycles);
}

/****************************************************************************/
