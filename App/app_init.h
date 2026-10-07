#ifndef __APP_INIT_H__
#define __APP_INIT_H__


#include "FreeRTOS.h"
#include "task.h"


/*    任务栈大小    */
#define LED_STACK_SIZE 										88
#define Sample_STACK_SIZE 								110


/*    任务优先级    */
#define LED_PRIORITY 											2
#define Sample_PRIORITY 									2


/*    任务控制块声明    */
static StaticTask_t LEDTaskTCB;
static StaticTask_t SampleTaskTCB;


/*    任务栈数组声明    */
static StackType_t LEDTaskStack[LED_STACK_SIZE];
static StackType_t SampleTaskStack[Sample_STACK_SIZE];


/*    任务句柄声明    */
static TaskHandle_t LEDTaskHandle;
static TaskHandle_t SampleTaskHandle;


void App_Init(void);

#endif
