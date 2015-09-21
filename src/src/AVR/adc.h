/*
 * adc.h
 *
 * Created: 27.05.2015 14:45:41
 *  Author: er
 */ 


#ifndef ADC_H_
#define ADC_H_

#include "Platform_Types.h"

#if defined (__AVR_ATmega2560__)

#define ADC_HZ1000         1
#define ADC_HZ100         10
#define ADC_HZ10         100
#define ADC_HZ5          200


#define ANZ_ADC 2
typedef struct {
  uint8 mux;       //0..15 beim atmega256x
  uint8 ref;       //wert fuer referenz
  uint8 scaler;    //0=nie, 1=jeden lauf, 2=jeden 2ten lauf, usw
  uint8 cnt;       //aufwaertszaehler
  uint16 value;    //adc messwert
  uint8 tid;       //wenn eine task gestartet werden soll
 } tadc;
extern tadc adc_cntrl[ANZ_ADC];

uint8 adc_sequencer( void );    //soll im system timer interrupt aufgerufen werden
uint16 adc_get( uint8 mux );  //holen des aktuellen wandlungswertes
uint16 adc_wait( uint8 mux ); //warten bis auf diesem kanal eine neue messung vorliegt und dann liefern
void adc_init( void );            //beim hochlauf aufzurufen

#endif

#endif /* ADC_H_ */
