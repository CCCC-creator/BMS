/*
 * FreeModbus Port: FreeRTOS事件队列版
 * 中断(串口收帧/定时器超时)投递事件，Modbus任务通过eMBPoll阻塞接收
 */

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ----------------------- Defines ------------------------------------------*/
/* 队列深度：覆盖eMBPoll处理期间到达的新事件，满了丢帧由主机重发 */
#define MB_EVENT_QUEUE_LEN      4															/* 队列里存的不是报文数据，只是事件类型枚举 */

/* ----------------------- Variables ----------------------------------------*/
static QueueHandle_t xMBEventQueue = NULL;										/* 代表：队列还没有创建。等到xMBPortEventInit()里面调用xQueueCreate()成功之后，它才会被赋值成有效的队列地址 */

/* ----------------------- Start implementation -----------------------------*/
BOOL
xMBPortEventInit( void )
{
    xMBEventQueue = xQueueCreate( MB_EVENT_QUEUE_LEN, sizeof( eMBEventType ) );
    return ( xMBEventQueue != NULL ) ? TRUE : FALSE;
}

/*
 * 投递事件。调用来源有两种：
 *   任务上下文：mb.c的eMBPoll发EV_EXECUTE、eMBEnable发EV_READY
 *   中断上下文：mbrtu.c收帧完成/定时器超时发EV_FRAME_RECEIVED
 * 通过__get_IPSR()自动分流到对应的队列APIIPSR是 ARM 内核中断状态寄存器，不等于 0 = 当前代码处于中断上下文
 */
BOOL
xMBPortEventPost( eMBEventType eEvent )
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE; /* xHigherPriorityTaskWoken 输出参数：函数自己改它的值，是否有更高优先级的任务被唤醒了 */

    if( __get_IPSR( ) != 0U )										/* IPSR是 ARM 内核中断状态寄存器，不等于 0 = 当前代码处于中断上下文 */
    {
        if( xQueueSendFromISR( xMBEventQueue, &eEvent, &xHigherPriorityTaskWoken ) != pdTRUE )	/* 中断上下文（定时器超时、串口相关回调）*/
        {
            return FALSE;
        }
        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
    else
    {
        if( xQueueSend( xMBEventQueue, &eEvent, 0 ) != pdTRUE )   /* 任务上下文（普通 FreeRTOS 任务里面调用）*/
        {
            return FALSE;
        }
    }
    return TRUE;
}

/* 带超时阻塞接收：有事件立即唤醒，无事件最多阻塞MB_EVENT_GET_TIMEOUT_MS后返回FALSE */
BOOL
xMBPortEventGet( eMBEventType * eEvent )
{
    if( xQueueReceive( xMBEventQueue, eEvent, pdMS_TO_TICKS( MB_EVENT_GET_TIMEOUT_MS ) ) == pdTRUE )
    {
        return TRUE;
    }
    return FALSE;
}
