#include "app_init.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "app_task.h"
#include "queue.h"



/*    任务栈大小    */
#define LED_STACK_SIZE 										128
#define Sample_STACK_SIZE 								160
#define Portect_STACK_SIZE 								160
#define Monitor_STACK_SIZE 								64


/*    任务优先级    */
#define LED_PRIORITY 											2
#define Sample_PRIORITY 									2
#define Portect_PRIORITY 									3
#define Monitor_PRIORITY 									1


/*    任务控制块声明    */
static StaticTask_t LEDTaskTCB;
static StaticTask_t SampleTaskTCB;
static StaticTask_t PortectTaskTCB;
static StaticTask_t MonitorTaskTCB;


/*    任务栈数组声明    */
static StackType_t LEDTaskStack[LED_STACK_SIZE];
static StackType_t SampleTaskStack[Sample_STACK_SIZE];
static StackType_t PortectTaskStack[Portect_STACK_SIZE];
static StackType_t MonitorTaskStack[Monitor_STACK_SIZE];

/*    任务句柄声明    */
TaskHandle_t LEDTaskHandle;
TaskHandle_t SampleTaskHandle;
TaskHandle_t PortectTaskHandle;
TaskHandle_t MonitorTaskHandle;

/*    队列    */
#define FH_Length														1					
#define	FH_Size												sizeof(uint8_t)
uint8_t	FH_Buffer[FH_Length * FH_Size];
StaticQueue_t	FHqueueTCB;
QueueHandle_t FHQueue;

void App_Init(void)
{
	FHQueue = xQueueCreateStatic(	FH_Length,
																FH_Size,
																FH_Buffer,
																&FHqueueTCB );
                        
	LEDTaskHandle = xTaskCreateStatic( 	LEDTask,
																			"LEDTask",
																			LED_STACK_SIZE,
																			NULL,
																			LED_PRIORITY,
																			LEDTaskStack,
																			&LEDTaskTCB );
	
	SampleTaskHandle = xTaskCreateStatic( 	SampleTask,
																					"SampleTask",
																					Sample_STACK_SIZE,
																					NULL,
																					Sample_PRIORITY,
																					SampleTaskStack,
																					&SampleTaskTCB );
	
	PortectTaskHandle = xTaskCreateStatic( 	PortectTask,
																					"PortectTask",
																					Portect_STACK_SIZE,
																					NULL,
																					Portect_PRIORITY,
																					PortectTaskStack,
																					&PortectTaskTCB );
																					
	MonitorTaskHandle = xTaskCreateStatic( 	MonitorTask,
																					"MonitorTask",
																					Monitor_STACK_SIZE,
																					NULL,
																					Monitor_PRIORITY,
																					MonitorTaskStack,
																					&MonitorTaskTCB );																				
}
