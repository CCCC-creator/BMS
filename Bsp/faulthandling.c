#include "faulthandling.h"
#include "faultcheck.h"
#include "bq76940.h"
#include "FreeRTOS.h"
#include "app_init.h"
#include "app_task.h"
#include "stdio.h"


#define SYS_STAT_UV													(1U << 3)
#define SYS_STAT_OV													(1U << 2)
#define SYS_STAT_SCD												(1U << 1)
#define SYS_STAT_OCD												(1U << 0)


/* 安全原则自动裁决：只要有一个故障说要关，就关 */
/* 故障处理 过压 欠压 充电过流 放电过流 短路 过温 低温 */
void FaultHandling(uint8_t flags)
{
	uint8_t state = 0;
	static uint8_t old_status;
	if((flags & FLAG_OV) == FLAG_OV || (flags & FLAG_COC) == FLAG_COC || (flags & FLAG_SC) == FLAG_SC ||
		(flags & FLAG_OT) == FLAG_OT || (flags & FLAG_UT) == FLAG_UT)						/* 关闭MOS管 */
		state |= CHG_Bit;
	if((flags & FLAG_UV) == FLAG_UV || (flags & FLAG_DOC) == FLAG_DOC || (flags & FLAG_SC) == FLAG_SC)
		state |= DSG_Bit;	
																																						/* FH_flag = 0 开启MOS管 */
		if(state != old_status)
		{
			if((state & CHG_Bit) == CHG_Bit)	/* SYS_CTRL2里对应的CHG_Bit置0 关闭充电MOS */
				ClearBit(SYS_CTRL2, CHG_Bit);
			else				
				SetBit(SYS_CTRL2, CHG_Bit);
			if((state & DSG_Bit) == DSG_Bit)	/* SYS_CTRL2里对应的DSG_Bit置0 关闭放电MOS */
				ClearBit(SYS_CTRL2, DSG_Bit);
			else
				SetBit(SYS_CTRL2, DSG_Bit);				
			old_status = state;
			printf("MOS执行：CHG=%d DSG=%d\r\n", !(state & CHG_Bit),  !(state & DSG_Bit));  /* 0 表示关闭  1 表示开启 */
		}
}

/* 清除硬件保护标志 */
/* 硬件保护触发时会发生三件事：
物理 FET 被硬件直接关掉（OV→断 CHG；UV/OCD/SCD→断 DSG）；
SYS_STAT 里的标志被锁存为 1，不会自己消失；
SYS_CTRL2 里的 CHG_ON/DSG_ON 位不变  */
void ClearFaultStatus(void)
{
	uint8_t sys_stat;
	BQ_ReadBlock(SYS_STAT, &sys_stat, 1); 
	if((g_bms.fault & FLAG_OV) != FLAG_OV && (sys_stat & SYS_STAT_OV) == SYS_STAT_OV)
		BQ_WriteReg(SYS_STAT, SYS_STAT_OV);
	if((g_bms.fault & FLAG_UV) != FLAG_UV && (sys_stat & SYS_STAT_UV) == SYS_STAT_UV)
		BQ_WriteReg(SYS_STAT, SYS_STAT_UV);	
	if((g_bms.fault & FLAG_COC) != FLAG_COC && (g_bms.fault & FLAG_DOC) != FLAG_DOC && (sys_stat & SYS_STAT_OCD) == SYS_STAT_OCD)
		BQ_WriteReg(SYS_STAT, SYS_STAT_OCD);	
	if((g_bms.fault & FLAG_SC) != FLAG_SC && (sys_stat & SYS_STAT_SCD) == SYS_STAT_SCD)
		BQ_WriteReg(SYS_STAT, SYS_STAT_SCD);
//	printf("sys_stat=0x%02X\r\n", sys_stat);
}

