/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    Canopen_app.c
  * @brief   CANopen从机应用层：任务、中断接线
  ******************************************************************************
  * @attention
  * 1. 用户业务代码集中在 Canopen_Task 和 bms_update() 里，协议栈在 Middleware/CANopen
  * 2. 数据流：
  *    TIM4 1ms中断 → HAL_TIM_PeriodElapsedCallback → canopen_app_interrupt()
  *                   （SYNC/RPDO/TPDO 的周期处理，含TPDO1每250ms自动上报）
  *    CAN收发中断 → stm32f1xx_it.c → HAL_CAN_IRQHandler → Driver_STM32驱动层
  *    Canopen_Task → canopen_app_process()（NMT/EMCY/SDO/心跳等非实时处理）
  * 3. 节点ID改 desiredNodeID；波特率改 can.c 位时序 + baudrate
  ******************************************************************************
  */
/* USER CODE END Header */
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "can.h"
#include "tim.h"
#include "Canopen_app.h"
#include "CO_app_STM32.h"
#include "CANopen.h"
#include "OD.h"
#include "app_task.h"
#include "bq76940.h"
#include "soc.h"
#include "faulthandling.h"
#include "balancing.h"
#include "stdbool.h"
#include "faultcheck.h"

/* ----------------------- 内部变量 -----------------------*/
#define CHG_bit                 (1U << 0)
#define DSG_bit                 (1U << 1)
#define BAL_bit                 (1U << 2)

static void bms_update(void);
static void bms_control(void);
static void bms_emcy(void);
	
/* ----------------------- EMCY -----------------------*/
/* CANopenNode 预定义好的 errorBit（内部故障编号） 0x40到0x4F errorRegister的bit7就自动置1（自定义含义）CO_Emergency.h可查看 */
/* 不同的errorBit就可能会自动置位不同的errorRegister的bit */
#define	EM_V                    (CO_EM_MANUFACTURER_START + 0x10U)					/* 0x40 电压类 */
#define	EM_C                    (CO_EM_MANUFACTURER_START + 0x11U)					/* 0x41 电流类 */
#define	EM_T                    (CO_EM_MANUFACTURER_START + 0x12U)					/* 0x42 温度类 */

/* ----------------------- CANopen从机任务 -----------------------*/
void Canopen_Task(void *argument)
{
  CANopenNodeSTM32 canOpenNodeSTM32;
  canOpenNodeSTM32.CANHandle      = &hcan;
  canOpenNodeSTM32.HWInitFunction = MX_CAN_Init;   /* 协议栈内部会调用它初始化CAN */
  canOpenNodeSTM32.timerHandle    = &htim4;         /* 1ms节拍定时器 */
  canOpenNodeSTM32.desiredNodeID  = 0x01;           /* 节点ID：TPDO1=0x181 RPDO1=0x201 SDO=0x581/0x601 */
  canOpenNodeSTM32.baudrate       = 500;            /* 与can.c的位时序一致 */
  canopen_app_init(&canOpenNodeSTM32);

  TickType_t lastWake = xTaskGetTickCount();
  while(1)
  {
    canopen_app_process();          /* NMT/心跳/SDO/EMCY等处理，>1ms间隔由内部判断 */
    bms_update(); 
		bms_control();
		bms_emcy();
		hb.hb_Canopen++;
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1));
  }
}

static void bms_update(void)					/* 上报上位机：只写测量值，只读控制值 */
{
	uint8_t n = 0;											/* 使用的电芯数 */
	uint16_t map = 0;										/* 电芯位图 */
	for(uint8_t i = 0; i < 15; i++)
	{
		if(cell_used[i] == 1)
		{
			OD_RAM.x2000_cell_Voltage[i] = (uint16_t)g_bms.Cell_mv[i];
			map |= (1U << i);
			n++;
		}
		else
			OD_RAM.x2000_cell_Voltage[i] = 0;
	}
	OD_RAM.x2001_total_Voltage = (uint16_t)g_bms.Total_mv;
	OD_RAM.x2002_cell_nct = n;
	OD_RAM.x2003_cell_map = map;			
	OD_RAM.x2004_current = (int16_t)g_bms.Cur_ma;
	OD_RAM.x2005_SOC = g_socinit_flag ? (uint16_t)g_bms.Ah_socX10 : 0xFFFF;		/* 没有校准成功就显示0xFFFF */
	OD_RAM.x2006_temp = (int16_t)g_bms.TemX10;
	OD_RAM.x2007_fault = g_bms.fault;
	OD_RAM.x2008_bal_mask = g_bms.bal_mask;
	OD_RAM.x2009_MOS_status = g_bms.MOS_Status;
	OD_RAM.x200A_sys_stat = g_bms.sys_stat;
}

static void bms_control(void)					/* 上位机控制：均衡MOS，充放电MOS */
{	
	if(CO == NULL)											/* 协议栈还没启动 */
		return;
	if(CO->RPDO[0].timeoutTimer == 0U)					/* 主站还没发过信息，首帧前不报警 */
		return;
	if(CO->RPDO[0].timeoutTimer > CO->RPDO[0].timeoutTime_us)  /* 超时，主站掉线了，上报EMCY */
	{
		CHG_enable = 0;
		DSG_enable = 0;
		BAL_enable = 0;
		return;
	}
	
	CHG_enable = (OD_RAM.x200B_balAndMos & CHG_bit) ? 1 : 0;
	DSG_enable = (OD_RAM.x200B_balAndMos & DSG_bit) ? 1 : 0;
	BAL_enable = (OD_RAM.x200B_balAndMos & BAL_bit) ? 1 : 0;
}

/* EMCY 注意：errorBit 不是 errorRegister，errorRegister在协议栈自动完成，CO_Emergency.h可以看其每个位的含义 */
/* 电压 errorBit：0x40 errorCode：CO_EMC_VOLTAGE_OUTPUT(0x3300) */
/* 电流 errorBit：0x41 errorCode：CO_EMC_CURRENT_OUTPUT(0x2300) */
/* 温度 errorBit：0x42 errorCode：CO_EMC_TEMPERATURE(0x4000) */
static void bms_emcy(void)									
{
	static bool last_v = false, last_c = false, last_t = false;  /* 存上一次电压，电流，温度 */
	uint8_t fault = g_bms.fault;

	if(CO == NULL)											/* 协议栈还没启动 */
		return;
	
	bool now_v = (fault & FLAG_OV) || (fault & FLAG_UV);
	bool now_c = (fault & FLAG_COC) || (fault & FLAG_DOC) || (fault & FLAG_SC);
	bool now_t = (fault & FLAG_OT) || (fault & FLAG_UT);	
	
	if(now_v && !last_v)																				/* 这一轮故障且上一轮无故障，边沿触发 */
		CO_errorReport(CO->em, EM_V, CO_EMC_VOLTAGE_OUTPUT, fault);/* CANopen 主站上报故障，发 EMCY 报文，且存历史日志，可存数量在OD的0x1003 */
	if(!now_v && last_v)
		CO_errorReset(CO->em, EM_V, 0);
	if(now_c && !last_c)																				
		CO_errorReport(CO->em, EM_C, CO_EMC_CURRENT_OUTPUT, fault);
	if(!now_c && last_c)
		CO_errorReset(CO->em, EM_C, 0);	
	if(now_t && !last_t)																				
		CO_errorReport(CO->em, EM_T, CO_EMC_TEMPERATURE, fault);
	if(!now_t && last_t)
		CO_errorReset(CO->em, EM_T, 0);	
	
	last_v = now_v;
	last_c = now_c;
	last_t = now_t;
}
/* ----------------------- TIM4 1ms中断 → 协议栈实时处理 -----------------------*/
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM4)
  {
    canopen_app_interrupt();   /* 里面处理SYNC/RPDO/TPDO，勿放耗时操作 */
  }
}

/* ----------------------- TIM4中断入口（沿用FreeModbus模板惯例，写在本文件） -----------------------*/
void TIM4_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim4);
}
