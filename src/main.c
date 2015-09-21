/**
* COPYRIGHT : GNU GPL V2 -
*
* This file contains the main file of the testbench
*
* Author: Armin Schlegel
* based on project eRTK (https://github.com/eRTK/eRTK)
* Creation date: 2015-09-21
* Last change: 2015-09-21
*
* Moduleversion: 0.0.1
****************************************************************************** */

/*
  Dies ist ein Beispiel der Anwendung und ein Test des Betriebssystems auf AVR (atmega256).
  Es werden 5 Tasks definiert, eine mit hoher Prioritaet und 4 mit niedrigerer Prioritaet.
  Die Task mit hoher Prio macht nichts anderes als zyklisch alle 10ms wieder zu starten, 
  in der Zwischenzeit suspendiert sie sich.
  Wenn man damit z.B. einen Portpin toggelt kann man den Jitter des erreichbaren Echtzeitverhaltens prüfen.
  Es sollten weniger als etwa 100 CPU Zyklen also bei 16MHz Takt 100/16 us Jitter erreichbar sein.
  Die anderen 4 Tasks nutzen den selben Code (code sharing) lediglich eine Variable indiziert die Instanz der Task
  welche aktuell laeuft, dies wird gleichzeitig in den Datenfeldern als Index genutzt.
  Initial wird ueber den Startparameter param0 dieser Index uebergeben.
  Zusammengefasst: 
    tskHighPrio wird alle 10ms kurz aktiv und beendet sich gleich wieder.   
	tskUART startet viermal, jede Instanz nimmt sich einen seriellen Port (UART)
	und sendet 1..n Zeichen, wenn eine Verbindung Rx<->Tx existiert wird auch etwas empfangen.
	Ansonsten entstehen time outs beim Warten auf die Zeichen.
	Diese Prozesse bestehen aus kurzen Phasen von CPU Aktivitaet und relativ langen Wartezeiten auf die Peripherie.
	Wenn das alles abgearbeitet ist wird die restliche CPU Zeit in der Idle Task verbracht.
	Hier kommen die perfcounter ins Spiel.
	Die Idle Task enthaelt eine Anzeige fuer die CPU Restzeit, 
	also wieviel Anteil die Summe aller Tasks von der verfügbaren Rechenzeit nimmt.
	Im Leerlauf  -> 1000
	Bei 50% Last ->  500
	Überlast     ->    0
	
	Mit diesem Beispiel erreiche ich um 930 Zaehler, also etwa 7% der Zeit ist die CPU in den Tasks unterwegs, 
	der Rest ist Leerlauf.
	Das bei einer Interruptbelastung von 1000Hz vom Systemtimer und 8 seriellen Sende- und Empfangsinterrupts
	mit jeweils 2000Hz bei 19200Bd und 3x1000Hz bei 9600Bd ein gutes Ergebnis ! 
*/

#if defined (__AVR_ATmega2560__)
  #include <avr/io.h>
  #include "AVR/uart.h"
//  #include "AVR/adc.h"
#endif

#include <Std_Types.h>
#include <stdio.h>
#include "Os/Os.h"
#include "Os/Os_Cfg.h"

void tskHighPrio( uint16 param0, void *param1 )
{ //prio ist 20
    while( 1 )
    { //kurze aktivitaet auf prio 20 muss alles auf prio 10 sofort unterbrechen
        Os_SleepMs( 10 );
    }
}

//tskUART wird viermal gestartet, param0 ist 0..3
//es wird derselbe programm code genutzt, anhand des param0 wird UART0..UART3 in jeder task genutzt
//und 1 byte, 16 byte == sendepuffergroesse oder 20 byte gesendet und wieder zurueckgelesen
//also eine sog. loop back gemacht.
//wird nichts empfangen so gibt es time outs.
//wird alles empfangen so geht es unverzueglich weiter in der schleife.
//ist gedacht als belastungstest der taskwechselmechanismen und datenstrukturen.
void tskUART( uint16 param0, void *param1 )
{ //prio ist 10
    while( 1 )
    { //com test
        char buffer[50];
        uint8 rec;
        tUART h=open( UART0+param0 ); //das klappt weil UART0+1=UART1, usw.
        while( h )
        { //bei einer loop back verbindung wird RX mit TX verbunden und es laeuft ohne time out
            read( h, NULL, 0, 0 ); //clear rx buffer
            write( h, "1", 1 ); //schreibe ein zeichen auf die leitung
            rec=read( h, buffer, 1, 100 ); //lies ein zeichen mit 100ms time out
            write( h, "abcdef", 6 );
            rec=read( h, buffer, 6, 100 );
            write( h, "0123456789ABCDEFGHIJ", 20 ); //hier ist der auszugebende string laenger als der interne puffer, es kommt ein spezieller mechanismus zum tragen, der abwartet bis der sendepuffer leer ist
            rec=read( h, buffer, 16, 100 ); //hier muss timeout entstehen da der empfangspuffer nur auf 16 zeichen eingestellt ist und wir nicht rechtzeitig auslesen koennen bevor ein overflow entsteht
        }
    }
}


int main( void )
{
    Os_Init();
    Os_TimerInit();
    //  adc_init();
    Os_Start();
    Os_ErrorHook( OS_SYS_UNKNOWN ); //hier duerfen wir nie wieder ankommen, wenn die verwaltungsstrukturen i.O. sind
}

