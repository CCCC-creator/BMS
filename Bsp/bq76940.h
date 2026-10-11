#ifndef __BQ76940_H__
#define __BQ76940_H__

#include "stdint.h"
#include "stm32f1xx_hal.h"
#include "stdbool.h"


extern const uint8_t cell_used[15];


HAL_StatusTypeDef BQ_WriteReg(uint8_t reg, uint8_t val);
bool SetBit(uint8_t reg_addr, uint8_t mask);
bool ClearBit(uint8_t reg_addr, uint8_t mask);
bool ToggleBit(uint8_t reg_addr, uint8_t mask);
	

void BQ_Init(void);
void BQ_GetAll(void);
void BQ_Comtrol(void);
void BQ_SHIP(void);
void BQ_GetTem(void);

#endif
