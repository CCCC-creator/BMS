#include "protect.h"
#include "bq76940.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stdio.h"
#include "app_task.h"


#define FLAG_OV											(1 << 0)							/* 过压 */
#define FLAG_UV											(1 << 1)							/* 欠压 */
#define FLAG_OC											(1 << 2)							/* 过流 */
#define FLAG_SC											(1 << 3)							/* 短路 */
#define FLAG_OT											(1 << 4)							/* 过温 */
#define FLAG_UT											(1 << 5)							/* 低温 */


/* 判断是否过压, 欠压, 过流, 短路, 过温, 低温。 软件层面 */
void Protect(void)
{
		static uint8_t prev = 0;
		uint8_t flag = 0;
		uint8_t i;
		for(i = 0; i <15; i++)																	/* 判断是否过压 */
		{
			if(cell_used[i] && g_bms.Cell_mv[i] >= OV_THRESHOLD)
			{
				flag |= FLAG_OV;
				break;
			}
		}
		if((flag & FLAG_OV) == FLAG_OV && ! (prev & FLAG_OV))		/* (flag & FLAG_OV) == FLAG_OV 判断这次是不是过压，!(prev & FLAG_OV) 判断上次有是不是没过压 */
			printf("过压故障\r\n");
		if((flag & FLAG_OV) != FLAG_OV && (prev & FLAG_OV))			/* (flag & FLAG_OV) != FLAG_OV 判断这次是不是没过压，(prev & FLAG_OV) 判断上次有是不是过压 */
		{
			printf("过压已解除\r\n");
			prev &= ~FLAG_OV;
		}
		if((flag & FLAG_OV) == FLAG_OV)													/* (flag & FLAG_OV) == FLAG_OV 判断这次是不是过压 */
			prev |= FLAG_OV;

/*--------------------------------------------------------------------------------------------------------------*/
		
		flag = 0;
			for(i = 0; i <15; i++)																/* 判断是否欠压 */
		{
			if(cell_used[i] && g_bms.Cell_mv[i] <= UV_THRESHOLD)
			{
				flag |= FLAG_UV;
				break;
			}
		}
		if((flag & FLAG_UV) == FLAG_UV && ! (prev & FLAG_UV))		/* (flag & FLAG_UV) == FLAG_UV 判断这次是不是欠压，!(prev & FLAG_UV) 判断上次有是不是没欠压 */
			printf("欠压故障\r\n");
		if((flag & FLAG_UV) != FLAG_UV && (prev & FLAG_UV))			/* (flag & FLAG_UV) != FLAG_UV 判断这次是不是没欠压，(prev & FLAG_UV) 判断上次有是不是欠压 */
		{
			printf("欠压已解除\r\n");
			prev &= ~FLAG_UV;
		}
		if((flag & FLAG_UV) == FLAG_UV)													/* (flag & FLAG_UV) == FLAG_UV 判断这次是不是欠压 */
			prev |= FLAG_UV;
		
/*--------------------------------------------------------------------------------------------------------------*/
	
		flag = 0;																							/* 判断是否过流 */
		if(g_bms.Cur_ma >= OC_THRESHOLD)
		{
			flag |= FLAG_OC;
		}
		if((flag & FLAG_OC) == FLAG_OC && ! (prev & FLAG_OC))		/* (flag & FLAG_OC) == FLAG_OC 判断这次是不是过流，!(prev & FLAG_OC) 判断上次有是不是没过流 */
			printf("过流故障\r\n");
		if((flag & FLAG_OC) != FLAG_OC && (prev & FLAG_OC))			/* (flag & FLAG_OC) != FLAG_OC 判断这次是不是没过流，(prev & FLAG_OC) 判断上次有是不是过流 */
		{
			printf("过流已解除\r\n");
			prev &= ~FLAG_OC;
		}
		if((flag & FLAG_OC) == FLAG_OC)													/* (flag & FLAG_OC) == FLAG_OC 判断这次是不是过流 */
			prev |= FLAG_OC;
		
/*--------------------------------------------------------------------------------------------------------------*/
		
		flag = 0;																							/* 判断是否短路 */
		if(g_bms.Cur_ma >= SC_THRESHOLD)
		{
			flag |= FLAG_SC;
		}
		if((flag & FLAG_SC) == FLAG_SC && ! (prev & FLAG_SC))		/* (flag & FLAG_SC) == FLAG_SC 判断这次是不是短路，!(prev & FLAG_SC) 判断上次有是不是没短路 */
			printf("短路故障\r\n");
		if((flag & FLAG_SC) != FLAG_SC && (prev & FLAG_SC))			/* (flag & FLAG_SC) != FLAG_SC 判断这次是不是没短路，(prev & FLAG_SC) 判断上次有是不是短路 */
		{
			printf("短路已解除\r\n");
			prev &= ~FLAG_SC;
		}
		if((flag & FLAG_SC) == FLAG_SC)													/* (flag & FLAG_SC) == FLAG_SC 判断这次是不是短路 */
			prev |= FLAG_SC;
		
/*--------------------------------------------------------------------------------------------------------------*/
		
		flag = 0;																								/* 判断是否过温 */
		if(g_bms.TemX10 >= OT_THRESHOLD_X10)
		{
			flag |= FLAG_OT;
		}
		if((flag & FLAG_OT) == FLAG_OT && ! (prev & FLAG_OT))		/* (flag & FLAG_OT) == FLAG_SC 判断这次是不是过温，!(prev & FLAG_OT) 判断上次有是不是没过温 */
			printf("过温故障\r\n");
		if((flag & FLAG_OT) != FLAG_OT && (prev & FLAG_OT))			/* (flag & FLAG_OT) != FLAG_SC 判断这次是不是没过温，(prev & FLAG_OT) 判断上次有是不是过温 */
		{
			printf("过温已解除\r\n");
			prev &= ~FLAG_OT;
		}
		if((flag & FLAG_OT) == FLAG_OT)													/* (flag & FLAG_OT) == FLAG_SC 判断这次是不是过温 */
			prev |= FLAG_OT;
		
/*--------------------------------------------------------------------------------------------------------------*/
		
		flag = 0;																								/* 判断是否低温 */
		if(g_bms.TemX10 <= UT_THRESHOLD_X10)
		{
			flag |= FLAG_UT;
		}
		if((flag & FLAG_UT) == FLAG_UT && ! (prev & FLAG_UT))		/* (flag & FLAG_UT) == FLAG_UT 判断这次是不是低温，!(prev & FLAG_UT) 判断上次有是不是没低温 */
			printf("低温故障\r\n");
		if((flag & FLAG_UT) != FLAG_UT && (prev & FLAG_UT))			/* (flag & FLAG_UT) != FLAG_UT 判断这次是不是没低温，(prev & FLAG_UT) 判断上次有是不是低温 */
		{
			printf("低温已解除\r\n");
			prev &= ~FLAG_UT;
		}
		if((flag & FLAG_UT) == FLAG_UT)													/* (flag & FLAG_UT) == FLAG_SC 判断这次是不是低温 */
			prev |= FLAG_UT;
}

