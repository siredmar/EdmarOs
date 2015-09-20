/*
 * uart.h
 *
 * Created: 04.05.2015 15:52:50
 *  Author: er
 */ 
#if defined (__AVR_ATmega2560__)

#include <util/atomic.h>
#include "../Os/Os.h"

#ifndef UART_H_
#define UART_H_


//uart port bitraten NUR DANN definieren wenn port gebraucht wird spart flash speicher
#define UART0_BAUD 19200lu
#define UART1_BAUD 9600lu
#define UART2_BAUD 9600lu
#define UART3_BAUD 9600lu


// Berechnungen z.T. zitiert aus www.microcontroller.net Forum
#define ROLLOVER( x, max )	x = ++x >= max ? 0 : x          //count up and wrap around

#ifdef UART0_BAUD
#define RX0_SIZE 16
#define TX0_SIZE 16
#define UBRR0_VAL ((F_CPU+UART0_BAUD*8)/(UART0_BAUD*16)-1)  //clever runden
#define BAUD0_REAL (F_CPU/(16*(UBRR0_VAL+1)))               //Reale Baudrate
#define BAUD0_ERROR ((BAUD0_REAL*1000)/UART0_BAUD)          //Fehler in Promille, 1000 = kein Fehler.
#if ((BAUD0_ERROR<990) || (BAUD0_ERROR>1010))
  #error Systematischer Fehler der Baudrate0 groesser 1% und damit zu hoch!
#endif

#endif

#ifdef UART1_BAUD
#define RX1_SIZE 16
#define TX1_SIZE 16
#define UBRR1_VAL ((F_CPU+UART1_BAUD*8)/(UART1_BAUD*16)-1)  //clever runden
#define BAUD1_REAL (F_CPU/(16*(UBRR1_VAL+1)))               //Reale Baudrate
#define BAUD1_ERROR ((BAUD1_REAL*1000)/UART1_BAUD)          //Fehler in Promille, 1000 = kein Fehler.
#if ((BAUD1_ERROR<990) || (BAUD1_ERROR>1010))
  #error Systematischer Fehler der Baudrate1 groesser 1% und damit zu hoch!
#endif
#endif

#ifdef UART2_BAUD
#define RX2_SIZE 16
#define TX2_SIZE 16
#define UBRR2_VAL ((F_CPU+UART2_BAUD*8)/(UART2_BAUD*16)-1)  //clever runden
#define BAUD2_REAL (F_CPU/(16*(UBRR2_VAL+1)))               //Reale Baudrate
#define BAUD2_ERROR ((BAUD2_REAL*1000)/UART2_BAUD)          //Fehler in Promille, 1000 = kein Fehler.
#if ((BAUD2_ERROR<990) || (BAUD2_ERROR>1010))
  #error Systematischer Fehler der Baudrate2 groesser 1% und damit zu hoch!
#endif
#endif

#ifdef UART3_BAUD
#define RX3_SIZE 16
#define TX3_SIZE 16
#define UBRR3_VAL ((F_CPU+UART3_BAUD*8)/(UART3_BAUD*16)-1)  //clever runden
#define BAUD3_REAL (F_CPU/(16*(UBRR3_VAL+1)))               //Reale Baudrate
#define BAUD3_ERROR ((BAUD3_REAL*1000)/UART3_BAUD)          //Fehler in Promille, 1000 = kein Fehler.
#if ((BAUD3_ERROR<990) || (BAUD3_ERROR>1010))
  #error Systematischer Fehler der Baudrate3 groesser 1% und damit zu hoch!
#endif
#endif

typedef enum {
#ifdef UART0_BAUD
  UART0=1,
#endif
#ifdef UART1_BAUD
  UART1=2,
#endif
#ifdef UART2_BAUD
  UART2=3,
#endif
#ifdef UART3_BAUD
  UART3=4,
#endif
 } tUART;

tUART open( tUART port );
//read liest am port maximal nbytes zeichen in den puffer ein und wartet maximal timeout ms
//read gibt die anzahl eingelesener zeicehn im puffer zurueck
uint8 read( tUART port, void * puffer, uint8 nbytes, uint8 timeout );
void write( tUART port, void * puffer, uint8 nbytes );

#endif
#endif /* UART_H_ */
