#include "monitor.h"
#include "app_task.h"
#include "iwdg.h"

/* 看门狗 3 秒超时， LSI 容差 ±30%，标称 3s → 实际 2.0~4.0s */
/* void BQ_Init(void)里面也要喂狗 */
void Monitor(void)
{
		static Heartbeat old;
		if((hb.hb_LED - old.hb_LED != 0) && (hb.hb_sample - old.hb_sample != 0)	/* 差值防止溢出 */
			&& (hb.hb_protect - old.hb_protect != 0))
			HAL_IWDG_Refresh(&hiwdg);
		old = hb;
}
