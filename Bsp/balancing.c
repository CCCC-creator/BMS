#include "balancing.h"
#include "app_task.h"
#include "stdlib.h"
#include "bq76940.h"
#include "stdio.h"
#include "FreeRTOS.h"
#include "task.h"


/* 临时测试：0=正常策略；1~15=只均衡指定的一节（其他全关）。
   用来确认 Cell 的均衡电流是否穿进 CC；测完改回 0。 */
#define BALANCE_FORCE_CELL      0

#if (BALANCE_FORCE_CELL < 0) || (BALANCE_FORCE_CELL > 15)
#error "BALANCE_FORCE_CELL must be 0..15"
#endif

/* 1=当前有均衡在跑（供 FaultCheck 跳过不可信的软件 SC） */
volatile uint8_t g_balancing_active = 0 ;

static bool find_min_max(int* min_mv, int* max_mv);

/* 找到最高最低  看是否需要均衡  将后选均衡电池给分  把最高分的先挑选均衡
选到的就去掉  如果 挑出来的相邻已经有更高分的电池在均衡  该电池就不均衡
再循环 再次把最高分的挑选均衡 再去掉...一直到没有后选均衡电池 */
/* 被动均衡只放高不放低，以最低节为基准，30mV 启动、10mV 停止做滞回；
同组相邻不能同时均衡，所以每轮按电压从高到低贪心选一个互不相邻的集合，
并给在任节加 5mV 优先权防抖；CELLBAL 会在 DEVICE_XREADY 时自动清零，
所以目标不变也每 10s 重写一次；所有前置条件只改 ok 标志，统一从一个出
口写 0 并打印，保证"算法状态"和"芯片状态"始终一致。*/
void BQ_Balancing(void)
{
		int min_mv = 0, max_mv = 0;
		uint16_t mask = 0;
		uint8_t changed = 0;
		TickType_t now = 0;
		bool ok = true;
		static uint8_t Balancing[15];																				/* 当前每节状态，也是上一轮挑选均衡后的结果 0：未开启，1：开启 */
		uint8_t candidate[15] = { 0 };																			/* 本次需要均衡的候选电池 */
		uint8_t selected[15] = { 0 };																				/* 本次选中要启动均衡的电池 */
		static TickType_t last_write_tick;
		/* 均衡期间 CC 会被均衡电流污染，读数可能是几十安培的假值。
		   一旦均衡已经在跑，就不能再用这个电流去停均衡：
		   否则会自己把自己关掉，然后假电流继续触发 SC/DOC/COC。 */
		if(g_bms.ready != 1 || g_bms.fault != 0x00 ||
		   (g_balancing_active == 0 && abs(g_bms.Cur_ma) > 50))									/* 不在均衡，但不处于静置状态 */
		{
			ok = false;
		}
		if(ok)
		{
			if(find_min_max(&min_mv, &max_mv) !=true)
			{
				ok = false;
			}
			else if(min_mv <= 3300 || max_mv >= 4200)																	/* 电压3300到4200做均衡 */
			{
				ok = false;
			}
		}
/*--------------------------------------------------------------------------------------------------------------*/
		if(ok)
		{
/* 标记需要均衡的 */		
			for(uint8_t i = 0; i < 15; i++)
			{
				if(!cell_used[i])
					continue;
/* 滞回 */			
				if(Balancing[i])
					candidate[i] = ((g_bms.Cell_mv[i] - min_mv) > 10);								/* 如果当前节在均衡，需要和最小值的高10mv才继续均衡 */
				else
					candidate[i] = ((g_bms.Cell_mv[i] - min_mv) > 30);
			}
/*--------------------------------------------------------------------------------------------------------------*/
/* 挑选，并判断同组里的候选是否相邻并做相应处理 */		
			for(uint8_t n = 0; n < 15; n++)
			{
				int best = -1;																									/* 第几节电池 */
				int best_score = -1;																						/* 得分 */
/* 找剩下的候选里分数最高的 */
				for(uint8_t i = 0; i < 15; i++)
				{
					if(candidate[i] == 0)																					/* 不用均衡的直接下一轮 */
						continue;
					int score = g_bms.Cell_mv[i] + (Balancing[i] ? 5 : 0);				/* 候选的如果是正在均衡的，给多+5更容易继续被选中，不频繁切来切去 */
					if(score > best_score)
					{
						best_score = score;
						best = i;
					}
				}
				if(best < 0)
					break;																														/* 候选已经循环挑选完了 */
				candidate[best] = 0;																								/* 挑过了就从候选去掉 */
/* 判断这一轮循环选中的节是否有相邻已经开启的均衡电池 */			
				uint8_t pos = best % 5;																							/* 这一节电池在本组的第几位 */
				if(pos > 0 && selected[best - 1])																					
					continue;																													/* 左边有正在均衡的电池 */
				if(pos < 4 && selected[best + 1])
					continue;																													/* 右边有正在均衡的电池 */
				selected[best] = 1;																									/* 左右两边都没有正在均衡的电池，选中 */
			}
		}
/*--------------------------------------------------------------------------------------------------------------*/
/* 测试模式：只均衡 BALANCE_FORCE_CELL 指定的那一节，其他全关。
   BALANCE_FORCE_CELL=0 时这里不生效，走上面的正常策略。 */
#if (BALANCE_FORCE_CELL > 0) && (BALANCE_FORCE_CELL < 15)		
		if(ok && BALANCE_FORCE_CELL > 0)
		{
			for(uint8_t i = 0; i < 15; i++)
				selected[i] = 0;
			selected[BALANCE_FORCE_CELL - 1] = 1;
		}
#endif		
/*--------------------------------------------------------------------------------------------------------------*/
/* 拼寄存器 */
		for(uint8_t i = 0; i < 15; i++)
		{
			if(selected[i] == 1)
				mask |= (1U << i);
		}
/*--------------------------------------------------------------------------------------------------------------*/
/* 写芯片，控制开关均衡 */	
		for(uint8_t i = 0; i < 15; i++)
		{
			if(selected[i] != Balancing[i])
				changed = 1;																											/* 这一次挑选要均衡的电池最少有一节状态不在均衡，就是“有变化” */
		}
		now = xTaskGetTickCount();																						
/* “无变化”，且没到 10s 重写时间，什么都不做 */
/* CELLBAL 位在 DEVICE_XREADY 置位或 SHIP→NORMAL 时会被芯片自动清零，
主机必须重写 */
		if(changed == 0 && last_write_tick != 0 && (now - last_write_tick) < pdMS_TO_TICKS(10000))
			return;
/* “有变化”，或到 10s 重写时间 */		
		if(mask != 0)
			g_balancing_active = 1;          /* 先标记，防止写完后到标志更新之间被 SC 误判 */

		if(BQ_SetBalanceMask(mask))
		{
			if(changed)
			{
				if(mask == 0)
					printf("全部均衡关闭\r\n");
				else
				{
					for(uint8_t i = 0; i < 15; i++)
					{
						if(selected[i] == 1)
							printf("Cell%d正在均衡\r\n", i + 1);
					}
				}
			}
			for(uint8_t i = 0; i < 15; i++)
				Balancing[i] = selected[i];																				/* 把这一轮均衡的电池记录给下一次循环任务用 */
			last_write_tick = now;
				if(mask == 0)
					g_balancing_active = 0;      /* 确认关闭后才清标志 */																									}	
		else
		{
			last_write_tick = 0;																								/* 写失败，下一轮均衡任务重新写 */
		}
}


/* 寻找最大和最小电池电压 返回false就是没有找到 */
static bool find_min_max(int* min_mv, int* max_mv)
{
		bool found = false;
		*min_mv = 10000;
		*max_mv = -1;
		for(uint8_t i = 0; i < 15; i++)
		{
			if(cell_used[i])
			{
				found = true;
				if(g_bms.Cell_mv[i] > *max_mv)
					*max_mv = g_bms.Cell_mv[i];
				if(g_bms.Cell_mv[i] < *min_mv)
					*min_mv = g_bms.Cell_mv[i];
			}
		}
		return found;
}


