#ifndef __APP_TASK_H__
#define __APP_TASK_H__


#endif

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"


/*    任务栈大小    */
#define LED_STACK_SIZE 									128



/*    任务优先级    */
#define LED_PRIORITY 								2



/*    任务控制块声明    */
extern StaticTask_t LEDTaskTCB;



/*    任务栈数组声明    */
extern StackType_t LEDTaskStack[LED_STACK_SIZE];



/*    任务句柄声明    */
extern TaskHandle_t LEDTaskHandle;



/*    任务函数声明    */
void LEDTask(void *pvParameters);
