#ifndef __FAULTCHECK_H__
#define __FAULTCHECK_H__



#include "stdint.h"

/* 故障标志 */
#define FLAG_OV											(1U << 0)							/* 过压 */
#define FLAG_UV											(1U << 1)							/* 欠压 */
#define FLAG_COC										(1U << 2)							/* 充电过流 */
#define FLAG_DOC										(1U << 3)							/* 放电过流 */
#define FLAG_SC											(1U << 4)							/* 短路 */
#define FLAG_OT											(1U << 5)							/* 过温 */
#define FLAG_UT											(1U << 6)							/* 低温 */

extern uint8_t prev;



uint8_t FaultCheck(void);


#endif
