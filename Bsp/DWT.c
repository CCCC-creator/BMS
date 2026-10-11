#include "main.h"
#include "dwt.h"


/* 如果运行中途被中断 / 高优先级任务抢占，DWT 依旧继续计数，会把抢占的时间也算进去 */
void DWT_Init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; 	 	/* 打开调试跟踪模块 */
  DWT->CYCCNT = 0;																		/* DWT->CYCCNT`：CPU 周期计数器 */
  DWT->CTRL   |= DWT_CTRL_CYCCNTENA_Msk;          	 	/* 启动周期计数器 */
}
