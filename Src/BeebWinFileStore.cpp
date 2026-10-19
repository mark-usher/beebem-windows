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

#include <ctype.h>
#include <stdio.h>

#include <algorithm>
#include <string>

#pragma warning(push)
#pragma warning(disable: 4458) // declaration of 'xxx' hides class member
using std::min;
using std::max;
#include <gdiplus.h>
#pragma warning(pop)

#include "BeebWin.h"
#include "BeebMem.h"
#include "DiscInfo.h"
#include "FileUtils.h"
#include "FileStore.h"
#include "FileStorePanelArt.h"
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
// or open to show the floppy drives), and below it a hard drive unit for
// each SCSI drive attached (see Hardware -> FileStore Hard Drive Units).
// Everything is drawn with GDI and GDI+, scaled to the window, so no
// bitmaps are needed.

/****************************************************************************/

// Labels on the flap have a half space before a trailing "S", as in
// "E01 S" and "E40 S"

constexpr char FS_HALF_SPACE = '\x01';

static std::string GetModelLabel(const char* Model)
{
	std::string Label = Model;
	const size_t Length = Label.size();

	if (Length > 1 && Label[Length - 1] == 'S' && isdigit((unsigned char)Label[Length - 2]))
	{
		Label.insert(Length - 1, 1, FS_HALF_SPACE);
	}

	return Label;
}

// Hard drive unit model from the drive image size

static const char* GetDriveUnitModel(unsigned long long Size)
{
	const unsigned long long SizeMB = Size / (1024 * 1024);

	if (SizeMB <= 25) return "E20";
	if (SizeMB <= 45) return "E40S";
	return "E60S";
}

// The label for the hard drive unit attached as a SCSI drive, or an empty
// string if there is no unit

std::string BeebWin::GetFileStoreUnitName(int Drive) const
{
	if (!IsFileStore(MachineType) || !SCSIDriveEnabled)
	{
		return "";
	}

	switch (FileStoreDrive[Drive])
	{
		case FileStoreDriveType::None:
		default:
			return "";

		case FileStoreDriveType::Auto: {
			char FileName[MAX_PATH];
			MakeFileName(FileName, MAX_PATH, HardDrivePath, "scsi%d.dat", Drive);

			WIN32_FILE_ATTRIBUTE_DATA Data;

			if (!GetFileAttributesEx(FileName, GetFileExInfoStandard, &Data))
			{
				return "";
			}

			const unsigned long long Size =
				((unsigned long long)Data.nFileSizeHigh << 32) | Data.nFileSizeLow;

			return GetModelLabel(GetDriveUnitModel(Size));
		}

		case FileStoreDriveType::E20:
			return GetModelLabel("E20");

		case FileStoreDriveType::E40S:
			return GetModelLabel("E40S");

		case FileStoreDriveType::E60S:
			return GetModelLabel("E60S");

		case FileStoreDriveType::Custom:
			// A space, so the unit is still shown without a label
			return m_FileStoreDriveLabel[Drive].empty() ? " " : m_FileStoreDriveLabel[Drive];
	}
}

/****************************************************************************/

// Hardware -> FileStore Hard Drive Units menu: a None, Auto, E20, E40 S,
// E60 S and Custom item for each drive

constexpr int FS_DRIVE_MENU_ITEMS = 6;

static UINT GetFileStoreDriveMenuID(int Drive, FileStoreDriveType Type)
{
	return IDM_FILESTORE_DRIVE0_NONE + Drive * FS_DRIVE_MENU_ITEMS + (int)Type;
}

void BeebWin::UpdateFileStoreDriveMenu()
{
	for (int Drive = 0; Drive < FILESTORE_MAX_DRIVES; Drive++)
	{
		// A custom label can only be set in the preferences file
		const UINT CustomID = GetFileStoreDriveMenuID(Drive, FileStoreDriveType::Custom);
		const bool HasLabel = !m_FileStoreDriveLabel[Drive].empty();

		const std::string Text = HasLabel ? "Custom: " + m_FileStoreDriveLabel[Drive]
		                                  : "Custom (set in the .cfg file)";

		ModifyMenu(m_hMenu, CustomID, MF_BYCOMMAND | MF_STRING, CustomID, Text.c_str());
		EnableMenuItem(CustomID, HasLabel);

		CheckMenuRadioItem(GetFileStoreDriveMenuID(Drive, FileStoreDriveType::None),
		                   CustomID,
		                   GetFileStoreDriveMenuID(Drive, FileStoreDrive[Drive]));
	}
}

// Returns false if MenuID isn't a FileStore drive menu item

bool BeebWin::HandleFileStoreDriveCommand(UINT MenuID)
{
	if (MenuID < IDM_FILESTORE_DRIVE0_NONE || MenuID > IDM_FILESTORE_DRIVE3_CUSTOM)
	{
		return false;
	}

	const int Item = MenuID - IDM_FILESTORE_DRIVE0_NONE;
	const int Drive = Item / FS_DRIVE_MENU_ITEMS;
	const FileStoreDriveType Type = static_cast<FileStoreDriveType>(Item % FS_DRIVE_MENU_ITEMS);

	FileStoreDrive[Drive] = Type;
	UpdateFileStoreDriveMenu();

	// Drives are attached when the FileStore starts, so restart it, letting
	// it flush its buffers first
	if (!BeginFileStoreShutdown(FileStoreShutdownAction::Reset, MachineType))
	{
		UpdateFileStorePanel();
	}

	return true;
}

/****************************************************************************/

// Work out which hard drive units are attached, and redraw the front panel.

void BeebWin::UpdateFileStorePanel()
{
	for (int Drive = 0; Drive < FILESTORE_MAX_DRIVES; Drive++)
	{
		m_FileStoreUnitName[Drive] = GetFileStoreUnitName(Drive);
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

	for (int Drive = 0; Drive < FILESTORE_MAX_DRIVES; Drive++)
	{
		if (!m_FileStoreUnitName[Drive].empty())
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
static const COLORREF FS_FASCIA         = RGB(30, 24, 22);    // Very dark black-brown
static const COLORREF FS_STRIPE         = RGB(236, 232, 214); // Stripes printed on the flap
static const COLORREF FS_FLAP_TINT      = RGB(200, 95, 25);   // See-through amber flap
static const COLORREF FS_BEVEL_LIT      = RGB(205, 140, 60);  // Bevelled edge of the flap
static const COLORREF FS_BEVEL_DARK     = RGB(140, 72, 22);
static const COLORREF FS_BEVEL_LIGHT    = RGB(255, 200, 120);
static const COLORREF FS_BEVEL_OUTLINE  = RGB(70, 25, 8);
static const COLORREF FS_LABEL          = RGB(18, 18, 18);
static const COLORREF FS_LABEL_TEXT     = RGB(232, 230, 215);

// Floppy drive
static const COLORREF FS_DRIVE_FILLER         = RGB(28, 24, 24);   // Blank panel beside each drive
static const COLORREF FS_DRIVE_SEAM           = RGB(90, 86, 80);
static const COLORREF FS_DRIVE_BEZEL          = RGB(20, 20, 22);
static const COLORREF FS_DRIVE_BAND           = RGB(30, 30, 32);   // Raised band above the slot
static const COLORREF FS_DRIVE_EDGE           = RGB(78, 78, 82);
static const COLORREF FS_DRIVE_SLOT_RIM       = RGB(60, 60, 64);
static const COLORREF FS_DRIVE_SLOT           = RGB(8, 8, 9);
static const COLORREF FS_DRIVE_SHUTTER        = RGB(26, 26, 28);   // Black shutter, no disc
static const COLORREF FS_DRIVE_SHUTTER_EDGE   = RGB(48, 48, 52);
static const COLORREF FS_DRIVE_DISC           = RGB(3, 3, 3);      // Disc in the drive
static const COLORREF FS_DRIVE_DISC_EDGE      = RGB(85, 85, 90);
static const COLORREF FS_DRIVE_STEP           = RGB(24, 24, 26);
static const COLORREF FS_DRIVE_EJECT          = RGB(18, 18, 20);
static const COLORREF FS_DRIVE_EJECT_RIM      = RGB(55, 55, 58);
static const COLORREF FS_DRIVE_EJECT_RIM_DISC = RGB(95, 95, 100);
static const COLORREF FS_DRIVE_LED_ON         = RGB(170, 255, 40); // Lime
static const COLORREF FS_DRIVE_LED_OFF        = RGB(66, 68, 64);

// MODE/DRIVE and POWER LEDs
static const COLORREF FS_LED_HOUSING      = RGB(42, 36, 34);
static const COLORREF FS_LED_HOUSING_EDGE = RGB(80, 74, 70);
static const COLORREF FS_RED_ON           = RGB(255, 45, 35);
static const COLORREF FS_RED_ON_RIM       = RGB(255, 170, 160);
static const COLORREF FS_RED_OFF          = RGB(115, 18, 20);
static const COLORREF FS_RED_OFF_RIM      = RGB(180, 140, 140);
static const COLORREF FS_GREEN_ON         = RGB(150, 245, 40);
static const COLORREF FS_GREEN_ON_RIM     = RGB(200, 255, 150);
static const COLORREF FS_GREEN_OFF        = RGB(40, 60, 30);
static const COLORREF FS_GREEN_OFF_RIM    = RGB(90, 110, 80);
static const COLORREF FS_LED_TEXT         = RGB(185, 160, 95);  // Tan lettering
static const COLORREF FS_SWITCH           = RGB(225, 218, 200);
static const COLORREF FS_SWITCH_SHADOW    = RGB(150, 144, 130);

// Opacity (out of 256) of the flap tint, and of the glow of a lit LED seen
// through the flap
constexpr int FS_FLAP_TINT_ALPHA = 70;
constexpr int FS_LED_GLOW_ALPHA  = 150;

/****************************************************************************/

// The panel is drawn into a 32-bit DIB section, so that the see-through
// flap can be drawn by blending pixels. Text and the Acorn logo are drawn
// with GDI+, for evenly spaced, anti-aliased text.

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
	if (Right > Left && Bottom > Top)
	{
		RECT Rect = { Left, Top, Right, Bottom };
		HBRUSH hBrush = CreateSolidBrush(Colour);
		FillRect(hDC, &Rect, hBrush);
		DeleteObject(hBrush);
	}
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

// Text. The model labels and the Acorn badge use Vidaloka, a close match
// to the lettering on the flap, drawn from the glyph outlines in
// FileStorePanelArt.h, so the spacing comes from the font's own advance
// widths and kerning. The LED labels use Arial Bold, drawn with GDI+.

enum class PanelFont
{
	ModelLabel,
	Acorn,
	LEDLabel
};

static Gdiplus::Color ToGdiplusColor(COLORREF Colour)
{
	return Gdiplus::Color(255, GetRValue(Colour), GetGValue(Colour), GetBValue(Colour));
}

static std::wstring ToWide(const char* Text)
{
	return std::wstring(Text, Text + strlen(Text));
}

static bool IsOutlineFont(PanelFont Font)
{
	return Font != PanelFont::LEDLabel;
}

static Gdiplus::Font* CreatePanelFont(float Size)
{
	static Gdiplus::FontFamily* pSansFamily = nullptr;

	if (pSansFamily == nullptr)
	{
		pSansFamily = new Gdiplus::FontFamily(L"Arial");
	}

	return new Gdiplus::Font(pSansFamily, Size, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
}

static const PanelGlyph* FindGlyph(char Char)
{
	for (const PanelGlyph& Glyph : PanelFontGlyphs)
	{
		if (Glyph.Char == Char)
		{
			return &Glyph;
		}
	}

	// Characters not in the outline font are drawn as spaces
	return &PanelFontGlyphs[0];
}

static int GetKerning(char Left, char Right)
{
	for (const PanelKerningPair& Pair : PanelFontKerning)
	{
		if (Pair.Left == Left && Pair.Right == Right)
		{
			return Pair.Value;
		}
	}

	return 0;
}

// Distance from the start of one character to the next, in font units.
// FS_HALF_SPACE is half the width of a space.

static int GetAdvance(char Char, char Next)
{
	if (Char == FS_HALF_SPACE)
	{
		return FindGlyph(' ')->Advance / 2;
	}

	return FindGlyph(Char)->Advance + GetKerning(Char, Next);
}

// Capital letter height as a fraction of the font size
static float GetCapHeight(PanelFont Font)
{
	return IsOutlineFont(Font) ? (float)PANEL_FONT_CAP_HEIGHT / PANEL_FONT_UNITS_PER_EM : 0.716f;
}

static float GetTextWidth(Gdiplus::Graphics& Graphics, PanelFont Font, float Size, const char* Text)
{
	if (IsOutlineFont(Font))
	{
		int Width = 0;

		for (const char* p = Text; *p != '\0'; p++)
		{
			Width += GetAdvance(p[0], p[1]);
		}

		return Width * Size / PANEL_FONT_UNITS_PER_EM;
	}

	Gdiplus::Font* pFont = CreatePanelFont(Size);

	const std::wstring WideText = ToWide(Text);
	Gdiplus::RectF Bounds;
	Graphics.MeasureString(WideText.c_str(), -1, pFont, Gdiplus::PointF(0.0f, 0.0f),
	                       Gdiplus::StringFormat::GenericTypographic(), &Bounds);

	delete pFont;

	return Bounds.Width;
}

// Largest font size, up to MaxSize, at which Text fits in Width pixels
static float GetFittedTextSize(Gdiplus::Graphics& Graphics, PanelFont Font, const char* Text,
                               float Width, float MaxSize)
{
	float Size = MaxSize;

	while (Size > 5.0f && GetTextWidth(Graphics, Font, Size, Text) > Width)
	{
		Size -= 0.5f;
	}

	return Size;
}

// Add one glyph's outline to a path, with its origin (on the baseline) at
// x, y. Font units have y up.

static void AddGlyphOutline(Gdiplus::GraphicsPath& Path, const PanelGlyph& Glyph, float x, float y, float Scale)
{
	const short* pOp = &PanelFontOutlines[Glyph.First];
	const short* pEnd = pOp + Glyph.Count;

	auto Point = [&](int i) { return Gdiplus::PointF(x + pOp[i] * Scale, y - pOp[i + 1] * Scale); };

	Gdiplus::PointF Current;

	while (pOp < pEnd)
	{
		switch (*pOp)
		{
			case PANEL_OP_MOVE:
				Path.StartFigure();
				Current = Point(1);
				pOp += 3;
				break;

			case PANEL_OP_LINE: {
				const Gdiplus::PointF To = Point(1);
				Path.AddLine(Current, To);
				Current = To;
				pOp += 3;
				break;
			}

			case PANEL_OP_CURVE: {
				const Gdiplus::PointF To = Point(5);
				Path.AddBezier(Current, Point(1), Point(3), To);
				Current = To;
				pOp += 7;
				break;
			}

			case PANEL_OP_CLOSE:
			default:
				Path.CloseFigure();
				pOp++;
				break;
		}
	}
}

// Draw text with its capitals centred vertically on CentreY, starting at x
static void DrawPanelText(Gdiplus::Graphics& Graphics, PanelFont Font, float Size, const char* Text,
                          float x, float CentreY, COLORREF Colour)
{
	const float Baseline = CentreY + Size * GetCapHeight(Font) / 2.0f;
	Gdiplus::SolidBrush Brush(ToGdiplusColor(Colour));

	if (IsOutlineFont(Font))
	{
		const float Scale = Size / PANEL_FONT_UNITS_PER_EM;
		Gdiplus::GraphicsPath Path(Gdiplus::FillModeWinding);

		for (const char* p = Text; *p != '\0'; p++)
		{
			AddGlyphOutline(Path, *FindGlyph(*p), x, Baseline, Scale);
			x += GetAdvance(p[0], p[1]) * Scale;
		}

		Graphics.FillPath(&Brush, &Path);
		return;
	}

	Gdiplus::Font* pFont = CreatePanelFont(Size);

	Gdiplus::FontFamily Family;
	pFont->GetFamily(&Family);
	const INT Style = pFont->GetStyle();
	const float Ascent = Size * Family.GetCellAscent(Style) / Family.GetEmHeight(Style);

	const std::wstring WideText = ToWide(Text);
	Graphics.DrawString(WideText.c_str(), -1, pFont, Gdiplus::PointF(x, Baseline - Ascent),
	                    Gdiplus::StringFormat::GenericTypographic(), &Brush);

	delete pFont;
}

/****************************************************************************/

// The Acorn logo, Height pixels high, with its top left corner at x, y

template <size_t N>
static void FillLogoPart(Gdiplus::Graphics& Graphics, Gdiplus::Brush& Brush,
                         const float (&Part)[N][2], float x, float y, float Height)
{
	Gdiplus::PointF Points[N];

	for (size_t i = 0; i < N; i++)
	{
		Points[i] = Gdiplus::PointF(x + Part[i][0] * Height, y + Part[i][1] * Height);
	}

	Graphics.FillPolygon(&Brush, Points, (INT)N);
}

static void DrawAcornLogo(Gdiplus::Graphics& Graphics, float x, float y, float Height, COLORREF Colour)
{
	Gdiplus::SolidBrush Brush(ToGdiplusColor(Colour));

	FillLogoPart(Graphics, Brush, PanelLogoNut, x, y, Height);
	FillLogoPart(Graphics, Brush, PanelLogoCup, x, y, Height);
	FillLogoPart(Graphics, Brush, PanelLogoStalk, x, y, Height);
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

// Front flap area, which is also the fascia behind it
constexpr double FS_FLAP_LEFT   = 0.012;
constexpr double FS_FLAP_RIGHT  = 0.988;
constexpr double FS_FLAP_TOP    = 0.10;
constexpr double FS_FLAP_BOTTOM = 0.60;

// The flap has a bevelled edge, and about 60 stripes. The black model
// label (10 stripes) and Acorn badge (9 stripes) are one stripe in from the
// ends, in a band across the middle of the flap that the stripes are cut
// short around.
constexpr double FS_FLAP_BEVEL          = 0.07; // Fraction of the flap height
constexpr int    FS_STRIPES             = 60;
constexpr int    FS_MODEL_LABEL_STRIPES = 10;
constexpr int    FS_ACORN_LABEL_STRIPES = 8;
constexpr double FS_LABEL_BAND_TOP      = 0.36; // Fraction of the flap height
constexpr double FS_LABEL_BAND_BOTTOM   = 0.64;

// The MODE/DRIVE and POWER LEDs are in the same place on every unit:
// centred on the flap, just left of the Acorn badge
constexpr double FS_LED_RED_X   = 0.745;
constexpr double FS_LED_GREEN_X = 0.808;
constexpr double FS_LED_Y       = 0.54; // Fraction of the flap height

/****************************************************************************/

// A 3.5" floppy drive, measured from photos of the real FileStore: black
// bezel, a raised band above the slot, the slot (closed by a black shutter
// when empty), a lower step, the activity LED at the bottom left and the
// eject button at the bottom right. Left and Right are fractions of the
// unit's width.

static void DrawFloppyDrive(HDC hDC, const FileStoreUnitLayout& Unit, double Left, double Right,
                            bool DiscInserted, bool LED)
{
	// Each drive bay has a blank panel on the left, then the 3.5" drive
	const int BayLeft     = Unit.X(Left);
	const int BezelLeft   = Unit.X(Left + (Right - Left) * 0.163);
	const int BezelRight  = Unit.X(Right);
	const int BezelTop    = Unit.Y(0.11);
	const int BezelBottom = Unit.Y(0.59);
	const int BezelWidth  = BezelRight - BezelLeft;
	const int BezelHeight = BezelBottom - BezelTop;

	FillSolidRect(hDC, BayLeft, BezelTop, BezelLeft - 1, BezelBottom, FS_DRIVE_FILLER);
	FillSolidRect(hDC, BezelLeft - 1, BezelTop, BezelLeft, BezelBottom, FS_DRIVE_SEAM);

	// Positions within the drive bezel
	auto DX = [&](double f) { return BezelLeft + (int)(f * BezelWidth); };
	auto DY = [&](double f) { return BezelTop + (int)(f * BezelHeight); };

	FillSolidRect(hDC, BezelLeft, BezelTop, BezelRight, BezelBottom, FS_DRIVE_BEZEL);

	// Raised band above the slot, its top edge catching the light
	FillSolidRect(hDC, DX(0.25), DY(0.09), DX(0.77), DY(0.20), FS_DRIVE_BAND);
	FillSolidRect(hDC, DX(0.25), DY(0.09), DX(0.77), DY(0.09) + 2, FS_DRIVE_EDGE);

	// Slot
	FillSolidRect(hDC, DX(0.05), DY(0.21), DX(0.95), DY(0.44), FS_DRIVE_SLOT_RIM);
	FillSolidRect(hDC, DX(0.05) + 1, DY(0.21) + 1, DX(0.95) - 1, DY(0.44) - 1, FS_DRIVE_SLOT);

	if (DiscInserted)
	{
		// A thin, filled-in disc in the slot
		FillSolidRect(hDC, DX(0.08), DY(0.29), DX(0.94), DY(0.38), FS_DRIVE_DISC);
		FillSolidRect(hDC, DX(0.08), DY(0.29), DX(0.94), DY(0.29) + 1, FS_DRIVE_DISC_EDGE);
	}
	else
	{
		// The black shutter, flush in the slot below the band
		FillSolidRect(hDC, DX(0.25), DY(0.21) + 1, DX(0.77), DY(0.44) - 1, FS_DRIVE_SHUTTER);
		FillSolidRect(hDC, DX(0.25), DY(0.21) + 1, DX(0.77), DY(0.21) + 2, FS_DRIVE_SHUTTER_EDGE);
	}

	// Lower ridge, stopping short of the eject button
	FillSolidRect(hDC, DX(0.26), DY(0.59), DX(0.77), DY(0.66), FS_DRIVE_STEP);
	FillSolidRect(hDC, DX(0.26), DY(0.59), DX(0.77), DY(0.59) + 1, FS_DRIVE_EDGE);

	// Activity LED
	FillSolidRect(hDC, DX(0.17), DY(0.78), DX(0.22), DY(0.85), LED ? FS_DRIVE_LED_ON : FS_DRIVE_LED_OFF);

	// Eject button
	FillSolidRect(hDC, DX(0.77), DY(0.68), DX(0.94), DY(0.84),
	              DiscInserted ? FS_DRIVE_EJECT_RIM_DISC : FS_DRIVE_EJECT_RIM);
	FillSolidRect(hDC, DX(0.77) + 1, DY(0.68) + 1, DX(0.94) - 1, DY(0.84) - 1, FS_DRIVE_EJECT);
}

/****************************************************************************/

static void GetLEDPosition(const FileStoreUnitLayout& Unit, int* pPanelHeight, int* pCentreY, int* pRedX, int* pGreenX)
{
	*pPanelHeight = Unit.Y(FS_FLAP_BOTTOM) - Unit.Y(FS_FLAP_TOP);
	*pCentreY     = Unit.Y(FS_FLAP_TOP) + (int)(*pPanelHeight * FS_LED_Y);
	*pRedX        = Unit.X(FS_LED_RED_X);
	*pGreenX      = Unit.X(FS_LED_GREEN_X);
}

/****************************************************************************/

// A rectangular LED with a light rim, in a slightly lighter housing

static void DrawLED(HDC hDC, int cx, int cy, int PanelHeight, bool On,
                    COLORREF OnColour, COLORREF OnRim, COLORREF OffColour, COLORREF OffRim)
{
	const int HousingHalfWidth  = std::max(3, (int)(PanelHeight * 0.13));
	const int HousingHalfHeight = std::max(3, (int)(PanelHeight * 0.115));

	const int HousingLeft   = cx - HousingHalfWidth;
	const int HousingRight  = cx + HousingHalfWidth;
	const int HousingTop    = cy - HousingHalfHeight;
	const int HousingBottom = cy + HousingHalfHeight;

	FillSolidRect(hDC, HousingLeft, HousingTop, HousingRight, HousingBottom, FS_LED_HOUSING);
	FillSolidRect(hDC, HousingLeft, HousingTop, HousingRight, HousingTop + 1, FS_LED_HOUSING_EDGE);

	const int HalfWidth  = std::max(2, (int)(PanelHeight * 0.10));
	const int HalfHeight = std::max(2, (int)(PanelHeight * 0.06));

	const int Left   = cx - HalfWidth;
	const int Right  = cx + HalfWidth;
	const int Top    = cy - HalfHeight;
	const int Bottom = cy + HalfHeight;

	FillSolidRect(hDC, Left, Top, Right, Bottom, On ? OnRim : OffRim);
	FillSolidRect(hDC, Left + 1, Top + 1, Right - 1, Bottom - 1, On ? OnColour : OffColour);
}

/****************************************************************************/

// Draw one unit: the case, its recessed base, what is behind the front
// flap, and (unless the FileStore's door is open) the see-through amber
// flap with its stripes and labels.

static void DrawUnit(const FileStoreCanvas& Canvas, const FileStoreUnitLayout& Unit,
                     const char* Name, bool FlapOpen, bool FileStore,
                     const char* RedLabel, bool RedOn,
                     const bool* DiscInserted, const bool* FloppyLED,
                     float ModelTextSize)
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

	// Behind the flap: the very dark black-brown fascia, the floppy drives
	// on the FileStore, the LEDs and their labels, and the power switch
	FillSolidRect(hDC, FlapLeft, FlapTop, FlapRight, FlapBottom, FS_FASCIA);

	if (FileStore)
	{
		DrawFloppyDrive(hDC, Unit, 0.025, 0.355, DiscInserted[0], FloppyLED[0]);
		DrawFloppyDrive(hDC, Unit, 0.375, 0.705, DiscInserted[1], FloppyLED[1]);

		// With the door open, the cream case dividers between the bays
		if (FlapOpen)
		{
			FillSolidRect(hDC, Unit.X(0.362), FlapTop, Unit.X(0.372), FlapBottom, FS_CASE);
			FillSolidRect(hDC, Unit.X(0.708), FlapTop, Unit.X(0.718), FlapBottom, FS_CASE);
		}
	}

	int PanelHeight, LEDy, RedX, GreenX;
	GetLEDPosition(Unit, &PanelHeight, &LEDy, &RedX, &GreenX);

	DrawLED(hDC, RedX, LEDy, PanelHeight, RedOn, FS_RED_ON, FS_RED_ON_RIM, FS_RED_OFF, FS_RED_OFF_RIM);
	DrawLED(hDC, GreenX, LEDy, PanelHeight, true, FS_GREEN_ON, FS_GREEN_ON_RIM, FS_GREEN_OFF, FS_GREEN_OFF_RIM);

	if (FlapOpen)
	{
		// Power switch
		const int SwitchLeft   = Unit.X(0.955);
		const int SwitchRight  = Unit.X(0.978);
		const int SwitchTop    = FlapTop + (int)(PanelHeight * 0.30);
		const int SwitchBottom = FlapTop + (int)(PanelHeight * 0.72);

		FillSolidRect(hDC, SwitchLeft, SwitchTop, SwitchRight, SwitchBottom, FS_SWITCH_SHADOW);
		FillSolidRect(hDC, SwitchLeft, SwitchTop, SwitchRight - 1, SwitchBottom - 1, FS_SWITCH);
	}

	// Tan LED labels under the LEDs
	{
		GdiFlush();

		Gdiplus::Graphics Graphics(hDC);
		Graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

		const float Size = PanelHeight * 0.12f;
		const float CentreY = FlapTop + PanelHeight * 0.70f + Size * GetCapHeight(PanelFont::LEDLabel) / 2.0f;

		DrawPanelText(Graphics, PanelFont::LEDLabel, Size, RedLabel,
		              RedX - GetTextWidth(Graphics, PanelFont::LEDLabel, Size, RedLabel) / 2.0f,
		              CentreY, FS_LED_TEXT);

		DrawPanelText(Graphics, PanelFont::LEDLabel, Size, "POWER",
		              GreenX - GetTextWidth(Graphics, PanelFont::LEDLabel, Size, "POWER") / 2.0f,
		              CentreY, FS_LED_TEXT);
	}

	if (FlapOpen)
	{
		return;
	}

	// The flap is see-through amber plastic...
	BlendRect(Canvas, FlapLeft, FlapTop, FlapRight, FlapBottom, FS_FLAP_TINT, FS_FLAP_TINT_ALPHA);

	// ...with a bevelled edge...
	const int FlapHeight  = FlapBottom - FlapTop;
	const int Bevel       = std::max(2, (int)(FlapHeight * FS_FLAP_BEVEL));
	const int InnerLeft   = FlapLeft + Bevel;
	const int InnerRight  = FlapRight - Bevel;
	const int InnerTop    = FlapTop + Bevel;
	const int InnerBottom = FlapBottom - Bevel;

	// ...and evenly spaced stripes printed on it, all the same whole number
	// of pixels wide. The stripes are cut short around the label band.
	const int InnerWidth  = InnerRight - InnerLeft;
	const int Pitch       = std::max(4, InnerWidth / FS_STRIPES);
	const int Stripes     = InnerWidth / Pitch;
	const int StripeWidth = Pitch / 2;
	const int FirstStripe = InnerLeft + (InnerWidth - Stripes * Pitch) / 2;

	const int BandTop    = FlapTop + (int)(FlapHeight * FS_LABEL_BAND_TOP);
	const int BandBottom = FlapTop + (int)(FlapHeight * FS_LABEL_BAND_BOTTOM);
	const int BandGap    = std::max(1, Pitch / 2);

	// Label positions, in stripes
	const int ModelLabelFirst = 1;
	const int ModelLabelLast  = ModelLabelFirst + FS_MODEL_LABEL_STRIPES;
	const int AcornLabelLast  = Stripes - 1;
	const int AcornLabelFirst = AcornLabelLast - FS_ACORN_LABEL_STRIPES;

	for (int i = 0; i < Stripes; i++)
	{
		const int x = FirstStripe + i * Pitch + (Pitch - StripeWidth) / 2;

		if ((i >= ModelLabelFirst && i < ModelLabelLast) ||
		    (i >= AcornLabelFirst && i < AcornLabelLast))
		{
			FillSolidRect(hDC, x, InnerTop, x + StripeWidth, BandTop - BandGap, FS_STRIPE);
			FillSolidRect(hDC, x, BandBottom + BandGap, x + StripeWidth, InnerBottom, FS_STRIPE);
		}
		else
		{
			FillSolidRect(hDC, x, InnerTop, x + StripeWidth, InnerBottom, FS_STRIPE);
		}
	}

	// Lit LEDs glow through the flap
	const int GlowWidth  = std::max(3, (int)(PanelHeight * 0.14));
	const int GlowHeight = std::max(2, (int)(PanelHeight * 0.10));

	if (RedOn)
	{
		BlendRect(Canvas, RedX - GlowWidth, LEDy - GlowHeight, RedX + GlowWidth, LEDy + GlowHeight,
		          FS_RED_ON, FS_LED_GLOW_ALPHA);
	}

	BlendRect(Canvas, GreenX - GlowWidth, LEDy - GlowHeight, GreenX + GlowWidth, LEDy + GlowHeight,
	          FS_GREEN_ON, FS_LED_GLOW_ALPHA);

	// The bevel: lit along the top and left, in shadow along the bottom and
	// right, with a catch-light on the inner top and left edges and a dark
	// outline
	BlendRect(Canvas, FlapLeft, FlapTop, FlapRight, InnerTop, FS_BEVEL_LIT, 200);
	BlendRect(Canvas, FlapLeft, InnerBottom, FlapRight, FlapBottom, FS_BEVEL_DARK, 220);
	BlendRect(Canvas, FlapLeft, InnerTop, InnerLeft, InnerBottom, FS_BEVEL_LIT, 170);
	BlendRect(Canvas, InnerRight, InnerTop, FlapRight, InnerBottom, FS_BEVEL_DARK, 200);
	BlendRect(Canvas, InnerLeft, InnerTop - 1, InnerRight, InnerTop, FS_BEVEL_LIGHT, 150);
	BlendRect(Canvas, InnerLeft - 1, InnerTop, InnerLeft, InnerBottom, FS_BEVEL_LIGHT, 120);
	BlendRect(Canvas, FlapLeft, FlapTop, FlapRight, FlapTop + 1, FS_BEVEL_OUTLINE, 220);
	BlendRect(Canvas, FlapLeft, FlapBottom - 1, FlapRight, FlapBottom, FS_BEVEL_OUTLINE, 220);
	BlendRect(Canvas, FlapLeft, FlapTop, FlapLeft + 1, FlapBottom, FS_BEVEL_OUTLINE, 220);
	BlendRect(Canvas, FlapRight - 1, FlapTop, FlapRight, FlapBottom, FS_BEVEL_OUTLINE, 220);

	// Black label and badge
	const int ModelLeft  = FirstStripe + ModelLabelFirst * Pitch;
	const int ModelRight = FirstStripe + ModelLabelLast * Pitch;
	const int AcornLeft  = FirstStripe + AcornLabelFirst * Pitch;
	const int AcornRight = FirstStripe + AcornLabelLast * Pitch;

	FillSolidRect(hDC, ModelLeft, BandTop, ModelRight, BandBottom, FS_LABEL);
	FillSolidRect(hDC, AcornLeft, BandTop, AcornRight, BandBottom, FS_LABEL);

	GdiFlush();

	Gdiplus::Graphics Graphics(hDC);
	Graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
	Graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

	const float BandCentre = (BandTop + BandBottom) / 2.0f;
	const float BandHeight = (float)(BandBottom - BandTop);

	// "FILESTORE E01" etc, centred on the label
	char Label[40];
	sprintf(Label, "FILESTORE %s", Name);

	const float LabelWidth = GetTextWidth(Graphics, PanelFont::ModelLabel, ModelTextSize, Label);

	DrawPanelText(Graphics, PanelFont::ModelLabel, ModelTextSize, Label,
	              ModelLeft + (ModelRight - ModelLeft - LabelWidth) / 2.0f, BandCentre, FS_LABEL_TEXT);

	// "Acorn" and the logo, towards the right of the badge. As on the real
	// badge, the logo is taller than the text: the cap height is 0.71 of
	// the logo height, and the text baseline is 0.80 of the way down it,
	// level with the left end of the stalk. The gap between the text and
	// the logo is the width of an "n".
	float LogoHeight = BandHeight * 0.84f;
	float AcornSize, AcornWidth, LogoWidth, LogoGap;

	for (;;)
	{
		AcornSize  = 0.71f * LogoHeight / GetCapHeight(PanelFont::Acorn);
		AcornWidth = GetTextWidth(Graphics, PanelFont::Acorn, AcornSize, "Acorn");
		LogoWidth  = PANEL_LOGO_WIDTH * LogoHeight;
		LogoGap    = FindGlyph('n')->Advance * AcornSize / PANEL_FONT_UNITS_PER_EM;

		if (AcornWidth + LogoGap + LogoWidth <= (AcornRight - AcornLeft) - BandHeight * 0.6f ||
		    LogoHeight < 6.0f)
		{
			break;
		}

		LogoHeight -= 0.5f;
	}

	const float LogoTop  = BandTop + (BandHeight - LogoHeight) / 2.0f;
	const float Baseline = LogoTop + 0.80f * LogoHeight;
	const float AcornX   = AcornRight - BandHeight * 0.3f - LogoWidth - LogoGap - AcornWidth;

	DrawPanelText(Graphics, PanelFont::Acorn, AcornSize, "Acorn", AcornX,
	              Baseline - AcornSize * GetCapHeight(PanelFont::Acorn) / 2.0f, FS_LABEL_TEXT);

	DrawAcornLogo(Graphics, AcornX + AcornWidth + LogoGap, LogoTop, LogoHeight, FS_LABEL_TEXT);
}

/****************************************************************************/

void BeebWin::PaintFileStore(HDC hDC)
{
	RECT ClientRect;
	GetClientRect(m_hWnd, &ClientRect);

	const int ClientWidth  = ClientRect.right - ClientRect.left;
	const int ClientHeight = ClientRect.bottom - ClientRect.top;

	if (ClientWidth <= 0 || ClientHeight <= 0)
	{
		return;
	}

	// Draw into a 32-bit top-down DIB section, which also avoids flicker
	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth       = ClientWidth;
	bmi.bmiHeader.biHeight      = -ClientHeight;
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biBitCount    = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* pBits = nullptr;
	HBITMAP hBitmap = CreateDIBSection(hDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);

	if (hBitmap == nullptr)
	{
		return;
	}

	FileStoreCanvas Canvas;
	Canvas.hDC    = CreateCompatibleDC(hDC);
	Canvas.Pixels = static_cast<DWORD*>(pBits);
	Canvas.Width  = ClientWidth;
	Canvas.Height = ClientHeight;

	HGDIOBJ hPrevBitmap = SelectObject(Canvas.hDC, hBitmap);

	FillSolidRect(Canvas.hDC, 0, 0, ClientWidth, ClientHeight, FS_BACKGROUND);

	int DriveCount = 0;

	for (int Drive = 0; Drive < FILESTORE_MAX_DRIVES; Drive++)
	{
		if (!m_FileStoreUnitName[Drive].empty())
		{
			DriveCount++;
		}
	}

	// The FileStore is at the top with its hard drive units stacked
	// underneath, scaled to fit the window
	const int Margin = GetFileStoreMargin(ClientWidth);
	const int Gap    = GetFileStoreUnitGap(ClientWidth);
	const int Units  = 1 + DriveCount;

	int UnitWidth  = ClientWidth - 2 * Margin;
	int UnitHeight = GetFileStoreUnitHeight(ClientWidth);

	if (Units * (UnitHeight + Gap) + Margin > ClientHeight)
	{
		UnitHeight = (ClientHeight - Margin) / Units - Gap;
		UnitWidth  = UnitHeight * 5;
	}

	FileStoreUnitLayout Unit;
	Unit.Left   = (ClientWidth - UnitWidth) / 2;
	Unit.Top    = Margin;
	Unit.Width  = UnitWidth;
	Unit.Height = UnitHeight;

	// The same model label text size on every unit: the largest that fits
	// the longest label in 90% of the label's width
	float ModelTextSize;

	{
		const int FlapHeight = Unit.Y(FS_FLAP_BOTTOM) - Unit.Y(FS_FLAP_TOP);
		const int Bevel      = std::max(2, (int)(FlapHeight * FS_FLAP_BEVEL));
		const int InnerWidth = Unit.X(FS_FLAP_RIGHT) - Unit.X(FS_FLAP_LEFT) - 2 * Bevel;
		const int Pitch      = std::max(4, InnerWidth / FS_STRIPES);

		Gdiplus::Graphics Graphics(Canvas.hDC);

		// Fit the longest of the standard labels and any custom ones
		std::string Longest = "FILESTORE " + GetModelLabel("E40S");
		float LongestWidth = GetTextWidth(Graphics, PanelFont::ModelLabel, 100.0f, Longest.c_str());

		for (int Drive = 0; Drive < FILESTORE_MAX_DRIVES; Drive++)
		{
			const std::string Label = "FILESTORE " + m_FileStoreUnitName[Drive];
			const float Width       = GetTextWidth(Graphics, PanelFont::ModelLabel, 100.0f, Label.c_str());

			if (!m_FileStoreUnitName[Drive].empty() && Width > LongestWidth)
			{
				Longest      = Label;
				LongestWidth = Width;
			}
		}

		ModelTextSize = GetFittedTextSize(Graphics, PanelFont::ModelLabel, Longest.c_str(),
		                                  FS_MODEL_LABEL_STRIPES * Pitch * 0.90f,
		                                  FlapHeight * (float)(FS_LABEL_BAND_BOTTOM - FS_LABEL_BAND_TOP) * 0.80f);
	}

	// FileStore E01 or E01S
	const bool E01S = MachineType == Model::FileStoreE01S;
	const bool DiscInserted[2] = { DiscInfo[0].Loaded, DiscInfo[1].Loaded };

	DrawUnit(Canvas, Unit, GetModelLabel(E01S ? "E01S" : "E01").c_str(), FileStoreDoorOpen, true,
	         "MODE", FileStoreModeLED, DiscInserted, LEDs.FloppyDisc, ModelTextSize);

	// Hard drive units
	for (int Drive = 0; Drive < FILESTORE_MAX_DRIVES; Drive++)
	{
		if (m_FileStoreUnitName[Drive].empty())
		{
			continue;
		}

		Unit.Top += UnitHeight + Gap;

		// The DRIVE LED shows disc activity, and is always red: the View ->
		// LEDs colour setting doesn't apply to the front panel
		DrawUnit(Canvas, Unit, m_FileStoreUnitName[Drive].c_str(), false, false,
		         "DRIVE", LEDs.HDisc[Drive], nullptr, nullptr, ModelTextSize);
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

	for (int Drive = 0; Drive < FILESTORE_MAX_DRIVES; Drive++)
	{
		if (LEDs.HDisc[Drive])
		{
			State |= 0x10 << Drive;
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
		m_FileStoreShutdownModel  = NewModelType;
		return true;
	}

	m_FileStoreShutdownAction = Action;
	m_FileStoreShutdownModel  = NewModelType;

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

	// Close the door, ready for the next start
	FileStoreDoorOpen = false;
	CheckMenuItem(IDM_FILESTORE_DOOR_OPEN, false);
	EnableMenuItem(IDM_FILESTORE_DOOR_OPEN, IsFileStore(MachineType));

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
