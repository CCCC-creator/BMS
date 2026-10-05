#include "main.h"
#include "stm32f1xx_hal.h"
#include "bq76940.h"
#include "i2c.h"
#include "stdio.h"



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
#define OV_THRESHOLD								4300    	/* 阈值，单位：mV */
#define UV_THRESHOLD								2500			/* 阈值，单位：mV */


static unsigned char CRC8(unsigned char *ptr, unsigned char len, unsigned char key);
static HAL_StatusTypeDef BQ_WriteReg(uint8_t reg, uint8_t val);
static HAL_StatusTypeDef BQ_ReadReg(uint8_t reg, uint8_t *val);


static void BQ_Wake(void);
static void BQ_Config(void);
static void BQ_Getoffset(void);
static void BQ_ConfigProtect(void);

static uint16_t GAIN14;
static int8_t Offset;


/*模数转换器的转换函数是一个线性方程，其定义如下：
V（单元） = 增益 x 模数转换器（单元） + 偏移量 
V(cell) = GAIN x ADC(cell) + OFFSET
增益以微伏/位为单位存储，而偏移量则以毫伏为单位存储。*/
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
void BQ_ReadAll(void)
{
		
}


/* 控制充放电，均衡开关 */
void BQ_Control(void)
{
		

}


/*--------------------------------------------------------------------------------------------------------------*/
/* BQ休眠模式SHIP。调用后 I2C 失效，唤醒需 PA8 上升沿脉冲 */
void BQ_SHIP(void)
{
		BQ_WriteReg(SYS_CTRL1, BIT_ADC_EN | BIT_TEMP_SEL | BIT_SHUT_B);
		HAL_Delay(20);
		BQ_WriteReg(SYS_CTRL1, BIT_ADC_EN | BIT_TEMP_SEL | BIT_SHUT_A);
}


/*--------------------------------------------------------------------------------------------------------------*/

/* 校准ADCOFFSET 
ADC 增益偏移值，最低 3 位 ADCGAIN<4：0> 是针对 ADC 转换函数的生产校准值，单位
为 μV/LSB。其范围为 365 μV/LSB 至 396 μV/LSB，步长为 1 μV/LSB，可按如下公
式计算：GAIN = 365 μV/LSB + (ADCGAIN<4：0>以十进制形式表示的值) × (1 μV/LSB)*/
static void BQ_Getoffset(void)
{
		uint8_t Gain[2];
		BQ_ReadReg(ADCOFFSET, (uint8_t*)&Offset);
		BQ_ReadReg(ADCGAIN1, &Gain[0]);	
		BQ_ReadReg(ADCGAIN2, &Gain[1]);
		GAIN14 = 365 + ((Gain[0] & 0x0C) << 1 | (Gain[1] & 0xE0) >> 5);
		printf("Offset=%02d， GAIN14=%02d\r\n", Offset, GAIN14);
}


/* 写寄存器，设置AFE保护 
(a) OV_TRIP_FULL = (OV– ADCOFFSET) ÷ ADCGAIN
(b) UV_TRIP_FULL = (UV– ADCOFFSET) ÷ ADCGAIN*/
static void BQ_ConfigProtect(void)
{
		uint8_t OV_TRIP_FULL;
		uint8_t UV_TRIP_FULL;
		float t = GAIN14/1000.0f;   									/* μV 换 m V */
		OV_TRIP_FULL = (uint8_t)((((unsigned int)((OV_THRESHOLD - Offset)/t + 0.5f)) >> 4 )& 0xFF);  /* 浮点数四舍五入公式：`float_val + 0.5f`，再强制转为整数。 */
		UV_TRIP_FULL = (uint8_t)((((unsigned int)((UV_THRESHOLD - Offset)/t + 0.5f)) >> 4 )& 0xFF);
		BQ_WriteReg(OV_TRIP, OV_TRIP_FULL);
		BQ_WriteReg(UV_TRIP, UV_TRIP_FULL);
		BQ_WriteReg(PROTECT1, 0xFF);
		BQ_WriteReg(PROTECT2, 0xFF);
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

/* 读寄存器 */
static HAL_StatusTypeDef BQ_ReadReg(uint8_t reg, uint8_t *val)
{
		HAL_StatusTypeDef I2C_Read_Status;
		uint8_t buf[2];
		uint8_t frame[2];
		I2C_Read_Status = HAL_I2C_Mem_Read(&hi2c1, BQ_Address, reg, I2C_MEMADD_SIZE_8BIT, buf, 2, 100);
		
		if(I2C_Read_Status != HAL_OK)
			return I2C_Read_Status;
		
		frame[0] = BQ_Address | 0x01;
		frame[1] = buf[0];
		if(CRC8(frame, 2, 0x07) != buf[1])
			return HAL_ERROR;
		
		*val = buf[0];
		HAL_Delay(20);		
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
    unsigned char i;
    unsigned char crc=0;
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
		static const unsigned char BQ769_INITReg[12] = {SYS_STAT, CELLBAL1, CELLBAL2, CELLBAL3, SYS_CTRL1, SYS_CTRL2,
																										PROTECT1, PROTECT2, PROTECT3, OV_TRIP, UV_TRIP, CC_CFG};
		static const unsigned char BQ769_INITdata[12] = {0xFF, 0x00, 0x00, 0x00, 0x18, 0x43,
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

