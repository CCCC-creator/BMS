#include "main.h"
#include "stm32f1xx_hal.h"
#include "bq76940.h"
#include "i2c.h"
#include "stdio.h"
#include "math.h"



#define BQ_Address								(0x08<<1)
#define REG_ADCGAIN1								0x50
#define BIT_SHUT_B								(1 << 0)
#define BIT_SHUT_A								(1 << 1)
#define BIT_TEMP_SEL							(1 << 3)
#define BIT_ADC_EN								(1 << 4)
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
#define OV_THRESHOLD								4250    	/* 阈值，单位：mV */
#define UV_THRESHOLD								2750			/* 阈值，单位：mV */
#define VC1_HI											0x0C
#define VC1_LO											0x0D
#define CC_HI												0x32
//#define CC_LO												0x33
#define BAT_HI											0x2A
//#define BAT_LO											0x2B
#define TotalCells										9
#define TS1_HI 											0x2C
//#define	TS1_LO  										0x2D



static void BQ_Wake(void);
static void BQ_Config(void);
static void BQ_Getoffset(void);
static void BQ_ConfigProtect(void);
//static void BQ_GetCell1(void);
static void BQ_GetAllCellV(void);
static void BQ_GetCurr(void);
static void BQ_GetTotalV(void);


/* bsp */
static unsigned char CRC8(unsigned char *ptr, unsigned char len, unsigned char key);
static HAL_StatusTypeDef BQ_WriteReg(uint8_t reg, uint8_t val);
//static HAL_StatusTypeDef BQ_ReadReg(uint8_t reg, uint8_t *val);
static HAL_StatusTypeDef BQ_ReadBlock(uint8_t reg, uint8_t *date, uint8_t len);
static const uint8_t cell_used[15] = {1, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1};

static uint16_t GAIN14;								/* 微伏 */
static int8_t Offset;									/* 毫伏 */



/*--------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------*/

/* BQ初始化 */
void BQ_Init(void)
{		
		
		BQ_Wake();
		BQ_Config();
		BQ_Getoffset();
		BQ_ConfigProtect();
//		uint8_t gain = 0;
//		HAL_StatusTypeDef I2C_Status;
//		I2C_Status = BQ_ReadReg(REG_ADCGAIN1, &gain);
//		printf("\r\n状态： %d， 值：%02X\r\n", I2C_Status, gain);
//		BQ_WriteReg(SYS_STAT, 0xFF);
}


/* 读取电压[9]，电流，温度 */
/*模数转换器的转换函数是一个线性方程，其定义如下：
V（单元） = 增益 x 模数转换器（单元） + 偏移量 
V(cell) = GAIN x ADC(cell) + OFFSET
增益以微伏/位为单位存储，而偏移量则以毫伏为单位存储。*/
void BQ_GetAll(void)
{
		BQ_GetAllCellV();
		BQ_GetTotalV();
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
		BQ_WriteReg(SYS_CTRL1, BIT_ADC_EN | BIT_TEMP_SEL | BIT_SHUT_B);
		HAL_Delay(20);
		BQ_WriteReg(SYS_CTRL1, BIT_ADC_EN | BIT_TEMP_SEL | BIT_SHUT_A);
}


/*--------------------------------------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------------------------------------*/

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
CC读数（单位：μV）= [16位二进制补码值] × (8.44 μV/LSB) */ 
static void BQ_GetCurr(void)
{
		uint8_t v[2] = { 0 };
		int cc = 0;
		int Curr = 0;
		BQ_ReadBlock(CC_HI, v, 2);
		cc = (int16_t)((uint16_t) (v[0] << 8 ) | v[1] );				
		Curr = (cc < 0) ? (cc * 2110 - 500) / 1000 : (cc * 2110 + 500) / 1000;				/* cc×8.44μV÷4mΩ×1000 = cc×2110μA。 整数四舍五入 1000/2 = 500， 解决负数四舍五入问题*/
		printf("cc=%d，电流：%dmA\r\n", cc, Curr);			
}

/* 测量温度 */
/*VTSX = (ADC in Decimal) x 382 μV/LSB
RTS = (10,000 × VTSX) ÷ (3.3– VTSX) */
void BQ_GetTem(void)
{
		float VTSX = 0;
		float RTS = 0;
		int Tem = 0;
		uint8_t v[2] = { 0 };
		BQ_ReadBlock(TS1_HI, v, 2);
		VTSX = ((uint16_t)((uint16_t)(v[0] & 0x3F)) << 8 | v[1] ) * 0.382f;			/* 382是uV */
		RTS = (10000.0f * VTSX) / (3300.f - VTSX);															/* 3.3是V */
		Tem = 1 / (1 / (273.15 + 25)+(log(RTS / 10000)) / 3380)- 273.15 + 0.5;	/*	NTC 热敏电阻 B 值公式 */
		printf("Tem = %d\r\n", Tem);
}

/* 读所有电池电压 */
static void BQ_GetAllCellV(void)
{
		int V_val = 0;
		uint16_t adc14 = 0;
		uint8_t v[2] = { 0 };
		int V_total = 0;
		uint8_t VC_HI = VC1_HI;
		uint8_t VC_LO = VC1_LO;
		for(int i = 0; i<15; i++)
		{
			BQ_ReadBlock(VC_HI, v, 2);
			adc14 = ((uint16_t) (v[0] & 0x3F) << 8 ) | v[1];
			V_val = (GAIN14 * adc14 + 1000/2 )/1000 + Offset;   /*  整数四舍五入 1000/2 */
			printf("Cell%d = %dmV\r\n", i + 1 , V_val);
			if(cell_used[i])
				V_total += V_val;
			VC_HI += 2;
			VC_LO += 2;
		}
		printf("V_total = %dmV\r\n", V_total);
		
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

/* 读总电压 */
/* V(BAT) = 4 × GAIN × ADC(cell) + (#Cells × OFFSET)。
其中，GAIN以μV/LSB为单位存储，OFFSET以mV为单位存储。 */
static void BQ_GetTotalV(void)
{
		int TotalV = 0;
		uint16_t bat = 0;
		uint8_t v[2] = { 0 };
		BQ_ReadBlock(BAT_HI, v, 2);
		bat = (uint16_t)((uint16_t) v[0] << 8 ) | v[1];
		TotalV = (4 * GAIN14 * bat + 1000/2 )/1000 + (TotalCells * Offset);   /*  整数四舍五入 1000/2 */
		printf("TotalV = %dmV\r\n", TotalV);
}

/* 写寄存器，设置AFE保护 
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

/* 写寄存器 */
static HAL_StatusTypeDef BQ_WriteReg(uint8_t reg, uint8_t val)
{
		uint8_t frame[3] = {BQ_Address, reg, val};
		uint8_t buf[3] = {reg, val, CRC8(frame, 3, 0x07)};
		HAL_StatusTypeDef I2C_Write_Status;
		I2C_Write_Status = HAL_I2C_Master_Transmit(&hi2c1, BQ_Address, buf, 3, 100 );
		HAL_Delay(20);
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
static HAL_StatusTypeDef BQ_ReadBlock(uint8_t reg, uint8_t *data, uint8_t len)
{		
		HAL_StatusTypeDef I2C_Read_Status;
		uint8_t raw[8]  = { 0 };
		uint8_t i = 0;
		uint8_t frame[2]  = { 0 };
	
		if(len > 4)
			return HAL_ERROR;
		
		I2C_Read_Status = HAL_I2C_Mem_Read(&hi2c1, BQ_Address, reg, I2C_MEMADD_SIZE_8BIT, raw, len * 2, 100);
		
		if(I2C_Read_Status != HAL_OK)
			return I2C_Read_Status;
		
		frame[0] = BQ_Address | 0x01;   									/* 按位与0x01 读 */
		frame[1] = raw[0];
		if(CRC8(frame, 2, 0x07) != raw[1])
			return HAL_ERROR;
		
		for(i = 1; i < len; i++)														/*后续数据字节的 CRC 则仅基于数据字节进行计算*/
		{
			frame[0] = raw[2 * i];
			if(CRC8(frame, 1, 0x07) != raw[2 * i + 1])
				return HAL_ERROR;
		}
		
		for(i = 0; i < len; i++)
			data[i] = raw[2 * i];
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
		const unsigned char BQ769_INITReg[12] = {SYS_STAT, CELLBAL1, CELLBAL2, CELLBAL3, SYS_CTRL1, SYS_CTRL2,
																										PROTECT1, PROTECT2, PROTECT3, OV_TRIP, UV_TRIP, CC_CFG};
		const unsigned char BQ769_INITdata[12] = {0xFF, 0x00, 0x00, 0x00, 0x18, 0x43,
																										 0x00, 0x00, 0x00, 0x00, 0x00, 0x19};
		char i;
		for(i=0; i<12; i++)
		{
				BQ_WriteReg(BQ769_INITReg[i], BQ769_INITdata[i]);
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

