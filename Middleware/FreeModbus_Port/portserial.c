#include "port.h"
#include "mb.h"
#include "mbport.h"
#include "stm32f1xx_hal.h"
#include "usart.h"

/* ----------------------- 静态函数声明，保留static，不用对外暴露 -----------------------*/
static void prvvUARTTxReadyISR( void );
static void prvvUARTRxISR( void );

/* ----------------------- 串口使能函数：协议栈调用，打开/关闭RX、TX中断 -----------------------*/
void vMBPortSerialEnable( BOOL xRxEnable, BOOL xTxEnable )
{
    // 接收中断 RXNE：收到字节触发
    if(xRxEnable)
    {
        __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
    }
    else
    {
        __HAL_UART_DISABLE_IT(&huart1, UART_IT_RXNE);
    }

    // 发送空中断 TXE：发送寄存器为空，可以继续发下一字节
    if(xTxEnable)
    {
        __HAL_UART_ENABLE_IT(&huart1, UART_IT_TXE);
    }
    else
    {
        __HAL_UART_DISABLE_IT(&huart1, UART_IT_TXE);
    }
}

/* ----------------------- 串口初始化函数 -----------------------*/
BOOL
xMBPortSerialInit( UCHAR ucPORT, ULONG ulBaudRate, UCHAR ucDataBits, eMBParity eParity, UCHAR ucStopBits )
{
    /* CubeMX已经完成USART1硬件初始化(9600 8N1)，协议栈调用此函数，直接返回TRUE
       注意：eMBInit里的波特率/校验参数必须和CubeMX配置一致 */
    return TRUE;
}


/* ----------------------- 向串口DR寄存器写入一个字节，协议栈发送时调用 -----------------------*/
BOOL
xMBPortSerialPutByte( CHAR ucByte )
{
    huart1.Instance->DR = ucByte;
    return TRUE;
}

/* ----------------------- 从串口DR寄存器读取收到的字节，协议栈接收时调用 -----------------------*/
BOOL
xMBPortSerialGetByte( CHAR * pucByte )
{
    *pucByte = (huart1.Instance->DR) & 0xFF;
    return TRUE;
}

/* ----------------------- 串口接收回调：收到1字节，通知FreeModbus协议栈 -----------------------*/
static void prvvUARTRxISR( void )
{
    ( void )pxMBFrameCBByteReceived();
}

/* ----------------------- 发送寄存器空回调：可以继续发送下一字节 -----------------------*/
static void prvvUARTTxReadyISR( void )
{
    ( void )pxMBFrameCBTransmitterEmpty();
}

/* ----------------------- USART1中断服务函数，写在本文件，同文件可以直接调用static函数 -----------------------*/
void USART1_IRQHandler(void)
{
    // 判断：RXNE接收中断触发
    if( (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE) != RESET) && (__HAL_UART_GET_IT_SOURCE(&huart1, UART_IT_RXNE) != RESET) )
    {
        prvvUARTRxISR();
    }

    // 判断：TXE发送空中断触发
    if( (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TXE) != RESET) && (__HAL_UART_GET_IT_SOURCE(&huart1, UART_IT_TXE) != RESET) )
    {
        prvvUARTTxReadyISR();
    }

    /* HAL库处理错误标志(溢出ORE、奇偶错误等)，必须放在最后！会清除中断标志 */
    HAL_UART_IRQHandler(&huart1);
}
