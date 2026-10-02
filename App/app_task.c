#include "app_task.h"

StaticTask_t LEDTaskTCB;
StackType_t LEDTaskStack[LED_STACK_SIZE];
TaskHandle_t LEDTaskHandle;

void LEDTask(void *pvParameters)
{
		(void)pvParameters;
		while(1)
		{
				HAL_GPIO_TogglePin(MCU_WAKE_BQ_GPIO_Port, MCU_WAKE_BQ_Pin);
				vTaskDelay(500);
		}
}

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

void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize )
{
	static StaticTask_t xTimerTaskTCB;
	static StackType_t xTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
		*ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
		*ppxTimerTaskStackBuffer = xTimerTaskStack;
		*pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
