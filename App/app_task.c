#include "app_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "stdio.h"
#include "bq76940.h"
#include "faultcheck.h"
#include "faulthandling.h"
#include "dwt.h"
#include "app_init.h"
#include "monitor.h"


volatile SampleDate g_bms;
volatile Heartbeat hb;


/* 空闲任务 */
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
/* LED闪烁任务 500ms*/
void LEDTask(void *pvParameters)
{
		(void)pvParameters;
		static TickType_t xLastWakeTime;
		xLastWakeTime	= xTaskGetTickCount();																			/* 记录上一次唤醒的 tick 计数 */
#if TASK_DEBUG
		static UBaseType_t LEDGetStack_StackWater = 0;
		TickType_t Cycle_start;																										/* 耗時測量：DWT 周期计数器在这颗片子上没跑（CYCCNT 静止），改用系统节拍（1ms 分辨率） */
		uint32_t Cycle_ms = 0;
#endif
		while(1)
		{
#if TASK_DEBUG
				Cycle_start = xTaskGetTickCount();
#endif
				HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
#if TASK_DEBUG
				Cycle_ms = (uint32_t)(xTaskGetTickCount() - Cycle_start) * portTICK_PERIOD_MS;
				LEDGetStack_StackWater = uxTaskGetStackHighWaterMark(NULL);
				printf("LEDTask剩余栈：%lu字  耗时：%lums\r\n", LEDGetStack_StackWater, (unsigned long)Cycle_ms);
#endif
				vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(500));										/* pdMS_TO_TICKS 把毫秒 (ms) 时间，转换成 FreeRTOS 的系统节拍 tick 计数值。 */
				hb.hb_LED++;
		}
}

/* 采集任务 250ms */
/* IIC：单主 + 命令通道 多任务共享需要用互斥锁 */
 void SampleTask(void *pvParameters)
{
		(void)pvParameters;
		uint8_t cycle = 0;
		static TickType_t xLastWakeTime;
		xLastWakeTime	= xTaskGetTickCount();																				
#if TASK_DEBUG
		static UBaseType_t SampleGetStack_StackWater = 0;
		TickType_t Cycle_start;
		uint32_t Cycle_ms = 0;
#endif
		while(1)
		{
#if TASK_DEBUG
				Cycle_start = xTaskGetTickCount();
#endif
				BQ_GetAll();																														/* 通过结构体传数据给PortectTask */
				cycle ++;
				if(cycle % 8 == 0)																											/* 采集温度芯片2s更新一次 2s打印一次数据 */
				{
						BQ_GetTem();																												/* 通过结构体传数据给PortectTask */
						for (int i = 0; i < 15; i++)
								printf("Cell%d = %dmV\r\n", i + 1 , g_bms.Cell_mv[i]);
						printf("V_total = %dmV\r\n", g_bms.Total_mv);
						printf("I=%dmA，T=%d.%d\r\n", g_bms.Cur_ma, g_bms.TemX10 / 10, g_bms.TemX10 % 10);
						g_bms.ready = 1;																	/* 第一轮SampleTask结束 通知PortectTask */
						cycle = 0;
				}
#if TASK_DEBUG
				Cycle_ms = (uint32_t)(xTaskGetTickCount() - Cycle_start) * portTICK_PERIOD_MS;
				SampleGetStack_StackWater = uxTaskGetStackHighWaterMark(NULL);
				printf("SampleTask剩余栈：%lu字  耗时：%lums\r\n", SampleGetStack_StackWater, (unsigned long)Cycle_ms);
#endif			
				vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(250));
				hb.hb_sample++;
		}
}

/* 故障检测与处理任务 250ms */
void PortectTask(void *pvParameters)
{
		(void)pvParameters;
		static TickType_t xLastWakeTime;
		xLastWakeTime	= xTaskGetTickCount();															
#if TASK_DEBUG
		static UBaseType_t PortectTask_StackWater = 0;
		TickType_t Cycle_start;
		uint32_t Cycle_ms = 0;
#endif
			while(1)			
			{
#if TASK_DEBUG
					Cycle_start = xTaskGetTickCount();
#endif
						if(g_bms.ready)																	/* 判断是否故障与处理 */																							/* 判断第一轮SampleTask是否结束 */
						{
							FaultHandling(FaultCheck());									/* 故障检测后传数据给故障处理 */																				
#if TASK_DEBUG
							Cycle_ms = (uint32_t)(xTaskGetTickCount() - Cycle_start) * portTICK_PERIOD_MS;
							PortectTask_StackWater = uxTaskGetStackHighWaterMark(NULL);
							printf("PortectTask剩余栈：%lu字  耗时：%lums\r\n", PortectTask_StackWater, (unsigned long)Cycle_ms);
#endif
						}
						vTaskDelayUntil(&xLastWakeTime,pdMS_TO_TICKS(250));
						hb.hb_protect++;
			}
}

/* 看门狗任务 1000ms */
void MonitorTask(void *pvParameters)
{
		(void)pvParameters;
		static TickType_t xLastWakeTime;
		xLastWakeTime	= xTaskGetTickCount();																			/* 记录上一次唤醒的 tick 计数 */
#if TASK_DEBUG
		static UBaseType_t MonitorGetStack_StackWater = 0;
		TickType_t Cycle_start;																										/* 耗時測量：DWT 周期计数器在这颗片子上没跑（CYCCNT 静止），改用系统节拍（1ms 分辨率） */
		uint32_t Cycle_ms = 0;
#endif
			while(1)
			{
#if TASK_DEBUG
				Cycle_start = xTaskGetTickCount();
#endif
				Monitor();
#if TASK_DEBUG
				Cycle_ms = (uint32_t)(xTaskGetTickCount() - Cycle_start) * portTICK_PERIOD_MS;
				MonitorGetStack_StackWater = uxTaskGetStackHighWaterMark(NULL);
				printf("MonitorTask剩余栈：%lu字  耗时：%lums\r\n", MonitorGetStack_StackWater, (unsigned long)Cycle_ms);
#endif
				vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));				
			}
}
