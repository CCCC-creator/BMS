#include "faultcheck.h"
#include "bq76940.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stdio.h"
#include "app_task.h"
#include "stdlib.h"



/* 软件保护 */
#define OV_THRESHOLD								4250    	/* 阈值，单位：mV */
#define UV_THRESHOLD								2750			/* 阈值，单位：mV */
#define OC_THRESHOLD								9000			/* 阈值，单位：mA */
#define	SC_THRESHOLD								18000			/* 阈值，单位：mA */
#define OT_THRESHOLD_X10						600				/* 阈值，60° */
#define UT_THRESHOLD_X10						0					/* 阈值，0° */

/* 过滤噪声 */
#define CONFIRM_N_SHORT										1
#define CONFIRM_N_VLT											3
#define CONFIRM_N_CURR										4
#define CONFIRM_N_TEMP										24

/* 判断是否过压, 欠压, 过流，短路, 过温, 低温。 软件层面 */
uint8_t FaultCheck(void)
{
		static uint8_t prev = 0;																/* 上一轮确认后的状态 */
		uint8_t out = 0;																				/* 本轮确认后的状态（返回给 FaultHandling） */
		uint8_t flag = 0;
		uint8_t i;
		static uint8_t cnt_OV = 0;
		static uint8_t cnt_UV = 0;
		static uint8_t cnt_COC = 0;
		static uint8_t cnt_DOC = 0;
		static uint8_t cnt_SC = 0;
		static uint8_t cnt_OT = 0;
		static uint8_t cnt_UT = 0;
	
/*--------------------------------------------------------------------------------------------------------------*/
		
		flag = 0;
		for(i = 0; i <15; i++)																	/* 判断是否过压 */
		{
			if(cell_used[i] && g_bms.Cell_mv[i] >= OV_THRESHOLD)
			{
				flag |= FLAG_OV;
				break;
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
		
/*--------------------------------------------------------------------------------------------------------------*/
		
		flag = 0;
		for(i = 0; i <15; i++)																	/* 判断是否欠压 */
		{
			if(cell_used[i] && g_bms.Cell_mv[i] <= UV_THRESHOLD)
			{
				flag |= FLAG_UV;
				break;
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
		
		flag = 0;																
		if(g_bms.Cur_ma >= OC_THRESHOLD)  												/* 判断是否充电过流 */
			flag |= FLAG_COC;
		if(flag)
		{
			if(cnt_COC < CONFIRM_N_CURR)														/* 250ms * 4 = 1s 过滤噪声  成立：计数封顶  */
				cnt_COC++;
		}
		else
			cnt_COC = 0;
		if(cnt_COC >= CONFIRM_N_CURR)
			out |= FLAG_COC;

/*--------------------------------------------------------------------------------------------------------------*/
		
		flag = 0;																
		if(g_bms.Cur_ma <= -OC_THRESHOLD)  												/* 判断是否放电过流 */
			flag |= FLAG_DOC;
		if(flag)
		{
			if(cnt_DOC < CONFIRM_N_CURR)														/* 250ms * 4 = 1s 过滤噪声  成立：计数封顶  */
				cnt_DOC++;
		}
		else
			cnt_DOC = 0;
		if(cnt_DOC >= CONFIRM_N_CURR)
			out |= FLAG_DOC;
		
/*--------------------------------------------------------------------------------------------------------------*/
		
		flag = 0;
		if(abs(g_bms.Cur_ma) >= SC_THRESHOLD)											/* 判断是否短路 abs（）取绝对值 */
				flag |= FLAG_SC;
		if(flag)
		{
			if(cnt_SC < CONFIRM_N_SHORT)														/* 250ms * 1 = 0.25s 过滤噪声  成立：计数封顶  */
				cnt_SC++;
		}
		else
			cnt_SC = 0;
		if(cnt_SC >= CONFIRM_N_SHORT)
			out |= FLAG_SC;
		
/*--------------------------------------------------------------------------------------------------------------*/
		
		flag = 0;
		if(g_bms.TemX10 >= OT_THRESHOLD_X10)											/* 判断是否过温 */
				flag |= FLAG_OT;
		if(flag)
		{
			if(cnt_OT < CONFIRM_N_TEMP)														/* 24 tick × 250ms = 6s（3 份读数）过滤噪声 */
				cnt_OT++;
		}
		else
			cnt_OT = 0;
		if(cnt_OT >= CONFIRM_N_TEMP)
			out |= FLAG_OT;
		
/*--------------------------------------------------------------------------------------------------------------*/
			
		flag = 0;																								/* 判断是否低温 */
		if(g_bms.TemX10 <= UT_THRESHOLD_X10)
				flag |= FLAG_UT;
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
		
		uint8_t edges = out ^ prev;															/* 这轮的状态和上一轮的状态变化的位 = 1 */
		if(edges & FLAG_OV)																			/* 判断变化的位是不是FLAG_OV的位 */
			printf((out & FLAG_OV) ? "过压故障\r\n" : "过压已解除\r\n");
		if(edges & FLAG_UV)																			
			printf((out & FLAG_UV) ? "欠压故障\r\n" : "欠压已解除\r\n");
		if(edges & FLAG_COC)																			
			printf((out & FLAG_COC) ? "充电过流故障\r\n" : "充电过流已解除\r\n");
		if(edges & FLAG_DOC)																			
			printf((out & FLAG_DOC) ? "放电过流故障\r\n" : "放电过流已解除\r\n");
		if(edges & FLAG_SC)																			
			printf((out & FLAG_SC) ? "短路故障\r\n" : "短路已解除\r\n");
		if(edges & FLAG_OT)																			
			printf((out & FLAG_OT) ? "过温故障\r\n" : "过温已解除\r\n");	
		if(edges & FLAG_UT)																			
			printf((out & FLAG_UT) ? "低温故障\r\n" : "低温已解除\r\n");
		
		prev = out;
		return out;
}
