#include "faultcheck.h"
#include "bq76940.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stdio.h"
#include "app_task.h"
#include "stdlib.h"
#include "balancing.h"



/* 软件保护 */
#define OV_TRIP_MV									4250    		/* 触发阈值，单位：mV */
#define OV_RECOVER_MV   						4150				/* 恢复阈值，单位：mV */
#define UV_TRIP_MV									3000    		/* 触发阈值，单位：mV */
#define UV_RECOVER_MV   						3100				/* 恢复阈值，单位：mV */
#define OC_THRESHOLD								9000				/* 阈值，单位：mA */
#define	SC_THRESHOLD								18000				/* 阈值，单位：mA */
#define OT_TRIP_X10       					600     	 	/* 60.0°C 触发 */
#define OT_RECOVER_X10   						550     	  /* 55.0°C 恢复 */
#define UT_TRIP_X10       					0      	  	/* 0.0°C 触发 */
#define UT_RECOVER_X10    					30      	  /* 3.0°C 恢复 */

/* 过滤噪声 */
/* 快断慢合——故障越危险，断得越快、合得越慢 */
#define CONFIRM_N_SHORT										1
#define CONFIRM_M_SHORT										20		/* 短路解除时间：250ms * 20 = 5s */
#define CONFIRM_N_VLT											3			  
#define CONFIRM_N_CURR										4
#define CONFIRM_M_CURR      							8			/* 过流解除时间：250ms * 8 = 2s */
#define CONFIRM_N_TEMP										24

/* 软件层面 判断是否过压, 欠压, 过流，短路, 过温, 低温。 滞回 时间窗 */
/* 均衡10s停1s读取电压，期间可以正常检测电流类故障 */
uint8_t FaultCheck(void)
{
		static uint8_t prev = 0;																/* 上一轮确认后的状态 */
		uint8_t out = 0;																				/* 本轮确认后的状态（返回给 FaultHandling） */
		uint8_t flag = 0;
		uint8_t i;
		static uint8_t cnt_OV = 0;
		static uint8_t cnt_UV = 0;
		static uint8_t cnt_COC1 = 0;
		static uint8_t cnt_COC2 = 0;
		static uint8_t cnt_DOC1 = 0;
		static uint8_t cnt_DOC2 = 0;
		static uint8_t cnt_SC1 = 0;
		static uint8_t cnt_SC2 = 0;
		static uint8_t cnt_OT = 0;
		static uint8_t cnt_UT = 0;
		uint8_t edges = 0;															
		
/*--------------------------------------------------------------------------------------------------------------*/
/* 滞回 防止数值在阈值附近来回抖动，造成频繁误触发、反复跳故障 */		
		flag = 0;
		for(i = 0; i <15; i++)																	/* 判断是否过压 */
		{
			if(!cell_used[i])																			/* 判断是否是使用的Cell */
				continue;
			if(prev & FLAG_OV)																		/* 判断上一轮是否过压报警 */
			{
					if(g_bms.Cell_mv[i] >= OV_RECOVER_MV)							/* 已在报警中 → 用恢复线（宽松） */
					{
						flag |= FLAG_OV;
						break;
					}
			}
			else																									
			{
					if(g_bms.Cell_mv[i] >= OV_TRIP_MV)								/* 未报警 → 用触发线（严格） */
					{
						flag |= FLAG_OV;
						break;						
					}
			}
		}
		if(flag)
		{
			if(cnt_OV < CONFIRM_N_VLT)														/* 250ms * 3 = 0.75s 过滤噪声  成立：计数封顶  */
				cnt_OV++;
		}
		else
			cnt_OV = 0;
		if(cnt_OV >= CONFIRM_N_VLT)
			out |= FLAG_OV;
																															/* 因为out原先就是0 所以原先就是解除 */		
/*--------------------------------------------------------------------------------------------------------------*/
		
		flag = 0;
		for(i = 0; i <15; i++)																	/* 判断是否欠压 */
		{
			if(!cell_used[i])																			/* 判断是否是使用的Cell */
				continue;
			if(prev & FLAG_UV)																		/* 判断上一轮是否欠压报警 */
			{
					if(g_bms.Cell_mv[i] <= UV_RECOVER_MV)							/* 已在报警中 → 用恢复线（宽松） */
					{
						flag |= FLAG_UV;
						break;
					}
			}
			else																									
			{
					if(g_bms.Cell_mv[i] <= UV_TRIP_MV)								/* 未报警 → 用触发线（严格） */
					{
						flag |= FLAG_UV;
						break;						
					}
			}
		}
		if(flag)
		{
			if(cnt_UV < CONFIRM_N_VLT)														/* 250ms * 3 = 0.75s 过滤噪声  成立：计数封顶  */
				cnt_UV++;
		}
		else
			cnt_UV = 0;
		if(cnt_UV >= CONFIRM_N_VLT)
			out |= FLAG_UV;
		
/*--------------------------------------------------------------------------------------------------------------*/
/* 电流 COC/DOC：电流方向会翻转、抖动大，用时间窗 
时间窗：不是检测到一次超标就立刻报故障；必须连续超标，维持一段固定时间，才判定真实故障*/
/* 均衡10s停1s读取电压，期间可以正常检测电流类故障 */		
		flag = 0;
		if(g_balancing_active == 0 && g_bms.Cur_ma >= OC_THRESHOLD)  	/* 均衡期间 CC 不可信，跳过 */
			flag |= FLAG_DOC;
		if(flag)
		{
			if(cnt_DOC1 < CONFIRM_N_CURR)														/* 250ms * 4 = 1s 过滤噪声  成立：计数封顶  */
			{
				cnt_DOC1++;
				cnt_DOC2 = 0;
			}
		}
		else
		{
			if(cnt_DOC2 < CONFIRM_M_CURR)														/* 250ms *8 = 2s 过滤噪声  成立：计数封顶  */
			{
				cnt_DOC2++;
				cnt_DOC1 = 0;
			}
		}
		 /* 本行决定本轮 COC 位要不要进 out（进 = 报过流，不进 = 解除）：
       ① cnt_COC1 >= N：过流条件连续成立够 N 份        → 报警
       ② prev 已在报警 且 cnt_COC2 < M：条件虽已消失，
          但"不成立"还没连续数够 M 份                 → 保持报警（解除确认窗口）
       两条都不成立 → 不置位 = 本轮解除  */
		/* cnt_COC1 >= CONFIRM_N_CURR：充电过流超过时间判定为故障 
		(prev & FLAG_COC) 上一次被判断为故障（cnt_COC2 没有超过 CONFIRM_M_CURR  就一直是 out |= FLAG_COC）  
		cnt_COC2 < CONFIRM_M_CURR 是否熬过 2s 解除延时，故障标志继续保持 */
		if(cnt_DOC1 >= CONFIRM_N_CURR || ((prev & FLAG_DOC) && cnt_DOC2 < CONFIRM_M_CURR))															
			out |= FLAG_DOC;
																															/* 因为out原先就是0 所以原先就是解除 */
/*--------------------------------------------------------------------------------------------------------------*/
	
		flag = 0;																
		if(g_balancing_active == 0 && g_bms.Cur_ma <= -OC_THRESHOLD)  	/* 均衡期间 CC 不可信，跳过 */
			flag |= FLAG_COC;
		if(flag)
		{
			if(cnt_COC1 < CONFIRM_N_CURR)														/* 250ms * 4 = 1s 过滤噪声  成立：计数封顶  */
			{
				cnt_COC1++;
				cnt_COC2 = 0;
			}
		}
		else
		{
			if(cnt_COC2 < CONFIRM_M_CURR)														/* 250ms * 8 = 2s 过滤噪声  成立：计数封顶  */
			{
				cnt_COC2++;
				cnt_COC1 = 0;
			}
		}
		if(cnt_COC1 >= CONFIRM_N_CURR || ((prev & FLAG_COC) && cnt_COC2 < CONFIRM_M_CURR))
			out |= FLAG_COC;
		
/*--------------------------------------------------------------------------------------------------------------*/
/* SC 的解除：不适合滞回（电流方向会翻转），用时间窗——abs(Cur_ma) < SC_THRESHOLD 持续 N 次 + 延时才解除，走信箱恢复 MOS	*/
		flag = 0;
		/* Cell1 的均衡电流会穿过 CC 的 100 欧姆滤波电阻，产生假的大电流；
		   均衡期间软件 SC 不可信，硬件 SCD（25A/100us）仍在保护。 */
		if(g_balancing_active == 0 && abs(g_bms.Cur_ma) >= SC_THRESHOLD)	/* 判断是否短路 abs（）取绝对值，均衡期间 CC 不可信，跳过 */
				flag |= FLAG_SC;
		if(flag)
		{
			if(cnt_SC1 < CONFIRM_N_SHORT)														/* 250ms * 1 = 0.25s 过滤噪声  成立：计数封顶  */
			{
				cnt_SC1++;
				cnt_SC2 = 0;
			}
		}
		else
		{
			if(cnt_SC2 < CONFIRM_M_SHORT)														/* 250ms * 20 = 5s 过滤噪声  成立：计数封顶  */
			{
				cnt_SC2++;
				cnt_SC1 = 0;
			}
		}
		if(cnt_SC1 >= CONFIRM_N_SHORT || ((prev & FLAG_SC) && cnt_SC2 < CONFIRM_M_SHORT))
			out |= FLAG_SC;
		
/*--------------------------------------------------------------------------------------------------------------*/
		
		flag = 0;
		if(prev & FLAG_OT)																			/* 判断上一轮是否过温报警 */
			{
					if(g_bms.TemX10 >= OT_RECOVER_X10)								/* 已在报警中 → 用恢复线（宽松） */
					{
						flag |= FLAG_OT;
					}
			}
			else																									
			{
					if(g_bms.TemX10 >= OT_TRIP_X10)										/* 未报警 → 用触发线（严格） */
					{
						flag |= FLAG_OT;					
					}
			}
		if(flag)
		{
			if(cnt_OT < CONFIRM_N_TEMP)														/* 24 tick * 250ms = 6s（3 份读数）过滤噪声 */
				cnt_OT++;
		}
		else
			cnt_OT = 0;
		if(cnt_OT >= CONFIRM_N_TEMP)
			out |= FLAG_OT;
		
/*--------------------------------------------------------------------------------------------------------------*/
			
		flag = 0;																								
		if(prev & FLAG_UT)																					/* 判断上一轮是否低温报警 */
			{
					if(g_bms.TemX10 <= UT_RECOVER_X10)							/* 已在报警中 → 用恢复线（宽松） */
					{
						flag |= FLAG_UT;
					}
			}
			else																									
			{
					if(g_bms.TemX10 <= UT_TRIP_X10)								/* 未报警 → 用触发线（严格） */
					{
						flag |= FLAG_UT;						
					}
			}
		if(flag)
		{
			if(cnt_UT < CONFIRM_N_TEMP)														/* 24 tick × 250ms = 6s（3 份读数）过滤噪声 */
				cnt_UT++;
		}
		else
			cnt_UT = 0;
		if(cnt_UT >= CONFIRM_N_TEMP)
			out |= FLAG_UT;	
		
/*--------------------------------------------------------------------------------------------------------------*/

		edges = out ^ prev; 																		/*这轮的状态和上一轮的状态变化的位 = 1 */		
		if(edges & FLAG_OV)																			/* 判断变化的位是不是FLAG_OV的位 */
			printf((out & FLAG_OV) ? "过压故障\r\n" : "过压已解除\r\n");
		if(edges & FLAG_UV)																			
			printf((out & FLAG_UV) ? "欠压故障\r\n" : "欠压已解除\r\n");
		if(edges & FLAG_DOC)																			
			printf((out & FLAG_DOC) ? "放电过流故障\r\n" : "放电过流已解除\r\n");
		if(edges & FLAG_COC)																			
			printf((out & FLAG_COC) ? "充电过流故障\r\n" : "充电过流已解除\r\n");
		if(edges & FLAG_SC)																			
			printf((out & FLAG_SC) ? "短路故障\r\n" : "短路已解除\r\n");
		if(edges & FLAG_OT)																			
			printf((out & FLAG_OT) ? "过温故障\r\n" : "过温已解除\r\n");	
		if(edges & FLAG_UT)																			
			printf((out & FLAG_UT) ? "低温故障\r\n" : "低温已解除\r\n");
		
		g_bms.fault = out;
		prev = out;
		return out;
}
