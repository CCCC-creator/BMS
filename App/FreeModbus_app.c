/*
 * FreeModbus Libary: BARE Demo Application
 * Copyright (C) 2006 Christian Walter <wolti@sil.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"
#include "mbutils.h"    // xMBUtilGetBits/xMBUtilSetBits 线圈位打包工具
#include "stm32f1xx_hal.h"

/* ----------------------- Defines ------------------------------------------*/
/* 地址基准：协议栈调用回调前已把报文地址+1，所以START填"报文地址+1"。
   即报文(0-based)地址X对应 buf[X-(START-1)]，报文0始终对应buf[0] */
// 输入寄存器（功能码04，只读），报文地址1000~1003
#define REG_INPUT_START 1001
#define REG_INPUT_NREGS 4

// 保持寄存器（功能码03/06/16，可读可写，调试推荐使用），报文地址0~9
#define REG_HOLD_START  1
#define REG_HOLD_NREGS  10

// 线圈（功能码01/05/15），报文地址0~7
#define REG_COIL_START  1
#define REG_COIL_NCOILS 8

/* ----------------------- Static variables ---------------------------------*/
static USHORT   usRegInputBuf[REG_INPUT_NREGS];    // 输入寄存器数组
static USHORT   usRegHoldingBuf[REG_HOLD_NREGS];   // 保持寄存器数组
static UCHAR    ucCoilBuf[REG_COIL_NCOILS / 8 +1]; // 线圈缓存，按bit存储

/* ----------------------- 寄存器回调函数实现 -----------------------------*/

/**
 * @brief 输入寄存器回调 功能码04 (只读)
 */
eMBErrorCode
eMBRegInputCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNRegs )
{
    eMBErrorCode    eStatus = MB_ENOERR;
    int             iRegIndex;

    if( ( usAddress >= REG_INPUT_START )
        && ( usAddress + usNRegs <= REG_INPUT_START + REG_INPUT_NREGS ) )
    {
        iRegIndex = ( int )( usAddress - REG_INPUT_START );
        while( usNRegs > 0 )
        {
            // Modbus大端模式：先发高字节，后发低字节
            *pucRegBuffer++ = ( unsigned char )( usRegInputBuf[iRegIndex] >> 8 );
            *pucRegBuffer++ = ( unsigned char )( usRegInputBuf[iRegIndex] & 0xFF );
            iRegIndex++;
            usNRegs--;
        }
    }
    else
    {
        eStatus = MB_ENOREG; // 寄存器地址越界，返回错误
    }
    return eStatus;
}

/**
 * @brief 保持寄存器回调 功能码03/06/16 (可读可写)
 */
eMBErrorCode
eMBRegHoldingCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNRegs,
                 eMBRegisterMode eMode )
{
    eMBErrorCode    eStatus = MB_ENOERR;
    int             iRegIndex;

    if( ( usAddress >= REG_HOLD_START )
        && ( usAddress + usNRegs <= REG_HOLD_START + REG_HOLD_NREGS ) )
    {
        iRegIndex = ( int )( usAddress - REG_HOLD_START );
        if( eMode == MB_REG_READ )
        {
            // 上位机读保持寄存器
            while( usNRegs > 0 )
            {
                *pucRegBuffer++ = ( unsigned char )( usRegHoldingBuf[iRegIndex] >> 8 );
                *pucRegBuffer++ = ( unsigned char )( usRegHoldingBuf[iRegIndex] & 0xFF );
                iRegIndex++;
                usNRegs--;
            }
        }
        else if( eMode == MB_REG_WRITE )
        {
            // 上位机写保持寄存器
            while( usNRegs > 0 )
            {
                usRegHoldingBuf[iRegIndex] = ( *pucRegBuffer << 8 ) | *( pucRegBuffer + 1 );
                pucRegBuffer += 2;
                iRegIndex++;
                usNRegs--;
            }
        }
    }
    else
    {
        eStatus = MB_ENOREG;
    }
    return eStatus;
}

/**
 * @brief 线圈回调 功能码01/05/15，读写线圈(bit)
 *
 * 核心调用约定(见mbfunccoils.c)：
 *  读(FC01)：缓冲区按位打包，1字节=8线圈，字节数=ceil(N/8)，字节内LSB对应最小地址
 *  写单(FC05)：核心传入1个线圈值0x01/0x00（不是0xFF！）
 *  写多(FC15)：缓冲区为报文原始打包字节，1字节=8线圈
 * 因此必须用xMBUtilGetBits/SetBits按位处理，不能1线圈消费1字节
 */
eMBErrorCode
eMBRegCoilsCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNCoils,
               eMBRegisterMode eMode )
{
    eMBErrorCode eStatus = MB_ENOERR;
    int iRegIndex;

    if( ( usAddress >= REG_COIL_START ) && ( usAddress + usNCoils <= REG_COIL_START + REG_COIL_NCOILS ) )
    {
        iRegIndex = ( int )( usAddress - REG_COIL_START );
        if( eMode == MB_REG_READ )
        {
            // 读：按位打包进应答帧，不足8个的最后一字节高位补0（GetBits自动掩掉）
            while( usNCoils > 0 )
            {
                UCHAR ucBitNum = ( usNCoils >= 8 ) ? 8 : usNCoils;
                *pucRegBuffer++ = xMBUtilGetBits( ucCoilBuf, iRegIndex, ucBitNum );
                iRegIndex += ucBitNum;
                usNCoils -= ucBitNum;
            }
        }
        else
        {
            // 写：FC05传0x01/0x00，FC15传打包字节；掩掉超出NBits的高位防止污染相邻位
            while( usNCoils > 0 )
            {
                UCHAR ucBitNum = ( usNCoils >= 8 ) ? 8 : usNCoils;
                xMBUtilSetBits( ucCoilBuf, iRegIndex, ucBitNum,
                                ( UCHAR )( *pucRegBuffer++ & ( ( 1 << ucBitNum ) - 1 ) ) );
                iRegIndex += ucBitNum;
                usNCoils -= ucBitNum;
            }
        }
    }
    else
    {
        eStatus = MB_ENOREG;
    }
    return eStatus;
}

/**
 * @brief 离散输入寄存器 功能码02，只读
 */
eMBErrorCode
eMBRegDiscreteCB( UCHAR * pucRegBuffer, USHORT usAddress, USHORT usNDiscrete )
{
    // 这里暂时不启用离散输入，直接返回无此寄存器
    return MB_ENOREG;
}

/* ----------------------- Modbus任务入口函数（FreeRTOS任务，xTaskCreate调用） -----------------------------*/
void FreeModbus_RTU_Task( void *pvParameters )
{
    eMBErrorCode    eStatus;
    ( void )pvParameters;
    /*
    eMBInit参数：
    1. 协议类型：MB_RTU
    2. 从站地址：0x01 (地址1，ModbusPoll用这个地址访问)
    3. 串口端口号：0，对应port层ucPort
    4. 波特率：9600
    5. 校验：MB_PAR_NONE 无校验
    6. 停止位：1
    注意：必须和CubeMX USART1配置 9600 8N1保持一致！
    */
    eStatus = eMBInit( MB_RTU, 0x01, 0, 9600, MB_PAR_NONE, 1 );
    if(eStatus != MB_ENOERR)
    {
        // Modbus初始化失败，可以加LED报错提示
        vTaskDelete( NULL );
    }
    /* Enable the Modbus Protocol Stack. */
    eStatus = eMBEnable(  );
    if(eStatus != MB_ENOERR)
    {
        vTaskDelete( NULL );
    }

    while(1)
    {
        ( void )eMBPoll(  );   // Modbus事件轮询；xMBPortEventGet最多阻塞10ms，此处无需额外延时
        /* 测试：输入寄存器第0个持续自增，Modbus Poll可以观察变化 */
        usRegInputBuf[0]++;
    }
}
