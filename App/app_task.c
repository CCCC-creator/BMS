#include "app_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "stdio.h"
#include "bq76940.h"
#include "faultcheck.h"
#include "faulthandling.h"
#include "app_init.h"
#include "monitor.h"
#include "i2c.h"
#include "soc.h"
#include "balancing.h"
#include "stdlib.h"


volatile SampleDate g_bms;
volatile Heartbeat hb;
TickType_t last_time;																				/* 全局变量具有生命周期，计算每一轮精确时间估算SOC */

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
																									/* 任务 */
/*--------------------------------------------------------------------------------------------------------------*/
/* LED闪烁任务 500ms*/
void LEDTask(void *pvParameters)
{
		(void)pvParameters;
		static TickType_t xLastWakeTime;
		xLastWakeTime	= xTaskGetTickCount();																			/* 记录上一次唤醒的 tick 计数 */
#if DEBUG_LED
		UBaseType_t LEDGetStack_StackWater = 0;
		TickType_t Cycle_start;																										
		uint32_t Cycle_ms = 0;
#endif
		while(1)
		{
#if DEBUG_LED
				Cycle_start = xTaskGetTickCount();
#endif
				HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
#if DEBUG_LED
		Cycle_ms = (uint32_t)(xTaskGetTickCount() - Cycle_start);
		LEDGetStack_StackWater = uxTaskGetStackHighWaterMark(NULL);
		printf("LEDTask剩余栈：%lu字  耗时：%lums\r\n", LEDGetStack_StackWater, (unsigned long)Cycle_ms);
#endif
		vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(500));										/* pdMS_TO_TICKS 把毫秒 (ms) 时间，转换成 FreeRTOS 的系统节拍 tick 计数值。 */
		hb.hb_LED++;
		}
}

/* 采集任务 250ms */
 void SampleTask(void *pvParameters)
{
		(void)pvParameters;
		uint8_t cycle = 0;
		static TickType_t xLastWakeTime;
		xLastWakeTime	= xTaskGetTickCount();																				
#if DEBUG_SAMPLE
		UBaseType_t SampleGetStack_StackWater = 0;
		TickType_t Cycle_start;
		uint32_t Cycle_ms = 0;
#endif
		while(1)
		{
#if DEBUG_SAMPLE
				Cycle_start = xTaskGetTickCount();
#endif
				BQ_GetAll();																														/* 通过结构体传数据给PortectTask */
				cycle ++;
				if(cycle % 8 == 0)																											/* 采集温度芯片2s更新一次 2s打印一次数据 */
				{
						BQ_GetTem();																												/* 通过结构体传数据给PortectTask */
//						for (int i = 0; i < 15; i++)
//								printf("Cell%d = %dmV\r\n", i + 1 , g_bms.Cell_mv[i]);
//						printf("V_total = %dmV\r\n", g_bms.Total_mv);
//						printf("Fault=0x%02X\r\n", g_bms.fault);
//						printf("I=%dmA，T=%d.%d\r\n", g_bms.Cur_ma, g_bms.TemX10 / 10, g_bms.TemX10 % 10);
						g_bms.ready = 1;																	/* 第一轮SampleTask结束 通知PortectTask */
						cycle = 0;
				}
#if DEBUG_SAMPLE
		Cycle_ms = (uint32_t)(xTaskGetTickCount() - Cycle_start);
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
#if DEBUG_PROTECT
		UBaseType_t PortectTask_StackWater = 0;
		TickType_t Cycle_start;
		uint32_t Cycle_ms = 0;
#endif
			while(1)			
			{
#if DEBUG_PROTECT
					Cycle_start = xTaskGetTickCount();
#endif
					if(g_bms.ready)																	/* 判断第一轮SampleTask是否结束 */
					{
						FaultHandling(FaultCheck());									/* 故障检测后传数据给故障处理 */	
						ClearFaultStatus();														/* 清除硬件保护标志 */
#if DEBUG_PROTECT
						Cycle_ms = (uint32_t)(xTaskGetTickCount() - Cycle_start);
						PortectTask_StackWater = uxTaskGetStackHighWaterMark(NULL);
						printf("PortectTask剩余栈：%lu字  耗时：%lums\r\n", PortectTask_StackWater, (unsigned long)Cycle_ms);
#endif
					}
					if(I2C_Curr_Err >= 5)														/* I2C故障超过5次，重启初始化I2C */
					{
						HAL_I2C_DeInit(&hi2c1);												
						MX_I2C1_Init();
						I2C_Curr_Err = 0;
					}
			//printf("I2C I2C_Tolat_Err：%d，I2C_Curr_Err：%d\r\n",I2C_Tolat_Err, I2C_Curr_Err);   
			vTaskDelayUntil(&xLastWakeTime,pdMS_TO_TICKS(250));
			hb.hb_protect++;
			}
}

/* 静置且不均衡时更新SOC */
/* 估算SOC任务 1000ms*/
void SOCTask(void *pvParameters)
{
		(void)pvParameters;
		static TickType_t xLastWakeTime;
		xLastWakeTime	= xTaskGetTickCount();																			/* 记录上一次唤醒的 tick 计数 */
#if DEBUG_SOC
		UBaseType_t SOCTask_StackWater = 0;
		TickType_t Cycle_start;																										
		uint32_t Cycle_ms = 0;
#endif
		while(1)
		{
#if DEBUG_SOC
				Cycle_start = xTaskGetTickCount();
#endif
			if(g_bms.ready)																														/* 采集是否完成 */
			{
				if(!g_socinit_flag)																											/* 上电初始化SOC 断电期间电池本来就是静置的，直接OCV锚定一次SOC */
				{
					last_time = xTaskGetTickCount();																			/* 真正开始积分的一刻打点 */
					if(!g_balancing_active && abs(g_bms.Cur_ma) <= 50)
					{
						Init_SOC();
					}
				}
				else
					SOC_Est();
			}
#if DEBUG_SOC
		Cycle_ms = (uint32_t)(xTaskGetTickCount() - Cycle_start);
		SOCTask_StackWater = uxTaskGetStackHighWaterMark(NULL);
		printf("SOCTask剩余栈：%lu字  耗时：%lums\r\n", SOCTask_StackWater, (unsigned long)Cycle_ms);
#endif
		hb.hb_soc++;	
		vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));										/* pdMS_TO_TICKS 把毫秒 (ms) 时间，转换成 FreeRTOS 的系统节拍 tick 计数值。 */
		}
}

/* 被动均衡任务 250ms*/
void BalancingTask(void *pvParameters)
{
		(void)pvParameters;
		static TickType_t xLastWakeTime;
		xLastWakeTime	= xTaskGetTickCount();																			/* 记录上一次唤醒的 tick 计数 */
#if DEBUG_BALANCING
		UBaseType_t BalancingGetStack_StackWater = 0;
		TickType_t Cycle_start;																										
		uint32_t Cycle_ms = 0;
#endif
		while(1)
		{
#if DEBUG_BALANCING
				Cycle_start = xTaskGetTickCount();
#endif
		BQ_Balancing();	
			
#if DEBUG_BALANCING
		Cycle_ms = (uint32_t)(xTaskGetTickCount() - Cycle_start);
		BalancingGetStack_StackWater = uxTaskGetStackHighWaterMark(NULL);
		printf("BalancingTask剩余栈：%lu字  耗时：%lums\r\n", BalancingGetStack_StackWater, (unsigned long)Cycle_ms);
#endif
		hb.hb_Balancing++;
		vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(250));										
		}
}

/* 看门狗任务 250ms */
void MonitorTask(void *pvParameters)
{
		(void)pvParameters;
		static TickType_t xLastWakeTime;
		xLastWakeTime	= xTaskGetTickCount();																			/* 记录上一次唤醒的 tick 计数 */
#if DEBUG_MONITOR
		UBaseType_t MonitorGetStack_StackWater = 0;
		TickType_t Cycle_start;																										
		uint32_t Cycle_ms = 0;
#endif
			while(1)
			{
#if DEBUG_MONITOR
				Cycle_start = xTaskGetTickCount();
#endif
				Monitor();
#if DEBUG_MONITOR
				Cycle_ms = (uint32_t)(xTaskGetTickCount() - Cycle_start);
				MonitorGetStack_StackWater = uxTaskGetStackHighWaterMark(NULL);
				printf("MonitorTask剩余栈：%lu字  耗时：%lums\r\n", MonitorGetStack_StackWater, (unsigned long)Cycle_ms);
#endif
				vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(250));				
			}
}
