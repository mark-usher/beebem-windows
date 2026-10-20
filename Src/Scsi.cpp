/****************************************************************
BeebEm - BBC Micro and Master 128 Emulator
Copyright (C) 2006  Jon Welch

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

// SCSI support for BeebEm
// Based on code written by Y. Tanaka
// 26/12/2011 JGH: Disk images at DiscsPath, not AppPath

// The Acorn SCSI host adaptor, as Pi1MHz emulates it (harddisc_emulator.c,
// https://github.com/dp111/Pi1MHz), with Pi1MHz's BeebSCSI drives behind it
// (BeebSCSI/scsi.cpp): drive controllers, mode pages and the scsiN.cfg
// drive descriptors.
//
// BeebSCSI is written for its own processor, and waits for the host's
// acknowledgement of each byte.  Here it runs as a fiber: each access the
// host makes to the data or select register runs it until it next waits
// for the host.

/*
Offset  Description                 Access
+00     data                        R/W
+01     read status                 R
+02     write select                W
+03     write irq enable            W
*/

#include <windows.h>

#include <string.h>

#include "Scsi.h"
#include "6502core.h"
#include "BeebMem.h"
#include "BeebWin.h"
#include "FileStore.h"
#include "FileUtils.h"
#include "Main.h"
#include "Model.h"
#include "BeebSCSI/filesystem.h"
#include "BeebSCSI/hostadapter.h"
#include "BeebSCSI/scsi.h"
#include "BeebSCSI/statusled.h"

/****************************************************************************/

// Status register bits
constexpr unsigned char STATUS_CND = 0x80; // Command, not data
constexpr unsigned char STATUS_INO = 0x40; // Input, not output
constexpr unsigned char STATUS_REQ = 0x20; // Request
constexpr unsigned char STATUS_IRQ = 0x10; // Interrupt request
constexpr unsigned char STATUS_BSY = 0x02; // Busy
constexpr unsigned char STATUS_MSG = 0x01; // Message

static unsigned char Status;      // Status register, without the IRQ bit
static bool IRQ;                  // Interrupt request
static bool IRQEnable;            // Written to +03
static unsigned char HostData;    // Last byte the host wrote (data or select)
static unsigned char TargetData;  // Data register, as the drive set it
static bool Ack;                  // The host has read or written the data register
static bool Select;               // The host has written the select register
static bool HostRestarted;        // A selection or reset ended a wait for Ack

static LPVOID MainFiber = nullptr;
static LPVOID ScsiFiber = nullptr;

bool SCSIDriveEnabled = false;

/****************************************************************************/

static void UpdateIRQ()
{
	if (IRQ)
	{
		IntStatus |= IRQ_HDC;
	}
	else
	{
		IntStatus &= ~IRQ_HDC;
	}
}

/****************************************************************************/

static void SetStatus(unsigned char Bits, bool State)
{
	if (State)
	{
		Status |= Bits;
	}
	else
	{
		Status &= ~Bits;
	}
}

/****************************************************************************/

// Hand back to the emulator until the host next accesses the drive

static void WaitForHost()
{
	SwitchToFiber(MainFiber);
}

/****************************************************************************/

static void CALLBACK ScsiFiberProc(void* /* Parameter */)
{
	for (;;)
	{
		scsiProcessEmulation();

		if (scsiDiagState() == SCSI_BUSFREE)
		{
			// A command has ended: run BUS FREE now, which drops BSY and
			// takes a selection the host has already made, before waiting
			scsiProcessEmulation();

			if (scsiDiagState() == SCSI_BUSFREE)
			{
				WaitForHost();
			}
		}
	}
}

/****************************************************************************/

// Run the drive until it waits for the host again

static void RunDrive()
{
	if (MainFiber == nullptr)
	{
		MainFiber = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(nullptr);

		if (MainFiber == nullptr)
		{
			return;
		}
	}

	if (ScsiFiber == nullptr)
	{
		ScsiFiber = CreateFiber(256 * 1024, ScsiFiberProc, nullptr);

		if (ScsiFiber == nullptr)
		{
			return;
		}
	}

	SwitchToFiber(ScsiFiber);
}

/****************************************************************************/

// Put the host adaptor and the drive back to BUS FREE.  A command in
// progress is abandoned, as on a BBC reset.

static void BusReset()
{
	if (ScsiFiber != nullptr)
	{
		DeleteFiber(ScsiFiber);
		ScsiFiber = nullptr;
	}

	Status = 0;
	IRQ = false;
	IRQEnable = false;
	Ack = false;
	Select = false;
	HostRestarted = false;

	UpdateIRQ();

	statusledActivity(0);

	scsiReset(0);
}

/****************************************************************************/

// The folder with the default drive descriptor, defscsi.cfg (used for a
// drive with no scsiN.cfg of its own; a defscsi.cfg in the hard drive folder
// comes first): the user data folder, or the UserData folder BeebEm was
// installed with, for a user data folder made before defscsi.cfg existed

static const char* GetDefaultDescriptorFolder()
{
	static char Folder[MAX_PATH];
	char FileName[MAX_PATH];

	strcpy(Folder, mainWin->GetUserDataPath());
	MakeFileName(FileName, MAX_PATH, Folder, "defscsi.cfg");

	if (GetFileAttributes(FileName) == INVALID_FILE_ATTRIBUTES)
	{
		MakeFileName(Folder, MAX_PATH, mainWin->GetAppPath(), "UserData");
	}

	return Folder;
}

/****************************************************************************/

void SCSIReset()
{
	BusReset();

	// The drives may have changed (another hard drive folder, or another
	// FileStore drive setting): stop them all, so each is opened again
	// when the host next uses it
	filesystemSetPaths(HardDrivePath, GetDefaultDescriptorFolder());
	filesystemReset();
}

/****************************************************************************/

void SCSIWrite(int Address, unsigned char Value)
{
	if (!SCSIDriveEnabled)
		return;

	switch (Address)
	{
		case 0x00: // Data register: the host has written a byte
			HostData = Value;
			SetStatus(STATUS_REQ, false);
			Ack = true;
			RunDrive();
			break;

		case 0x01:
			break;

		case 0x02: // Select: the target ID bit pattern on the data bus
			HostData = Value;
			Select = true;
			RunDrive();
			break;

		case 0x03: // Interrupt enable (D0)
			if ((Value & 1) == 0)
			{
				IRQEnable = false;
				IRQ = false;
			}
			else
			{
				IRQEnable = true;

				if (Status & STATUS_REQ)
				{
					IRQ = true;
				}
			}

			UpdateIRQ();
			break;
	}
}

/****************************************************************************/

unsigned char SCSIRead(int Address)
{
	if (!SCSIDriveEnabled)
		return 0xff;

	unsigned char Data = 0xff;

	switch (Address)
	{
		case 0x00: // Data register: the host has taken the byte
			Data = TargetData;
			SetStatus(STATUS_REQ, false);
			Ack = true;
			RunDrive();
			break;

		case 0x01: // Status register
			Data = Status;

			if (IRQ)
			{
				Data |= STATUS_IRQ;
			}
			break;

		case 0x02:
		case 0x03:
			break;
	}

	return Data;
}

/****************************************************************************/

void SCSIClose()
{
	BusReset();
	filesystemReset();
}

/****************************************************************************/

// The host adaptor, for BeebSCSI (BeebSCSI/hostadapter.h)

/****************************************************************************/

// Wait for the host to read or write the data register.  False if the host
// started again (a selection or a reset) instead.

static bool WaitAck()
{
	while (!Ack && !Select)
	{
		WaitForHost();
	}

	if (!Ack)
	{
		HostRestarted = true;
		return false;
	}

	return true;
}

/****************************************************************************/

uint8_t hostadapterReadDatabus()
{
	return HostData;
}

/****************************************************************************/

uint8_t hostadapterReadByte()
{
	hostadapterWriteRequestFlag(true);
	WaitAck();
	hostadapterWriteRequestFlag(false);

	return HostData;
}

/****************************************************************************/

void hostadapterWriteByte(uint8_t databusValue)
{
	TargetData = databusValue;

	hostadapterWriteRequestFlag(true);
	WaitAck();
	hostadapterWriteRequestFlag(false);
}

/****************************************************************************/

uint32_t hostadapterPerformReadDMALen(const uint8_t *dataBuffer, uint32_t length)
{
	uint32_t currentByte = 0;

	do
	{
		TargetData = dataBuffer[currentByte++];

		hostadapterWriteRequestFlag(true);

		if (!WaitAck())
		{
			return currentByte - 1;
		}

		hostadapterWriteRequestFlag(false);
	}
	while (currentByte < length);

	return currentByte;
}

/****************************************************************************/

uint32_t hostadapterPerformReadDMA(const uint8_t *dataBuffer)
{
	return hostadapterPerformReadDMALen(dataBuffer, 256);
}

/****************************************************************************/

uint32_t hostadapterPerformWriteDMALen(uint8_t *dataBuffer, uint32_t length)
{
	uint32_t currentByte = 0;

	do
	{
		hostadapterWriteRequestFlag(true);

		if (!WaitAck())
		{
			return currentByte;
		}

		dataBuffer[currentByte++] = HostData;

		hostadapterWriteRequestFlag(false);
	}
	while (currentByte < length);

	return currentByte;
}

/****************************************************************************/

uint32_t hostadapterPerformWriteDMA(uint8_t *dataBuffer)
{
	return hostadapterPerformWriteDMALen(dataBuffer, 256);
}

/****************************************************************************/

bool hostadapterConnectedToExternalBus()
{
	return true;
}

/****************************************************************************/

void hostadapterWriteResetFlag(bool flagState)
{
	HostRestarted = flagState;
}

/****************************************************************************/

bool hostadapterReadResetFlag()
{
	return HostRestarted;
}

/****************************************************************************/

void hostadapterWriteDataPhaseFlags(bool message, bool commandNotData, bool inputNotOutput)
{
	SetStatus(STATUS_MSG, message);
	SetStatus(STATUS_CND, commandNotData);
	SetStatus(STATUS_INO, inputNotOutput);
}

/****************************************************************************/

void hostadapterWriteBusyFlag(bool flagState)
{
	SetStatus(STATUS_BSY, flagState);

	if (flagState)
	{
		Select = false;
	}
}

/****************************************************************************/

void hostadapterWriteRequestFlag(bool flagState)
{
	if (flagState)
	{
		Ack = false;
		SetStatus(STATUS_REQ, true);

		if (IRQEnable)
		{
			IRQ = true;
			UpdateIRQ();
		}
	}
	else
	{
		SetStatus(STATUS_REQ, false);
	}
}

/****************************************************************************/

bool hostadapterReadSelectFlag()
{
	return Select;
}

/****************************************************************************/

// On a FileStore the drives 0-3 are SCSI targets 0-3, selected by their ID
// bit, and a drive slot set to None (or Auto, with no image) has no drive
// to answer.  ADFS selects one target, its drives being LUNs 0-7.

bool hostadapterTargetAttached(uint8_t HostID)
{
	if (!IsFileStore(MachineType))
	{
		return true;
	}

	int Drive;

	switch (HostID)
	{
		case 0x01: Drive = 0; break;
		case 0x02: Drive = 1; break;
		case 0x04: Drive = 2; break;
		case 0x08: Drive = 3; break;
		default:   Drive = -1; break;
	}

	bool Attached = Drive >= 0 && FileStoreDrive[Drive] != FileStoreDriveType::None;

	if (Attached && FileStoreDrive[Drive] == FileStoreDriveType::Auto)
	{
		char FileName[MAX_PATH];
		MakeFileName(FileName, MAX_PATH, HardDrivePath, "scsi%d.dat", Drive);

		Attached = GetFileAttributes(FileName) != INVALID_FILE_ATTRIBUTES;
	}

	if (!Attached)
	{
		// The selection goes unanswered: the host times out
		Select = false;
	}

	return Attached;
}

/****************************************************************************/

// The drive LEDs

void statusledActivity(uint8_t ledState)
{
	for (int i = 0; i < 4; i++)
	{
		LEDs.HDisc[i] = false;
	}

	if (ledState)
	{
		const uint8_t Lun = scsiCurrentLun();

		if (Lun < 4)
		{
			LEDs.HDisc[Lun] = true;
		}
	}
}
