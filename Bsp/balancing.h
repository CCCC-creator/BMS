#ifndef __BALANCING_H__
#define __BALANCING_H__

#include "stdint.h"

/* 1 = balancing active; FaultCheck skips software SC while balancing */
extern volatile uint8_t g_balancing_active;

void BQ_Balancing(void);


#endif

