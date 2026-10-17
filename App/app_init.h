#ifndef __APP_INIT_H__
#define __APP_INIT_H__

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/*    任务句柄声明    */
extern TaskHandle_t LEDTaskHandle;
extern TaskHandle_t	SampleTaskHandle;
extern TaskHandle_t PortectTaskHandle;


/*    互斥信号量    */
extern SemaphoreHandle_t I2C_Mutex;

void App_Init(void);
	
#endif
