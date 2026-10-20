/************************************************************************
   statusled.h

   BeebSCSI status LED

   BeebEm: lights the hard drive LED (Scsi.cpp)

************************************************************************/

#ifndef STATUSLED_H_
#define STATUSLED_H_

#include <stdint.h>

void statusledActivity(uint8_t ledState);

#endif /* STATUSLED_H_ */
