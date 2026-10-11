#include "faulthandling.h"
#include "faultcheck.h"
#include "bq76940.h"


#define SYS_CTRL2													0x05
#define DSG_ON													(1U << 1)						/* 充电MOS开启 */		
#define CHG_ON													(1U << 0)						/* 放电MOS开启 */

///* 故障标志 */
//#define FLAG_OV											(1U << 0)							/* 过压 */
//#define FLAG_UV											(1U << 1)							/* 欠压 */
//#define FLAG_COC										(1U << 2)							/* 充电过流 */
//#define FLAG_DOC										(1U << 3)							/* 放电过流 */
//#define FLAG_SC											(1U << 4)							/* 短路 */
//#define FLAG_OT											(1U << 5)							/* 过温 */
//#define FLAG_UT											(1U << 6)							/* 低温 */


/* 充电 MOS = 要断充电 ? 断 : 通放电 MOS = 要断放电 ? 断 : 通 
多个故障同时发生怎么办 安全原则自动裁决：只要有一个故障说要关，就关*/
/* 故障处理 过压 欠压 充电过流 放电过流 短路 过温 低温 */
void FaultHandling(uint8_t flags)
{
	
	
}

//void FaultHandling(uint8_t flags)
//{
//		static uint8_t prev_OV = 0;
//		static uint8_t flag_OV1 = 0;
//		static uint8_t flag_OV2 = 0;
//		static uint8_t cnt_OV = 0;
//		if(!SetBit(SYS_CTRL2, DSG_ON) && SetBit(SYS_CTRL2, CHG_ON))	/* !SetBit(SYS_CTRL2, DSG_ON) && SetBit(SYS_CTRL2, CHG_ON) 表示该过压故障是否已处理 */ 
//		{
//			if(((flags & FLAG_OV) == FLAG_OV) && !prev_OV)	/* (flags & FLAG_OV) == FLAG_OV 判断这次是不是过压，!(prev & FLAG_OV) 判断上次有是不是没过压 */
//			{
//				cnt_OV++;
//				prev_OV = 1;																								/* prev = 1 表示这一次过压且上一次未过压*/
//			}
//			if(((flags & FLAG_OV) == FLAG_OV) && prev_OV)								/* (flags & FLAG_OV) == FLAG_OV 判断这次是不是过压，prev 判断上次有是不是没过压 */
//				cnt_OV++;
//			if(cnt_OV >= CONFIRM_N_VLT)																/* 250ms * 3 = 0.75s 过滤噪声 */
//			{
//				flag_OV1 = 1;																						/* flag_OV1 = 1 	DSG_ON */
//				flag_OV2 = 0;																						/* flag_OV2 = 0 	CHG_OFF */
//				cnt_OV = 0;																							/* 因为已经执行了故障处理 所以cnt_OV = 0 prev_OV = 0 不用计数和记录上一次的情况*/
//				prev_OV = 0;
//			}
//		}

//		
//		
//		if(SetBit(SYS_CTRL2, DSG_ON))
//		{
//			if(!flag_OV1 |            )																/*  有一个需要关闭DSG就关闭 */
//				ClearBit(SYS_CTRL2, DSG_ON);
//		}
//		if(SetBit(SYS_CTRL2, CHG_ON))
//		{
//			if(!flag_OV2 |            )																/*  有一个需要关闭DSG就关闭 */
//				ClearBit(SYS_CTRL2, CHG_ON);
//		}
//		
//		
//		/* flag_OV1 flag_OV2 恢复后都要清零*/
//		
//		
///*--------------------------------------------------------------------------------------------------------------*/
//		
//		static uint8_t cnt_UV = 0;
//		if((flags & FLAG_UV) == FLAG_UV)													/* 过压处理 */
//		{
//			cnt_UV++;
//			if(cnt_UV >= CONFIRM_N_VLT)															/* 250ms * 3 = 0.75s 过滤噪声 */
//			{
//				SetBit(SYS_CTRL2, DSG_ON);
//				ClearBit(SYS_CTRL2, CHG_ON);
//				cnt_UV = 0;
//			}
//		}

///*--------------------------------------------------------------------------------------------------------------*/
//		
//		static uint8_t cnt_COC = 0;
//		if((flags & FLAG_COC) == FLAG_COC)												/* 充电过流处理 */
//		{
//			cnt_COC++;
//			if(cnt_COC >= CONFIRM_N_CURR)														/* 250ms * 4 = 1s 过滤噪声 */
//			{
//				ClearBit(SYS_CTRL2, DSG_ON);
//				SetBit(SYS_CTRL2, CHG_ON);
//				cnt_COC = 0;
//			}
//		}
//		
///*--------------------------------------------------------------------------------------------------------------*/
//		
//		static uint8_t cnt_DOC = 0;
//		if((flags & FLAG_DOC) == FLAG_DOC)												/* 放电过流处理 */
//		{
//			cnt_DOC++;
//			if(cnt_DOC >= CONFIRM_N_CURR)														/* 250ms * 4 = 1s 过滤噪声 */
//			{
//				SetBit(SYS_CTRL2, DSG_ON);
//				ClearBit(SYS_CTRL2, CHG_ON);
//				cnt_DOC = 0;
//			}
//		}
//		
///*--------------------------------------------------------------------------------------------------------------*/
//		
//		static uint8_t cnt_SC = 0;
//		if((flags & FLAG_SC) == FLAG_SC)													/* 放电过流处理 */
//		{
//			cnt_SC++;
//			if(cnt_SC >= CONFIRM_N_SHORT)														/* 250ms * 1 = 0.25s 过滤噪声 */
//			{
//				ClearBit(SYS_CTRL2, DSG_ON);
//				ClearBit(SYS_CTRL2, CHG_ON);
//				cnt_SC = 0;
//			}
//		}		
//		
///*--------------------------------------------------------------------------------------------------------------*/
//		
//		static uint8_t cnt_OT = 0;
//		if((flags & FLAG_SC) == FLAG_OT)													/* 过温处理 */
//		{
//			cnt_OT++;
//			if(cnt_OT >= CONFIRM_N_TEMP)														/* 250ms * 4 = 1s 过滤噪声 */
//			{
//				ClearBit(SYS_CTRL2, DSG_ON);
//				SetBit(SYS_CTRL2, CHG_ON);
//				cnt_OT = 0;
//			}
//		}		
//		
///*--------------------------------------------------------------------------------------------------------------*/
//		
//		static uint8_t cnt_UT = 0;
//		if((flags & FLAG_SC) == FLAG_UT)													/* 低温处理 */
//		{
//			cnt_UT++;
//			if(cnt_UT >= CONFIRM_N_TEMP)														/* 250ms * 4 = 1s 过滤噪声 */
//			{
//				ClearBit(SYS_CTRL2, DSG_ON);
//				SetBit(SYS_CTRL2, CHG_ON);
//				cnt_UT = 0;
//			}
//		}		
//}
