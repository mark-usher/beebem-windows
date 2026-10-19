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
static const COLORREF FS_STRIPE         = RGB(236, 232, 214); // Stripes printed on the flap
static const COLORREF FS_FLAP_TINT      = RGB(200, 95, 25);   // See-through amber flap
static const COLORREF FS_FLAP_EDGE      = RGB(110, 40, 15);
static const COLORREF FS_LABEL          = RGB(18, 18, 18);
static const COLORREF FS_LABEL_TEXT     = RGB(235, 235, 235);
static const COLORREF FS_FASCIA_E01     = RGB(88, 96, 92);    // Behind the flap
static const COLORREF FS_FASCIA_E01S    = RGB(30, 30, 32);
static const COLORREF FS_LED_PANEL      = RGB(44, 34, 32);    // Behind MODE/POWER
static const COLORREF FS_LED_PANEL_TEXT = RGB(205, 185, 130);
static const COLORREF FS_FLOPPY         = RGB(22, 22, 24);    // 3.5" drive bezel
static const COLORREF FS_SLOT           = RGB(6, 6, 6);
static const COLORREF FS_SHUTTER        = RGB(28, 28, 30);    // Black shutter, empty drive
static const COLORREF FS_SHUTTER_EDGE   = RGB(58, 58, 62);
static const COLORREF FS_DISC           = RGB(2, 2, 2);       // Disc in the drive
static const COLORREF FS_DISC_EDGE      = RGB(130, 130, 136);
static const COLORREF FS_EJECT          = RGB(26, 26, 28);
static const COLORREF FS_EJECT_OUT      = RGB(96, 96, 102);
static const COLORREF FS_SWITCH         = RGB(220, 214, 200);
static const COLORREF FS_SWITCH_ROCKER  = RGB(120, 116, 108);
static const COLORREF FS_RED_ON         = RGB(255, 40, 30);
static const COLORREF FS_RED_OFF        = RGB(90, 20, 16);
static const COLORREF FS_GREEN_ON       = RGB(40, 240, 90);
static const COLORREF FS_GREEN_OFF      = RGB(20, 70, 36);
static const COLORREF FS_FLOPPY_LED_ON  = RGB(170, 255, 40);  // Lime
static const COLORREF FS_FLOPPY_LED_OFF = RGB(52, 56, 50);

// Opacity (out of 256) of the flap tint, the flap edge, and the glow of a
// lit LED seen through the flap
constexpr int FS_FLAP_TINT_ALPHA = 170;
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

// The model labels use a high contrast bold serif. Bodoni MT (installed
// with Microsoft Office) is closest; otherwise Times New Roman.

static int CALLBACK FontFoundCallback(const LOGFONT* /* pLogFont */,
                                     const TEXTMETRIC* /* pTextMetric */,
                                     DWORD /* FontType */,
                                     LPARAM lParam)
{
	*reinterpret_cast<bool*>(lParam) = true;
	return 0; // Stop enumerating
}

static bool IsFontInstalled(const char* FaceName)
{
	HDC hDC = GetDC(nullptr);

	LOGFONT lf = {};
	lf.lfCharSet = DEFAULT_CHARSET;
	strcpy(lf.lfFaceName, FaceName);

	bool Found = false;

	EnumFontFamiliesEx(hDC, &lf, FontFoundCallback, reinterpret_cast<LPARAM>(&Found), 0);

	ReleaseDC(nullptr, hDC);

	return Found;
}

static HFONT CreatePanelFont(int Height, bool Serif, bool Bold)
{
	static const bool Bodoni = IsFontInstalled("Bodoni MT");

	const char* FaceName = !Serif ? "Arial" :
	                       Bold && Bodoni ? "Bodoni MT" : "Times New Roman";

	return CreateFont(Height, 0, 0, 0, Bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
	                  ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
	                  ANTIALIASED_QUALITY,
	                  VARIABLE_PITCH | (Serif ? FF_ROMAN : FF_SWISS),
	                  FaceName);
}

static int GetTextWidth(HDC hDC, const char* Text, int Height, bool Serif, bool Bold)
{
	HFONT hFont = CreatePanelFont(Height, Serif, Bold);
	HGDIOBJ hPrevFont = SelectObject(hDC, hFont);

	SIZE Size;
	GetTextExtentPoint32(hDC, Text, (int)strlen(Text), &Size);

	SelectObject(hDC, hPrevFont);
	DeleteObject(hFont);

	return Size.cx;
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
		if (GetTextWidth(hDC, Text, Height, Serif, Bold) <= Width)
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

// The flap has about 60 stripes. The black model label and Acorn badge each
// cover 10 stripes, one stripe in from the ends, in a band across the
// middle of the flap that the stripes are cut short around.
constexpr int    FS_STRIPES            = 60;
constexpr int    FS_LABEL_STRIPES      = 10;
constexpr double FS_LABEL_BAND_TOP     = 0.36; // Fraction of the flap height
constexpr double FS_LABEL_BAND_BOTTOM  = 0.64;
constexpr double FS_STRIPE_MARGIN      = 0.06; // Above and below the stripes

// The two LEDs (MODE/POWER on the FileStore, DRIVE/POWER on a hard drive
// unit) are in the same place on every unit, centred on the flap, just
// left of the Acorn badge
constexpr double FS_LED_RED_X   = 0.745;
constexpr double FS_LED_GREEN_X = 0.785;
constexpr double FS_LED_Y       = 0.35; // Centre

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
	const int y      = Unit.Y(FS_LED_Y) - Size / 2;

	DrawLED(hDC, RedX, y, Size, RedOn, FS_RED_ON, FS_RED_OFF);
	DrawLED(hDC, GreenX, y, Size, true, FS_GREEN_ON, FS_GREEN_OFF); // Power

	// Labels centred under the LEDs
	const int TextHeight = std::max(6, (int)(Unit.Height * 0.055));

	DrawPanelText(hDC, RedLabel,
	              RedX + Size / 2 - GetTextWidth(hDC, RedLabel, TextHeight, false, true) / 2,
	              y + Size + 2, TextHeight, false, true, FS_LED_PANEL_TEXT);

	DrawPanelText(hDC, "POWER",
	              GreenX + Size / 2 - GetTextWidth(hDC, "POWER", TextHeight, false, true) / 2,
	              y + Size + 2, TextHeight, false, true, FS_LED_PANEL_TEXT);
}

/****************************************************************************/

// A 3.5" floppy drive, as on the real FileStore: black bezel, a stepped
// slot with a raised shutter in the middle, the activity LED at the bottom
// left and the eject button at the bottom right. Left and Right are
// fractions of the unit's width.

static void DrawFloppyDrive(HDC hDC, const FileStoreUnitLayout& Unit, double Left, double Right,
                            bool DiscInserted, bool LED)
{
	// Position across the drive, as a fraction of the drive's width
	auto DX = [&](double f) { return Unit.X(Left + (Right - Left) * f); };

	FillSolidRect(hDC, DX(0.0), Unit.Y(0.16), DX(1.0), Unit.Y(0.54), FS_FLOPPY);

	// Slot opening, with the lips at each side
	FillSolidRect(hDC, DX(0.06), Unit.Y(0.28), DX(0.94), Unit.Y(0.34), FS_SLOT);

	const int ShutterLeft   = DX(0.30);
	const int ShutterRight  = DX(0.75);
	const int ShutterTop    = Unit.Y(0.25);
	const int ShutterBottom = Unit.Y(0.37);

	if (DiscInserted)
	{
		// Black disc in the slot, outlined so it stands out from the drive,
		// and the eject button out
		FillSolidRect(hDC, ShutterLeft, ShutterTop, ShutterRight, ShutterBottom, FS_DISC);
		FillSolidRect(hDC, ShutterLeft, ShutterTop, ShutterRight, ShutterTop + 1, FS_DISC_EDGE);
		FillSolidRect(hDC, ShutterLeft, ShutterBottom - 1, ShutterRight, ShutterBottom, FS_DISC_EDGE);
		FillSolidRect(hDC, ShutterLeft, ShutterTop, ShutterLeft + 1, ShutterBottom, FS_DISC_EDGE);
		FillSolidRect(hDC, ShutterRight - 1, ShutterTop, ShutterRight, ShutterBottom, FS_DISC_EDGE);

		FillSolidRect(hDC, DX(0.76), Unit.Y(0.43), DX(0.89), Unit.Y(0.50), FS_EJECT_OUT);
	}
	else
	{
		// Empty: the black shutter, with a faint top edge
		FillSolidRect(hDC, ShutterLeft, ShutterTop, ShutterRight, ShutterBottom, FS_SHUTTER);
		FillSolidRect(hDC, ShutterLeft, ShutterTop, ShutterRight, ShutterTop + 1, FS_SHUTTER_EDGE);

		FillSolidRect(hDC, DX(0.76), Unit.Y(0.43), DX(0.89), Unit.Y(0.50), FS_EJECT);
		FillSolidRect(hDC, DX(0.76), Unit.Y(0.43), DX(0.89), Unit.Y(0.43) + 1, FS_SHUTTER_EDGE);
	}

	// Lower lip of the slot
	FillSolidRect(hDC, DX(0.30), Unit.Y(0.405), DX(0.88), Unit.Y(0.405) + 1, FS_SHUTTER_EDGE);

	// Activity LED
	FillSolidRect(hDC, DX(0.18), Unit.Y(0.45), DX(0.24), Unit.Y(0.48), LED ? FS_FLOPPY_LED_ON : FS_FLOPPY_LED_OFF);
}

/****************************************************************************/

// The Acorn logo: an acorn with a domed cap and stalk, and below a small
// gap, the nut

static void DrawAcornLogo(HDC hDC, int cx, int cy, int Height)
{
	const double h = Height;
	const double w = h * 0.62;

	HBRUSH hBrush = CreateSolidBrush(FS_LABEL_TEXT);
	HGDIOBJ hPrevBrush = SelectObject(hDC, hBrush);
	HGDIOBJ hPrevPen = SelectObject(hDC, GetStockObject(NULL_PEN));

	// Cap: the top half of an ellipse
	const int CapLeft   = cx - (int)(w * 0.50);
	const int CapRight  = cx + (int)(w * 0.50);
	const int CapTop    = cy - (int)(h * 0.40);
	const int CapBottom = cy + (int)(h * 0.16);
	const int CapMiddle = (CapTop + CapBottom) / 2;

	Chord(hDC, CapLeft, CapTop, CapRight, CapBottom, CapRight, CapMiddle, CapLeft, CapMiddle);

	// Stalk
	RECT Stalk = { cx - std::max(1, (int)(w * 0.05)), cy - (int)(h * 0.52), cx + std::max(1, (int)(w * 0.05)), cy - (int)(h * 0.36) };
	FillRect(hDC, &Stalk, hBrush);

	// Nut
	Ellipse(hDC, cx - (int)(w * 0.36), cy - (int)(h * 0.04), cx + (int)(w * 0.36), cy + (int)(h * 0.50));

	SelectObject(hDC, hPrevPen);
	SelectObject(hDC, hPrevBrush);
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

		// Panel behind the MODE and POWER LEDs
		FillSolidRect(hDC, Unit.X(0.718), FlapTop, FlapRight, FlapBottom, FS_LED_PANEL);

		// With the door open, the cream case dividers between the drive bays
		if (FlapOpen)
		{
			FillSolidRect(hDC, Unit.X(0.362), FlapTop, Unit.X(0.372), FlapBottom, FS_CASE);
			FillSolidRect(hDC, Unit.X(0.708), FlapTop, Unit.X(0.718), FlapBottom, FS_CASE);
		}
	}

	DrawUnitLEDs(hDC, Unit, RedLabel, RedOn);

	if (FlapOpen)
	{
		// Power switch
		FillSolidRect(hDC, Unit.X(0.90), Unit.Y(0.22), Unit.X(0.94), Unit.Y(0.46), FS_SWITCH);
		FillSolidRect(hDC, Unit.X(0.91), Unit.Y(0.26), Unit.X(0.93), Unit.Y(0.40), FS_SWITCH_ROCKER);
		return;
	}

	// The flap is see-through amber plastic...
	BlendRect(Canvas, FlapLeft, FlapTop, FlapRight, FlapBottom, FS_FLAP_TINT, FS_FLAP_TINT_ALPHA);

	// ...with evenly spaced stripes printed on it, all the same whole number
	// of pixels wide. The stripes are cut short around the label band where
	// the model label and Acorn badge are.
	const int FlapWidth   = FlapRight - FlapLeft;
	const int FlapHeight  = FlapBottom - FlapTop;
	const int Pitch       = std::max(4, FlapWidth / FS_STRIPES);
	const int Stripes     = FlapWidth / Pitch;
	const int StripeWidth = Pitch / 2;
	const int FirstStripe = FlapLeft + (FlapWidth - Stripes * Pitch) / 2;

	const int StripeTop    = FlapTop + std::max(1, (int)(FlapHeight * FS_STRIPE_MARGIN));
	const int StripeBottom = FlapBottom - std::max(1, (int)(FlapHeight * FS_STRIPE_MARGIN));
	const int BandTop      = FlapTop + (int)(FlapHeight * FS_LABEL_BAND_TOP);
	const int BandBottom   = FlapTop + (int)(FlapHeight * FS_LABEL_BAND_BOTTOM);
	const int BandGap      = std::max(1, Pitch / 2);

	// Label positions, in stripes
	const int ModelLabelFirst = 1;
	const int ModelLabelLast  = ModelLabelFirst + FS_LABEL_STRIPES;
	const int AcornLabelLast  = Stripes - 1;
	const int AcornLabelFirst = AcornLabelLast - FS_LABEL_STRIPES;

	for (int i = 0; i < Stripes; i++)
	{
		const int x = FirstStripe + i * Pitch + (Pitch - StripeWidth) / 2;

		if ((i >= ModelLabelFirst && i < ModelLabelLast) ||
		    (i >= AcornLabelFirst && i < AcornLabelLast))
		{
			FillSolidRect(hDC, x, StripeTop, x + StripeWidth, BandTop - BandGap, FS_STRIPE);
			FillSolidRect(hDC, x, BandBottom + BandGap, x + StripeWidth, StripeBottom, FS_STRIPE);
		}
		else
		{
			FillSolidRect(hDC, x, StripeTop, x + StripeWidth, StripeBottom, FS_STRIPE);
		}
	}

	// ...and the lit LEDs glow through it
	const int LEDSize = GetLEDSize(Unit);
	const int LEDy = Unit.Y(FS_LED_Y) - LEDSize / 2;

	if (RedOn)
	{
		const int x = Unit.X(FS_LED_RED_X);
		BlendRect(Canvas, x - LEDSize / 2, LEDy - LEDSize / 2, x + LEDSize + LEDSize / 2, LEDy + LEDSize + LEDSize / 2,
		          FS_RED_ON, FS_LED_GLOW_ALPHA);
	}

	const int GreenX = Unit.X(FS_LED_GREEN_X);
	BlendRect(Canvas, GreenX - LEDSize / 2, LEDy - LEDSize / 2, GreenX + LEDSize + LEDSize / 2, LEDy + LEDSize + LEDSize / 2,
	          FS_GREEN_ON, FS_LED_GLOW_ALPHA);

	// Darker moulded edges around the flap
	const int Edge = std::max(2, (int)(Unit.Height * 0.02));

	BlendRect(Canvas, FlapLeft, FlapTop, FlapRight, FlapTop + Edge, FS_FLAP_EDGE, FS_FLAP_EDGE_ALPHA);
	BlendRect(Canvas, FlapLeft, FlapBottom - Edge, FlapRight, FlapBottom, FS_FLAP_EDGE, FS_FLAP_EDGE_ALPHA);
	BlendRect(Canvas, FlapLeft, FlapTop, FlapLeft + Edge, FlapBottom, FS_FLAP_EDGE, FS_FLAP_EDGE_ALPHA);
	BlendRect(Canvas, FlapRight - Edge, FlapTop, FlapRight, FlapBottom, FS_FLAP_EDGE, FS_FLAP_EDGE_ALPHA);

	// Model label
	const int BandHeight = BandBottom - BandTop;

	char Label[40];
	sprintf(Label, "FILESTORE %s", Name);

	const int ModelLeft  = FirstStripe + ModelLabelFirst * Pitch;
	const int ModelRight = FirstStripe + ModelLabelLast * Pitch;
	const int LabelWidth = GetTextWidth(hDC, Label, LabelTextHeight, true, true);

	FillSolidRect(hDC, ModelLeft, BandTop, ModelRight, BandBottom, FS_LABEL);
	DrawPanelText(hDC, Label,
	              ModelLeft + (ModelRight - ModelLeft - LabelWidth) / 2,
	              BandTop + (BandHeight - LabelTextHeight) / 2 - 1,
	              LabelTextHeight, true, true, FS_LABEL_TEXT);

	// Acorn badge: "Acorn" and the logo, centred on the badge
	const int AcornLeft  = FirstStripe + AcornLabelFirst * Pitch;
	const int AcornRight = FirstStripe + AcornLabelLast * Pitch;

	FillSolidRect(hDC, AcornLeft, BandTop, AcornRight, BandBottom, FS_LABEL);

	const int AcornTextHeight = GetFittedTextHeight(hDC, "Acorn", (AcornRight - AcornLeft) * 62 / 100,
	                                                BandHeight * 7 / 10, true, false);
	const int AcornTextWidth = GetTextWidth(hDC, "Acorn", AcornTextHeight, true, false);
	const int LogoWidth = BandHeight / 2;
	const int LogoGap = std::max(2, LogoWidth / 3);
	const int AcornTextX = AcornLeft + (AcornRight - AcornLeft - AcornTextWidth - LogoGap - LogoWidth) / 2;

	DrawPanelText(hDC, "Acorn", AcornTextX, BandTop + (BandHeight - AcornTextHeight) / 2 - 1,
	              AcornTextHeight, true, false, FS_LABEL_TEXT);

	DrawAcornLogo(hDC, AcornTextX + AcornTextWidth + LogoGap + LogoWidth / 2,
	              (BandTop + BandBottom) / 2, BandHeight * 7 / 10);
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
	const int FlapWidth = Unit.X(FS_FLAP_RIGHT) - Unit.X(FS_FLAP_LEFT);
	const int FlapHeight = Unit.Y(FS_FLAP_BOTTOM) - Unit.Y(FS_FLAP_TOP);
	const int Pitch = std::max(4, FlapWidth / FS_STRIPES);

	const int LabelTextHeight = GetFittedTextHeight(
		Canvas.hDC, "FILESTORE E01S",
		FS_LABEL_STRIPES * Pitch * 88 / 100,
		(int)(FlapHeight * (FS_LABEL_BAND_BOTTOM - FS_LABEL_BAND_TOP) * 0.75),
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
