#ifndef __APP_TASK_H__
#define __APP_TASK_H__

#include "FreeRTOS.h"
#include "task.h"


/*    Debug    */
#define LED_TASK_DEBUG										0


/*    任务栈大小    */
#define LED_STACK_SIZE 										88


/*    任务优先级    */
#define LED_PRIORITY 											2


/*    任务控制块声明    */
static StaticTask_t LEDTaskTCB;


/*    任务栈数组声明    */
static StackType_t LEDTaskStack[LED_STACK_SIZE];


/*    任务句柄声明    */
static TaskHandle_t LEDTaskHandle;


/*    任务函数声明    */
void LEDTask(void *pvParameters);

#endif
