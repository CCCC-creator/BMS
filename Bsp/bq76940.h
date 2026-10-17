#ifndef __BQ76940_H__
#define __BQ76940_H__

#include "stdint.h"
#include "stm32f1xx_hal.h"
#include "stdbool.h"


#define SYS_CTRL2										0x05
#define SYS_STAT										0x00

extern const uint8_t cell_used[15];
extern volatile uint32_t I2C_Tolat_Err;
extern volatile uint8_t I2C_Curr_Err;

HAL_StatusTypeDef BQ_ReadBlock(uint8_t reg, uint8_t *date, uint8_t len);
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
