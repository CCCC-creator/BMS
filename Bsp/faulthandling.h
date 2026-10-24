#ifndef __FAULTHANDLING_H__
#define __FAULTHANDLING_H__


#include "stdint.h"


void FaultHandling(uint8_t flags);
void ClearFaultStatus(void);

extern volatile uint8_t CHG_enable;
extern volatile uint8_t DSG_enable;

#endif

