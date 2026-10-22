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
#define MB_EVENT_QUEUE_LEN      4

/* ----------------------- Variables ----------------------------------------*/
static QueueHandle_t xMBEventQueue = NULL;

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
 * 通过__get_IPSR()自动分流到对应的队列API
 */
BOOL
xMBPortEventPost( eMBEventType eEvent )
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if( __get_IPSR( ) != 0U )
    {
        if( xQueueSendFromISR( xMBEventQueue, &eEvent, &xHigherPriorityTaskWoken ) != pdTRUE )
        {
            return FALSE;
        }
        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
    else
    {
        if( xQueueSend( xMBEventQueue, &eEvent, 0 ) != pdTRUE )
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
