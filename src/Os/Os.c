/*
 * eRTK.cpp
 *
 * Created: 13.04.2015 07:52:46
 *  Author: er
 */
#include <string.h>
#include <stdio.h>
#include "Os_Cfg.h"
#include "Os.h"
#include "Os_Lcfg.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include "../AVR/adc.h"

static void timer0_init(void);
static Os_TaskConfigType *Os_TaskConfig;

#ifdef ERTK_DEBUG
uint8 Os_Stack[OS_MAX_NUMBER_OF_TASKS+1][OS_STACKSIZE]  __attribute__ ((aligned (256)));  /* Jede Task hat einen Stack a' STACKSIZE Byte */
#else
uint8 Os_Stack[OS_MAX_NUMBER_OF_TASKS+1][OS_STACKSIZE];  /* Jede Task hat Stack a' STACKSIZE Byte */
#endif

void * Os_StackPointer[OS_MAX_NUMBER_OF_TASKS+1]; /* Ablage fuer Stackpointer der Tasks */

volatile Os_TaskIdType Os_CurrentRunningTask;    /* Nummer der aktuellen Task, 0 = idle */

/* struktur task control block */
typedef struct Os_TaskControlBlockType {    /* queue mit ordnungskriterium task prioritaet 255..0 */
    struct Os_TaskControlBlockType * NextTask;   /* 1. element immer verkettungszeiger !!! */
    struct Os_TaskControlBlockType * PreviousTask; /* vorgaenger in liste */
    Os_TaskIdType                    TaskId;          /* index aus der reihenfolge der statischen deklaration */
    Os_TaskPrioType                  TaskPrio;         /* 0..255 */
    Os_TaskTimerType                 TaskTimer;        /* timeout counter mit system tick aufloesung */
#ifdef ERTK_DEBUG
    uint16 param0;
    void * param1;
#endif
} Os_TaskControlBlockType;


Os_TaskControlBlockType Os_TaskControlBlock[OS_MAX_NUMBER_OF_TASKS+1];    /* indizierung ueber task index */

//semaphoren
static Os_SemaphoreType Os_Semaphore[OS_MAX_NUM_OF_SEMAPHORE];

Os_TaskControlBlockType * TaskReadyList;         /* einfache verkettung aller ready tasks ueber Os_TaskControlBlock.next in reihenfolge der prioritaet */

/*
  die perfcounter:
  stellen in einem schieberegister die letzten 256 messergebnisse dar.
  bei system im leerlauf stehen die zaehler auf 1000-overhead.
  eine schleife dauert 16 zyklen, alle 1ms wird abgefragt und da sind 16000 zyklen ausgefuehrt worden.
  je mehr systemlast um so kleiner der zaehlerstand.
  1000 -> leerlauf
   900 -> 100*16=1600 zyklen wurden anderweitig verbraucht (in diesem 1ms intervall)
 */
//#ifdef ERTK_DEBUG
volatile uint16 Os_CurrentPerformanceCount;        //aktueller counter
volatile uint16 Os_PerformanceCounter[256]; //counter array der letzen 256ms
volatile uint16 Os_Ticks;            //wie spaet ist es nach dem urknall in ms
volatile uint8  Os_OverloadCounter;     //zaehlt die aufeinanderfolgenden overload phasen

volatile uint8  Os_SystemStarted;                 //wird gesetzt wenn das system gestartet ist
volatile uint8  Os_PerformanceCounterIndex;              //index im array 0..255
//#endif



void Os_Init(void)
{ /* Initialisierung der Daten des EchtzeiOs_ErrorHookTypetems */
    Os_TaskPrioType prio = 0u;
    Os_TaskIdType n = 0u;
    uint8 index = 0u;

    Os_TaskIdType task;

    Os_TaskConfig = (Os_TaskConfigType*) Os_GetLcfgData();

    for(n = 0; n < OS_MAX_NUMBER_OF_TASKS+1; n++)
    {
        Os_TaskControlBlock[n].NextTask = NULL; /* verkettung der Os_TaskControlBlock's in unsortiertem grundzustand */
        Os_TaskControlBlock[n].PreviousTask = NULL;
        Os_TaskControlBlock[n].TaskId = n;
        Os_TaskControlBlock[n].TaskPrio = n ? Os_TaskConfig[n-1].TaskPrio : 0; //idle task ist immer da mit pri 0
        Os_TaskControlBlock[n].TaskTimer = 0;
        //die parameter merken zum debuggen
#ifdef ERTK_DEBUG
        Os_TaskControlBlock[n].param0 =  n ? 0 : Os_TaskConfig[n-1].param0;
        Os_TaskControlBlock[n].param1 =  n ? 0 : Os_TaskConfig[n-1].param1;
#endif
    }
    //einsetzen der idle task, die muss immer da sein !
    TaskReadyList = (Os_TaskControlBlockType *)&Os_TaskControlBlock[0];
    TaskReadyList->NextTask = NULL;
    TaskReadyList->PreviousTask = NULL;
    /* einsortieren der tasks mit verkettung nach absteigender prioritaet */
    char hit[OS_MAX_NUMBER_OF_TASKS+1];
    memset(hit, 0, sizeof hit);
    for(task = 1; task<OS_MAX_NUMBER_OF_TASKS+1; task++)
    {
        prio = 0;
        index = 0;
        for(n = 1; n < OS_MAX_NUMBER_OF_TASKS+1; n++)
        { //hoechstpriore ready und noch nicht verkettete task finden
            if(Os_TaskControlBlock[n].TaskPrio >= prio && !hit[n])
            { /* prio>prio und noch nicht in liste */
                prio = Os_TaskControlBlock[n].TaskPrio;
                index = n;
            }
        }
        Os_SetReady(index); //pTaskReady -> Os_TaskControlBlockx -> txdy -> 0
        hit[index] = 1;
    }
    //
    for(n = 0; n<OS_MAX_NUMBER_OF_TASKS+1; n++)
    { /* SP der Tasks auf jeweiliges Stackende setzen */
        for(uint16 f = 0; f < OS_STACKSIZE/4; f++) memcpy(Os_Stack[n]+4*f, "\xde\xad\xbe\xef", 4);
        //memcpy(&stack[n][214], "0123456789abcdef0123456789ABCDEF", 32);
        /* startadressen und parameter auf den stack */
        Os_Stack[n][OS_STACKSIZE-9] = 0; //r1 = 0
        //
        union
        {
            uint32 ui32;
            uint8 ui8[4];
            void (*TaskPointer)(uint16 param0, void *param1);
            void (*IdleTaskPointer)(void);
            void (*ErrorHookPointer)(Os_ErrorHookType);
        } TaskAddress;

        memset(&TaskAddress, 0, sizeof TaskAddress);
        TaskAddress.ErrorHookPointer = Os_ErrorHook;
        Os_Stack[n][OS_STACKSIZE-1] = TaskAddress.ui8[0];
        Os_Stack[n][OS_STACKSIZE-2] = TaskAddress.ui8[1];
        Os_Stack[n][OS_STACKSIZE-3] = TaskAddress.ui8[2];
        if(n)
        {
            TaskAddress.TaskPointer = Os_TaskConfig[n-1].task;
        }
        else
        {
            TaskAddress.IdleTaskPointer = Os_Idle;
        }
        Os_Stack[n][OS_STACKSIZE-4] = TaskAddress.ui8[0];
        Os_Stack[n][OS_STACKSIZE-5] = TaskAddress.ui8[1];
        Os_Stack[n][OS_STACKSIZE-6] = TaskAddress.ui8[2];
        //
        //                      hi  lo         hi  lo
        //zwei parameter in p0 = r25:r24 und p1 = r23:r22
        if(n)
        {
            Os_Stack[n][OS_STACKSIZE-32] = Os_TaskConfig[n-1].param0&0xff;
            Os_Stack[n][OS_STACKSIZE-33] = Os_TaskConfig[n-1].param0>>8;
            Os_Stack[n][OS_STACKSIZE-30] = (uint16)(Os_TaskConfig[n-1].param1)&0xff;
            Os_Stack[n][OS_STACKSIZE-31] = (uint16)(Os_TaskConfig[n-1].param1)>>8;
        }
        //
        Os_StackPointer[n] = &Os_Stack[n][OS_STACKSIZE-40];
    }
    Os_SemaphoreInit();
}





void  __attribute__ ((optimize("O2"))) Os_Idle(void)
{
#ifdef ERTK_DEBUG
    while(1)
{           //14+2 = 16 cycles pro loop -> 16MHz -> 1000 inc/ms
        cli();               //cli = 1clock
        ++Os_CurrentPerformanceCount;    //lds,lds,adiw,sts,sts = 5x2clocks
        sei();               //sei = 1clock
        oIDLEfast(1);      //2 cycles fuer 2xnop oder ein output bit setzen
    }                     //rjmp = 2clocks
#else
    while(1)
{
        set_sleep_mode(SLEEP_MODE_IDLE);
        sleep_enable();
        sei();
        oIDLE(1);
        sleep_cpu();
        sleep_disable();
    }
#endif
}

__attribute__ ((noinline)) void Os_ErrorHook(Os_ErrorHookType error)
{
    oIDLE(0);
    while(1); //this is the end...
}


void * Os_PushPopStackPointer; //speicher fuer stackpointer waehrend push/pop

//push pc, push r0, r0<-sreg, push r0..r31
#define Os_PushStack() \
{ \
        asm volatile (\
        "push   r0 \n"/*push r0*/ \
        "in r0, __SREG__ \n" \
        "cli\n" /*push sreg, push r1..r31 */\
        "push   r0\npush  r1\npush   r2\npush  r3\npush  r4\npush  r5\npush  r6\npush  r7\npush  r8\npush  r9\npush r10\npush   r11\npush   r12\npush   r13\npush   r14\npush   r15\npush   r16\npush   r17\npush   r18\npush   r19\npush   r20\npush   r21\npush   r22\npush   r23\npush   r24\npush   r25\npush   r26\npush   r27\npush   r28\npush   r29\npush   r30\npush   r31\n" \
        "in r0, __SP_L__ \n" \
        "sts Os_PushPopStackPointer, r0 \n" \
        "in r0, __SP_H__ \n" \
        "sts Os_PushPopStackPointer+1, r0 \n" \
       ); \
}

//pop r31 .. r0, sreg<-r0, pop r0
#define Os_PopStack() \
{ \
        asm volatile (\
        "lds r0, Os_PushPopStackPointer \n" \
        "out __SP_L__, r0 \n" \
        "lds r0, Os_PushPopStackPointer+1 \n" \
        "out __SP_H__, r0 \n" \
        /*pop r31..sreg*/ \
        "pop r31\npop   r30\npop    r29\npop    r28\npop    r27\npop    r26\npop    r25\npop    r24\npop    r23\npop    r22\npop    r21\npop    r20\npop    r19\npop    r18\npop    r17\npop    r16\npop    r15\npop    r14\npop    r13\npop    r12\npop    r11\npop    r10\npop     r9\npop     r8\npop     r7\npop     r6\npop     r5\npop     r4\npop     r3\npop     r2\npop     r1\npop     r0\n" \
        "out __SREG__, r0 \n" \
        "pop r0 \n"/*pop r0*/ \
        "sei \n" \
       ); \
}

void __attribute__ ((naked)) Os_Scheduler(void)
{ /* start der hoechstprioren ready task, oder idle task, oder der bueffel wenn alles scheitert ;) */
    Os_PushStack();
#ifdef ERTK_DEBUG
    //stack overflow check, stack pointer in Os_PushPopStackPointer uebergeben
    if(Os_PushPopStackPointer < (void *)&Os_Stack[Os_CurrentRunningTask][OS_STACKSIZE-ERTK_STACKLOWMARK])
    {
        Os_ErrorHook(OS_SYS_STACKOVERFLOW);
    }
#endif
    Os_StackPointer[Os_CurrentRunningTask] = Os_PushPopStackPointer;
    //
    if(TaskReadyList)
    { //da muss natuerlich immer was drinstehen ;)
        //do round robin bei mehreren mit gleicher prio
        register Os_TaskControlBlockType *p = TaskReadyList;
        while(p->TaskId != Os_CurrentRunningTask)
        {
            p = p->NextTask; //finde aktuelle task
            if(!p) break;
        }
        if(p)
        { //task stand noch in der ready liste
            //teste pri des nachfolgers
            if(p->NextTask != NULL)
            { //wenn es nachfolger gibt
                if(p->TaskPrio == p->NextTask->TaskPrio)
                { //schalte weiter wenn nachfolger prio genauso ist
                    Os_CurrentRunningTask = p->NextTask->TaskId;
                }
                else
                {
                    Os_CurrentRunningTask = TaskReadyList->TaskId;
                }
            }
            else
            { //sonst nimm den ersten in der liste, der muss per definition die gleiche prio haben da wir bei kleineren prios gar nicht suchen !
                Os_CurrentRunningTask = TaskReadyList->TaskId;
            }
        }
        else
        {
            Os_CurrentRunningTask = TaskReadyList->TaskId; //nimm das erstbeste aus der ready liste ;)
        }
    }
    else
    {
        Os_ErrorHook(OS_SYS_NOTASK);
    }
    //
    Os_PushPopStackPointer = Os_StackPointer[Os_CurrentRunningTask];
    Os_PopStack();
    sei();
    asm volatile ("ret");
}


void Os_Start(void)
{ /* start der hoechstprioren ready task, notfalls idle */
    if(TaskReadyList) Os_CurrentRunningTask = TaskReadyList->TaskId;
    else Os_ErrorHook(OS_SYS_NOTASK);
    //
    Os_SystemStarted = 1;
    Os_PushPopStackPointer = Os_StackPointer[Os_CurrentRunningTask];
    //#if TARGET_CPU == SAMD21J18A
    //  asm volatile(  "movs r0, #0 \n"); /* privilegierte ausfuehrung und nehme den msp stack. */
    //  asm volatile(  "msr CONTROL, r0 \n");
    //#endif
    Os_PopStack();
    asm volatile("ret");
    //#elif TARGET_CPU == SAMD21J18A
    //#endif
    Os_ErrorHook(OS_SYS_UNKNOWN); //hier duerfen wir nie wieder ankommen, wenn die verwaltungsstrukturen i.O. sind
}

/* Prinzip der Ready Liste:
 * pTskRdy ist nie 0 und zeigt zumindest auf den Os_TaskControlBlock[0]==idle task.
 * wenn eine task bereit wird geschieht dies durch einhaengen in die liste
 * und zwar vor der ersten task mit niedrigerer prio.
 * zunaechst also pTskRdy->Os_TaskControlBlock[0]->0 wird pTskRdy->Os_TaskControlBlock[x]->Os_TaskControlBlock[0]->0 (falls Os_TaskControlBlock[x].TaskPrio>0)
 * wenn Os_TaskControlBlock[y].TaskPrio > Os_TaskControlBlock[x].TaskPrio dann pTskRdy->Os_TaskControlBlock[y]->Os_TaskControlBlock[x]->Os_TaskControlBlock[0]->0
 * sonst pTskRdy->Os_TaskControlBlock[x]->Os_TaskControlBlock[y]->0
 * usw.
 * Der scheduler sucht immer ob Os_TaskControlBlock[Os_CurrentRunningTask] noch in der liste steht.
 *   wenn nein, nimmt er den ersten eintrag der liste zum start (highest prio task always runs)
 *   wenn ja, schaut er nach ob der nachfolger gleiche pri hat. (round robin)
 *     wenn ja nimmt er diesen (weiterschaltung in prio ebene)
 *     wenn nein nimmt er den ersten eintrag der liste (das ist ja immer der mit der hoechsten prio)
 *
 *  Ausgangszustand nur mit idle task:
 *
 *  pTsk-->Os_TaskControlBlock[0]
 *         NextTask=0
 *         PreviousTask=0
 *
 *  task 2 dazu mit Os_InsertTask(this=&Os_TaskControlBlock[0], new=&Os_TaskControlBlock[2])
 *
 *         new          this
 *  pTsk-->Os_TaskControlBlock[2]       Os_TaskControlBlock[0]
 *         NextTask------->NextTask=0
 *         PreviousTask=0 <--PreviousTask
 *
 *  task 1 dazu mit Os_InsertTask(this=&Os_TaskControlBlock[0], new=&Os_TaskControlBlock[1])
 *
 *                      new          this
 *  pTsk-->Os_TaskControlBlock[2]       Os_TaskControlBlock[1]       Os_TaskControlBlock[0]
 *         NextTask------->NextTask------->NextTask=0
 *         PreviousTask=0 <--PreviousTask<-----PreviousTask
 *
 */
static __inline__ void Os_InsertTask(Os_TaskControlBlockType *pthis, Os_TaskControlBlockType *newone)
{ //newone vor pthis eintragen
    if(!pthis || !newone) Os_ErrorHook(OS_SYS_NULLPTR);
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if(pthis->PreviousTask == NULL)
        { //der erste Os_TaskControlBlock
            TaskReadyList = newone;
            newone->PreviousTask = NULL;
        }
        else { //Os_TaskControlBlock in der kette
            pthis->PreviousTask->NextTask = newone;
            newone->PreviousTask = pthis->PreviousTask;
        }
        newone->NextTask = pthis;
        pthis->PreviousTask = newone;
    }
}

static __inline__ void Os_RemoveTask(Os_TaskControlBlockType *pthis)
{ //den node pthis austragen
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if(pthis == NULL || pthis == &Os_TaskControlBlock[0])
        { //0 oder idle task
            Os_ErrorHook(OS_SYS_NULLPTR);
        }
        if(pthis->PreviousTask == NULL)
        { //es ist der erste block auf den pTskReady zeigt
            if(pthis->NextTask == NULL)
            { //und er hat keinen nachfolger den man vorziehen kann
                Os_ErrorHook(OS_SYS_NULLPTR);
            }
            TaskReadyList = pthis->NextTask; //nachfolger einhaengen
            pthis->NextTask->PreviousTask = NULL;
        }
        else { //mittendrin in der kette
            pthis->PreviousTask->NextTask = pthis->NextTask;
            pthis->NextTask->PreviousTask = pthis->PreviousTask;
        }
        pthis->NextTask = NULL; //diesen block aus verkettung nehmen
        pthis->PreviousTask = NULL;
    }
}

Os_TaskIdType Os_GetTaskId(void)
{ //holen der eigenen task id
    return Os_CurrentRunningTask;
}

//Verwaltung der ready list mit einfuegen/ausfuegen in der reihenfolge der prio
void Os_SetReady(Os_TaskIdType TaskId)
{
    if(!TaskId)
    {
        Os_ErrorHook(OS_SYS_NULLPTR); //idle task ist immer ready
    }

    if(Os_TaskControlBlock[TaskId].NextTask || Os_TaskControlBlock[TaskId].PreviousTask) Os_ErrorHook(OS_SYS_VERIFY); //war gar nicht suspendiert
    //
    Os_TaskControlBlockType *pthis = TaskReadyList;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        Os_TaskControlBlock[TaskId].TaskTimer = 0; //timer loeschen damit er nicht spaeter nochmal startet
        do { //pthis->hoechstprioren Os_TaskControlBlock oder idle
            if(Os_TaskControlBlock[TaskId].TaskPrio > pthis->TaskPrio || pthis->NextTask == NULL)
            { //neuer eintrag hat hoehere prio oder es gibt keinen nachfolger
                Os_InsertTask(pthis, &Os_TaskControlBlock[TaskId]);
                break;
            }
        } while((pthis = pthis->NextTask));
    }
}

//pTskRdy->Os_TaskControlBlockx->Os_TaskControlBlocky->0 es muss immer mindestens ein Os_TaskControlBlock (idle) in der liste bleiben
void Os_SetSuspended(Os_TaskIdType TaskId)
{ //Os_TaskControlBlock[TaskId] aus der ready list austragen
    if(!TaskId)
    {
        Os_ErrorHook(OS_SYS_NULLPTR); //idle task darf nicht suspendiert werden
    }

    if(!Os_TaskControlBlock[TaskId].PreviousTask && !Os_TaskControlBlock[TaskId].NextTask) Os_ErrorHook(OS_SYS_VERIFY); //war nicht in ready list
    Os_TaskControlBlockType *pthis = TaskReadyList;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        do {
            if(pthis->TaskId == TaskId)
            {
                Os_RemoveTask(pthis);
                break;
            }
        } while((pthis = pthis->NextTask));
    }
}

void Os_SuspendAndWait(uint8 timeout)
{
    if(timeout)
    { //sonst klinkt sich die task in einem wait_until() fuer immer aus
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        { //14+2 = 16 cycles pro loop -> 16MHz -> 1000 inc/ms
            if(Os_TaskControlBlock[Os_CurrentRunningTask].TaskTimer)
            {
                Os_ErrorHook(OS_SYS_UNKNOWN);
            }

            Os_TaskControlBlock[Os_CurrentRunningTask].TaskTimer = timeout;
            Os_SetSuspended(Os_CurrentRunningTask);
            Os_Scheduler();
        }
#ifdef ERTK_DEBUG
        else Os_ErrorHook(OS_SYS_NULLTIMER);
#endif
    }
}


void Os_SleepMs(uint16 ms)
{ //macht nur auf avr sinn
    while(ms)
    {
        if(ms>255)
        {
            Os_SuspendAndWait(255);
            ms -= 255;
        }
        else {
            Os_SuspendAndWait(ms);
            ms = 0;
        }
    }
}

static inline __attribute__ ((__always_inline__)) uint8 Os_ExchangeContent(uint8 volatile *p, uint8 x)
{
    //wenn die (AVR) cpu xch kennt:
    // __asm volatile ("xch %a1,%0" : "+r"(x) : "z"(p) : "memory");
    register uint8 tmp;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        tmp = *p;
        *p = x;
        return tmp;
    }
}

void Os_GetSema(Os_SemaphoreType semaid)
{ /* Warten bis Semaphore frei ist und danach besetzen */
    if(semaid > OS_MAX_NUM_OF_SEMAPHORE)
    {
        Os_ErrorHook(OS_SYS_UNKNOWN);
    }
    while(Os_ExchangeContent(&Os_Semaphore[semaid], 1))
    { /* >0 = sema blockiert */
        sei();
        Os_SuspendAndWait(1);
    }
}

void Os_FreeSema(Os_SemaphoreType semaid)
{
    if(semaid > OS_MAX_NUM_OF_SEMAPHORE)
    {
        Os_ErrorHook(OS_SYS_UNKNOWN);
    }
    Os_ExchangeContent(&Os_Semaphore[semaid], 0) ;
}

void Os_SemaphoreInit(void)
{
    static uint8 n;
    for(n = 0; n < OS_MAX_NUM_OF_SEMAPHORE; n++)
    {
        Os_Semaphore[n] = 0; /* ff=keine task wartend */
    }
}


/*
  Ein Prozess holt den aktuellen Zaehlerstand in ms mit Os_GetTimer().
  Wenn er in 10ms erneut starten will, kann er folgendes tun:
    uint8 now = Os_GetTimer();
    ...do something
    eRTK_WaitUntil(now+10);
 */
union
{
    volatile uint8 timer8[4];
    volatile uint16 timer16[2];
    volatile uint32 timer32;
} eRTK_m_timer;

uint8 Os_GetTimer8(void)
{ //256ms bis overflow
    return eRTK_m_timer.timer8[0];
}

uint16 Os_GetTimer16(void)
{ //wenn 256ms nicht reichen
    register uint16 val;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    val = eRTK_m_timer.timer16[0];
    return val;
}

void Os_WaitUntil(uint8 then)
{
    Os_SuspendAndWait(then-Os_GetTimer8());
}

//setzen der prioritaet fuer eine task
//TaskId=0 setze die eigene prioritaet oder sonst die einer anderen
//prio=neue prioritaet
//schedule_immediately wenn true dann wird sofort eine neue Prozesstabelle ermittelt und der hoechstpriore prozess gestartet
void Os_ChangeTaskPriority(Os_TaskIdType TaskId, Os_TaskPrioType prio, Os_ScheduleImmediatyleType schedule)
{
    if(TaskId == 0)
    {
        TaskId = Os_CurrentRunningTask;
    }

    if(TaskId < OS_MAX_NUMBER_OF_TASKS)
    {
        Os_TaskControlBlock[Os_CurrentRunningTask].TaskPrio = prio;
        if(schedule == OS_TASK_SCHEDULE_IMMEDIATELY)
        {
            Os_Scheduler();
        }
    } else Os_ErrorHook(OS_SYS_VERIFY); //die task id ist unbekannt
}

//damit im irq alle register gesichert werden
__inline__ void __attribute__ ((always_inline)) Os_Timertick(void)
{
    uint16 hibyte, lobyte;
    uint32 ticktimer;

    oIDLE(0);

    /* TODO: Dangerous? Won't increment if Os_Ticks >= 65535 */
    if(Os_Ticks < UINT_MAX)
    {
        ++Os_Ticks;
    }

    ticktimer  =  (0xFFFF0000 & (eRTK_m_timer.timer16[1] << 8)) && (eRTK_m_timer.timer16[0] & 0xFFFF);
    ticktimer++;
    eRTK_m_timer.timer16[0] = ticktimer & 0xFFFF;
    eRTK_m_timer.timer16[1] = (ticktimer >> 8) & 0xFFFF;
    register Os_TaskControlBlockType *p = Os_TaskControlBlock;
    //timer service
    for(register uint8 n = 0; n < OS_MAX_NUMBER_OF_TASKS+1; n++)
    {
        if(p->TaskTimer)
        {
            if(!--p->TaskTimer)
            {
                Os_SetReady(p->TaskId);
            }
        }
        ++p;
    }
#ifdef ERTK_DEBUG
    if(Os_SystemStarted && Os_Ticks>eRTK_STARTUP_MS)
    { //solange geben wir dem system zum hochlauf
        Os_PerformanceCounter[ (Os_PerformanceCounterIndex++)/*&0x0f*/ ] = Os_CurrentPerformanceCount;
        if(Os_CurrentPerformanceCount < 1)
        {
            ++Os_OverloadCounter;
            if(eRTK_MAX_OVERLOAD && Os_OverloadCounter>eRTK_MAX_OVERLOAD) Os_ErrorHook(OS_SYS_OVERLOAD);
        }
        else Os_OverloadCounter = 0;
        Os_CurrentPerformanceCount = 0;
    }
#endif
    if(Os_SystemStarted)
    {
        Os_Scheduler();
    }
}


ISR(TIMER0_OVF_vect)
{ //1kHz timer
    TCNT0 = (uint8)(-1*OS_TIMER_PRELOAD);
    Os_Timertick();
}

static void timer0_init(void)
{
    TCCR0B = /*(1 << CS12)|*/(1 << CS01)|(1 << CS00); //prescaler CLK/64
    TIMSK0 |= (1 << TOIE0);
    TCNT0 = (uint8)(-1*OS_TIMER_PRELOAD);
}

void Os_TimerInit(void)
{
    timer0_init();
}




