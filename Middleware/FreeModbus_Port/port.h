/*
 * FreeModbus Libary: BARE Port
 * Copyright (C) 2006 Christian Walter <wolti@sil.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * File: $Id$
 */

#ifndef _PORT_H
#define _PORT_H

#include <assert.h>
#include <inttypes.h>

/* FreeRTOS内核头：事件队列、临界区宏依赖 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define	INLINE                      inline
#define PR_BEGIN_EXTERN_C           extern "C" {
#define	PR_END_EXTERN_C             }

/* 临界区：屏蔽优先级>=configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5)的中断
   裸机版的空宏会让RTU状态机与串口/定时器中断产生竞争，必须改成RTOS临界区 */
#define ENTER_CRITICAL_SECTION()    taskENTER_CRITICAL()
#define EXIT_CRITICAL_SECTION()     taskEXIT_CRITICAL()

/* eMBPoll在xMBPortEventGet里阻塞等待事件的超时时间(ms)
   有事件立即唤醒，无事件超时返回，Modbus任务不会长期占死也不会饿死 */
#define MB_EVENT_GET_TIMEOUT_MS     10 

typedef uint8_t BOOL;

typedef unsigned char UCHAR;
typedef char CHAR;

typedef uint16_t USHORT;
typedef int16_t SHORT;

typedef uint32_t ULONG;
typedef int32_t LONG;

#ifndef TRUE
#define TRUE            1
#endif

#ifndef FALSE
#define FALSE           0
#endif

#endif
