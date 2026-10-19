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
#include "Messages.h"
#include "Resource.h"
#include "Scsi.h"
#include "WindowUtils.h"

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

	FitFileStoreWindow();

	if (m_hWnd != nullptr)
	{
		InvalidateRect(m_hWnd, nullptr, FALSE);
	}
}

/****************************************************************************/

// Layout of the stack of units in the window, shared by PaintFileStore()
// and FitFileStoreWindow(). Each unit is about 5:1 including its base.

static int GetFileStoreMargin(int ClientWidth)
{
	return ClientWidth / 40;
}

static int GetFileStoreUnitGap(int ClientWidth)
{
	return GetFileStoreMargin(ClientWidth) / 3;
}

static int GetFileStoreUnitHeight(int ClientWidth)
{
	return (ClientWidth - 2 * GetFileStoreMargin(ClientWidth)) / 5;
}

/****************************************************************************/

// Make the window just tall enough for the FileStore and its hard drive
// units, so there is no empty space. The window size used by the other
// models is kept, and restored when changing to another model.

void BeebWin::FitFileStoreWindow()
{
	if (m_hWnd == nullptr || m_FullScreen)
	{
		return;
	}

	if (!IsFileStore(MachineType))
	{
		if (m_FileStoreWindowFitted)
		{
			m_FileStoreWindowFitted = false;

			PostMessage(m_hWnd,
			            WM_SET_WINDOW_CLIENT_SIZE,
			            0,
			            reinterpret_cast<LPARAM>(new WindowPos(m_FileStoreSavedWinSize[0],
			                                                   m_FileStoreSavedWinSize[1])));
		}

		return;
	}

	if (!m_FileStoreWindowFitted)
	{
		m_FileStoreSavedWinSize[0] = m_XLastWinSize;
		m_FileStoreSavedWinSize[1] = m_YLastWinSize;
		m_FileStoreWindowFitted = true;
	}

	RECT ClientRect;
	GetClientRect(m_hWnd, &ClientRect);

	const int Width = ClientRect.right - ClientRect.left;

	if (Width <= 0)
	{
		return;
	}

	int Units = 1;

	for (int Drive = 0; Drive < 4; Drive++)
	{
		if (m_FileStoreDriveSize[Drive] >= 0)
		{
			Units++;
		}
	}

	const int Height = 2 * GetFileStoreMargin(Width) +
	                   Units * GetFileStoreUnitHeight(Width) +
	                   (Units - 1) * GetFileStoreUnitGap(Width);

	if (Height != ClientRect.bottom - ClientRect.top)
	{
		PostMessage(m_hWnd,
		            WM_SET_WINDOW_CLIENT_SIZE,
		            0,
		            reinterpret_cast<LPARAM>(new WindowPos(Width, Height)));
	}
}

/****************************************************************************/

// Colours, taken from photos of the real units

static const COLORREF FS_BACKGROUND     = RGB(40, 40, 44);
static const COLORREF FS_CASE           = RGB(222, 213, 190); // Cream case
static const COLORREF FS_CASE_HIGHLIGHT = RGB(238, 232, 214);
static const COLORREF FS_CASE_SHADOW    = RGB(170, 160, 138);
static const COLORREF FS_PLINTH         = RGB(212, 202, 176); // Recessed base
static const COLORREF FS_PLINTH_SHADOW  = RGB(150, 141, 120);
static const COLORREF FS_STRIPE         = RGB(236, 232, 220); // Flap stripes
static const COLORREF FS_STRIPE_GAP     = RGB(28, 28, 30);
static const COLORREF FS_EDGE_AMBER     = RGB(214, 140, 40);  // E01S/E40S/E60S flap edge
static const COLORREF FS_EDGE_SMOKE     = RGB(150, 150, 150); // E01/E20 flap edge
static const COLORREF FS_LABEL          = RGB(18, 18, 18);
static const COLORREF FS_LABEL_TEXT     = RGB(235, 235, 235);
static const COLORREF FS_FASCIA_E01     = RGB(88, 96, 92);    // Behind the flap
static const COLORREF FS_FASCIA_E01S    = RGB(30, 30, 32);
static const COLORREF FS_FLOPPY         = RGB(24, 24, 26);
static const COLORREF FS_SHUTTER        = RGB(222, 190, 40);
static const COLORREF FS_SLOT           = RGB(8, 8, 8);
static const COLORREF FS_EJECT          = RGB(60, 60, 64);
static const COLORREF FS_SWITCH         = RGB(220, 214, 200);
static const COLORREF FS_SWITCH_ROCKER  = RGB(120, 116, 108);
static const COLORREF FS_FASCIA_TEXT    = RGB(220, 220, 220);
static const COLORREF FS_RED_ON         = RGB(255, 40, 30);
static const COLORREF FS_RED_OFF        = RGB(90, 20, 16);
static const COLORREF FS_GREEN_ON       = RGB(40, 240, 90);
static const COLORREF FS_GREEN_OFF      = RGB(20, 70, 36);
static const COLORREF FS_FLOPPY_LED_ON  = RGB(60, 200, 80);
static const COLORREF FS_FLOPPY_LED_OFF = RGB(30, 60, 36);

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
	FillSolidRect(hDC, x - 1, y - 1, x + Size + 1, y + Size + 1, RGB(0, 0, 0));
	FillSolidRect(hDC, x, y, x + Size, y + Size, On ? OnColour : OffColour);
}

/****************************************************************************/

static HFONT CreatePanelFont(int Height, bool Serif, bool Bold)
{
	return CreateFont(Height, 0, 0, 0, Bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
	                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
	                  ANTIALIASED_QUALITY,
	                  VARIABLE_PITCH | (Serif ? FF_ROMAN : FF_SWISS),
	                  Serif ? "Times New Roman" : "Arial");
}

/****************************************************************************/

static void DrawPanelText(HDC hDC, const char* Text, int x, int y, int Height, bool Serif, bool Bold, COLORREF Colour)
{
	HFONT hFont = CreatePanelFont(std::max(6, Height), Serif, Bold);
	HGDIOBJ hPrevFont = SelectObject(hDC, hFont);

	SetBkMode(hDC, TRANSPARENT);
	SetTextColor(hDC, Colour);
	TextOut(hDC, x, y, Text, (int)strlen(Text));

	SelectObject(hDC, hPrevFont);
	DeleteObject(hFont);
}

/****************************************************************************/

// Draw text in the largest serif font that fits the box, centred vertically

static void DrawFittedText(HDC hDC, const char* Text, int Left, int Top, int Right, int Bottom, bool Bold)
{
	int Height = (Bottom - Top) * 7 / 10;
	HFONT hFont = nullptr;

	SetBkMode(hDC, TRANSPARENT);
	SetTextColor(hDC, FS_LABEL_TEXT);

	for (; Height > 6; Height--)
	{
		hFont = CreatePanelFont(Height, true, Bold);
		HGDIOBJ hPrevFont = SelectObject(hDC, hFont);

		SIZE Size;
		GetTextExtentPoint32(hDC, Text, (int)strlen(Text), &Size);

		SelectObject(hDC, hPrevFont);

		if (Size.cx <= Right - Left)
		{
			break;
		}

		DeleteObject(hFont);
		hFont = nullptr;
	}

	if (hFont == nullptr)
	{
		hFont = CreatePanelFont(6, true, Bold);
	}

	HGDIOBJ hPrevFont = SelectObject(hDC, hFont);
	TextOut(hDC, Left, Top + (Bottom - Top - Height) / 2 - 1, Text, (int)strlen(Text));
	SelectObject(hDC, hPrevFont);
	DeleteObject(hFont);
}

/****************************************************************************/

// Layout of one unit (FileStore or hard drive unit), in pixels. Positions
// are given as fractions of the unit's width and height.

struct FileStoreUnitLayout
{
	int Left, Top, Width, Height;

	int X(double f) const { return Left + (int)(f * Width); }
	int Y(double f) const { return Top + (int)(f * Height); }
};

// Front flap area
constexpr double FS_FLAP_LEFT   = 0.012;
constexpr double FS_FLAP_RIGHT  = 0.988;
constexpr double FS_FLAP_TOP    = 0.10;
constexpr double FS_FLAP_BOTTOM = 0.60;

// The two LEDs (MODE/POWER on the FileStore, DRIVE/POWER on a hard drive
// unit) are in the same place on every unit
constexpr double FS_LED_X       = 0.745;
constexpr double FS_LED_SPACING = 0.045;
constexpr double FS_LED_Y       = 0.27;

/****************************************************************************/

static void DrawUnitLEDs(HDC hDC, const FileStoreUnitLayout& Unit,
                         const char* RedLabel, bool RedOn, bool FlapOpen)
{
	const int Size = std::max(4, (int)(Unit.Height * 0.07));

	const int RedX   = Unit.X(FS_LED_X);
	const int GreenX = Unit.X(FS_LED_X + FS_LED_SPACING);
	const int y      = Unit.Y(FS_LED_Y);

	DrawLED(hDC, RedX, y, Size, RedOn, FS_RED_ON, FS_RED_OFF);
	DrawLED(hDC, GreenX, y, Size, true, FS_GREEN_ON, FS_GREEN_OFF); // Power

	if (FlapOpen)
	{
		const int TextHeight = (int)(Unit.Height * 0.07);

		DrawPanelText(hDC, RedLabel, RedX - Size * 6 / 10, y + Size + 2, TextHeight, false, false, FS_FASCIA_TEXT);
		DrawPanelText(hDC, "POWER", GreenX - Size * 6 / 10, y + Size + 2, TextHeight, false, false, FS_FASCIA_TEXT);
	}
}

/****************************************************************************/

static void DrawFloppyDrive(HDC hDC, const FileStoreUnitLayout& Unit, double Left, double Right, bool LED)
{
	FillSolidRect(hDC, Unit.X(Left), Unit.Y(0.16), Unit.X(Right), Unit.Y(0.54), FS_FLOPPY);

	// Disc slot with its shutter, and the eject button
	FillSolidRect(hDC, Unit.X(Left + 0.03), Unit.Y(0.31), Unit.X(Right - 0.07), Unit.Y(0.36), FS_SHUTTER);
	FillSolidRect(hDC, Unit.X(Left + 0.035), Unit.Y(0.325), Unit.X(Right - 0.075), Unit.Y(0.345), FS_SLOT);
	FillSolidRect(hDC, Unit.X(Right - 0.06), Unit.Y(0.38), Unit.X(Right - 0.02), Unit.Y(0.50), FS_EJECT);

	// Activity LED
	const int Size = std::max(3, (int)(Unit.Height * 0.05));
	const int x = Unit.X(Left + 0.015);
	const int y = Unit.Y(0.44);
	FillSolidRect(hDC, x, y, x + Size, y + Size, LED ? FS_FLOPPY_LED_ON : FS_FLOPPY_LED_OFF);
}

/****************************************************************************/

// Draw one unit: the case, its recessed base, and the front flap (closed),
// or for the FileStore with the door open, the drives behind it.

static void DrawUnit(HDC hDC, const FileStoreUnitLayout& Unit,
                     const char* Name, bool Amber, bool FlapOpen,
                     const char* RedLabel, bool RedOn,
                     bool Floppy0, bool Floppy1)
{
	// Recessed base
	FillSolidRect(hDC, Unit.X(0.05), Unit.Y(0.66), Unit.X(0.95), Unit.Y(0.96), FS_PLINTH);
	FillSolidRect(hDC, Unit.X(0.05), Unit.Y(0.93), Unit.X(0.95), Unit.Y(0.96), FS_PLINTH_SHADOW);

	// Case front
	FillSolidRect(hDC, Unit.X(0.0), Unit.Y(0.0), Unit.X(1.0), Unit.Y(0.66), FS_CASE);
	FillSolidRect(hDC, Unit.X(0.0), Unit.Y(0.0), Unit.X(1.0), Unit.Y(0.05), FS_CASE_HIGHLIGHT);
	FillSolidRect(hDC, Unit.X(0.0), Unit.Y(0.63), Unit.X(1.0), Unit.Y(0.66), FS_CASE_SHADOW);

	const int FlapLeft   = Unit.X(FS_FLAP_LEFT);
	const int FlapRight  = Unit.X(FS_FLAP_RIGHT);
	const int FlapTop    = Unit.Y(FS_FLAP_TOP);
	const int FlapBottom = Unit.Y(FS_FLAP_BOTTOM);

	if (FlapOpen)
	{
		// Fascia with the floppy drives (drives 4 and 5), LEDs and power switch
		FillSolidRect(hDC, FlapLeft, FlapTop, FlapRight, FlapBottom, Amber ? FS_FASCIA_E01S : FS_FASCIA_E01);

		DrawFloppyDrive(hDC, Unit, 0.03, 0.36, Floppy0);
		DrawFloppyDrive(hDC, Unit, 0.375, 0.705, Floppy1);

		DrawUnitLEDs(hDC, Unit, RedLabel, RedOn, true);

		FillSolidRect(hDC, Unit.X(0.90), Unit.Y(0.22), Unit.X(0.94), Unit.Y(0.46), FS_SWITCH);
		FillSolidRect(hDC, Unit.X(0.91), Unit.Y(0.26), Unit.X(0.93), Unit.Y(0.40), FS_SWITCH_ROCKER);
		return;
	}

	// Closed flap: vertical stripes over a dark backing. The LEDs show
	// between the stripes.
	FillSolidRect(hDC, FlapLeft, FlapTop, FlapRight, FlapBottom, FS_STRIPE_GAP);

	const int Stripes = 88;
	const double Pitch = (double)(FlapRight - FlapLeft) / Stripes;

	for (int i = 0; i < Stripes; i++)
	{
		FillSolidRect(hDC,
		              FlapLeft + (int)(i * Pitch + Pitch * 0.40), FlapTop,
		              FlapLeft + (int)((i + 1) * Pitch), FlapBottom,
		              FS_STRIPE);
	}

	DrawUnitLEDs(hDC, Unit, RedLabel, RedOn, false);

	// Flap edge: amber on the E01S, E40S and E60S, smoked on the E01 and E20
	const COLORREF Edge = Amber ? FS_EDGE_AMBER : FS_EDGE_SMOKE;
	const int EdgeHeight = std::max(2, (int)(Unit.Height * 0.03));
	const int EdgeWidth = std::max(2, (int)(Unit.Width * 0.004));

	FillSolidRect(hDC, FlapLeft, FlapTop, FlapRight, FlapTop + EdgeHeight, Edge);
	FillSolidRect(hDC, FlapLeft, FlapBottom - EdgeHeight, FlapRight, FlapBottom, Edge);
	FillSolidRect(hDC, FlapLeft, FlapTop, FlapLeft + EdgeWidth, FlapBottom, Edge);
	FillSolidRect(hDC, FlapRight - EdgeWidth, FlapTop, FlapRight, FlapBottom, Edge);

	// Model label on the left, Acorn badge on the right
	char Label[40];
	sprintf(Label, "FILESTORE %s", Name);

	FillSolidRect(hDC, Unit.X(0.025), Unit.Y(0.27), Unit.X(0.235), Unit.Y(0.45), FS_LABEL);
	DrawFittedText(hDC, Label, Unit.X(0.035), Unit.Y(0.27), Unit.X(0.225), Unit.Y(0.45), true);

	FillSolidRect(hDC, Unit.X(0.81), Unit.Y(0.27), Unit.X(0.975), Unit.Y(0.45), FS_LABEL);
	DrawFittedText(hDC, "Acorn", Unit.X(0.86), Unit.Y(0.27), Unit.X(0.94), Unit.Y(0.45), false);

	// Acorn logo
	const int r = std::max(2, (int)(Unit.Height * 0.04));
	const int cx = Unit.X(0.955);
	const int cy = Unit.Y(0.36);

	HBRUSH hBrush = CreateSolidBrush(FS_LABEL_TEXT);
	HGDIOBJ hPrevBrush = SelectObject(hDC, hBrush);
	HGDIOBJ hPrevPen = SelectObject(hDC, GetStockObject(NULL_PEN));
	Ellipse(hDC, cx - r, cy - r, cx + r, cy + r);
	SelectObject(hDC, hPrevPen);
	SelectObject(hDC, hPrevBrush);
	DeleteObject(hBrush);
}

/****************************************************************************/

// Hard drive unit model from the drive image size

static const char* GetDriveUnitName(int SizeMB)
{
	if (SizeMB <= 25) return "E20";
	if (SizeMB <= 45) return "E40S";
	return "E60S";
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

	// The FileStore is at the top with its hard drive units stacked
	// underneath, scaled to fit the window
	const int Margin = GetFileStoreMargin(ClientWidth);
	const int Gap = GetFileStoreUnitGap(ClientWidth);
	const int Units = 1 + DriveCount;

	int UnitWidth = ClientWidth - 2 * Margin;
	int UnitHeight = GetFileStoreUnitHeight(ClientWidth);

	if (Units * (UnitHeight + Gap) + Margin > ClientHeight)
	{
		UnitHeight = (ClientHeight - Margin) / Units - Gap;
		UnitWidth = UnitHeight * 5;
	}

	FileStoreUnitLayout Unit;
	Unit.Left = (ClientWidth - UnitWidth) / 2;
	Unit.Top = Margin;
	Unit.Width = UnitWidth;
	Unit.Height = UnitHeight;

	// FileStore E01 (smoked flap) or E01S (amber flap)
	const bool E01S = MachineType == Model::FileStoreE01S;

	DrawUnit(hMemDC, Unit, E01S ? "E01S" : "E01", E01S, FileStoreDoorOpen,
	         "MODE", FileStoreModeLED,
	         LEDs.FloppyDisc[0], LEDs.FloppyDisc[1]);

	// Hard drive units
	for (int Drive = 0; Drive < 4; Drive++)
	{
		if (m_FileStoreDriveSize[Drive] < 0)
		{
			continue;
		}

		Unit.Top += UnitHeight + Gap;

		const char* Name = GetDriveUnitName(m_FileStoreDriveSize[Drive]);

		DrawUnit(hMemDC, Unit, Name, strcmp(Name, "E20") != 0, false,
		         "DRIVE", LEDs.HDisc[Drive], false, false);
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

	// Don't let the door be closed while the buffers are being flushed
	EnableMenuItem(IDM_FILESTORE_DOOR_OPEN, false);

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
	EnableMenuItem(IDM_FILESTORE_DOOR_OPEN, IsFileStore(MachineType));
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
