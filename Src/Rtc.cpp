/****************************************************************
BeebEm - BBC Micro and Master 128 Emulator
Copyright (C) 1994  David Alan Gilbert
Copyright (C) 1997  Mike Wyatt
Copyright (C) 2001  Richard Gellman
Copyright (C) 2004  Ken Lowe

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

/* CMOS Ram finalised 06/01/2001 - Richard Gellman */

#include <windows.h>

#include <stdio.h>
#include <time.h>

#include "Rtc.h"
#include "6502core.h"
#include "Bcd.h"
#include "Debug.h"
#include "DebugTrace.h"
#include "Main.h"
#include "Model.h"

// #define DEBUG_RTC

/*-------------------------------------------------------------------------*/

// The Acorn Master Series and FileStore use a HD146818. This provides a RTC
// (Real Time Clock), a programmable periodic interrupt and square wave
// generator (which BeebEm doesn't emulate), and 50 bytes CMOS RAM.
//
// The FileStore also uses the update-ended flag and interrupt, which
// BeebEm emulates for the FileStore only, see RTCPoll().
//
//                                 Memory Map
//
// +----+----+-------------------------------------------------+---------------+
// | Address | Description                                     |   DM=0    =1  |
// +----+----+-------------------------------------------------+---------------+
// | 00 | 00 | Seconds (0 to 59)                               | Binary or BCD |
// | 01 | 01 | Alarm Seconds (0 to 59)                         | Binary or BCD |
// | 02 | 02 | Minutes (0 to 59)                               | Binary or BCD |
// | 03 | 03 | Alarm Minutes (0 to 59)                         | Binary or BCD |
// | 04 | 04 | Hours (0 to 23)                                 | Binary or BCD |
// | 05 | 05 | Alarm Hours (0 to 23)                           | Binary or BCD |
// | 06 | 06 | Day of week (1 = Sun, 7 = Sat)                  | Binary or BCD |
// | 07 | 07 | Day of month (1 to 31)                          | Binary or BCD |
// | 08 | 08 | Month (1 = Jan, 12 = Dec)                       | Binary or BCD |
// | 09 | 09 | Year (last 2 digits)                            | Binary or BCD |
// +----+----+-------------------------------------------------+---------------+
// | 10 | 0A | Register A  |  UIP | DV2 | DV1 | DV0 |  RS3 | RS2 |  RS1  | RS0 |
// | 11 | 0B | Register B  |  SET | PIE | AIE | UIE | SQWE |  DM | 24/12 | DSE |
// | 12 | 0C | Register C  | IRQF | PF  | AF  | UF  |   -  |  -  |   -   |  -  |
// | 13 | 0D | Register D  |  VRT |  -  |  -  |  -  |   -  |  -  |   -   |  -  |
// +----+----+-----------------------------------------------------------------+
// | 50 bytes of user RAM                                                      |
// |---------------------------------------------------------------------------|
// | Master 128 http://beebwiki.mdfs.net/CMOS_configuration_RAM_allocation     |
// | See https://chrisacorns.computinghistory.org.uk/docs/Acorn/AN/203.pdf for |
// | factory default settings                                                  |
// |---------------------------------------------------------------------------|
// | 14 | 0E |       Econet station number                *SetStation nnn      |
// | 15 | 0F |       File server number                   *CO. FS nnn          |
// | 16 | 10 |       File server network                  *CO. FS nnn.sss      |
// | 17 | 11 |       Printer server number                *CO. PS nnn          |
// | 18 | 12 |       Printer server network               *CO. PS nnn.sss      |
// | 19 | 13 | b0-b3 Default filing system ROM            *CO. File nn         |
// |    |    | b4-b7 Default language ROM                 *CO. Lang nn         |
// | 20 | 14 |       ROMs 0-7 unplugged/inserted          *Insert n/*Unplug n  |
// | 21 | 15 |       ROMs 8-F unplugged/inserted          *Insert n/*Unplug n  |
// | 22 | 16 | b0-b2 EDIT screen mode                                          |
// |    |    | b3    EDIT TAB to columns/words                                 |
// |    |    | b4    EDIT overwrite/insert                                     |
// |    |    | b5    EDIT display/returns                                      |
// |    |    | b6-b7 Not used                                                  |
// | 23 | 17 |       Telecoms software                                         |
// | 24 | 18 | b0-b2 Default screen mode                  *CO. Mode nn         |
// |    |    | b3    Shadow screen mode (128-135)                              |
// |    |    | b4    Default TV interlace                 *CO. TV xx,n         |
// |    |    | b5-b7 Default TV position 0-3, -4 to -1    *CO. TV nn,x         |
// | 25 | 19 | b0-b2 Default floppy speed                 *CO. FDrive n        |
// |    |    | b3    Shift Caps on start up               *CO. ShCaps          |
// |    |    | b4    No CAPS lock on start up             *CO. NoCaps          |
// |    |    | b5    CAPS lock on start up                *CO. Caps            |
// |    |    | b6    ADFS load dir on start up            *CO. NoDir/Dir       |
// |    |    | b7    ADFS floppy/hard drive on start up   *CO. Floppy/Hard     |
// | 26 | 1A |       Keyboard repeat delay                *CO. Delay nnn       |
// | 27 | 1B |       Keyboard repeat rate                 *CO. Repeat nnn      |
// | 28 | 1C |       Printer ignore character             *CO. Ignore nnn      |
// | 29 | 1D | b0    Ignore/enable Tube                   *CO. NoTube/Tube     |
// |    |    | b1    Ignore printer ignore character      *CO. Ignore nnn      |
// |    |    | b2-b4 Default RS232 serial speed, 0-7      *CO. Baud n          |
// |    |    | b5-b7 Default printer device, 0-7          *CO. Print n         |
// | 30 | 1E | b0    Default to shadow screen on start    *CO. Shadow          |
// |    |    | b1    Default BELL (Ctrl+G) volume         *CO. Quiet/Loud      |
// |    |    | b2    Internal/External Tube               *CO. InTube/ExTube   |
// |    |    | b3    Scrolling enabled/protected          *CO. Scroll/NoScroll |
// |    |    | b4    Noboot/boot on reset                 *CO. NoBoot/Boot     |
// |    |    | b5-b7 Default serial data format           *CO. Data n          |
// | 31 | 1F | b0    ANFS raise 2 pages of workspace      *CO. NoSpace/Space   |
// |    |    | b1    ANFS run *FindLib on logon           *-Net-Opt 5,n        |
// |    |    | b2    ANFS uses &0Bxx-&0Cxx or &0Exx-&0Fxx *-Net-Opt 6,n        |
// |    |    | b3-b5 Unused                                                    |
// |    |    | b6    ANFS protected                       *-Net-Opt 8,n        |
// |    |    | b7    Display version number on start up                        |
// | 32 | 20 |       Unused                                                    |
// | 33 | 21 |       Unused                                                    |
// | 34 | 22 |       34-43 Reserved for Acorn future expansion                 |
// | 35 | 23 |                                                                 |
// | 36 | 24 |                                                                 |
// | 37 | 25 |                                                                 |
// | 38 | 26 |                                                                 |
// | 39 | 27 |                                                                 |
// | 40 | 28 |                                                                 |
// | 41 | 29 |                                                                 |
// | 42 | 2A |                                                                 |
// | 43 | 2B |                                                                 |
// | 44 | 2C |       44-53 Reserved for use by commercial third parties        |
// | 45 | 2D |                                                                 |
// | 46 | 2E |                                                                 |
// | 47 | 2F |                                                                 |
// | 48 | 30 |                                                                 |
// | 49 | 31 |                                                                 |
// | 50 | 32 |                                                                 |
// | 51 | 33 |                                                                 |
// | 52 | 34 |                                                                 |
// | 53 | 35 |                                                                 |
// | 54 | 36 |       54-63 Reserved for use by users                           |
// | 55 | 37 |                                                                 |
// | 56 | 38 |                                                                 |
// | 57 | 39 |                                                                 |
// | 58 | 3A |                                                                 |
// | 59 | 3B |                                                                 |
// | 60 | 3C |                                                                 |
// | 61 | 3D |                                                                 |
// | 62 | 3E |                                                                 |
// | 63 | 3F |                                                                 |
// +----+----+-----------------------------------------------------------------+
// | FileStore E01 and E01S                                                    |
// |---------------------------------------------------------------------------|
// |         | Default value in BeebEm                                         |
// |         | hex | Description                                               |
// +----+----+-----+-----------------------------------------------------------+
// | 14 | 0E | FE  | Station number                                            |
// | 15 | 0F | 01  | Station number check byte (station number EOR &FF)        |
// | 16 | 10 | 00  | Reserved                                                  |
// | 17 | 11 | 00  | Reserved                                                  |
// | 18 | 12 | 00  | Reserved                                                  |
// | 19 | 13 | 00  | Reserved                                                  |
// | 20 | 14 | 00  | Last error number                                         |
// | 21 | 15 | 00  | Last error X                                              |
// | 22 | 16 | 00  | Last error Y                                              |
// | 23 | 17 | 00  | Last error LSB                                            |
// | 24 | 18 | 00  | Last error MSB                                            |
// | 25 | 19 | FF  | SIN LSB                                                   |
// | 26 | 1A | FF  | SIN CSB                                                   |
// | 27 | 1B | FF  | SIN MSB                                                   |
// | 28 | 1C | 80  | Maximum number of users                                   |
// | 29 | 1D | 06  | Maximum drive number + 1                                  |
// | 30 | 1E | 50  | Printer server name, 6 characters ("Puce"), 00 = disabled |
// | .. | .. | ..  |                                                           |
// | 35 | 23 | 00  |                                                           |
// | 36 | 24 | 53  | Maintenance mode user name, 11 characters ("SYST")        |
// | .. | .. | ..  |                                                           |
// | 46 | 2E | 00  |                                                           |
// | 47 | 2F | 54  | User name check byte                                      |
// | 48 | 30 | 4E  | Printer page feeds: 0 = on, anything else = off           |
// | 49 | 31 | 01  | Hard drive copyright string check: 0 = yes, 1 = no        |
// | 50 | 32 | 00  | Unused                                                    |
// | .. | .. | ..  |                                                           |
// | 63 | 3F | 00  |                                                           |
// +----+----+-----+-----------------------------------------------------------+

// Offset from PC clock (seconds)
static time_t RTCTimeOffset = 0;

// Index into CMOSType::Register for each machine that has CMOS RAM
constexpr int CMOS_MASTER_128     = 0;
constexpr int CMOS_MASTER_ET      = 1;
constexpr int CMOS_FILESTORE_E01  = 2;
constexpr int CMOS_FILESTORE_E01S = 3;
constexpr int CMOS_COUNT          = 4;

struct CMOSType
{
	bool ChipEnable;
	unsigned char Address;
	// RTC registers + 50 bytes CMOS RAM for Master 128, Master ET,
	// FileStore E01 and FileStore E01S
	unsigned char Register[CMOS_COUNT][64];
};

static CMOSType CMOS;

const unsigned char RTC146818_REG_A = 0x0A;
const unsigned char RTC146818_REG_B = 0x0B;
const unsigned char RTC146818_REG_C = 0x0C;
const unsigned char RTC146818_REG_D = 0x0D;

const unsigned char RTC146818_REG_A_UIP    = 0x80;
const unsigned char RTC146818_REG_A_DV     = 0x70; // Time base divider
const unsigned char RTC146818_REG_A_RS     = 0x0f; // Periodic interrupt rate

const unsigned char RTC146818_REG_B_SET    = 0x80;
const unsigned char RTC146818_REG_B_PIE    = 0x40;
const unsigned char RTC146818_REG_B_AIE    = 0x20;
const unsigned char RTC146818_REG_B_UIE    = 0x10;
const unsigned char RTC146818_REG_B_SQWE   = 0x08;
const unsigned char RTC146818_REG_B_BINARY = 0x04;
const unsigned char RTC146818_REG_B_24HOUR = 0x02;

const unsigned char RTC146818_REG_C_IRQF   = 0x80;
const unsigned char RTC146818_REG_C_PF     = 0x40;
const unsigned char RTC146818_REG_C_AF     = 0x20;
const unsigned char RTC146818_REG_C_UF     = 0x10;

const unsigned char RTC146818_REG_D_VRT    = 0x80; // Valid RAM and time

// Master 128 CMOS defaults from byte 14 (RAM bytes only)

const unsigned char CMOSDefault_Master128[50] =
{
	0x01, // 14 - Econet station number: 1
	0xfe, // 15 - File server number: 254
	0x00, // 16 - File server network: 0
	0xea, // 17 - Printer server number: 235
	0x00, // 18 - Printer server network: 0
	0xc9, // 19 - Default language ROM: 12, Default filing system ROM: 9
	0xff, // 20 - ROMS 0-7 unplugged/inserted
	0xff, // 21 - ROMS 8-F unplugged/inserted
	0x32, // 22 - Edit mode K
	0x00, // 23
	0x07, // 24 - Default screen mode 7, *TV 0, 0
	0xc0, // 25 - Default floppy speed: 10, Floppy, NoDir, Caps
	0x1e, // 26 - Keyboard auto-repeat delay: 30cs
	0x08, // 27 - Keyboard repeat rate: 8cs
	0x00, // 28 - Printer ignore character
	0x31, // 29 - Parallel port printer, RS232 1200 baud, Tube enabled
	0xa2  // 30 - Serial format 8N1, no boot on reset, scroll enabled, external tube, Loud
};

// Master ET CMOS defaults from byte 14 (RAM bytes only)

const unsigned char CMOSDefault_MasterET[50] =
{
	0x01, // 14 - Econet station number: 1
	0xfe, // 15 - File server number: 254
	0x00, // 16 - File server network: 0
	0xea, // 17 - Printer server number: 235
	0x00, // 18 - Printer server network: 0
	0xde, // 19 - Default language ROM: 13, Default filing system ROM: 14
	0xff, // 20 - ROMS 0-7 unplugged/inserted
	0xff, // 21 - ROMS 8-F unplugged/inserted
	0x32, // 22 - Edit mode K
	0x00, // 23
	0x07, // 24 - Default screen mode 7, *TV 0, 0
	0xc0, // 25 - Default floppy speed: 10, Floppy, NoDir, Caps
	0x1e, // 26 - Keyboard auto-repeat delay: 30cs
	0x08, // 27 - Keyboard repeat rate: 8cs
	0x00, // 28 - Printer ignore character
	0x31, // 29 - Parallel port printer, RS232 1200 baud, Tube enabled
	0xa2  // 30 - Serial format 8N1, no boot on reset, scroll enabled, external tube, Loud
};

// FileStore E01/E01S CMOS defaults from byte 14 (RAM bytes only)

const unsigned char CMOSDefault_FileStore[50] =
{
	0xfe, // 14 - Station number: 254
	0x01, // 15 - Station number check byte
	0x00, // 16 - Reserved
	0x00, // 17 - Reserved
	0x00, // 18 - Reserved
	0x00, // 19 - Reserved
	0x00, // 20 - Last error number
	0x00, // 21 - Last error X
	0x00, // 22 - Last error Y
	0x00, // 23 - Last error LSB
	0x00, // 24 - Last error MSB
	0xff, // 25 - SIN LSB
	0xff, // 26 - SIN CSB
	0xff, // 27 - SIN MSB
	0x80, // 28 - Maximum number of users: 128
	0x06, // 29 - Maximum drive number + 1
	0x50, // 30 - Printer server name: "Puce"
	0x75, // 31
	0x63, // 32
	0x65, // 33
	0x00, // 34
	0x00, // 35
	0x53, // 36 - Maintenance mode user name: "SYST"
	0x59, // 37
	0x53, // 38
	0x54, // 39
	0x00, // 40
	0x00, // 41
	0x00, // 42
	0x00, // 43
	0x00, // 44
	0x00, // 45
	0x00, // 46
	0x54, // 47 - User name check byte
	0x4e, // 48 - Printer page feeds off
	0x01, // 49 - Hard drive copyright string check off
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 50-56
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // 57-63
};

// FileStore 146818 timing. The FileStore has a 32.768kHz crystal, and
// relies on the update cycle and interrupts, so these are emulated in CPU
// cycles for the FileStore:
// - One update cycle per second. UIP is set 244us (8 ticks) before it
//   starts, and is cleared when it ends 1984us (65 ticks) later, when UF is
//   set and the alarm is checked.
// - The periodic interrupt, at the rate selected by register A RS3-RS0.
constexpr int RTC_CPU_CLOCK          = 2000000;
constexpr int RTC_TIME_BASE          = 32768;  // Ticks per second
constexpr int RTC_UIP_TICKS          = 8;      // UIP set before an update
constexpr int RTC_UPDATE_TICKS       = 65;     // Length of an update cycle
constexpr unsigned char RTC_DV_RESET = 0x60;   // DV2 and DV1 set: divider held in reset

static int RTCTickCycles = 0;   // CPU cycles towards the next tick, times RTC_TIME_BASE
static int RTCSecondTicks = 0;  // Ticks since the last update cycle started
static int RTCPeriodicTicks = 0;

// The FileStore's clock normally follows the PC clock. If it is allowed to
// set its own clock, it is kept as an offset from the PC clock.
static bool FileStoreClockSettable = false;
static time_t FileStoreClockOffset = 0;

// Pointer used to switch between the CMOS RAM for the different machines
unsigned char* pCMOS = nullptr;

/*-------------------------------------------------------------------------*/

static int GetCMOSIndex(Model model)
{
	switch (model)
	{
		case Model::Master128:
		default:
			return CMOS_MASTER_128;

		case Model::MasterET:
			return CMOS_MASTER_ET;

		case Model::FileStoreE01:
			return CMOS_FILESTORE_E01;

		case Model::FileStoreE01S:
			return CMOS_FILESTORE_E01S;
	}
}

/*-------------------------------------------------------------------------*/

// Convert the contents of the RTC registers to a time_t, applying a
// correction based on the PC clock because the RTC only stores a two-digit
// year.

static time_t RTCConvertClock()
{
	struct tm Base;

	if (pCMOS[RTC146818_REG_B] & RTC146818_REG_B_BINARY)
	{
		Base.tm_sec   = pCMOS[0];
		Base.tm_min   = pCMOS[2];
		Base.tm_hour  = pCMOS[4];
		Base.tm_mday  = pCMOS[7];
		Base.tm_mon   = pCMOS[8] - 1;
		Base.tm_year  = pCMOS[9];
	}
	else
	{
		Base.tm_sec   = BCDToBin(pCMOS[0]);
		Base.tm_min   = BCDToBin(pCMOS[2]);
		Base.tm_hour  = BCDToBin(pCMOS[4]);
		Base.tm_mday  = BCDToBin(pCMOS[7]);
		Base.tm_mon   = BCDToBin(pCMOS[8]) - 1;
		Base.tm_year  = BCDToBin(pCMOS[9]); // Years since 1900
	}

	Base.tm_wday  = -1;
	Base.tm_yday  = -1;
	Base.tm_isdst = -1;

	// Add the century from the PC clock.
	time_t SysTime;
	time(&SysTime);
	struct tm *CurTime = localtime(&SysTime);
	Base.tm_year += (CurTime->tm_year / 100) * 100;

	return mktime(&Base);
}

/*-------------------------------------------------------------------------*/

static void RTCSetTimeAndDateRegisters(const struct tm* pTime)
{
	if (pCMOS[RTC146818_REG_B] & RTC146818_REG_B_BINARY)
	{
		// Update with current time values in binary format
		pCMOS[0] = static_cast<unsigned char>(pTime->tm_sec);
		pCMOS[2] = static_cast<unsigned char>(pTime->tm_min);
		pCMOS[4] = static_cast<unsigned char>(pTime->tm_hour);
		pCMOS[6] = static_cast<unsigned char>(pTime->tm_wday + 1);
		pCMOS[7] = static_cast<unsigned char>(pTime->tm_mday);
		pCMOS[8] = static_cast<unsigned char>(pTime->tm_mon + 1);
		pCMOS[9] = static_cast<unsigned char>(pTime->tm_year % 100);
	}
	else
	{
		// Update with current time values in BCD format
		pCMOS[0] = BCD(static_cast<unsigned char>(pTime->tm_sec));
		pCMOS[2] = BCD(static_cast<unsigned char>(pTime->tm_min));
		pCMOS[4] = BCD(static_cast<unsigned char>(pTime->tm_hour));
		pCMOS[6] = BCD(static_cast<unsigned char>(pTime->tm_wday + 1));
		pCMOS[7] = BCD(static_cast<unsigned char>(pTime->tm_mday));
		pCMOS[8] = BCD(static_cast<unsigned char>(pTime->tm_mon + 1));
		pCMOS[9] = BCD(static_cast<unsigned char>(pTime->tm_year % 100));
	}
}

/*-------------------------------------------------------------------------*/

static void RTCUpdate();

void RTCInit()
{
	// Select CMOS memory
	switch (MachineType)
	{
		case Model::Master128:
		case Model::MasterET:
		case Model::FileStoreE01:
		case Model::FileStoreE01S:
			pCMOS = CMOS.Register[GetCMOSIndex(MachineType)];
			break;

		default: // Machine doesn't use CMOS
			pCMOS = nullptr;
			if (DebugEnabled)
				DebugDisplayTrace(DebugType::CMOS, true, "RTC: Timer off");
			return;
	}

	if (IsFileStore(MachineType))
	{
		// Registers A-D aren't kept in the preferences file, so start as a
		// battery-backed chip would be: 32.768kHz time base, no periodic
		// interrupt, 24 hour BCD, and the battery good. Then as on a reset:
		// the interrupt enables and flags are cleared.
		if ((pCMOS[RTC146818_REG_A] & RTC146818_REG_A_DV) != 0x20)
		{
			pCMOS[RTC146818_REG_A] = 0x20;
			pCMOS[RTC146818_REG_B] = RTC146818_REG_B_24HOUR;
		}

		pCMOS[RTC146818_REG_A] &= ~RTC146818_REG_A_UIP;
		pCMOS[RTC146818_REG_B] &= ~(RTC146818_REG_B_PIE | RTC146818_REG_B_AIE |
		                            RTC146818_REG_B_UIE | RTC146818_REG_B_SQWE);
		pCMOS[RTC146818_REG_C] = 0;
		pCMOS[RTC146818_REG_D] = RTC146818_REG_D_VRT;

		IntStatus &= ~IRQ_RTC;

		RTCTickCycles = 0;
		RTCSecondTicks = 0;
		RTCPeriodicTicks = 0;
	}

	time_t SysTime;
	time(&SysTime);
	struct tm *CurTime = localtime(&SysTime);

	RTCSetTimeAndDateRegisters(CurTime);

	RTCTimeOffset = SysTime - RTCConvertClock();

	if (IsFileStore(MachineType) && FileStoreClockSettable)
	{
		RTCTimeOffset = FileStoreClockOffset;
		RTCUpdate();
	}

	#ifdef DEBUG_RTC
	DebugTrace("RTCInit: RTCTimeOffset set to %d\n", (int)RTCTimeOffset);
	#endif
}

/*-------------------------------------------------------------------------*/

static void RTCUpdate()
{
	time_t SysTime;
	time(&SysTime);
	SysTime -= RTCTimeOffset;
	struct tm *CurTime = localtime(&SysTime);

	RTCSetTimeAndDateRegisters(CurTime);
}

/*-------------------------------------------------------------------------*/

void RTCChipEnable(bool Enable)
{
	CMOS.ChipEnable = Enable;
}

/*-------------------------------------------------------------------------*/

bool RTCIsChipEnable()
{
	return CMOS.ChipEnable;
}

/*-------------------------------------------------------------------------*/

void RTCWriteAddress(unsigned char Address)
{
	#ifdef DEBUG_RTC
	DebugTrace("RTC: Write address %02X\n", Address);
	#endif

	// The 146818 has 64 registers
	CMOS.Address = Address & 0x3f;
}

/*-------------------------------------------------------------------------*/

void RTCWriteData(unsigned char Value)
{
	#ifdef DEBUG_RTC
	DebugTrace("RTC: Write data: address %02X value %02X\n", CMOS.Address, Value);
	#endif

	if (DebugEnabled)
	{
		DebugDisplayTraceF(DebugType::CMOS, true,
		                   "CMOS: Write address %X value %02X",
		                   CMOS.Address, Value);
	}

	// Many thanks to Tom Lees for supplying me with info on the 146818 registers
	// for these two functions.
	if (CMOS.Address <= 0x9)
	{
		// Clock registers
		#ifdef DEBUG_RTC
		DebugTrace("RTCWriteData: Set register %d to %02X\n", CMOS.Address, Value);
		#endif

		// BCD or binary format?
		if (pCMOS[RTC146818_REG_B] & RTC146818_REG_B_BINARY)
		{
			pCMOS[CMOS.Address] = BCD(Value);
		}
		else
		{
			pCMOS[CMOS.Address] = Value;
		}
	}
	else if (CMOS.Address == RTC146818_REG_A)
	{
		// Control register A
		pCMOS[CMOS.Address] = Value & 0x7f; // Top bit not writable
	}
	else if (CMOS.Address == RTC146818_REG_B)
	{
		// Control register B
		// Bit-7 SET - 0=clock running, 1=clock update halted
		if (IsFileStore(MachineType) && !FileStoreClockSettable)
		{
			// The FileStore clock follows the PC clock, unless it's allowed
			// to set its own clock
		}
		else if (Value & RTC146818_REG_B_SET)
		{
			RTCUpdate();
		}
		else if ((pCMOS[CMOS.Address] & RTC146818_REG_B_SET) && !(Value & RTC146818_REG_B_SET))
		{
			// New clock settings
			time_t SysTime;
			time(&SysTime);
			RTCTimeOffset = SysTime - RTCConvertClock();

			if (IsFileStore(MachineType))
			{
				FileStoreClockOffset = RTCTimeOffset;
			}

			#ifdef DEBUG_RTC
			DebugTrace("RTCWriteData: RTCTimeOffset set to %d\n", (int)RTCTimeOffset);
			#endif
		}

		// Write the value to CMOS anyway
		pCMOS[CMOS.Address] = Value;
	}
	else if (CMOS.Address == RTC146818_REG_C || CMOS.Address == RTC146818_REG_D)
	{
		// Control register C and D - read only
	}
	else if (CMOS.Address < 0x40)
	{
		// User RAM
		pCMOS[CMOS.Address] = Value;
	}
}

/*-------------------------------------------------------------------------*/

unsigned char RTCReadData()
{
	if (CMOS.Address <= 0x9)
	{
		RTCUpdate();
	}

	unsigned char Value = pCMOS[CMOS.Address];

	if (CMOS.Address == RTC146818_REG_C)
	{
		// Reading register C clears the interrupt flags and the interrupt
		pCMOS[RTC146818_REG_C] = 0;

		IntStatus &= ~IRQ_RTC;
	}

	if (DebugEnabled)
	{
		DebugDisplayTraceF(DebugType::CMOS, true,
		                   "CMOS: Read address %X value %02X",
		                   CMOS.Address, Value);
	}

	#ifdef DEBUG_RTC
	DebugTrace("RTC: Read address %02X value %02X\n", CMOS.Address, Value);
	#endif

	return Value;
}

/*-------------------------------------------------------------------------*/

void RTCSetCMOSDefaults(Model model)
{
	const int Index = GetCMOSIndex(model);

	switch (model)
	{
		case Model::Master128:
		default:
			memcpy(&CMOS.Register[Index][14], CMOSDefault_Master128, 50);
			break;

		case Model::MasterET:
			memcpy(&CMOS.Register[Index][14], CMOSDefault_MasterET, 50);
			break;

		case Model::FileStoreE01:
		case Model::FileStoreE01S:
			memcpy(&CMOS.Register[Index][14], CMOSDefault_FileStore, 50);
			break;
	}
}

/*-------------------------------------------------------------------------*/

unsigned char RTCGetData(Model model, unsigned char Address)
{
	const int Index = GetCMOSIndex(model);

	return CMOS.Register[Index][Address];
}

/*-------------------------------------------------------------------------*/

const unsigned char* RTCGetCMOSData(Model model)
{
	const int Index = GetCMOSIndex(model);

	return &CMOS.Register[Index][14];
}

/*-------------------------------------------------------------------------*/

void RTCSetCMOSData(Model model, const unsigned char* pData, int Size)
{
	const int Index = GetCMOSIndex(model);

	memcpy(&CMOS.Register[Index][14], pData, Size);
}

/*-------------------------------------------------------------------------*/

// Set IRQF and the interrupt when any flag is set with its interrupt enabled

static void RTCUpdateIRQ()
{
	const unsigned char Flags = pCMOS[RTC146818_REG_C] & pCMOS[RTC146818_REG_B] &
	                            (RTC146818_REG_C_PF | RTC146818_REG_C_AF | RTC146818_REG_C_UF);

	if (Flags != 0)
	{
		pCMOS[RTC146818_REG_C] |= RTC146818_REG_C_IRQF;
		IntStatus |= IRQ_RTC;
	}
	else
	{
		pCMOS[RTC146818_REG_C] &= ~RTC146818_REG_C_IRQF;
		IntStatus &= ~IRQ_RTC;
	}
}

/*-------------------------------------------------------------------------*/

// True if an alarm register (1, 3 or 5) matches its time register. Values
// of &C0 and over are "don't care".

static bool RTCAlarmMatches(int Alarm)
{
	return pCMOS[Alarm] >= 0xc0 || pCMOS[Alarm] == pCMOS[Alarm - 1];
}

/*-------------------------------------------------------------------------*/

// One tick of the 32.768kHz time base

static void RTCTick()
{
	// Periodic interrupt. Rates 1 and 2 are the same as 8 and 9.
	int Rate = pCMOS[RTC146818_REG_A] & RTC146818_REG_A_RS;

	if (Rate == 1 || Rate == 2)
	{
		Rate += 7;
	}

	if (Rate != 0 && ++RTCPeriodicTicks >= (1 << (Rate - 1)))
	{
		RTCPeriodicTicks = 0;
		pCMOS[RTC146818_REG_C] |= RTC146818_REG_C_PF;
		RTCUpdateIRQ();
	}

	// Update cycle, once a second. None while the SET bit is set.
	if (++RTCSecondTicks >= RTC_TIME_BASE)
	{
		RTCSecondTicks = 0;
	}

	if (pCMOS[RTC146818_REG_B] & RTC146818_REG_B_SET)
	{
		pCMOS[RTC146818_REG_A] &= ~RTC146818_REG_A_UIP;
		return;
	}

	if (RTCSecondTicks == RTC_TIME_BASE - RTC_UIP_TICKS)
	{
		pCMOS[RTC146818_REG_A] |= RTC146818_REG_A_UIP;
	}
	else if (RTCSecondTicks == RTC_UPDATE_TICKS)
	{
		RTCUpdate();

		pCMOS[RTC146818_REG_A] &= ~RTC146818_REG_A_UIP;
		pCMOS[RTC146818_REG_C] |= RTC146818_REG_C_UF;

		if (RTCAlarmMatches(1) && RTCAlarmMatches(3) && RTCAlarmMatches(5))
		{
			pCMOS[RTC146818_REG_C] |= RTC146818_REG_C_AF;
		}

		RTCUpdateIRQ();

		#ifdef DEBUG_RTC
		DebugTrace("RTC: Update cycle, register C %02X\n", pCMOS[RTC146818_REG_C]);
		#endif
	}
}

/*-------------------------------------------------------------------------*/

// Called from FileStorePoll() with the number of CPU cycles since the last
// call. Counts 32.768kHz ticks while the time base divider is running.

void RTCPoll(int Cycles)
{
	if (pCMOS == nullptr)
	{
		return;
	}

	if ((pCMOS[RTC146818_REG_A] & RTC_DV_RESET) == RTC_DV_RESET)
	{
		// Divider held in reset
		RTCTickCycles = 0;
		RTCSecondTicks = 0;
		pCMOS[RTC146818_REG_A] &= ~RTC146818_REG_A_UIP;
		return;
	}

	RTCTickCycles += Cycles * RTC_TIME_BASE;

	while (RTCTickCycles >= RTC_CPU_CLOCK)
	{
		RTCTickCycles -= RTC_CPU_CLOCK;
		RTCTick();
	}
}

/*-------------------------------------------------------------------------*/

void RTCSetFileStoreClockSettable(bool Settable)
{
	FileStoreClockSettable = Settable;

	if (!Settable)
	{
		FileStoreClockOffset = 0;
	}

	if (IsFileStore(MachineType) && pCMOS != nullptr)
	{
		RTCTimeOffset = FileStoreClockOffset;
		RTCUpdate();
	}
}

bool RTCGetFileStoreClockSettable()
{
	return FileStoreClockSettable;
}

void RTCSetFileStoreClockOffset(int Offset)
{
	FileStoreClockOffset = FileStoreClockSettable ? Offset : 0;
}

int RTCGetFileStoreClockOffset()
{
	return static_cast<int>(FileStoreClockOffset);
}

/*-------------------------------------------------------------------------*/

void DebugRTCState()
{
	if (pCMOS == nullptr)
	{
		DebugDisplayInfo("RTC: Not present on this model");
		return;
	}

	DebugDisplayInfoF("RTC: Clock %02X/%02X/%02X %02X:%02X:%02X (%s)",
	                  pCMOS[7], pCMOS[8], pCMOS[9],
	                  pCMOS[4], pCMOS[2], pCMOS[0],
	                  (pCMOS[RTC146818_REG_B] & RTC146818_REG_B_BINARY) ? "binary" : "BCD");

	DebugDisplayInfoF("RTC: Register A %02X, B %02X, C %02X, D %02X",
	                  pCMOS[RTC146818_REG_A], pCMOS[RTC146818_REG_B],
	                  pCMOS[RTC146818_REG_C], pCMOS[RTC146818_REG_D]);

	DebugDisplayInfoF("RTC: Chip enable %s, address %02X",
	                  CMOS.ChipEnable ? "on" : "off", CMOS.Address);

	// CMOS RAM starts at address 0x0e
	for (int Row = 0x00; Row < 0x40; Row += 0x10)
	{
		char Line[80];
		int Length = sprintf(Line, "CMOS: %02X:", Row);

		for (int Address = Row; Address < Row + 0x10; Address++)
		{
			if (Address < 0x0e)
			{
				Length += sprintf(Line + Length, "   ");
			}
			else
			{
				Length += sprintf(Line + Length, " %02X", pCMOS[Address]);
			}
		}

		DebugDisplayInfo(Line);
	}
}

/*-------------------------------------------------------------------------*/
