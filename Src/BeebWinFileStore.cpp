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
#include "DiscInfo.h"
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
static const COLORREF FS_STRIPE         = RGB(244, 240, 230); // Stripes printed on the flap
static const COLORREF FS_FLAP_TINT      = RGB(225, 140, 30);  // See-through amber flap
static const COLORREF FS_FLAP_EDGE      = RGB(170, 90, 10);
static const COLORREF FS_LABEL          = RGB(18, 18, 18);
static const COLORREF FS_LABEL_TEXT     = RGB(235, 235, 235);
static const COLORREF FS_FASCIA_E01     = RGB(88, 96, 92);    // Behind the flap
static const COLORREF FS_FASCIA_E01S    = RGB(30, 30, 32);
static const COLORREF FS_FASCIA_TEXT    = RGB(220, 220, 220);
static const COLORREF FS_FLOPPY         = RGB(14, 14, 16);    // Floppy drive
static const COLORREF FS_SHUTTER        = RGB(74, 74, 80);    // Empty drive
static const COLORREF FS_SHUTTER_EDGE   = RGB(110, 110, 118);
static const COLORREF FS_DISC           = RGB(4, 4, 4);       // Disc in the drive
static const COLORREF FS_DISC_EDGE      = RGB(120, 120, 126);
static const COLORREF FS_EJECT          = RGB(46, 46, 50);
static const COLORREF FS_EJECT_OUT      = RGB(90, 90, 96);
static const COLORREF FS_SWITCH         = RGB(220, 214, 200);
static const COLORREF FS_SWITCH_ROCKER  = RGB(120, 116, 108);
static const COLORREF FS_RED_ON         = RGB(255, 40, 30);
static const COLORREF FS_RED_OFF        = RGB(90, 20, 16);
static const COLORREF FS_GREEN_ON       = RGB(40, 240, 90);
static const COLORREF FS_GREEN_OFF      = RGB(20, 70, 36);
static const COLORREF FS_FLOPPY_LED_ON  = RGB(60, 200, 80);
static const COLORREF FS_FLOPPY_LED_OFF = RGB(30, 60, 36);

// Opacity (out of 256) of the flap tint, the flap edge, and the glow of a
// lit LED seen through the flap
constexpr int FS_FLAP_TINT_ALPHA = 140;
constexpr int FS_FLAP_EDGE_ALPHA = 200;
constexpr int FS_LED_GLOW_ALPHA  = 150;

/****************************************************************************/

// The panel is drawn into a 32-bit DIB section, so that the see-through
// flap can be drawn by blending pixels

struct FileStoreCanvas
{
	HDC hDC;
	DWORD* Pixels; // Top-down, 0x00RRGGBB
	int Width;
	int Height;
};

/****************************************************************************/

static void FillSolidRect(HDC hDC, int Left, int Top, int Right, int Bottom, COLORREF Colour)
{
	RECT Rect = { Left, Top, Right, Bottom };
	HBRUSH hBrush = CreateSolidBrush(Colour);
	FillRect(hDC, &Rect, hBrush);
	DeleteObject(hBrush);
}

/****************************************************************************/

// Blend a colour over a rectangle, Alpha out of 256

static void BlendRect(const FileStoreCanvas& Canvas, int Left, int Top, int Right, int Bottom,
                      COLORREF Colour, int Alpha)
{
	GdiFlush();

	Left   = std::max(Left, 0);
	Top    = std::max(Top, 0);
	Right  = std::min(Right, Canvas.Width);
	Bottom = std::min(Bottom, Canvas.Height);

	const int R = GetRValue(Colour) * Alpha;
	const int G = GetGValue(Colour) * Alpha;
	const int B = GetBValue(Colour) * Alpha;
	const int InverseAlpha = 256 - Alpha;

	for (int y = Top; y < Bottom; y++)
	{
		DWORD* p = Canvas.Pixels + y * Canvas.Width + Left;

		for (int x = Left; x < Right; x++, p++)
		{
			const DWORD Pixel = *p;

			const int PixelR = (Pixel >> 16) & 0xff;
			const int PixelG = (Pixel >> 8) & 0xff;
			const int PixelB = Pixel & 0xff;

			*p = (((PixelR * InverseAlpha + R) >> 8) << 16) |
			     (((PixelG * InverseAlpha + G) >> 8) << 8) |
			      ((PixelB * InverseAlpha + B) >> 8);
		}
	}
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

// Returns the largest font height, up to MaxHeight, at which Text fits in
// Width pixels

static int GetFittedTextHeight(HDC hDC, const char* Text, int Width, int MaxHeight, bool Serif, bool Bold)
{
	int Height = MaxHeight;

	for (; Height > 6; Height--)
	{
		HFONT hFont = CreatePanelFont(Height, Serif, Bold);
		HGDIOBJ hPrevFont = SelectObject(hDC, hFont);

		SIZE Size;
		GetTextExtentPoint32(hDC, Text, (int)strlen(Text), &Size);

		SelectObject(hDC, hPrevFont);
		DeleteObject(hFont);

		if (Size.cx <= Width)
		{
			break;
		}
	}

	return Height;
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

// Labels on the flap
constexpr double FS_LABEL_TOP    = 0.27;
constexpr double FS_LABEL_BOTTOM = 0.45;

// The two LEDs (MODE/POWER on the FileStore, DRIVE/POWER on a hard drive
// unit) are in the same place on every unit
constexpr double FS_LED_RED_X   = 0.745;
constexpr double FS_LED_GREEN_X = 0.79;
constexpr double FS_LED_Y       = 0.27;

/****************************************************************************/

static int GetLEDSize(const FileStoreUnitLayout& Unit)
{
	return std::max(4, (int)(Unit.Height * 0.07));
}

/****************************************************************************/

static void DrawUnitLEDs(HDC hDC, const FileStoreUnitLayout& Unit, const char* RedLabel, bool RedOn)
{
	const int Size   = GetLEDSize(Unit);
	const int RedX   = Unit.X(FS_LED_RED_X);
	const int GreenX = Unit.X(FS_LED_GREEN_X);
	const int y      = Unit.Y(FS_LED_Y);

	DrawLED(hDC, RedX, y, Size, RedOn, FS_RED_ON, FS_RED_OFF);
	DrawLED(hDC, GreenX, y, Size, true, FS_GREEN_ON, FS_GREEN_OFF); // Power

	const int TextHeight = (int)(Unit.Height * 0.07);

	DrawPanelText(hDC, RedLabel, RedX - Size * 6 / 10, y + Size + 2, TextHeight, false, false, FS_FASCIA_TEXT);
	DrawPanelText(hDC, "POWER", GreenX - Size * 6 / 10, y + Size + 2, TextHeight, false, false, FS_FASCIA_TEXT);
}

/****************************************************************************/

static void DrawFloppyDrive(HDC hDC, const FileStoreUnitLayout& Unit, double Left, double Right,
                            bool DiscInserted, bool LED)
{
	FillSolidRect(hDC, Unit.X(Left), Unit.Y(0.16), Unit.X(Right), Unit.Y(0.54), FS_FLOPPY);

	// Disc slot
	const int SlotLeft   = Unit.X(Left + 0.03);
	const int SlotRight  = Unit.X(Right - 0.07);
	const int SlotTop    = Unit.Y(0.29);
	const int SlotBottom = Unit.Y(0.38);

	FillSolidRect(hDC, SlotLeft, SlotTop, SlotRight, SlotBottom, RGB(0, 0, 0));

	if (DiscInserted)
	{
		// Black disc filling the slot, with highlights along its top and
		// bottom edges to show it against the drive, and the eject button out
		FillSolidRect(hDC, SlotLeft + 1, SlotTop + 1, SlotRight - 1, SlotBottom - 1, FS_DISC);
		FillSolidRect(hDC, SlotLeft + 1, SlotTop + 1, SlotRight - 1, SlotTop + 2, FS_DISC_EDGE);
		FillSolidRect(hDC, SlotLeft + 1, SlotBottom - 2, SlotRight - 1, SlotBottom - 1, FS_DISC_EDGE);
		FillSolidRect(hDC, Unit.X(Right - 0.06), Unit.Y(0.36), Unit.X(Right - 0.02), Unit.Y(0.50), FS_EJECT_OUT);
	}
	else
	{
		// Empty: the drive's shutter closes the slot
		FillSolidRect(hDC, SlotLeft + 1, SlotTop + 1, SlotRight - 1, SlotBottom - 1, FS_SHUTTER);
		FillSolidRect(hDC, SlotLeft + 1, SlotTop + 1, SlotRight - 1, SlotTop + 2, FS_SHUTTER_EDGE);
		FillSolidRect(hDC, Unit.X(Right - 0.06), Unit.Y(0.38), Unit.X(Right - 0.02), Unit.Y(0.50), FS_EJECT);
	}

	// Activity LED
	const int Size = std::max(3, (int)(Unit.Height * 0.05));
	const int x = Unit.X(Left + 0.015);
	const int y = Unit.Y(0.44);
	FillSolidRect(hDC, x, y, x + Size, y + Size, LED ? FS_FLOPPY_LED_ON : FS_FLOPPY_LED_OFF);
}

/****************************************************************************/

// The Acorn logo: an acorn with its cap and stalk

static void DrawAcornLogo(HDC hDC, int cx, int cy, int Height)
{
	const double h = Height;
	const double w = h * 0.62;

	HBRUSH hBrush = CreateSolidBrush(FS_LABEL_TEXT);
	HBRUSH hBackground = CreateSolidBrush(FS_LABEL);
	HGDIOBJ hPrevBrush = SelectObject(hDC, hBrush);
	HGDIOBJ hPrevPen = SelectObject(hDC, GetStockObject(NULL_PEN));

	// Nut
	Ellipse(hDC, cx - (int)(w * 0.42), cy - (int)(h * 0.18), cx + (int)(w * 0.42), cy + (int)(h * 0.50));

	// Cap
	Ellipse(hDC, cx - (int)(w * 0.50), cy - (int)(h * 0.42), cx + (int)(w * 0.50), cy + (int)(h * 0.02));

	// Line between the cap and the nut
	RECT Line = { cx - (int)(w * 0.50), cy - (int)(h * 0.08), cx + (int)(w * 0.50), cy - (int)(h * 0.02) };
	FillRect(hDC, &Line, hBackground);

	// Stalk
	RECT Stalk = { cx - (int)(w * 0.06), cy - (int)(h * 0.55), cx + (int)(w * 0.06), cy - (int)(h * 0.38) };
	FillRect(hDC, &Stalk, hBrush);

	SelectObject(hDC, hPrevPen);
	SelectObject(hDC, hPrevBrush);
	DeleteObject(hBackground);
	DeleteObject(hBrush);
}

/****************************************************************************/

// Draw one unit: the case, its recessed base, what is behind the front
// flap, and (unless the FileStore's door is open) the see-through amber
// flap with its stripes and labels.

static void DrawUnit(const FileStoreCanvas& Canvas, const FileStoreUnitLayout& Unit,
                     const char* Name, bool E01Style, bool FlapOpen, bool FileStore,
                     const char* RedLabel, bool RedOn,
                     const bool* DiscInserted, const bool* FloppyLED,
                     int LabelTextHeight)
{
	HDC hDC = Canvas.hDC;

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

	// Behind the flap: the fascia (slate grey on the E01 and E20, black on
	// the E01S, E40S and E60S), the floppy drives on the FileStore, the LEDs
	// and the power switch
	FillSolidRect(hDC, FlapLeft, FlapTop, FlapRight, FlapBottom, E01Style ? FS_FASCIA_E01 : FS_FASCIA_E01S);

	if (FileStore)
	{
		DrawFloppyDrive(hDC, Unit, 0.03, 0.36, DiscInserted[0], FloppyLED[0]);
		DrawFloppyDrive(hDC, Unit, 0.375, 0.705, DiscInserted[1], FloppyLED[1]);
	}

	DrawUnitLEDs(hDC, Unit, RedLabel, RedOn);

	FillSolidRect(hDC, Unit.X(0.90), Unit.Y(0.22), Unit.X(0.94), Unit.Y(0.46), FS_SWITCH);
	FillSolidRect(hDC, Unit.X(0.91), Unit.Y(0.26), Unit.X(0.93), Unit.Y(0.40), FS_SWITCH_ROCKER);

	if (FlapOpen)
	{
		return;
	}

	// The flap is see-through amber plastic...
	BlendRect(Canvas, FlapLeft, FlapTop, FlapRight, FlapBottom, FS_FLAP_TINT, FS_FLAP_TINT_ALPHA);

	// ...with evenly spaced stripes printed on it, all the same width...
	const int FlapWidth = FlapRight - FlapLeft;
	const int Pitch = std::max(4, FlapWidth / 72);
	const int StripeWidth = std::max(2, Pitch * 45 / 100);
	const int Stripes = FlapWidth / Pitch;
	const int FirstStripe = FlapLeft + (FlapWidth - Stripes * Pitch + (Pitch - StripeWidth)) / 2;

	for (int i = 0; i < Stripes; i++)
	{
		const int x = FirstStripe + i * Pitch;
		FillSolidRect(hDC, x, FlapTop, x + StripeWidth, FlapBottom, FS_STRIPE);
	}

	// ...and the lit LEDs glow through it
	const int LEDSize = GetLEDSize(Unit);
	const int LEDy = Unit.Y(FS_LED_Y);

	if (RedOn)
	{
		const int x = Unit.X(FS_LED_RED_X);
		BlendRect(Canvas, x - LEDSize / 2, LEDy - LEDSize / 2, x + LEDSize + LEDSize / 2, LEDy + LEDSize + LEDSize / 2,
		          FS_RED_ON, FS_LED_GLOW_ALPHA);
	}

	const int GreenX = Unit.X(FS_LED_GREEN_X);
	BlendRect(Canvas, GreenX - LEDSize / 2, LEDy - LEDSize / 2, GreenX + LEDSize + LEDSize / 2, LEDy + LEDSize + LEDSize / 2,
	          FS_GREEN_ON, FS_LED_GLOW_ALPHA);

	// Darker moulded edges along the top and bottom of the flap
	const int EdgeHeight = std::max(2, (int)(Unit.Height * 0.025));

	BlendRect(Canvas, FlapLeft, FlapTop, FlapRight, FlapTop + EdgeHeight, FS_FLAP_EDGE, FS_FLAP_EDGE_ALPHA);
	BlendRect(Canvas, FlapLeft, FlapBottom - EdgeHeight, FlapRight, FlapBottom, FS_FLAP_EDGE, FS_FLAP_EDGE_ALPHA);

	// Model label on the left, Acorn badge on the right
	const int LabelTop    = Unit.Y(FS_LABEL_TOP);
	const int LabelBottom = Unit.Y(FS_LABEL_BOTTOM);
	const int LabelHeight = LabelBottom - LabelTop;

	char Label[40];
	sprintf(Label, "FILESTORE %s", Name);

	FillSolidRect(hDC, Unit.X(0.025), LabelTop, Unit.X(0.235), LabelBottom, FS_LABEL);
	DrawPanelText(hDC, Label, Unit.X(0.035), LabelTop + (LabelHeight - LabelTextHeight) / 2 - 1,
	              LabelTextHeight, true, true, FS_LABEL_TEXT);

	FillSolidRect(hDC, Unit.X(0.81), LabelTop, Unit.X(0.975), LabelBottom, FS_LABEL);

	const int AcornTextHeight = GetFittedTextHeight(hDC, "Acorn", Unit.X(0.935) - Unit.X(0.855),
	                                                LabelHeight * 7 / 10, true, false);

	DrawPanelText(hDC, "Acorn", Unit.X(0.855), LabelTop + (LabelHeight - AcornTextHeight) / 2 - 1,
	              AcornTextHeight, true, false, FS_LABEL_TEXT);

	DrawAcornLogo(hDC, Unit.X(0.952), Unit.Y(0.36), LabelHeight * 62 / 100);
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

	// Draw into a 32-bit top-down DIB section, which also avoids flicker
	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = ClientWidth;
	bmi.bmiHeader.biHeight = -ClientHeight;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* pBits = nullptr;
	HBITMAP hBitmap = CreateDIBSection(hDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);

	if (hBitmap == nullptr)
	{
		return;
	}

	FileStoreCanvas Canvas;
	Canvas.hDC = CreateCompatibleDC(hDC);
	Canvas.Pixels = static_cast<DWORD*>(pBits);
	Canvas.Width = ClientWidth;
	Canvas.Height = ClientHeight;

	HGDIOBJ hPrevBitmap = SelectObject(Canvas.hDC, hBitmap);

	FillSolidRect(Canvas.hDC, 0, 0, ClientWidth, ClientHeight, FS_BACKGROUND);

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

	// The same label text size on every unit: the largest that fits the
	// longest label
	const int LabelTextHeight = GetFittedTextHeight(
		Canvas.hDC, "FILESTORE E01S",
		Unit.X(0.225) - Unit.X(0.035),
		(Unit.Y(FS_LABEL_BOTTOM) - Unit.Y(FS_LABEL_TOP)) * 7 / 10,
		true, true);

	// FileStore E01 or E01S
	const bool E01S = MachineType == Model::FileStoreE01S;
	const bool DiscInserted[2] = { DiscInfo[0].Loaded, DiscInfo[1].Loaded };

	DrawUnit(Canvas, Unit, E01S ? "E01S" : "E01", !E01S, FileStoreDoorOpen, true,
	         "MODE", FileStoreModeLED, DiscInserted, LEDs.FloppyDisc, LabelTextHeight);

	// Hard drive units
	for (int Drive = 0; Drive < 4; Drive++)
	{
		if (m_FileStoreDriveSize[Drive] < 0)
		{
			continue;
		}

		Unit.Top += UnitHeight + Gap;

		const char* Name = GetDriveUnitName(m_FileStoreDriveSize[Drive]);

		DrawUnit(Canvas, Unit, Name, strcmp(Name, "E20") == 0, false, false,
		         "DRIVE", LEDs.HDisc[Drive], nullptr, nullptr, LabelTextHeight);
	}

	GdiFlush();

	BitBlt(hDC, 0, 0, ClientWidth, ClientHeight, Canvas.hDC, 0, 0, SRCCOPY);

	SelectObject(Canvas.hDC, hPrevBitmap);
	DeleteObject(hBitmap);
	DeleteDC(Canvas.hDC);
}

/****************************************************************************/

// Returns the LED and disc state as a bit mask, so changes can be detected

static int GetFileStoreLEDState()
{
	int State = 0;

	if (FileStoreModeLED)     State |= 0x01;
	if (LEDs.FloppyDisc[0])   State |= 0x02;
	if (LEDs.FloppyDisc[1])   State |= 0x04;

	// Also redraw when a floppy disc is inserted or removed
	if (DiscInfo[0].Loaded)   State |= 0x100;
	if (DiscInfo[1].Loaded)   State |= 0x200;

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
