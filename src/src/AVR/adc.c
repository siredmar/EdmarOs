/*
 * adc.c
 *
 * Created: 27.05.2015 12:57:21
 *  Author: er
 */ 
#include "../Os/Os.h"
#include "adc.h"

#if defined (__AVR_ATmega2560__)

/*
  Der ADC wird von einer Sequencer Liste gesteuert.
  Die Sequenz wird vom System Timer angestossen und lauft bis zum Ende durch.
  In der Liste stehen Bloecke vom Typ <tadu>, jeder steuert einen separaten ADC Kanal.
  In jedem Block wird ein Zaehler erhoeht und falls dieser den Scalerwert erreicht startet dieser Kanal.
  Im anderen Falle wird weitergeschaltet bis zum Ende der Liste.
  Wird ein arbeitsbereiter Block gefunden wird der zugehoerige Kanal zur Wandlung gestartet.
  Nach Ablauf jeder Wandlung startet die "End of Conversion" Interrupt Prozedur und schaltet auf den naechsten Block weiter.
  Am Ende eines Durchlaufes wird dann im Timerinterrupt die Liste neu gestartet.

  Bei 125k adc Takt dauert eine Wandlung etwa 104us.
  Wenn der Sequencer also alle 1000us==1/1000Hz gestartet wird so sind 9 Wandlungen moeglich.    

  Der erlaubte Takt des ADC liegt max. bei 200kHz (2Bit) oder bis 1MHz bei verringerter Genauigkeit (3Bit)
  bei F_CPU von 16MHz gibt es folgenden Zusammenhang:
  Scaler  ADC-Clock   Conversion Time   ADPS2 ADPS1 ADPS0
  /128    125k        104us             1     1     1
  /64     250k         52us             1     1     0
  /32     500k         26us             1     0     1
  /16     1M           13us             1     0     0

*/



/*
messzeitpunkte bei verschiedenen scaler einstellungen und anzahl messungen pro zeitpunkt:

time[ms]  scaler=1  scaler=2    scaler=3  scaler=4  scaler=5  scaler=6  scaler=7  scaler=8  scaler=9  scaler=10  cnt  gesamtdauer
0         1         0           0         0         0         0         0         0         0         0           1     104us
1         1         1           0         0         0         0         0         0         0         0           2     208us
2         1         0           1         0         0         0         0         0         0         0           2     208us
3         1         1           0         1         0         0         0         0         0         0           3     312us
4         1         0           0         0         1         0         0         0         0         0           2     208us
5         1         1           1         0         0         1         0         0         0         0           4     416us
6         1         0           0         0         0         0         1         0         0         0           2     208us
7         1         1           0         1         0         0         0         1         0         0           4     416us
8         1         0           1         0         0         0         0         0         1         0           3     312us
9         1         1           0         0         1         0         0         0         0         1           4     416us
10        1         0           0         0         0         0         0         0         0         0           1     104us
11        1         1           1         1         0         1         0         0         0         0           5     520us
12        1         0           0         0         0         0         0         0         0         0           1     104us
13        1         1           0         0         0         0         1         0         0         0           3     312us
14        1         0           1         0         1         0         0         0         0         0           3     312us
15        1         1           0         1         0         0         0         1         0         0           4     416us
16        1         0           0         0         0         0         0         0         0         0           1     104us
17        1         1           1         0         0         1         0         0         1         0           5     520us
18        1         0           0         0         0         0         0         0         0         0           1     104us
19        1         1           0         1         1         0         0         0         0         1           5     520us
*/

static tadc * padc_active;  //aktuell wandelnder adc kanal

ISR( ADC_vect ) { //adc interrupt
  uint8 m_ready=0;
  if( padc_active<adc_cntrl+ANZ_ADC ) {
    padc_active->value=ADCW;
    uint8 tid=padc_active->tid;
    if( tid ) { //falls ein task suspendiert wartet
      padc_active->tid=0; //event loeschen
      m_ready=1; 
      eRTK_SetReady( tid ); //dann aktivieren 
     }
    //weiterschalten bis zum naechsten bereiten block oder ende der liste
    while( 1 ) {
      if( ++padc_active>=adc_cntrl+ANZ_ADC ) { //listenende erreicht
        padc_active=adc_cntrl;
        break;
       }
      else {
        if( ++( padc_active->cnt ) >= padc_active->scaler ) { //adc kanal starten
          padc_active->cnt=0;
          ADMUX=( padc_active->mux&0x07 ) | padc_active->ref; 
          if( padc_active->mux<=7 ) ADCSRB&=~( 1<<MUX5 );
          else ADCSRB|=( 1<<MUX5 );
          ADCSRA|=( 1<<ADSC );
          break;
         }
       }
     }
   }
  else padc_active=adc_cntrl;
  if( m_ready ) eRTK_scheduler(); //scheduling durchfuehren falls eine task aktiviert wurde
 }

uint8 adc_sequencer( void ) { //soll im system timer interrupt DISABLED aufgerufen werden
  if( padc_active==adc_cntrl ) {
    //finde startklaren adc kanal
    while( padc_active < adc_cntrl+ANZ_ADC ) {
      if( ++( padc_active->cnt ) >= padc_active->scaler ) { //adc kanal starten
        padc_active->cnt=0;
        ADMUX=( padc_active->mux&0x07 ) | padc_active->ref;
        if( padc_active->mux<=7 ) ADCSRB&=~( 1<<MUX5 );
        else ADCSRB|=( 1<<MUX5 );
        ADCSRA|=( 1<<ADSC );
        break;
       }
      ++padc_active;
     }
    return !0;
   }
  else return 0;
 }

void adc_init( void ) { //beim hochlauf aufzurufen
  ADMUX = 0;  //Kanal waehlen 0-7
  ADMUX |= /*(1<<REFS1) |*/ (1<<REFS0); // avcc Referenzspannung nutzen
  ADCSRA = (1<<ADIE)|(1<<ADEN)|(1<<ADPS2)|(1<<ADPS1)|(1<<ADPS0); //Frequenzvorteiler setzen auf %128, ADC aktivieren, int aktivieren
  padc_active=adc_cntrl;
 }


uint16 adc_get( uint8 mux ) { //holen des aktuellen wandlungswertes
  uint16 val=-1;
  register tadc * padc=adc_cntrl;
  while( padc<adc_cntrl+ANZ_ADC ) {
    if( padc->mux==mux ) {
      ATOMIC_BLOCK( ATOMIC_RESTORESTATE	) {
        val=padc->value;
        break;
       }
     }
    ++padc;
   }
  return val;
 }

uint16 adc_wait( uint8 mux ) { //warten bis auf diesem kanal eine neue messung vorliegt und dann liefern
  register tadc * padc=adc_cntrl;
  while( padc<adc_cntrl+ANZ_ADC ) {
    if( padc->mux==mux ) { //dieser kanal
      uint8 tid=eRTK_GetTaskId();
      uint8 m_sc;
      ATOMIC_BLOCK( ATOMIC_RESTORESTATE ) {
        padc->tid=tid;
        m_sc=padc->scaler;
        if( !m_sc ) {
          padc->scaler=1; //damit er im naechsten takt wandelt
         }
        eRTK_SetSuspended( tid );
        eRTK_scheduler();
       }
      padc->scaler=m_sc;
      return padc->value;
     }
    ++padc;
   }
  Os_ErrorHook( OS_SYS_UNKNOWN );
  return 0;
 }

#endif
