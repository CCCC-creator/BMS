#include "app_init.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "app_task.h"
#include "queue.h"
#include "semphr.h"


/*    任务栈大小    */
#define LED_STACK_SIZE 										128
#define Sample_STACK_SIZE 								160
#define Portect_STACK_SIZE 								160
#define Monitor_STACK_SIZE 								128
#define Soc_STACK_SIZE 										256
#define Balancing_STACK_SIZE 							256


/*    任务优先级    */
#define LED_PRIORITY 											2
#define Sample_PRIORITY 									2
#define Portect_PRIORITY 									3
#define Monitor_PRIORITY 									1
#define Soc_PRIORITY 											2
#define Balancing_PRIORITY 								2


/*    任务控制块声明    */
static StaticTask_t LEDTaskTCB;
static StaticTask_t SampleTaskTCB;
static StaticTask_t PortectTaskTCB;
static StaticTask_t MonitorTaskTCB;
static StaticTask_t SocTaskTCB;
static StaticTask_t BalancingTaskTCB;


/*    任务栈数组声明    */
static StackType_t LEDTaskStack[LED_STACK_SIZE];
static StackType_t SampleTaskStack[Sample_STACK_SIZE];
static StackType_t PortectTaskStack[Portect_STACK_SIZE];
static StackType_t MonitorTaskStack[Monitor_STACK_SIZE];
static StackType_t SocTaskStack[Soc_STACK_SIZE];
static StackType_t BalancingTaskStack[Balancing_STACK_SIZE];


/*    任务句柄声明    */
TaskHandle_t LEDTaskHandle;
TaskHandle_t SampleTaskHandle;
TaskHandle_t PortectTaskHandle;
TaskHandle_t MonitorTaskHandle;
TaskHandle_t SocTaskHandle;
TaskHandle_t BalancingTaskHandle;



/*    互斥信号量    */
StaticSemaphore_t I2C_MutexBuffer;
SemaphoreHandle_t I2C_Mutex;


void App_Init(void)
{
	I2C_Mutex = xSemaphoreCreateMutexStatic( &I2C_MutexBuffer );    

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
																					
	SocTaskHandle = xTaskCreateStatic( 	SOCTask,
																			"SOCTask",
																			Soc_STACK_SIZE,
																			NULL,
																			Soc_PRIORITY,
																			SocTaskStack,
																			&SocTaskTCB );																						

	BalancingTaskHandle = xTaskCreateStatic( 	BalancingTask,
																						"BalancingTask",
																						Balancing_STACK_SIZE,
																						NULL,
																						Balancing_PRIORITY,
																						BalancingTaskStack,
																						&BalancingTaskTCB );	
																					
	MonitorTaskHandle = xTaskCreateStatic( 	MonitorTask,
																					"MonitorTask",
																					Monitor_STACK_SIZE,
																					NULL,
																					Monitor_PRIORITY,
																					MonitorTaskStack,
																					&MonitorTaskTCB );																				
}
