/************************************************************************
   debug.h

   BeebSCSI debug output

   BeebEm: BeebSCSI's own debug output (to its serial port on Pi1MHz) is
   not used.  The calls stay in scsi.cpp, as in Pi1MHz, and do nothing:
   the flags are always false.

************************************************************************/

#ifndef DEBUG_H_
#define DEBUG_H_

#include <stdint.h>

#define PSTR(s) (s)

#define DEBUG_INFO         0
#define DEBUG_SUCCESS      1
#define DEBUG_ERROR        2
#define DEBUG_SCSI_COMMAND 3

// Defined in scsi.cpp, always false
extern bool debugFlag_scsiCommands;
extern bool debugFlag_scsiBlocks;
extern bool debugFlag_scsiState;

inline void debugString_P(const char * /* string */) {}
inline void debugString_C(const char * /* string */, int /* type */) {}
inline void debugStringInt16_P(const char * /* string */, uint16_t /* value */, bool /* newLine */) {}
inline void debugStringInt32_P(const char * /* string */, uint32_t /* value */, bool /* newLine */) {}
inline void debugSectorBufferHex(const uint8_t * /* buffer */, uint16_t /* length */) {}

#endif /* DEBUG_H_ */
