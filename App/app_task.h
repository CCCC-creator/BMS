#ifndef __APP_TASK_H__
#define __APP_TASK_H__


#include "stdint.h"


/*    Debug开关    */
#define TASK_DEBUG														0



typedef struct {
		int Cell_mv[15];
		int Total_mv;
		int Cur_ma;
		int TemX10;        									/* 温度×10 */
		uint8_t ready;
} SampleDate;


extern volatile SampleDate g_bms;




/*--------------------------------------------------------------------------------------------------------------*/
/* 任务函数声明 */
/*--------------------------------------------------------------------------------------------------------------*/

void LEDTask(void *pvParameters);
void SampleTask(void *pvParameters);
void PortectTask(void *pvParameters);

#endif
