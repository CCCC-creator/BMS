#ifndef __SOC_H__
#define __SOC_H__

#include "stdint.h"


void SOC_Est(void);
void Init_SOC(void);

//uint16_t v_cell_min(void);
uint16_t v_cell_median(void);


extern volatile uint8_t g_socinit_flag;

#endif



