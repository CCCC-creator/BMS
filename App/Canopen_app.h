/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    Canopen_app.h
  * @brief   CANopen从机应用层对外接口
  ******************************************************************************
  * @attention
  * 用户业务代码只写在本文件对应的 Canopen_app.c 里，Middleware/CANopen 当黑盒
  ******************************************************************************
  */
/* USER CODE END Header */
#ifndef CANOPEN_APP_H
#define CANOPEN_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* CANopen从机任务：在main.c的USER CODE区创建 */
void Canopen_Task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* CANOPEN_APP_H */
