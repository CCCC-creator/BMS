#ifndef __BQ76940_H__
#define __BQ76940_H__

#include "stdint.h"


/* 保护 */
#define OV_THRESHOLD								4250    	/* 阈值，单位：mV */
#define UV_THRESHOLD								2750			/* 阈值，单位：mV */
#define OC_THRESHOLD								11000			/* 阈值，单位：mA */
#define	SC_THRESHOLD								25000			/* 阈值，单位：mA */
#define OT_THRESHOLD_X10						600				/* 阈值，60° */
#define UT_THRESHOLD_X10						0					/* 阈值，0° */

extern const uint8_t cell_used[15];

void BQ_Init(void);
void BQ_GetAll(void);
void BQ_Comtrol(void);
void BQ_SHIP(void);
void BQ_GetTem(void);

#endif
