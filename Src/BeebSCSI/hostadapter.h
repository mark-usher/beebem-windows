/************************************************************************
   hostadapter.h

   BeebSCSI Acorn host adapter functions
    BeebSCSI - BBC Micro SCSI Drive Emulator
    Copyright (C) 2018 Simon Inns

   This file is part of BeebSCSI.

    BeebSCSI is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

   Email: simon.inns@gmail.com

   BeebEm: implemented by Scsi.cpp, the Acorn host adaptor

************************************************************************/

#ifndef HOSTADAPTER_H_
#define HOSTADAPTER_H_

#include <stdint.h>

uint8_t hostadapterReadDatabus(void);

uint8_t hostadapterReadByte(void);
void hostadapterWriteByte(uint8_t databusValue);

// One block of `length` bytes (the LUN's block size); the plain forms move
// a 256-byte block
uint32_t hostadapterPerformReadDMALen(const uint8_t *dataBuffer, uint32_t length);
uint32_t hostadapterPerformWriteDMALen(uint8_t *dataBuffer, uint32_t length);
uint32_t hostadapterPerformReadDMA(const uint8_t *dataBuffer);
uint32_t hostadapterPerformWriteDMA(uint8_t *dataBuffer);

bool hostadapterConnectedToExternalBus(void);

void hostadapterWriteResetFlag(bool flagState);
bool hostadapterReadResetFlag(void);

void hostadapterWriteDataPhaseFlags(bool message, bool commandNotData, bool inputNotOutput);
void hostadapterWriteBusyFlag(bool flagState);
void hostadapterWriteRequestFlag(bool flagState);

bool hostadapterReadSelectFlag(void);

// BeebEm: is there a drive for this selection (the target ID bit pattern on
// the data bus)?  A FileStore drive slot set to None does not answer.
bool hostadapterTargetAttached(uint8_t hostID);

#endif /* HOSTADAPTER_H_ */
