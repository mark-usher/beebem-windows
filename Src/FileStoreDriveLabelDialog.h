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

#ifndef FILESTORE_DRIVE_LABEL_DIALOG_HEADER
#define FILESTORE_DRIVE_LABEL_DIALOG_HEADER

#include <string>

#include "Dialog.h"

// Asks for a custom label for a FileStore hard drive unit, shown on its
// front flap after "FILESTORE"

class FileStoreDriveLabelDialog : public Dialog
{
	public:
		FileStoreDriveLabelDialog(
			HINSTANCE hInstance,
			HWND hwndParent,
			int Drive,
			const std::string& Label
		);

	public:
		const std::string& GetLabel() const;

	private:
		virtual INT_PTR DlgProc(
			UINT   nMessage,
			WPARAM wParam,
			LPARAM lParam
		);

	private:
		int m_Drive;
		std::string m_Label;
};

#endif
