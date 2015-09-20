#ifndef OS_CFG_H_
#define OS_CFG_H_
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <Std_Types.h>


#if TARGET_CPU == ATMEGA2560
#include <avr/io.h>
#include <util/atomic.h>
#include <avr/interrupt.h>
#endif

#if TARGET_CPU == ATMEGA2560
#define F_CPU       (16000000ul)
#endif

//#define ERTK_DEBUG                     //gibt ein paar mehr infos zum debugging, kostet aber laufzeit

#define OS_FREQUENCY_HZ            (1000u)         //system tick rate in HZ

#define TARGET_CPU ATMEGA2560

#define OS_MAX_NUMBER_OF_TASKS    (6u)  //anzahl definierter prozesse

#define OS_STACKSIZE     (256u)         //stack in byte bei 8 bit bzw. word bei 32 bit maschinen
#define OS_STACKLOWMARK  (240u)         //wenn debug aktiviert wird auf unterschreitung dieser marke geprueft


//#define IDLELED                        //led in idle task ansteuern oder nichts machen

//ueberwachungsfunktionen
#define eRTK_STARTUP_MS       (0u)         //solange geben wir allen tasks zusammen zum hochlauf bis wir overload pruefen
#define eRTK_MAX_OVERLOAD     (0u)         //und dies ist die max. erlaubte zahl an overload phasen bevor deadbeef() aufgerufen wird


#if TARGET_CPU == ATMEGA2560
#define OS_TIMER_PRESCALER       64ul         //verteiler timer clock
#define OS_TIMER_PRELOAD ( F_CPU/( OS_TIMER_PRESCALER*OS_FREQUENCY_HZ ) )
#if (OS_TIMER_PRELOAD>254) /* weil es ein 8 bit timer ist */
#error eRTK:Systematischer 8 Bit Overflow Fehler im SYSTIMER !
#endif
#endif

#if TARGET_CPU == ATMEGA2560
#ifdef IDLELED
#ifndef sbi
#define sbi(port,nr) (port|=_BV(nr))
#define cbi(port,nr) (port&=~_BV(nr))
#endif
//diagnose led fuer idle anzeige, wenn gewuenscht
#define oIDLE(a)          { (a) ? sbi( PORTE, PE2 ) : cbi( PORTE, PE2 ); sbi( DDRE, PE2 ); }
#define oIDLEfast(a)      { (a) ? sbi( PORTE, PE2 ) : cbi( PORTE, PE2 ); }
#else
//sonst zwei nops damit das timing passt
#define oIDLE(a)          /**/
#define oIDLEfast(a)      asm volatile ( "nop\n nop\n" );
#endif
#endif



#endif
