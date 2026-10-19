#ifndef __APP_TASK_H__
#define __APP_TASK_H__


#include "stdint.h"
#include "FreeRTOS.h"


/*    Debug开关    */
#define TASK_DEBUG													0
	
#define CHG_Bit													(1U << 0)						/* 充电MOS位 */
#define DSG_Bit													(1U << 1)						/* 放电MOS位 */

typedef struct {
		int Cell_mv[15];
		int Total_mv;
		int Cur_ma;
		int TemX10;        									/* 温度×10 */
		uint8_t ready;
		uint8_t fault;       								/* 当前故障位图（FaultCheck 的返回值） */
		uint16_t socX10;										/* OCV得出SOC */
		int16_t Ah_socX10;									/* 安时积分法SOC */
} SampleDate;

/* 任务心跳 */
typedef struct {
		uint32_t hb_LED;
		uint32_t hb_sample;
		uint32_t hb_protect;
		uint32_t hb_soc;
		uint32_t hb_Balancing;
} Heartbeat;

extern volatile SampleDate g_bms;
extern volatile Heartbeat hb;

extern TickType_t last_time;	


/*--------------------------------------------------------------------------------------------------------------*/
/* 任务函数声明 */
/*--------------------------------------------------------------------------------------------------------------*/

void LEDTask(void *pvParameters);
void SampleTask(void *pvParameters);
void PortectTask(void *pvParameters);
void MonitorTask(void *pvParameters);
void SOCTask(void *pvParameters);
void BalancingTask(void *pvParameters);

#endif
