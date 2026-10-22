#ifndef _FREEMODBUS_APP_H
#define _FREEMODBUS_APP_H

#include "mb.h"

/* Modbus RTU从机任务：初始化协议栈并轮询事件 */
void FreeModbus_RTU_Task( void *pvParameters );

#endif
