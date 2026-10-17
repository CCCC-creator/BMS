#include "monitor.h"
#include "app_task.h"
#include "iwdg.h"
#include "stdio.h"



/* 看门狗 8 秒超时， LSI 容差 ±30%，标称 8s → 实际 5.6~10.4s */
/* void BQ_Init(void)里面也要喂狗 */
void Monitor(void)
{
		static Heartbeat old;
		uint32_t d_led, d_smp, d_prt, d_soc;

		d_led = hb.hb_LED     - old.hb_LED;					
		d_smp = hb.hb_sample  - old.hb_sample;
		d_prt = hb.hb_protect - old.hb_protect;
		d_soc = hb.hb_soc - old.hb_soc;
	
		if(d_led != 0 && d_smp != 0 && d_prt != 0 && d_soc != 0)	/* 根据任务周期决定喂狗时间差 */
		{
				HAL_IWDG_Refresh(&hiwdg);						
		}
		old = hb;															
}
