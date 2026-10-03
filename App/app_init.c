#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "app_task.h"
#include "app_init.h"





void App_Init(void)
{
	taskENTER_CRITICAL();               /* 进入临界区 */
	xTaskCreateStatic( 	LEDTask,
											"LEDTask",
											LED_STACK_SIZE,
											NULL,
											LED_PRIORITY,
											LEDTaskStack,
											&LEDTaskTCB );
	
	
	taskEXIT_CRITICAL();                /* 退出临界区 */
}
