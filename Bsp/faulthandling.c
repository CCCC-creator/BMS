#include "faulthandling.h"
#include "faultcheck.h"
#include "bq76940.h"
#include "FreeRTOS.h"
#include "app_init.h"
#include "app_task.h"
#include "stdio.h"


#define SYS_STAT_DEVICE											(1U << 5)
#define SYS_STAT_UV													(1U << 3)      
#define SYS_STAT_OV													(1U << 2)				/* 硬件过压的位 写一清除 */
#define SYS_STAT_SCD												(1U << 1)
#define SYS_STAT_OCD												(1U << 0)

static uint8_t sys_stat;
/* 安全原则自动裁决：只要有一个故障说要关，就关 */
/* 故障处理 过压 欠压 充电过流 放电过流 短路 过温 低温 */
void FaultHandling(uint8_t flags)
{
	uint8_t state = 0;
	uint8_t MOS_now = 0;
	uint8_t ctrl2 = 0;
	if((flags & FLAG_OV) == FLAG_OV || (flags & FLAG_COC) == FLAG_COC || (flags & FLAG_SC) == FLAG_SC ||
		(flags & FLAG_OT) == FLAG_OT || (flags & FLAG_UT) == FLAG_UT)																			/* 关闭MOS管 */
		state |= CHG_Bit;
	if((flags & FLAG_UV) == FLAG_UV || (flags & FLAG_DOC) == FLAG_DOC || (flags & FLAG_SC) == FLAG_SC)		/* 关闭MOS管 */
		state |= DSG_Bit;	
/* 硬件触发故障关闭mos，进而SYS_CTRL2的CHG DSG也被关闭了，但软件检测是需要时间的，刚好没检测到，就代表没有故障，state也就没有变化了
实际上因为硬件触发，SYS_CTRL2的CHG DSG也被关闭，软件却不知道，以为没有变化，因为SYS_CTRL2 里对应的位会被芯片清 0，且芯片不会自己置回，
必须主机重写，即使故障恢复了，也不会恢复MOS，所以要对比是当前的SYS_CTRL2状态才能避免。而且额外解决了I2C写失败，当软件以为写进去了的问题；	*/																																						
		if(BQ_ReadBlock(SYS_STAT, &sys_stat, 1) != HAL_OK)
			return;
		if(BQ_ReadBlock(SYS_CTRL2, &ctrl2, 1) != HAL_OK)
			return;
		MOS_now = (~ctrl2) & 0x03;			/* state是置1是关闭，而MOS_now是0为关闭，需要翻转 */
		if(sys_stat & SYS_STAT_OV)			/* 硬件保护还没解除，就别逆着它开，清除硬件保护标志在void ClearFaultStatus(void)，后面才进行清除 */
			MOS_now &= ~0x01;
		if(sys_stat & SYS_STAT_UV || sys_stat & SYS_STAT_SCD || sys_stat & SYS_STAT_OCD)
			MOS_now &= ~0x02;
		if(state != MOS_now)
		{
			if((state & CHG_Bit) == CHG_Bit)	/* SYS_CTRL2里对应的CHG_Bit置0 关闭充电MOS */
				ClearBit(SYS_CTRL2, CHG_Bit);
			else				
				SetBit(SYS_CTRL2, CHG_Bit);
			if((state & DSG_Bit) == DSG_Bit)	/* SYS_CTRL2里对应的DSG_Bit置0 关闭放电MOS */
				ClearBit(SYS_CTRL2, DSG_Bit);
			else
				SetBit(SYS_CTRL2, DSG_Bit);				
		}
}

/* 清除硬件保护标志 */
/* 硬件保护触发时会发生三件事：
物理 FET 被硬件直接关掉（OV→断 CHG；UV/OCD/SCD→断 DSG）；
SYS_STAT 里的标志被锁存为 1，不会自己消失；
SYS_CTRL2 里对应的位会被芯片清 0，且芯片不会自己置回，必须主机重写  */
/* ALERT引脚R34 470K 下拉到 GND、未接 MCU。所以OVRD_ALERT不用管 */
void ClearFaultStatus(void)
{
	static uint8_t n = 0;
	if((sys_stat & SYS_STAT_DEVICE) == SYS_STAT_DEVICE)
	{
		if(++n >= 8)															/* 连续8拍清除 2s */
		{
			BQ_WriteReg(SYS_STAT, SYS_STAT_DEVICE);
			n = 0;
		}
	}
	
	if((g_bms.fault & FLAG_OV) != FLAG_OV && (sys_stat & SYS_STAT_OV) == SYS_STAT_OV)
		BQ_WriteReg(SYS_STAT, SYS_STAT_OV);
	if((g_bms.fault & FLAG_UV) != FLAG_UV && (sys_stat & SYS_STAT_UV) == SYS_STAT_UV)
		BQ_WriteReg(SYS_STAT, SYS_STAT_UV);	
	if((g_bms.fault & FLAG_COC) != FLAG_COC && (g_bms.fault & FLAG_DOC) != FLAG_DOC && (sys_stat & SYS_STAT_OCD) == SYS_STAT_OCD)
		BQ_WriteReg(SYS_STAT, SYS_STAT_OCD);	
	if((g_bms.fault & FLAG_SC) != FLAG_SC && (sys_stat & SYS_STAT_SCD) == SYS_STAT_SCD)
		BQ_WriteReg(SYS_STAT, SYS_STAT_SCD);
	printf("sys_stat=0x%02X\r\n", sys_stat);
}

