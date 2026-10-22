#include "port.h"
#include "mb.h"
#include "mbport.h"
#include "stm32f1xx_hal.h"
#include "tim.h"

/* ----------------------- 静态函数声明 -----------------------*/
static void prvvTIMERExpiredISR( void );

/* ----------------------- 全局变量：软件计数 -----------------------*/
static uint16_t timerCnt = 0;
static uint16_t timerTarget = 0;

/* ----------------------- 定时器初始化函数，协议栈调用 -----------------------*/
BOOL
xMBPortTimersInit( USHORT usTim1Timerout50us )
{
    // FreeModbus传入：需要多少个50us，作为3.5字符空闲时间
    timerTarget = usTim1Timerout50us;
    timerCnt = 0;
    return TRUE;
}

/* ----------------------- 定时器使能：协议栈收到字节时打开定时器，重置计数器 -----------------------*/
inline void
vMBPortTimersEnable(  )
{
    timerCnt = 0;
    // 启动TIM4更新中断
    HAL_TIM_Base_Start_IT(&htim4);
}

/* ----------------------- 定时器关闭：协议栈不需要计时的时候关闭 -----------------------*/
inline void
vMBPortTimersDisable(  )
{
    HAL_TIM_Base_Stop_IT(&htim4);
}

/* ----------------------- 定时器超时回调：3.5字符时间到，通知协议栈一帧结束 -----------------------*/
static void prvvTIMERExpiredISR( void )
{
    ( void )pxMBPortCBTimerExpired();
}

/* ----------------------- HAL定时器溢出全局回调函数 -----------------------*/
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if(htim->Instance == TIM4)
    {
        timerCnt++;
        // 累计时间达到3.5字符空闲时间
        if(timerCnt >= timerTarget)
        {
            prvvTIMERExpiredISR();
            timerCnt = 0;
            HAL_TIM_Base_Stop_IT(&htim4);
        }
    }
}

/* ----------------------- TIM4中断服务函数，写在当前文件 -----------------------*/
void TIM4_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim4);
}
