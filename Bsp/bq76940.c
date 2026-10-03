#include "main.h"
#include "stm32f1xx_hal.h"
#include "bq76940.h"
#include "i2c.h"
#include "stdio.h"



#define BQ_Address								(0x08<<1)
#define REG_ADCGAIN1								0x50

static unsigned char CRC8(unsigned char *ptr, unsigned char len, unsigned char key);
static HAL_StatusTypeDef BQ_WriterReg(uint8_t reg, uint8_t val);
static HAL_StatusTypeDef BQ_ReadReg(uint8_t reg, uint8_t *val);


static void BQ_Wake(void);


/* BQ初始化 */
void BQ_Init(void)
{		
		uint8_t gain = 0;
		BQ_Wake();
		HAL_StatusTypeDef I2C_Status;
		I2C_Status = BQ_ReadReg(REG_ADCGAIN1, &gain);
		printf("状态： %d， 值：%02d", I2C_Status, gain);
}


/* 唤醒BQ */
static void BQ_Wake(void)
{
		HAL_GPIO_WritePin(MCU_WAKE_BQ_GPIO_Port, MCU_WAKE_BQ_Pin, GPIO_PIN_SET);
		HAL_Delay(100);
		HAL_GPIO_WritePin(MCU_WAKE_BQ_GPIO_Port, MCU_WAKE_BQ_Pin, GPIO_PIN_RESET);
		HAL_Delay(10);
}

/* 写寄存器 */
static HAL_StatusTypeDef BQ_WriterReg(uint8_t reg, uint8_t val)
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
