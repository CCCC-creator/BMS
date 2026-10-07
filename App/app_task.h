#ifndef __APP_TASK_H__
#define __APP_TASK_H__



/*    Debug开关    */
#define TASK_DEBUG										0



typedef struct {
		int Cell_mv[15];
		int Total_mv;
		int Cur_ma;
		int TemX10;        									/* 温度×10 */
} SampleDate;


extern volatile SampleDate g_bms;

/*--------------------------------------------------------------------------------------------------------------*/
/* 任务函数声明 */
/*--------------------------------------------------------------------------------------------------------------*/

void LEDTask(void *pvParameters);
void SampleTask(void *pvParameters);
void ProtectTask(void *pvParameters);

#endif
