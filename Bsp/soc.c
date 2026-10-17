#include "soc.h"
#include "FreeRTOS.h"
#include "stdlib.h"
#include "app_task.h"
#include "task.h"
#include "bq76940.h"
#include "stdio.h"


/* SOC = 0,10%,...,100% */
static const uint16_t OCV_SOC_MV[11] = {3000, 3350, 3500, 3600, 3660, 3720, 3800, 3880, 3960, 4060, 4200};


/* 安时积分法累计的电量变化*/
static float soc_acc = 0.0f;


/* 任务 1 秒跑一次，取当前这一瞬间的电流，当作这 1s 窗口的平均电流 精度低一点，但是不用高频采样，减少CPU占用 */
/* SOC估算 最小单体电压 */
void SOC_Est(void)
{
		static TickType_t tick_start = 0;
		uint16_t min_mv = 0;
		static uint16_t last_mv = 0;
		static uint8_t cnt = 0;
		static float offset = 0.0f;
		static float cur_cet = 0.0f;
		TickType_t now = xTaskGetTickCount();
		uint32_t dt_ms = (now - last_time);																							/* 一个Tick就是1ms FreeRTOS任务抖动 计算任务循环周期精确时间 */
		last_time = now;
		if(abs(g_bms.Cur_ma) <= 50)																												/* 是否静置 */
		{	
			offset += ((float)g_bms.Cur_ma - offset) * 0.01f;																/* 算偏置电流 0.01f是滤波系数 */
		}
		cur_cet = (float)g_bms.Cur_ma - offset;
/* 3600s * 1000ms = 3600000ms，1200maAh总容量，100.0f把小数比例转换成百分比 */
		soc_acc += cur_cet * (float)dt_ms / 3600000.0f / 1200.0f * 100.0f;	/* soc_acc是用OCV测出SOC后的电池容量累计变化 放电是负数 soc_pct算出来也是负数 */
		(soc_acc >= 0) ? (g_bms.Ah_socX10 = g_bms.socX10 + (int16_t)(soc_acc *10.0f + 0.5f))
		: (g_bms.Ah_socX10 = g_bms.socX10 + (int16_t)(soc_acc *10.0f - 0.5f));							/* OCV开路法得出的初始电压与soc_acc得出新的SOC，g_bms.socX10是经过OCV得出，不进行下一次OCV是不会变的 */
/* 打印安时积分法SOC */			
		if(g_bms.Ah_socX10 < 0)
		{
			g_bms.Ah_socX10 = 0;
			printf("SOC：0.0%%\r\n");
		}
		else if(g_bms.Ah_socX10 > 1000)
		{
			g_bms.Ah_socX10 = 1000;
			printf("SOC：100.0%%\r\n");
		}
		else
		{
			printf("SOC：%d.%d%%\r\n", g_bms.Ah_socX10 / 10, g_bms.Ah_socX10 % 10);
		}
		
/* OCV开路法 */	
		if(abs(g_bms.Cur_ma) <= 50)																												/* 是否静置 */
		{	
			if(tick_start == 0)
				tick_start = xTaskGetTickCount();
			else if((xTaskGetTickCount() - tick_start) >= pdMS_TO_TICKS(5 * 60 * 1000))   	/* OCV开路法，静置5分钟 */
			{
				for(uint8_t i = 0; i<15; i++)																									/* 遍历 找第一节使用的电池电压赋值给min_mv */
				{
					if(cell_used[i] == 1)
					{
						min_mv = g_bms.Cell_mv[i];
						break;
					}
				}
				for(uint8_t i = 0; i<15; i++)																									/* 遍历 找最小单体电压 */
				{
					if(cell_used[i] == 1)
					{
						if(g_bms.Cell_mv[i] < min_mv)
							min_mv = g_bms.Cell_mv[i];
					}
				}
/* 拿到min_mv之后，进入一致性门判断 */			
				if(last_mv != 0 && (min_mv - last_mv > 30 || min_mv - last_mv < -30))
				{
						if(++cnt >= 2)																											/* 连续2次都是突变才更新SOC */
						{
							for(uint8_t j = 0; j < 11; j++)																								
							{
								if(min_mv == OCV_SOC_MV[j])																									/* 电压恰好等于对应SOC */
									g_bms.socX10 = 100*j;
								else if(j < 10 && OCV_SOC_MV[j] < min_mv && min_mv < OCV_SOC_MV[j +1])			/* 电压在两个SOC之间 用线性插值法 */
									g_bms.socX10 = (uint16_t)(10 * (10*j + (10 * (j+1) - 10 * j) * 
								(float)(min_mv - OCV_SOC_MV[j]) / (float)(OCV_SOC_MV[j + 1] - OCV_SOC_MV[j])));
							}
							soc_acc = 0.0f;																																/* OCV得出新的SOC，清零soc_acc重新累加 */
							last_mv = min_mv;																															/* 锚点：把当前这个跳变很大的电压存起来，作为下一轮的对比基准，持续观察这个新电压能不能稳住，判断是不是真的是干扰 */
							cnt = 0;
							printf("OCV测得SOC：%d (Vmin=%dmV)\r\n", g_bms.socX10 / 10, min_mv);
						}
				}
				else
				{	
					for(uint8_t j = 0; j < 11; j++)																								
					{
						if(min_mv == OCV_SOC_MV[j])																									/* 电压恰好等于对应SOC */
							g_bms.socX10 = 100*j;
						else if(j < 10 && OCV_SOC_MV[j] < min_mv && min_mv < OCV_SOC_MV[j +1])			/* 电压在两个SOC之间 用线性插值法 */
							g_bms.socX10 = (uint16_t)(10 * (10*j + (10 * (j+1) - 10 * j) * 
						(float)(min_mv - OCV_SOC_MV[j]) / (float)(OCV_SOC_MV[j + 1] - OCV_SOC_MV[j])));
					}
					soc_acc = 0.0f;																																/* OCV得出新的SOC，清零soc_acc重新累加 */
					last_mv = min_mv;
					cnt = 0;
					printf("初始化SOC：%d.%d\r\n", g_bms.socX10 / 10, g_bms.socX10 % 10);
				}
				tick_start = xTaskGetTickCount();																							/* 开启新一轮OCV */	
				}
			}
		else
			tick_start = 0;
}



/* 上电初始化SOC 断电期间电池本来就是静置的，直接OCV锚定一次SOC */
void Init_SOC(void)
{
		uint16_t min_mv = 0;
		for(uint8_t i = 0; i<15; i++)																									/* 遍历 找第一节使用的电池电压赋值给min_mv */
		{
			if(cell_used[i] == 1)
			{
				min_mv = g_bms.Cell_mv[i];
				break;
			}
		}
		for(uint8_t i = 0; i<15; i++)																									/* 遍历 找最小单体电压 */
		{
			if(cell_used[i] == 1)
			{
				if(g_bms.Cell_mv[i] < min_mv)
					min_mv = g_bms.Cell_mv[i];
			}
		}
		for(uint8_t j = 0; j < 11; j++)																								
		{
			if(min_mv == OCV_SOC_MV[j])																									/* 电压恰好等于对应SOC */
				g_bms.socX10 = 100*j;
			else if(j < 10 && OCV_SOC_MV[j] < min_mv && min_mv < OCV_SOC_MV[j +1])								/* 电压在两个SOC之间 用线性插值法 */
				g_bms.socX10 = (uint16_t)(10 * (10*j + (10 * (j+1) - 10 * j) * 
			(float)(min_mv - OCV_SOC_MV[j]) / (float)(OCV_SOC_MV[j + 1] - OCV_SOC_MV[j])));
		}
		soc_acc = 0.0f;																																	/* OCV得出新的SOC，清零soc_acc重新累加 */
		printf("初始化SOC：%d.%d\r\n", g_bms.socX10 / 10, g_bms.socX10 % 10);		
}	
