#ifndef __APP_TASK_H__
#define __APP_TASK_H__


#include "stdint.h"


/*    Debug开关    */
#define TASK_DEBUG														0

#define DSG_ON													(1U << 1)						/* 充电MOS开启 */		
#define CHG_ON													(1U << 0)						/* 放电MOS开启 */

typedef struct {
		int Cell_mv[15];
		int Total_mv;
		int Cur_ma;
		int TemX10;        									/* 温度×10 */
		uint8_t ready;
} SampleDate;

/* 任务心跳 */
typedef struct {
			uint32_t hb_LED;
			uint32_t hb_sample;
			uint32_t hb_protect;
} Heartbeat;

extern volatile SampleDate g_bms;
extern volatile Heartbeat hb;




/*--------------------------------------------------------------------------------------------------------------*/
/* 任务函数声明 */
/*--------------------------------------------------------------------------------------------------------------*/

void LEDTask(void *pvParameters);
void SampleTask(void *pvParameters);
void PortectTask(void *pvParameters);
void MonitorTask(void *pvParameters);


#endif
