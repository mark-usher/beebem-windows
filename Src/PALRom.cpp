/****************************************************************
BeebEm - BBC Micro and Master 128 Emulator
Copyright (C) 2024 Steve Inglis

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

/*
 * (C) Nigel Barnes (original work from Mame)
 * (C) Steve Inglis derivative work for BeebEm
 *
 * BBC Micro PALPROM carrier boards original work done by Nigel.
 * License MIT 3 clause
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "PALRom.h"
#include "zlib/zlib.h"

/****************************************************************************/

PALRomState PALRom[MAX_PAL_ROMS];

/****************************************************************************/

static unsigned char ReadInterWord(int Bank, int Offset)
{
    switch (Offset & 0x3FE0)
    {
        case 0x0060:
        case 0x3FC0: PALRom[Bank].Bank = 0; break;
        case 0x0040:
        case 0x3FA0:
        case 0x3FE0: PALRom[Bank].Bank = 1; break;
    }

    return PALRom[Bank].Rom[(Offset & 0x3FFF) | (PALRom[Bank].Bank << 14)];
}

/****************************************************************************/

static unsigned char ReadInterBase(int Bank, int Offset)
{
    switch (Offset & 0x3FE0)
    {
        case 0x3F80: PALRom[Bank].Bank = 0; break;
        case 0x3FA0: PALRom[Bank].Bank = 1; break;
        case 0x3FC0: PALRom[Bank].Bank = 2; break;
        case 0x3FE0: PALRom[Bank].Bank = 3; break;
    }

    return PALRom[Bank].Rom[(Offset & 0x3FFF) | (PALRom[Bank].Bank << 14)];
}

/****************************************************************************/

static unsigned char ReadSpellMaster(int Bank, int Offset)
{
    if (Offset == 0x3FE0)
    {
        PALRom[Bank].Bank = 0;
    }
    else if (PALRom[Bank].Bank == 0)
    {
        switch (Offset & 0x3FE0)
        {
            case 0x3FC0: PALRom[Bank].Bank = 1; break;
            case 0x3FA0: PALRom[Bank].Bank = 2; break;
            case 0x3F80: PALRom[Bank].Bank = 3; break;
            case 0x3F60: PALRom[Bank].Bank = 4; break;
            case 0x3F40: PALRom[Bank].Bank = 5; break;
            case 0x3F20: PALRom[Bank].Bank = 6; break;
            case 0x3F00: PALRom[Bank].Bank = 7; break;
        }
    }

    return PALRom[Bank].Rom[(Offset & 0x3FFF) | (PALRom[Bank].Bank << 14)];
}

/****************************************************************************/

static unsigned char ReadViewTrilogy(int Bank, int Offset)
{
    switch (Offset & 0x3FF8)
    {
        case 0x3FF8: PALRom[Bank].Bank = Offset & 0x03; break;
    }

    return PALRom[Bank].Rom[(Offset & 0x3FFF) | (PALRom[Bank].Bank << 14)];
}

/****************************************************************************/

static unsigned char ReadAdvancedBasicEditor(int Bank, int Offset)
{
    switch (Offset & 0x3FFC)
    {
        case 0x3FF8: PALRom[Bank].Bank = 1; break;
        case 0x3FFC: PALRom[Bank].Bank = 0; break;
    }

    return PALRom[Bank].Rom[(Offset & 0x3FFF) | (PALRom[Bank].Bank << 14)];
}

/****************************************************************************/

static unsigned char ReadAdvancedBasicEditorPlus(int Bank, int Offset)
{
    switch (Offset & 0x3FFC)
    {
        case 0x3FF8: PALRom[Bank].Bank = 0; break;
        case 0x3FFC: PALRom[Bank].Bank = 1; break;
    }

    return PALRom[Bank].Rom[(Offset & 0x3FFF) | (PALRom[Bank].Bank << 14)];
}

/****************************************************************************/

static unsigned char ReadQuest(int Bank, int Offset)
{
    switch (Offset & 0x3FE0)
    {
        case 0x0820: PALRom[Bank].Bank = 2; break;
        case 0x11E0: PALRom[Bank].Bank = 1; break;
        case 0x12C0: PALRom[Bank].Bank = 3; break;
        case 0x1340: PALRom[Bank].Bank = 0; break;
    }

    if (Offset & 0x2000)
    {
        return PALRom[Bank].Rom[(Offset & 0x1FFF) | (PALRom[Bank].Bank << 13)];
    }
    else
    {
        return PALRom[Bank].Rom[(Offset & 0x1FFF)];
    }
}

/****************************************************************************/

static unsigned char ReadWappingEditor(int Bank, int Offset)
{
    switch (Offset & 0x3FE0)
    {
        case 0x1F00: PALRom[Bank].Bank = 0; break;
        case 0x1F20: PALRom[Bank].Bank = 1; break;
        case 0x1F40: PALRom[Bank].Bank = 2; break;
        case 0x1F60: PALRom[Bank].Bank = 3; break;
        case 0x1F80: PALRom[Bank].Bank = 4; break;
        case 0x1FA0: PALRom[Bank].Bank = 5; break;
        case 0x1FC0: PALRom[Bank].Bank = 6; break;
        case 0x1FE0: PALRom[Bank].Bank = 7; break;
    }

    if (Offset & 0x2000)
    {
        return PALRom[Bank].Rom[(Offset & 0x1FFF) | (PALRom[Bank].Bank << 13)];
    }
    else
    {
        return PALRom[Bank].Rom[Offset & 0x1FFF];
    }
}

/****************************************************************************/

static unsigned char ReadTeletextEditor(int Bank, int Offset)
{
    switch (Offset & 0x3FE0)
    {
        case 0x1F80: PALRom[Bank].Bank = 0; break;
        case 0x1FA0: PALRom[Bank].Bank = 1; break;
        case 0x1FC0: PALRom[Bank].Bank = 2; break;
        case 0x1FE0: PALRom[Bank].Bank = 3; break;
    }

    if (Offset & 0x2000)
    {
        return PALRom[Bank].Rom[(Offset & 0x1FFF) | (PALRom[Bank].Bank << 13)];
    }
    else
    {
        return PALRom[Bank].Rom[Offset & 0x1FFF];
    }
}

/****************************************************************************/

static unsigned char ReadWordwisePlus2(int Bank, int Offset)
{
    switch (Offset & 0x3FE0)
    {
        case 0x3F80: PALRom[Bank].Bank = 0; break;
        case 0x3FA0: PALRom[Bank].Bank = 1; break;
    }

    return PALRom[Bank].Rom[(Offset & 0x3FFF) | (PALRom[Bank].Bank << 14)];
}

/****************************************************************************/

unsigned char PALRomRead(int Bank, int Offset)
{
    switch (PALRom[Bank].Type)
    {
        case PALRomType::None:
        default:
            return PALRom[Bank].Rom[Offset];

        case PALRomType::InterWord:
            return ReadInterWord(Bank, Offset);

        case PALRomType::SpellMaster:
            return ReadSpellMaster(Bank, Offset);

        case PALRomType::InterBase:
            return ReadInterBase(Bank, Offset);

        case PALRomType::ViewTrilogy:
            return ReadViewTrilogy(Bank, Offset);

        case PALRomType::AdvancedBasicEditor:
            return ReadAdvancedBasicEditor(Bank, Offset);

        case PALRomType::AdvancedBasicEditorPlus:
            return ReadAdvancedBasicEditorPlus(Bank, Offset);

        case PALRomType::Quest:
            return ReadQuest(Bank, Offset);

        case PALRomType::WappingEditor:
            return ReadWappingEditor(Bank, Offset);

        case PALRomType::TeletextEditor:
            return ReadTeletextEditor(Bank, Offset);

        case PALRomType::WordwisePlus2:
            return ReadWordwisePlus2(Bank, Offset);
    }
}

/****************************************************************************/

PALRomType GuessRomType(unsigned char* Rom, int Size)
{
    char RomName[11];
    memcpy(RomName, &Rom[9], 10);
    RomName[10] = '\0';
    unsigned int Crc = crc32(0, Rom, Size);

    if (strstr(RomName, "ViewSpell") && Size == PALROM_64K && Crc == 0xE56B0E01)
    {
        return PALRomType::ViewTrilogy; // Acornsoft
    }
    else if (strstr(RomName, "MegaROM") && Size == PALROM_128K && Crc == 0xFAAC60BB)
    {
        return PALRomType::SpellMaster; // Computer Concepts
    }
    else if (strstr(RomName, "INTER-BASE") && Size == PALROM_64K && Crc == 0x4332ED95)
    {
        return PALRomType::InterBase; // Computer Concepts
    }
    else if (strstr(RomName, "INTER-WORD") && Size == PALROM_32K && Crc == 0xC93E3C33)
    {
        return PALRomType::InterWord; // Computer Concepts
    }
    else if (strstr(RomName, "AMX") && Size == PALROM_32K && Crc == 0xA72E2E59)
    {
        // outstanding needs updated crc
        return PALRomType::InterWord;
    }
    else if (strstr(RomName, "SPELLMASTE") && Size == PALROM_128K && Crc == 0xC26533EC)
    {
        return PALRomType::SpellMaster; // Computer Concepts
    }
    else if (strstr(RomName, "Advanced") && Size == PALROM_32K && Crc == 0xE0B93F43)
    {
        return PALRomType::AdvancedBasicEditor; // PRES
    }
    else if (strstr(RomName, "The BASIC") && Size == PALROM_32K && Crc == 0x7E3A119A)
    {
        return PALRomType::AdvancedBasicEditorPlus; // PRES
    }
    else if (strstr(RomName, "QUEST") && Size == PALROM_32K && (Crc == 0x7880EB9A || Crc == 0x839A9B34))
    {
        return PALRomType::Quest; // Watford Electronics
    }
    else if (strstr(RomName, "WAPPING ED") && Size == PALROM_64K && Crc == 0x26431B44)
    {
        return PALRomType::WappingEditor; // Watford Electronics
    }
    else if (strstr(RomName, "TED 32K\0 1") && Size == PALROM_32K && Crc == 0x2D4C6458)
    {
        return PALRomType::TeletextEditor; // Watford Electronics
    }
    else if (strstr(RomName, "CONQUEST") && Size == PALROM_32K && Crc == 0xF9634ECE)
    {
        return PALRomType::Quest; // Watford Electronics
    }
    else if (strstr(RomName, "WORDWISE-P") && Size == PALROM_32K && Crc == 0x1F560E1F)
    {
        return PALRomType::WordwisePlus2; // Computer Concepts
    }
    else if (strstr(RomName, "MASTER ROM") && Size == PALROM_32K && Crc == 0x6F0CB588)
    {
        return PALRomType::WordwisePlus2; // Computer Concepts
    }

    return PALRomType::None;
}

/****************************************************************************/
