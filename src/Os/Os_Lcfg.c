#include "Os.h"
#include "Os_Cfg.h"
#include "Os_Lcfg.h"

extern void tskUART( uint16 param0, void *param1 );
extern void tskHighPrio( uint16 param0, void *param1 );

const Os_TaskConfigType Os_LcfgTaskConfig[OS_MAX_NUMBER_OF_TASKS] =
{
        /* Id, Handler, prio, parameter0, parameter1 */
        /* 1 */
        {
                tskUART,
                10,
                0,
                "UART1"
        },
        /* 2 */
        {
                tskUART,
                10,
                1,
                "UART2"
        },
        /* 3 */
        { tskUART,
                10,
                2,
                "UART3"
        },
        /* 4 */
        {
                tskUART,
                10,
                3, "UART4"
        },
        /* 5 */
        {
                tskHighPrio,
                20,
                1,
                "highp"
        },
        /* 4 */
        {
                tskUART,
                10,
                3,
                "UART4"
        }
};


const void *Os_GetLcfgData(void)
{
   return ((void*) &Os_LcfgTaskConfig);
}
