/****************************************************************
BeebEm - BBC Micro and Master 128 Emulator
Copyright (C) 2026 Mark Usher

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

#include <windows.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "FileStoreDriveLabelDialog.h"
#include "FileStore.h"
#include "Resource.h"

/****************************************************************************/

FileStoreDriveLabelDialog::FileStoreDriveLabelDialog(HINSTANCE hInstance,
                                                     HWND hwndParent,
                                                     int Drive,
                                                     const std::string& Label) :
	Dialog(hInstance, hwndParent, IDD_FILESTORE_DRIVE_LABEL),
	m_Drive(Drive),
	m_Label(Label)
{
}

/****************************************************************************/

INT_PTR FileStoreDriveLabelDialog::DlgProc(
	UINT   nMessage,
	WPARAM wParam,
	LPARAM /* lParam */)
{
	switch (nMessage)
	{
	case WM_INITDIALOG: {
		char Prompt[100];
		sprintf(Prompt, "Label for drive %d, up to %d characters:", m_Drive, FILESTORE_DRIVE_LABEL_LENGTH);
		SetDlgItemText(IDC_FILESTORE_DRIVE_PROMPT, Prompt);

		SendDlgItemMessage(IDC_FILESTORE_DRIVE_LABEL, EM_LIMITTEXT, FILESTORE_DRIVE_LABEL_LENGTH, 0);
		SetDlgItemText(IDC_FILESTORE_DRIVE_LABEL, m_Label);
		SetDlgItemFocus(IDC_FILESTORE_DRIVE_LABEL);
		SendDlgItemMessage(IDC_FILESTORE_DRIVE_LABEL, EM_SETSEL, 0, -1);

		// Focus has been set
		return FALSE;
	}

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDOK: {
			std::string Label = GetDlgItemText(IDC_FILESTORE_DRIVE_LABEL);

			for (char& c : Label)
			{
				c = (char)toupper((unsigned char)c);
			}

			// Remove leading and trailing spaces
			const size_t First = Label.find_first_not_of(' ');
			const size_t Last = Label.find_last_not_of(' ');
			Label = First == std::string::npos ? "" : Label.substr(First, Last - First + 1);

			if (Label.empty() || Label.find_first_not_of(FILESTORE_DRIVE_LABEL_CHARS) != std::string::npos)
			{
				MessageBox(m_hwnd,
				           "Enter 1 to 4 characters: letters, digits, spaces, '-', '.' or '/'.",
				           "BeebEm", MB_OK | MB_ICONWARNING);

				SetDlgItemFocus(IDC_FILESTORE_DRIVE_LABEL);
				return TRUE;
			}

			m_Label = Label;
			EndDialog(wParam);
			return TRUE;
		}

		case IDCANCEL:
			EndDialog(wParam);
			return TRUE;
		}
	}

	return FALSE;
}

/****************************************************************************/

const std::string& FileStoreDriveLabelDialog::GetLabel() const
{
	return m_Label;
}

/****************************************************************************/
