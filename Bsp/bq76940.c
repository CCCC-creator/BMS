#include "bq76940.h"
#include "main.h"
#include "stm32f1xx_hal.h"
#include "i2c.h"
#include "stdio.h"
#include "math.h"
#include "app_task.h"
#include "stdbool.h"
#include "iwdg.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "app_init.h"
#include "stdlib.h"


#define BQ_Address								(0x08<<1)
#define REG_ADCGAIN1								0x50
#define BIT_SHUT_B								(1U << 0)
#define BIT_SHUT_A								(1U << 1)
#define BIT_TEMP_SEL							(1U << 3)
#define BIT_ADC_EN								(1U << 4)
#define SYS_STAT										0x00
#define CELLBAL1										0x01
#define CELLBAL2										0x02
#define CELLBAL3										0x03
#define SYS_CTRL1										0x04
#define SYS_CTRL2										0x05
#define PROTECT1 										0x06
#define PROTECT2 										0x07
#define PROTECT3 										0x08
#define OV_TRIP 										0x09
#define UV_TRIP 										0x0A
#define CC_CFG 					 						0x0B
#define ADCOFFSET										0x51
#define ADCGAIN1										0x50
#define ADCGAIN2										0x59
#define VC1_HI											0x0C
#define VC1_LO											0x0D
#define CC_HI												0x32
//#define CC_LO												0x33
#define BAT_HI											0x2A
//#define BAT_LO											0x2B
#define TotalCells										9
#define TS1_HI 											0x2C
//#define	TS1_LO  										0x2D



/* 硬件保护 */
#define OV_THRESHOLD								4300    	/* 阈值，单位：mV */
#define UV_THRESHOLD								2600			/* 阈值，单位：mV */
#define OC_THRESHOLD								11000			/* 阈值，单位：mA */
#define	SC_THRESHOLD								25000			/* 阈值，单位：mA */



static void BQ_Wake(void);
static void BQ_Config(void);
static void BQ_Getoffset(void);
static void BQ_ConfigProtect(void);
//static void BQ_GetCell1(void);
static void BQ_GetAllCellV(void);
static void BQ_GetCurr(void);
//static void BQ_GetTotalV(void);


/* bsp */
static unsigned char CRC8(unsigned char *ptr, unsigned char len, unsigned char key);
//static HAL_StatusTypeDef BQ_ReadReg(uint8_t reg, uint8_t *val);
const uint8_t cell_used[15] = {1, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1};   			/* 使用了的电池 */

static uint16_t GAIN14;														/* 微伏 */
static int8_t Offset;															/* 毫伏 */
volatile uint32_t I2C_Tolat_Err = 0;
volatile uint8_t I2C_Curr_Err = 0;

static const unsigned char BQ_INITReg[12] = {SYS_STAT, CELLBAL1, CELLBAL2, CELLBAL3, SYS_CTRL1, SYS_CTRL2,
																						PROTECT1, PROTECT2, PROTECT3, OV_TRIP, UV_TRIP, CC_CFG};
static const unsigned char BQ_INITdata[12] = {0xFF, 0x00, 0x00, 0x00, 0x18, 0x43,
																							0x00, 0x00, 0x00, 0x00, 0x00, 0x19};

/*--------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------*/

/* BQ初始化
 * 注意：IWDG 在本函数执行之前就已启动（main 里的 MX_IWDG_Init），
 *       而唯一能喂狗的 MonitorTask 要等调度器启动、且首次确认心跳都在推进之后
 *       才喂第一口 → 本函数全程处于"无人喂狗"区间。
 *       所以这里必须分步喂：把最长空档从"整个初始化（最坏 2.3s）"
 *       切成"单步（最长约 0.2s）"，3 秒超时才装得下；否则 I2C 异常时会复位循环。
 * 顺序不可调换：ConfigProtect 的阈值公式要用 Getoffset 算出的 GAIN/Offset。 */
void BQ_Init(void)
{			
		BQ_Wake();
		HAL_IWDG_Refresh(&hiwdg);
		BQ_Config();
		HAL_IWDG_Refresh(&hiwdg);
		BQ_Getoffset();
		HAL_IWDG_Refresh(&hiwdg);
		BQ_ConfigProtect();
		HAL_IWDG_Refresh(&hiwdg);
}


/* 读取电压[9]，电流，温度 */
/*模数转换器的转换函数是一个线性方程，其定义如下：
V（单元） = 增益 x 模数转换器（单元） + 偏移量 
V(cell) = GAIN x ADC(cell) + OFFSET
增益以微伏/位为单位存储，而偏移量则以毫伏为单位存储。*/
void BQ_GetAll(void)
{
		BQ_GetAllCellV();
		BQ_GetCurr();
}


/* 控制充放电，均衡开关 */
void BQ_Control(void)
{
		
		
}


/*--------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------*/

/* BQ休眠模式SHIP。调用后 I2C 失效，唤醒需 PA8 上升沿脉冲 */
void BQ_SHIP(void)
{
		BQ_WriteReg(SYS_CTRL1, BIT_ADC_EN | BIT_TEMP_SEL | BIT_SHUT_B);					/* TEMP_SEL = 1：TS 寄存器读取外部 NTC 热敏电阻 */
		HAL_Delay(20);
		BQ_WriteReg(SYS_CTRL1, BIT_ADC_EN | BIT_TEMP_SEL | BIT_SHUT_A);
}


/*--------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------*/

/* 测量电流 */
/* SYS_STAT (0x00)
CC_READY（第 7 位）：表示新的库仑计数器读数已可用。
请注意，如果在两个相邻的 CC 读数可用之间该位未被清零，则该位会保持为 1。
此位只能由主机清除（而不能设置）。 
0 = 尚未有新的 CC 读数可用或该位已被主机微控制器清除。 
1 = 新的 CC 读数已可用。该位会保持高电平直至被主机清除。*/
/* CC_HI(0x32)andCC_LO(0x33)
CC15:8（位 7 - 0）：库仑计数器的上 8 位最高有效位。
如果在同一事务中（通过地址自动递增的方式）读取高、低两个寄
存器，则始终以原子值的形式返回。
d CC7:0（位 7 - 0）：库仑计数器的下 8 位最低有效位 */
/* CC：16 位有符号数（补码） */
/* CC Reading (in μV) = [16-bit 2’s Complement Value] × (8.44 μV/LSB)
CC读数（单位：μV）= [16位二进制补码值] × (8.44 μV/LSB) 
	250ms 采集一次*/ 
static void BQ_GetCurr(void)
{
		uint8_t v[2] = { 0 };
		int cc = 0;
		int Curr = 0;
		if(BQ_ReadBlock(CC_HI, v, 2) == HAL_OK)								/*  如果读取失败，直接丢弃，保持上一轮的值 */
		{	
			cc = (int16_t)((uint16_t) (v[0] << 8 ) | v[1] );
/* I(μA) = cc × k，k 为实测标定值
   理论值：8.44μV/LSB ÷ 4mΩ = 2.11mA/计数 = 2110μA  ← 与实物不符（实测偏大 1.67×）
   实测标定(2026-10-13)：负载 470Ω(万用表实测 460Ω)、V_load=34.9V → I_真=75.8mA
                        对应 cc=59 → k = 75.8mA/59 = 1.259mA/计数 = 1285μA */
			Curr = (cc < 0) ? (cc * 1259 - 500) / 1000 : (cc * 1259 + 500) / 1000;				/* (cc×8.44μV)÷4mΩ×1000 = cc×2110μA。(cc×8.44μV)÷4mΩ 单位：mA。代码要用整数运算，×1000把最小单位取成μA。整数四舍五入 1000/2 = 500， 解决负数四舍五入问题 */
			g_bms.Cur_ma = Curr;		
		}			
}

/* 测量温度 */
/*VTSX = (ADC in Decimal) x 382 μV/LSB
RTS = (10,000 × VTSX) ÷ (3.3– VTSX) 
	1s 采集一次*/
void BQ_GetTem(void)
{
		float VTSX = 0;
		float RTS = 0;
		float Tem = 0;
		uint8_t v[2] = { 0 };
		if(BQ_ReadBlock(TS1_HI, v, 2) == HAL_OK)								/*  如果读取失败，直接丢弃，保持上一轮的值 */
		{		
			VTSX = ((uint16_t)((uint16_t)(v[0] & 0x3F)) << 8 | v[1] ) * 0.382f;			/* 382是uV */
			RTS = (10000.0f * VTSX) / (3300.f - VTSX);															/* 3.3是V */
			Tem = 1 / (1 / (273.15f + 25.0f)+(log(RTS / 10000.0f)) / 3380.0f)- 273.15f;				/*	NTC 热敏电阻 B 值公式 */
			g_bms.TemX10 = (int) (Tem * 10 + (((Tem >= 0) ? 0.5f : -0.5f)));
		}
}

/* 读所有电池电压及总电压
	250ms 采集一次*/
static void BQ_GetAllCellV(void)
{
		uint16_t adc14 = 0;
		uint8_t v[2] = { 0 };
		int V_total = 0;
		uint8_t VC_HI = VC1_HI;
		uint8_t VC_LO = VC1_LO;
		for(int i = 0; i<15; i++)
		{
			v[0] = v[1] = 0;																			/*  防止读失败把上一节的读到的电池电压值用 */
			if(BQ_ReadBlock(VC_HI, v, 2) == HAL_OK)								/*  如果读取失败，直接丢弃，保持上一轮的电压值 */
			{
				adc14 = ((uint16_t) (v[0] & 0x3F) << 8 ) | v[1];
				g_bms.Cell_mv[i] = (GAIN14 * adc14 + 1000/2 )/1000 + Offset;   /*  整数四舍五入 1000/2 */
			}
			if(cell_used[i])
				V_total += g_bms.Cell_mv[i];			
			VC_HI += 2;
			VC_LO += 2;			
		}
		g_bms.Total_mv = V_total;	
}

///* 读Cell1电压 */
//static void BQ_GetCell1(void)
//{
//		int V_val;
//		uint16_t adc14;
//		uint8_t v[2];
//		BQ_ReadBlock(VC1_HI, v, 2);
//		adc14 = ((uint16_t) (v[0] & 0x3F) << 8 ) | v[1];
//		V_val = (GAIN14 * adc14 + 1000/2 )/1000 + Offset;  /*  整数四舍五入 1000/2 */
//		printf("Cell1 = %dmV\r\n", V_val);
//}

///* 读总电压 */
///* V(BAT) = 4 × GAIN × ADC(cell) + (#Cells × OFFSET)。
//其中，GAIN以μV/LSB为单位存储，OFFSET以mV为单位存储。 */
//static void BQ_GetTotalV(void)
//{
//		int TotalV = 0;
//		uint16_t bat = 0;
//		uint8_t v[2] = { 0 };
//		BQ_ReadBlock(BAT_HI, v, 2);
//		bat = (uint16_t)((uint16_t) v[0] << 8 ) | v[1];
//		TotalV = (4 * GAIN14 * bat + 1000/2 )/1000 + (TotalCells * Offset);   /*  整数四舍五入 1000/2 */
//		printf("TotalV = %dmV\r\n", TotalV);
//}

/* AFE保护 硬件层面
(a) OV_TRIP_FULL = (OV– ADCOFFSET) ÷ ADCGAIN
(b) UV_TRIP_FULL = (UV– ADCOFFSET) ÷ ADCGAIN*/
/* 短路保护： 1.设计目标：25A 短路保护
2.换算成电压：25A × 4mΩ(采样电阻) = 100mV
3.延迟 100 μs*/
/* 过流保护： 1.设计目标：11A 过流保护
2.换算成电压：11A × 4mΩ(采样电阻) = 44mV
3.延迟 320 ms*/
static void BQ_ConfigProtect(void)
{
		uint8_t OV_TRIP_FULL = 0;
		uint8_t UV_TRIP_FULL = 0;
		float t = GAIN14/1000.0f;   									/* μV 换 m V */
		OV_TRIP_FULL = (uint8_t)((((unsigned int)((OV_THRESHOLD - Offset)/t + 0.5f)) >> 4 )& 0xFF);  /* 浮点数四舍五入公式：`float_val + 0.5f`，再强制转为整数。 过压：4300mV */
		UV_TRIP_FULL = (uint8_t)((((unsigned int)((UV_THRESHOLD - Offset)/t + 0.5f)) >> 4 )& 0xFF);		/* 欠压：2500mV */
		BQ_WriteReg(OV_TRIP, OV_TRIP_FULL);
		BQ_WriteReg(UV_TRIP, UV_TRIP_FULL);
		BQ_WriteReg(PROTECT1, 0x0F); 
		BQ_WriteReg(PROTECT2, 0x5D); 
}

/* 校准ADCOFFSET 
ADC 增益偏移值，最低 3 位 ADCGAIN<4：0> 是针对 ADC 转换函数的生产校准值，单位
为 μV/LSB。其范围为 365 μV/LSB 至 396 μV/LSB，步长为 1 μV/LSB，可按如下公
式计算：GAIN = 365 μV/LSB + (ADCGAIN<4：0>以十进制形式表示的值) × (1 μV/LSB)
ADCOFFSET；满量程输入范围为 -128 毫伏至 127 毫伏，最小二进制位为 1 毫伏。*/
static void BQ_Getoffset(void)
{
		uint8_t Gain[2] = { 0 };
		BQ_ReadBlock(ADCOFFSET, (uint8_t*)&Offset, 1);
		BQ_ReadBlock(ADCGAIN1, &Gain[0], 1);
	  BQ_ReadBlock(ADCGAIN2, &Gain[1], 1);
		GAIN14 = 365 + ((Gain[0] & 0x0C) << 1 | (Gain[1] & 0xE0) >> 5);
		printf("Offset=%02d， GAIN=%02d\r\n", Offset, GAIN14);
}

/* 写寄存器 */
HAL_StatusTypeDef BQ_WriteReg(uint8_t reg, uint8_t val)
{
		uint8_t frame[3] = {BQ_Address, reg, val};
		uint8_t buf[3] = {reg, val, CRC8(frame, 3, 0x07)};
		HAL_StatusTypeDef I2C_Write_Status = HAL_ERROR;
		BaseType_t locked = pdFAIL;
		if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)							/* 调度器未启动就不获取互斥量，直接执行I2C，在调度器未启动前不会出现抢占I2C情况 */
		{
			if(xSemaphoreTake(I2C_Mutex, pdMS_TO_TICKS(250)) != pdPASS)						
				return HAL_BUSY;																										/* 调度器启动后I2C被占用，返回HAL_BUSY */
			locked = 1;																														/* 调度器启动后I2C没有被占用 */
		}
/* BQ_Init()调度器没启动也可以执行 */		
		I2C_Write_Status = HAL_I2C_Master_Transmit(&hi2c1, BQ_Address, buf, 3, 100 );
		if(locked)	
			xSemaphoreGive(I2C_Mutex);
	
		if(I2C_Write_Status == HAL_OK)
			I2C_Curr_Err = 0;
		else
		{
			I2C_Curr_Err++;
			I2C_Tolat_Err++;
		}
		return I2C_Write_Status;
}

///* 读寄存器 */
///* 在单字节读取操作中，循环冗余校验（CRC）是在第二次启动后计算的，并使用从机地址和数据字节。
//在块读取操作中，第一个数据字节的 CRC 在第二次启动后计算，并使用从机地址和数据字节。
//后续数据字节的 CRC 则仅基于数据字节进行计算。*/
//static HAL_StatusTypeDef BQ_ReadReg(uint8_t reg, uint8_t *val)
//{
//		HAL_StatusTypeDef I2C_Read_Status;
//		uint8_t buf[2] = { 0 };
//		uint8_t frame[2] = { 0 };
//		I2C_Read_Status = HAL_I2C_Mem_Read(&hi2c1, BQ_Address, reg, I2C_MEMADD_SIZE_8BIT, buf, 2, 100);
//		
//		if(I2C_Read_Status != HAL_OK)
//			return I2C_Read_Status;
//		
//		frame[0] = BQ_Address | 0x01;   									/* 按位与0x01 读 */
//		frame[1] = buf[0];
//		if(CRC8(frame, 2, 0x07) != buf[1])
//			return HAL_ERROR;
//		
//		*val = buf[0];		
//		return HAL_OK;
//}

/* 读寄存器 */
/* 块读 */
/* 在单字节读取操作中，循环冗余校验（CRC）是在第二次启动后计算的，并使用从机地址和数据字节。
在块读取操作中，第一个数据字节的 CRC 在第二次启动后计算，并使用从机地址和数据字节。
后续数据字节的 CRC 则仅基于数据字节进行计算。*/
HAL_StatusTypeDef BQ_ReadBlock(uint8_t reg, uint8_t *data, uint8_t len)
{		
		HAL_StatusTypeDef I2C_Read_Status = HAL_ERROR;
		uint8_t raw[8]  = { 0 };
		uint8_t i = 0;
		uint8_t frame[2]  = { 0 };
		BaseType_t locked = pdFAIL;
	
		if(len > 4)
			return HAL_ERROR;
		
		if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)							/* 调度器未启动就不获取互斥量，直接执行I2C，在调度器未启动前不会出现抢占I2C情况 */
		{
				if(xSemaphoreTake(I2C_Mutex, pdMS_TO_TICKS(250)) != pdPASS)					
					return HAL_BUSY;																									/* 调度器启动后I2C被占用，返回HAL_BUSY */
				locked = 1;																													/* 调度器启动后I2C没有被占用 */
		}
/* BQ_Init()调度器没启动也可以执行 */		
		I2C_Read_Status = HAL_I2C_Mem_Read(&hi2c1, BQ_Address, reg, I2C_MEMADD_SIZE_8BIT, raw, len * 2, 100);
		
		if(I2C_Read_Status != HAL_OK)
		{
				I2C_Curr_Err++;
				I2C_Tolat_Err++;
				if(locked)
					xSemaphoreGive(I2C_Mutex);
				return I2C_Read_Status;
		}
			
		frame[0] = BQ_Address | 0x01;   										/* 按位与0x01 读 */
		frame[1] = raw[0];
		if(CRC8(frame, 2, 0x07) != raw[1])
		{
			I2C_Curr_Err++;
			I2C_Tolat_Err++;
			if(locked)
				xSemaphoreGive(I2C_Mutex);			
			return HAL_ERROR;
		}
		
		for(i = 1; i < len; i++)														/*后续数据字节的 CRC 则仅基于数据字节进行计算*/
		{
			frame[0] = raw[2 * i];
			if(CRC8(frame, 1, 0x07) != raw[2 * i + 1])
			{
				I2C_Curr_Err++;
				I2C_Tolat_Err++;
				if(locked)
					xSemaphoreGive(I2C_Mutex);				
				return HAL_ERROR;
			}
		}
			
		for(i = 0; i < len; i++)
			data[i] = raw[2 * i];
			
		if(locked)
			xSemaphoreGive(I2C_Mutex);
		I2C_Curr_Err = 0;
		return HAL_OK;
}


/* CRC8校验计算
unsigned char *ptr
`ptr` 是指针，指向要计算 CRC 的那一串数据的首地址。
通俗说：就是存放待校验数据数组的起始位置。
unsigned char len
`len` = length，代表一共有多少个字节的数据，要拿去算 CRC**。
unsigned char key
就是 CRC 多项式，BQ76940 固定填 `0x07`*/
static unsigned char CRC8(unsigned char *ptr, unsigned char len, unsigned char key)
{
    unsigned char i = 0;
    unsigned char crc = 0;
    while(len--!=0)
    {
        for(i=0x80; i!=0; i/=2)
        {
            if((crc & 0x80) != 0)
            {
                crc *= 2;
                crc ^= key;
            }
            else
                crc *= 2;

            if((*ptr & i)!=0)
                crc ^= key;
        }
        ptr++;
    }
    return crc;
}


/* 初始化BQ寄存器 */
static void BQ_Config(void)
{
		char i;
		for(i=0; i<12; i++)
		{
				BQ_WriteReg(BQ_INITReg[i], BQ_INITdata[i]);
		}
}

/* 唤醒BQ */
static void BQ_Wake(void)
{
		HAL_GPIO_WritePin(MCU_WAKE_BQ_GPIO_Port, MCU_WAKE_BQ_Pin, GPIO_PIN_SET);
		HAL_Delay(100);
		HAL_GPIO_WritePin(MCU_WAKE_BQ_GPIO_Port, MCU_WAKE_BQ_Pin, GPIO_PIN_RESET);
		HAL_Delay(10);
}

/* 执行开关均衡 */
bool BQ_SetBalanceMask(uint16_t mask)
{
	if(BQ_WriteReg(CELLBAL1, (mask & 0x1F)) != HAL_OK)									/* 每个CELLBAL的低5位有效 */
		return false;
	if(BQ_WriteReg(CELLBAL2, ((mask >> 5) & 0x1F)) != HAL_OK)
		return false;
	if(BQ_WriteReg(CELLBAL3, ((mask >> 10) & 0x1F)) != HAL_OK)
		return false;
	return true;
}

/* RMW(读 - 改 - 写) 函数 注意：SYS_STAT 是“写 1 清除”型寄存器 因为读出来的故障位是1， 然后重新写进去的也是1，
如果只是想清除一个位需要置1，但是读出来的不想清除的故障位也是1，又一起写进去了，把这个位也清除了
修改位只适用于 BQ_WriteReg(SYS_STAT, mask),写0不变不清除 */
/* mask掩码必须是1U 不是0U 因为是要把这个位 置1或清0或翻转 */
/* 置1寄存器里的值的某一位 注意：reg_addr：寄存器的地址，不是寄存器里的值  mask:掩码 */
bool SetBit(uint8_t reg_addr, uint8_t mask)
{
		uint8_t val = 0;																		/* val 装寄存器里的值 */
		if(BQ_ReadBlock(reg_addr, &val, 1) != HAL_OK)
			return false;
		val |= mask;																				/* 寄存器里的值某一位置1 */
		if(BQ_WriteReg(reg_addr, val) != HAL_OK)
			return false;
		return true;
}

/* 清零寄存器里的值的某一位 注意：reg_addr：寄存器的地址，不是寄存器里的值  mask:掩码 */
bool ClearBit(uint8_t reg_addr, uint8_t mask)
{
		uint8_t val = 0;																		/* val 装寄存器里的值 */
		if(BQ_ReadBlock(reg_addr, &val, 1) != HAL_OK)
			return false;
		val &= ~mask;																				/* 寄存器里的值某一位清零 */
		if(BQ_WriteReg(reg_addr, val) != HAL_OK)
			return false;
		return true;
}

/* 翻转寄存器里的值的某一位 注意：reg_addr：寄存器的地址，不是寄存器里的值  mask:掩码 */
bool ToggleBit(uint8_t reg_addr, uint8_t mask)
{
		uint8_t val = 0;																		/* val 装寄存器里的值 */
		if(BQ_ReadBlock(reg_addr, &val, 1) != HAL_OK)
			return false;
		val ^= mask;																				/* 寄存器里的值某一位翻转 */
		if(BQ_WriteReg(reg_addr, val) != HAL_OK)
			return false;
		return true;
}
