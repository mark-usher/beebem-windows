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

// BeebWin support for the Acorn FileStore E01/E01S: front panel drawing,
// LEDs, drive door and shutdown

#include <windows.h>

#include <stdio.h>

#include <algorithm>

#include "BeebWin.h"
#include "BeebMem.h"
#include "FileUtils.h"
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

	// Show the floppy drives when the door is open
	UpdateFileStorePanel();
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

// The FileStore is a headless device, so the emulator window shows a
// drawing of its front panel: the E01/E01S unit (with the door flap closed,
// or open to show the floppy drives), and below it one E20/E40S/E60S hard
// drive unit for each SCSI drive image found. Everything is drawn with GDI,
// scaled to the window, so no bitmaps are needed.

// Look for the SCSI drive images, and redraw the front panel.

void BeebWin::UpdateFileStorePanel()
{
	for (int Drive = 0; Drive < 4; Drive++)
	{
		m_FileStoreDriveSize[Drive] = -1;

		if (IsFileStore(MachineType) && SCSIDriveEnabled)
		{
			char FileName[MAX_PATH];
			MakeFileName(FileName, MAX_PATH, HardDrivePath, "scsi%d.dat", Drive);

			WIN32_FILE_ATTRIBUTE_DATA Data;

			if (GetFileAttributesEx(FileName, GetFileExInfoStandard, &Data))
			{
				const unsigned long long Size =
					((unsigned long long)Data.nFileSizeHigh << 32) | Data.nFileSizeLow;

				m_FileStoreDriveSize[Drive] = (int)(Size / (1024 * 1024));
			}
		}
	}

	// Force the LEDs to be redrawn
	m_FileStoreLEDState = -1;

	if (m_hWnd != nullptr)
	{
		InvalidateRect(m_hWnd, nullptr, FALSE);
	}
}

/****************************************************************************/

// Colours

static const COLORREF FS_BACKGROUND   = RGB(32, 32, 36);
static const COLORREF FS_CASE         = RGB(214, 206, 184); // Cream case
static const COLORREF FS_CASE_SHADOW  = RGB(160, 152, 132);
static const COLORREF FS_CASE_HIGHLIGHT = RGB(236, 230, 214);
static const COLORREF FS_FLAP         = RGB(52, 52, 56);    // Door flap strip
static const COLORREF FS_FLAP_DRIVE   = RGB(70, 66, 60);    // Drive unit flap strip
static const COLORREF FS_RECESS       = RGB(18, 18, 20);    // Behind the open door
static const COLORREF FS_FLOPPY       = RGB(46, 46, 50);    // Floppy drive fascia
static const COLORREF FS_SLOT         = RGB(8, 8, 8);
static const COLORREF FS_LABEL        = RGB(40, 40, 44);
static const COLORREF FS_LABEL_TEXT   = RGB(230, 230, 230);
static const COLORREF FS_TEXT         = RGB(60, 56, 50);
static const COLORREF FS_RED_ON       = RGB(255, 40, 30);
static const COLORREF FS_RED_OFF      = RGB(70, 10, 8);
static const COLORREF FS_GREEN_ON     = RGB(40, 255, 60);
static const COLORREF FS_GREEN_OFF    = RGB(10, 60, 14);

/****************************************************************************/

static void FillSolidRect(HDC hDC, int Left, int Top, int Right, int Bottom, COLORREF Colour)
{
	RECT Rect = { Left, Top, Right, Bottom };
	HBRUSH hBrush = CreateSolidBrush(Colour);
	FillRect(hDC, &Rect, hBrush);
	DeleteObject(hBrush);
}

/****************************************************************************/

static void DrawLED(HDC hDC, int x, int y, int Size, bool On, COLORREF OnColour, COLORREF OffColour)
{
	HBRUSH hBrush = CreateSolidBrush(On ? OnColour : OffColour);
	HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
	HGDIOBJ hPrevBrush = SelectObject(hDC, hBrush);
	HGDIOBJ hPrevPen = SelectObject(hDC, hPen);

	Ellipse(hDC, x, y, x + Size, y + Size);

	SelectObject(hDC, hPrevPen);
	SelectObject(hDC, hPrevBrush);
	DeleteObject(hPen);
	DeleteObject(hBrush);
}

/****************************************************************************/

static void DrawPanelText(HDC hDC, const char* Text, int x, int y, int Height, int Weight, COLORREF Colour)
{
	HFONT hFont = CreateFont(Height, 0, 0, 0, Weight, FALSE, FALSE, FALSE,
	                         ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
	                         ANTIALIASED_QUALITY, VARIABLE_PITCH | FF_SWISS, "Arial");
	HGDIOBJ hPrevFont = SelectObject(hDC, hFont);

	SetBkMode(hDC, TRANSPARENT);
	SetTextColor(hDC, Colour);
	TextOut(hDC, x, y, Text, (int)strlen(Text));

	SelectObject(hDC, hPrevFont);
	DeleteObject(hFont);
}

/****************************************************************************/

// Layout of one unit (E01/E01S or a hard drive unit), in pixels

struct FileStoreUnitLayout
{
	int Left, Top, Width, Height;

	int X(double f) const { return Left + (int)(f * Width); }
	int Y(double f) const { return Top + (int)(f * Height); }
};

// Position of the green (power) and red (MODE / drive activity) LEDs,
// the same on every unit
constexpr double FS_LED_X_GREEN = 0.905;
constexpr double FS_LED_X_RED   = 0.935;
constexpr double FS_LED_Y       = 0.42;

/****************************************************************************/

static void DrawUnitCase(HDC hDC, const FileStoreUnitLayout& Unit)
{
	// Case with a highlight along the top and a shadow along the bottom
	FillSolidRect(hDC, Unit.Left, Unit.Top, Unit.Left + Unit.Width, Unit.Top + Unit.Height, FS_CASE);
	FillSolidRect(hDC, Unit.Left, Unit.Top, Unit.Left + Unit.Width, Unit.Y(0.04) + 1, FS_CASE_HIGHLIGHT);
	FillSolidRect(hDC, Unit.Left, Unit.Y(0.94), Unit.Left + Unit.Width, Unit.Top + Unit.Height, FS_CASE_SHADOW);
}

/****************************************************************************/

static void DrawUnitLabel(HDC hDC, const FileStoreUnitLayout& Unit, const char* Name, const char* Model, const char* SubText)
{
	// Label panel on the left hand side
	FillSolidRect(hDC, Unit.X(0.025), Unit.Y(0.18), Unit.X(0.185), Unit.Y(0.82), FS_LABEL);

	const int TextHeight = std::max(8, (int)(Unit.Height * 0.15));

	DrawPanelText(hDC, "Acorn", Unit.X(0.035), Unit.Y(0.21), TextHeight, FW_BOLD, FS_LABEL_TEXT);

	if (Name != nullptr)
	{
		DrawPanelText(hDC, Name, Unit.X(0.035), Unit.Y(0.40), TextHeight, FW_NORMAL, FS_LABEL_TEXT);
	}

	DrawPanelText(hDC, Model, Unit.X(0.035), Name != nullptr ? Unit.Y(0.59) : Unit.Y(0.45), TextHeight, FW_BOLD, FS_LABEL_TEXT);

	if (SubText != nullptr)
	{
		DrawPanelText(hDC, SubText, Unit.X(0.21), Unit.Y(0.80), std::max(7, (int)(Unit.Height * 0.11)), FW_NORMAL, FS_TEXT);
	}
}

/****************************************************************************/

static void DrawFloppyDrive(HDC hDC, const FileStoreUnitLayout& Unit, double Left, double Right, bool LED, const char* Name)
{
	FillSolidRect(hDC, Unit.X(Left), Unit.Y(0.24), Unit.X(Right), Unit.Y(0.76), FS_FLOPPY);

	// Disc slot and eject button
	FillSolidRect(hDC, Unit.X(Left + 0.04), Unit.Y(0.46), Unit.X(Right - 0.04), Unit.Y(0.53), FS_SLOT);
	FillSolidRect(hDC, Unit.X(Right - 0.07), Unit.Y(0.60), Unit.X(Right - 0.03), Unit.Y(0.70), FS_CASE_SHADOW);

	// Activity LED
	const int LEDSize = std::max(4, (int)(Unit.Height * 0.09));
	DrawLED(hDC, Unit.X(Left + 0.02), Unit.Y(0.62), LEDSize, LED, FS_GREEN_ON, FS_GREEN_OFF);

	DrawPanelText(hDC, Name, Unit.X(Left + 0.02), Unit.Y(0.27), std::max(7, (int)(Unit.Height * 0.11)), FW_NORMAL, FS_LABEL_TEXT);
}

/****************************************************************************/

static const char* GetDriveUnitName(int SizeMB)
{
	if (SizeMB > 0 && SizeMB <= 25) return "E20";
	if (SizeMB > 25 && SizeMB <= 45) return "E40S";
	if (SizeMB > 45 && SizeMB <= 70) return "E60S";
	return "SCSI drive";
}

/****************************************************************************/

void BeebWin::PaintFileStore(HDC hDC)
{
	RECT ClientRect;
	GetClientRect(m_hWnd, &ClientRect);

	const int ClientWidth = ClientRect.right - ClientRect.left;
	const int ClientHeight = ClientRect.bottom - ClientRect.top;

	if (ClientWidth <= 0 || ClientHeight <= 0)
	{
		return;
	}

	// Draw into a memory bitmap to avoid flicker
	HDC hMemDC = CreateCompatibleDC(hDC);
	HBITMAP hBitmap = CreateCompatibleBitmap(hDC, ClientWidth, ClientHeight);
	HGDIOBJ hPrevBitmap = SelectObject(hMemDC, hBitmap);

	FillSolidRect(hMemDC, 0, 0, ClientWidth, ClientHeight, FS_BACKGROUND);

	int DriveCount = 0;

	for (int Drive = 0; Drive < 4; Drive++)
	{
		if (m_FileStoreDriveSize[Drive] >= 0)
		{
			DriveCount++;
		}
	}

	// Units are about 6:1, stacked with a small gap, scaled to fit
	const int Margin = ClientWidth / 40;
	int UnitWidth = ClientWidth - 2 * Margin;
	int UnitHeight = UnitWidth / 6;
	const int Units = 1 + DriveCount;

	if (Units * (UnitHeight + Margin / 2) + Margin > ClientHeight)
	{
		UnitHeight = (ClientHeight - Margin) / Units - Margin / 2;
		UnitWidth = UnitHeight * 6;
	}

	FileStoreUnitLayout Unit;
	Unit.Left = (ClientWidth - UnitWidth) / 2;
	Unit.Top = Margin;
	Unit.Width = UnitWidth;
	Unit.Height = UnitHeight;

	const int LEDSize = std::max(4, (int)(UnitHeight * 0.1));

	// FileStore E01/E01S
	DrawUnitCase(hMemDC, Unit);
	DrawUnitLabel(hMemDC, Unit, "FileStore",
	              MachineType == Model::FileStoreE01 ? "E01" : "E01S",
	              nullptr);

	if (FileStoreDoorOpen)
	{
		// Door open: the floppy drives (drives 4 and 5)
		FillSolidRect(hMemDC, Unit.X(0.21), Unit.Y(0.18), Unit.X(0.87), Unit.Y(0.82), FS_RECESS);
		DrawFloppyDrive(hMemDC, Unit, 0.23, 0.53, LEDs.FloppyDisc[0], "4");
		DrawFloppyDrive(hMemDC, Unit, 0.55, 0.85, LEDs.FloppyDisc[1], "5");
	}
	else
	{
		// Door flap strip
		FillSolidRect(hMemDC, Unit.X(0.21), Unit.Y(0.30), Unit.X(0.87), Unit.Y(0.70), FS_FLAP);
	}

	DrawLED(hMemDC, Unit.X(FS_LED_X_GREEN), Unit.Y(FS_LED_Y), LEDSize, true, FS_GREEN_ON, FS_GREEN_OFF);
	DrawLED(hMemDC, Unit.X(FS_LED_X_RED), Unit.Y(FS_LED_Y), LEDSize, FileStoreModeLED, FS_RED_ON, FS_RED_OFF);
	DrawPanelText(hMemDC, "MODE", Unit.X(FS_LED_X_RED - 0.01), Unit.Y(0.62), std::max(7, (int)(UnitHeight * 0.1)), FW_NORMAL, FS_TEXT);

	// Hard drive units, stacked underneath
	for (int Drive = 0; Drive < 4; Drive++)
	{
		if (m_FileStoreDriveSize[Drive] < 0)
		{
			continue;
		}

		Unit.Top += UnitHeight + Margin / 2;

		char SubText[40];
		sprintf(SubText, "Drive %d  %d MB", Drive, m_FileStoreDriveSize[Drive]);

		DrawUnitCase(hMemDC, Unit);
		DrawUnitLabel(hMemDC, Unit, nullptr, GetDriveUnitName(m_FileStoreDriveSize[Drive]), SubText);
		FillSolidRect(hMemDC, Unit.X(0.21), Unit.Y(0.30), Unit.X(0.87), Unit.Y(0.70), FS_FLAP_DRIVE);

		DrawLED(hMemDC, Unit.X(FS_LED_X_GREEN), Unit.Y(FS_LED_Y), LEDSize, true, FS_GREEN_ON, FS_GREEN_OFF);
		DrawLED(hMemDC, Unit.X(FS_LED_X_RED), Unit.Y(FS_LED_Y), LEDSize, LEDs.HDisc[Drive], FS_RED_ON, FS_RED_OFF);
	}

	BitBlt(hDC, 0, 0, ClientWidth, ClientHeight, hMemDC, 0, 0, SRCCOPY);

	SelectObject(hMemDC, hPrevBitmap);
	DeleteObject(hBitmap);
	DeleteDC(hMemDC);
}

/****************************************************************************/

// Returns the LED state as a bit mask, so changes can be detected

static int GetFileStoreLEDState()
{
	int State = 0;

	if (FileStoreModeLED)     State |= 0x01;
	if (LEDs.FloppyDisc[0])   State |= 0x02;
	if (LEDs.FloppyDisc[1])   State |= 0x04;

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

// Called every emulated frame. Redraws the front panel if any LEDs have
// changed.

void BeebWin::UpdateFileStoreLEDs()
{
	const int State = GetFileStoreLEDState();

	if (State != m_FileStoreLEDState)
	{
		m_FileStoreLEDState = State;
		InvalidateRect(m_hWnd, nullptr, FALSE);
	}
}

/****************************************************************************/

// Before the FileStore is reset, closed or changed to another model, it
// must flush its buffers to disc. Opening the drive door puts it into
// maintenance mode, which does this, so open the door and wait two seconds
// before carrying out the action. If the door is already open, the
// buffers have already been flushed.
//
// Returns true if the action will be carried out later by
// CompleteFileStoreShutdown(), or false if the caller should carry it out
// now (not a FileStore).

bool BeebWin::BeginFileStoreShutdown(FileStoreShutdownAction Action, Model NewModelType)
{
	if (!IsFileStore(MachineType))
	{
		return false;
	}

	if (m_FileStoreShutdownAction != FileStoreShutdownAction::None)
	{
		// Already shutting down: the latest request wins
		m_FileStoreShutdownAction = Action;
		m_FileStoreShutdownModel = NewModelType;
		return true;
	}

	m_FileStoreShutdownAction = Action;
	m_FileStoreShutdownModel = NewModelType;

	if (FileStoreDoorOpen)
	{
		CompleteFileStoreShutdown();
	}
	else
	{
		SetFileStoreDoorOpen(true);
		SetTimer(m_hWnd, TIMER_FILESTORE_SHUTDOWN, 2000, nullptr);
	}

	return true;
}

/****************************************************************************/

void BeebWin::CompleteFileStoreShutdown()
{
	const FileStoreShutdownAction Action = m_FileStoreShutdownAction;
	m_FileStoreShutdownAction = FileStoreShutdownAction::None;

	// Close the door and leave command mode, ready for the next start
	FileStoreDoorOpen = false;
	CheckMenuItem(IDM_FILESTORE_DOOR_OPEN, false);
	FileStoreCommandMode = false;
	CheckMenuItem(IDM_FILESTORE_COMMAND_MODE, false);

	switch (Action)
	{
		case FileStoreShutdownAction::Reset:
			ResetBeebSystem(MachineType, false);
			UpdateFileStoreDiscMenu();
			break;

		case FileStoreShutdownAction::Exit:
			DestroyWindow(m_hWnd);
			break;

		case FileStoreShutdownAction::ChangeModel:
			ResetBeebSystem(m_FileStoreShutdownModel, true);
			UpdateModelMenu();
			break;

		case FileStoreShutdownAction::None:
		default:
			break;
	}
}

/****************************************************************************/
