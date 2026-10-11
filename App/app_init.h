#ifndef __APP_INIT_H__
#define __APP_INIT_H__

#include "FreeRTOS.h"
#include "task.h"

/*    ÈÎÎñ¾ä±úÉùÃ÷    */
extern TaskHandle_t LEDTaskHandle;
extern TaskHandle_t	SampleTaskHandle;
extern TaskHandle_t PortectTaskHandle;


void App_Init(void);

#endif
