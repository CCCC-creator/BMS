#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "app_task.h"
#include "app_init.h"


/*    任务栈大小    */
#define LED_STACK_SIZE 										88
#define Sample_STACK_SIZE 								110
#define Protect_STACK_SIZE 								256


/*    任务优先级    */
#define LED_PRIORITY 											2
#define Sample_PRIORITY 									2
#define Protect_PRIORITY 									2


/*    任务控制块声明    */
static StaticTask_t LEDTaskTCB;
static StaticTask_t SampleTaskTCB;
static StaticTask_t ProtectTaskTCB;


/*    任务栈数组声明    */
static StackType_t LEDTaskStack[LED_STACK_SIZE];
static StackType_t SampleTaskStack[Sample_STACK_SIZE];
static StackType_t ProtectTaskStack[Protect_STACK_SIZE];


void App_Init(void)
{
	xTaskCreateStatic( 	LEDTask,
											"LEDTask",
											LED_STACK_SIZE,
											NULL,
											LED_PRIORITY,
											LEDTaskStack,
											&LEDTaskTCB );
	
	xTaskCreateStatic( 	SampleTask,
											"SampleTask",
											Sample_STACK_SIZE,
											NULL,
											Sample_PRIORITY,
											SampleTaskStack,
											&SampleTaskTCB );
	
	xTaskCreateStatic( 	ProtectTask,
											"ProtectTask",
											Protect_STACK_SIZE,
											NULL,
											Protect_PRIORITY,
											ProtectTaskStack,
											&ProtectTaskTCB );
}
