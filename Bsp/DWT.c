#include "main.h"
#include "DWT.h"



void DWT_Init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  /* 打开调试跟踪模块 */
  DWT->CYCCNT = 0;
  DWT->CTRL   |= DWT_CTRL_CYCCNTENA_Msk;           /* 启动周期计数器 */
}
