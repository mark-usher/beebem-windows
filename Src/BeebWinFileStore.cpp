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

// BeebWin support for the Acorn FileStore E01/E01S: front panel image,
// LEDs and drive door

#include <windows.h>

#include "BeebWin.h"
#include "FileStore.h"
#include "Main.h"
#include "Resource.h"
#include "Scsi.h"

/****************************************************************************/

// Open or close the FileStore drive door. Opening the door puts the
// FileStore into maintenance mode, which flushes its buffers to disc.

void BeebWin::SetFileStoreDoorOpen(bool Open)
{
	FileStoreDoorOpen = Open;
	CheckMenuItem(IDM_FILESTORE_DOOR_OPEN, FileStoreDoorOpen);

	// Only allow floppy discs to be changed when the door is open
	UpdateFileStoreDiscMenu();

	// Show the drives when the door is open
	LoadFileStoreBitmap();
}

/****************************************************************************/

void BeebWin::UpdateFileStoreDiscMenu()
{
	const bool Enabled = !IsFileStore(MachineType) || FileStoreDoorOpen;

	EnableMenuItem(IDM_LOADDISC0, Enabled && MachineType != Model::MasterET);
	EnableMenuItem(IDM_LOADDISC1, Enabled && MachineType != Model::MasterET);
	EnableMenuItem(IDM_NEWDISC0, Enabled && MachineType != Model::MasterET);
	EnableMenuItem(IDM_NEWDISC1, Enabled && MachineType != Model::MasterET);
	EnableMenuItem(IDM_EJECTDISC0, Enabled && MachineType != Model::MasterET);
	EnableMenuItem(IDM_EJECTDISC1, Enabled && MachineType != Model::MasterET);
}

/****************************************************************************/

// Load the front panel image for the current model and door state, or
// release it if the current model isn't a FileStore.

void BeebWin::LoadFileStoreBitmap()
{
	if (m_hFileStoreBitmap == nullptr && !IsFileStore(MachineType))
	{
		return;
	}

	if (m_hFileStoreBitmap != nullptr)
	{
		DeleteObject(m_hFileStoreBitmap);
		m_hFileStoreBitmap = nullptr;
	}

	if (IsFileStore(MachineType))
	{
		int BitmapID;

		if (FileStoreDoorOpen)
		{
			BitmapID = IDB_FILESTORE_DRIVES;
		}
		else
		{
			BitmapID = MachineType == Model::FileStoreE01 ? IDB_FILESTORE_E01 : IDB_FILESTORE_E01S;
		}

		m_hFileStoreBitmap = (HBITMAP)LoadImage(hInst,
		                                        MAKEINTRESOURCE(BitmapID),
		                                        IMAGE_BITMAP,
		                                        0,
		                                        0,
		                                        LR_CREATEDIBSECTION);
	}

	// Force the LEDs to be redrawn
	m_FileStoreLEDState = -1;

	if (m_hWnd != nullptr)
	{
		InvalidateRect(m_hWnd, nullptr, TRUE);
	}
}

/****************************************************************************/

// Draw the front panel image and LEDs. The FileStore has no video output,
// so this is done with GDI whichever display renderer is selected.

void BeebWin::PaintFileStore(HDC hDC)
{
	RECT ClientRect;
	GetClientRect(m_hWnd, &ClientRect);
	FillRect(hDC, &ClientRect, (HBRUSH)GetStockObject(BLACK_BRUSH));

	if (m_hFileStoreBitmap != nullptr)
	{
		HDC hMemDC = CreateCompatibleDC(hDC);
		HGDIOBJ hPrevBitmap = SelectObject(hMemDC, m_hFileStoreBitmap);

		BITMAP Bitmap;
		GetObject(m_hFileStoreBitmap, sizeof(Bitmap), &Bitmap);

		BitBlt(hDC, 0, 0, Bitmap.bmWidth, Bitmap.bmHeight, hMemDC, 0, 0, SRCCOPY);

		SelectObject(hMemDC, hPrevBitmap);
		DeleteDC(hMemDC);
	}

	DrawFileStoreLEDs(hDC);
}

/****************************************************************************/

static void DrawFileStoreLED(HDC hDC, int x, int y, bool On, COLORREF OnColour, COLORREF OffColour)
{
	HBRUSH hBrush = CreateSolidBrush(On ? OnColour : OffColour);
	HGDIOBJ hPrevBrush = SelectObject(hDC, hBrush);

	Rectangle(hDC, x, y, x + 7, y + 7);

	SelectObject(hDC, hPrevBrush);
	DeleteObject(hBrush);
}

/****************************************************************************/

// Returns the LED state as a bit mask, so changes can be detected

static int GetFileStoreLEDState()
{
	int State = 0;

	if (FileStoreModeLED)     State |= 0x01;
	if (LEDs.FloppyDisc[0])   State |= 0x02;
	if (LEDs.FloppyDisc[1])   State |= 0x04;
	if (SCSIDriveEnabled)     State |= 0x08;

	for (int i = 0; i < 4; i++)
	{
		if (LEDs.HDisc[i])
		{
			State |= 0x10 << i;
		}
	}

	return State;
}

/****************************************************************************/

void BeebWin::DrawFileStoreLEDs(HDC hDC)
{
	const COLORREF Red      = RGB(255, 0, 0);
	const COLORREF DarkRed  = RGB(54, 0, 0);
	const COLORREF Green    = RGB(0, 255, 0);
	const COLORREF DarkGreen = RGB(0, 54, 0);
	const COLORREF Black    = RGB(0, 0, 0);

	// MODE LED
	DrawFileStoreLED(hDC, 487, 34, FileStoreModeLED, Red, DarkRed);

	// Floppy drives 0 and 1 (drives 4 and 5 on the FileStore)
	DrawFileStoreLED(hDC, 173, 50, LEDs.FloppyDisc[0], Green, DarkGreen);
	DrawFileStoreLED(hDC, 390, 50, LEDs.FloppyDisc[1], Green, DarkGreen);

	// Hard drives 0-3, not shown if SCSI is disabled
	for (int i = 0; i < 4; i++)
	{
		DrawFileStoreLED(hDC, 560 + i * 20, 80, LEDs.HDisc[i],
		                 SCSIDriveEnabled ? Red : Black,
		                 SCSIDriveEnabled ? DarkRed : Black);
	}

	m_FileStoreLEDState = GetFileStoreLEDState();
}

/****************************************************************************/

// Called every emulated frame. Redraws the LEDs if any have changed.

void BeebWin::UpdateFileStoreLEDs()
{
	if (GetFileStoreLEDState() != m_FileStoreLEDState)
	{
		HDC hDC = GetDC(m_hWnd);
		DrawFileStoreLEDs(hDC);
		ReleaseDC(m_hWnd, hDC);
	}
}

/****************************************************************************/
