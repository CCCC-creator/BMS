#ifndef __FAULTHANDLING_H__
#define __FAULTHANDLING_H__


#include "stdint.h"


void FaultHandling(uint8_t flags);
void ClearFaultStatus(void);

#endif

