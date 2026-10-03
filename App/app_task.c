#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "stdio.h"
#include "app_task.h"



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

/*任务*/
void LEDTask(void *pvParameters)
{
		(void)pvParameters;
#if LED_TASK_DEBUG
		static UBaseType_t LEDGetStack = 0;
#endif
		while(1)
		{
				HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
#if LED_TASK_DEBUG
				LEDGetStack = uxTaskGetStackHighWaterMark(NULL);
				printf("LEDTask：%d字\r\n", LEDGetStack);
#endif
				vTaskDelay(500);			
		}
}
