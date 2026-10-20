#ifndef __BALANCING_H__
#define __BALANCING_H__

#include "stdint.h"

/* 1=当前有均衡在跑（供 FaultCheck 跳过不可信的软件 SC） */
extern volatile uint8_t g_balancing_active;

void BQ_Balancing(void);


#endif

