#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "stdio.h"
#include "app_task.h"
#include "bq76940.h"
#include "protect.h"

volatile SampleDate g_bms;

/*空闲任务*/
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, 
																		StackType_t **ppxIdleTaskStackBuffer, 
																		uint32_t *pulIdleTaskStackSize )
{
	static StaticTask_t xIdleTaskTCB;
	static StackType_t xIdleTaskStack[configMINIMAL_STACK_SIZE];
		*ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
		*ppxIdleTaskStackBuffer = xIdleTaskStack;
		*pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/*定时器服务任务*/
void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize )
{
	static StaticTask_t xTimerTaskTCB;
	static StackType_t xTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
		*ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
		*ppxTimerTaskStackBuffer = xTimerTaskStack;
		*pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}


/*--------------------------------------------------------------------------------------------------------------*/
/*任务*/
/*--------------------------------------------------------------------------------------------------------------*/
/* LED500ms闪烁任务 */
void LEDTask(void *pvParameters)
{
		(void)pvParameters;
#if TASK_DEBUG
		static UBaseType_t LEDGetStack = 0;
#endif
		while(1)
		{
				HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
#if TASK_DEBUG
				LEDGetStack = uxTaskGetStackHighWaterMark(NULL);
				printf("LEDTask：%lu字\r\n", LEDGetStack);
#endif
				vTaskDelay(pdMS_TO_TICKS(500));										/* pdMS_TO_TICKS 把毫秒 (ms) 时间，转换成 FreeRTOS 的系统节拍 tick 计数值。 */
		}
}

/* 采集任务 */
 void SampleTask(void *pvParameters)
{
		(void)pvParameters;
		uint8_t cycle = 0;
#if TASK_DEBUG
		static UBaseType_t SampleGetStack = 0;
#endif
		while(1)
		{
				BQ_GetAll();
				cycle ++;
				if(cycle % 8 == 0)																	/* 2s采集温度 */
				{
						BQ_GetTem();
						for (int i = 0; i < 15; i++)
								printf("Cell%d = %dmV\r\n", i + 1 , g_bms.Cell_mv[i]);
						printf("V_total = %dmV\r\n", g_bms.Total_mv);
						printf("I=%dmA，T=%d.%d\r\n", g_bms.Cur_ma, g_bms.TemX10 / 10, g_bms.TemX10 % 10);
						cycle = 0;
				}
				vTaskDelay(pdMS_TO_TICKS(250));
#if TASK_DEBUG
				SampleGetStack = uxTaskGetStackHighWaterMark(NULL);
				printf("SampleTask：%lu字\r\n", SampleGetStack);
#endif			
		}
}

void ProtectTask(void *pvParameters)
{
		(void)pvParameters;
#if TASK_DEBUG
		static UBaseType_t ProtectGetStack = 0;
#endif
		while(1)
		{
				Protect();
#if TASK_DEBUG
				ProtectGetStack = uxTaskGetStackHighWaterMark(NULL);
				printf("ProtectTask：%lu字\r\n", ProtectGetStack);
#endif
				vTaskDelay(pdMS_TO_TICKS(500));												
		}
}
