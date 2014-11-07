#include <JeeLib.h>
#include <avr/sleep.h>
#define DEBUG 1
#define RF69_COMPAT 1
#define TRANSMIT_DELAY 4000 /* 2.15x this number is the delay. 
  Shit, I don't know. Synchronise the transmit delay with the receiver, if you
  feel like doing arithmetic to compensate for hardware problems and you
  want to know when you're out of battery or you forgot to turn the fuckin' thing on.*/

struct {
    int sequence; // a counter
    int hallEffectValue; // light sensor
    int temperature; // from the TMP37
    int battVoltage;  // battery voltage
} payload;

MilliTimer timer;

// This power-saving code was shamelessly stolen from the rooms.pde sketch,
// see http://code.jeelabs.org/viewvc/svn/jeelabs/trunk/jeemon/sketches/rooms/

EMPTY_INTERRUPT(WDT_vect); // just wakes us up to resume

static void watchdogInterrupts (char mode) {
    MCUSR &= ~(1<<WDRF); // only generate interrupts, no reset
    cli();
    WDTCSR |= (1<<WDCE) | (1<<WDE); // start timed sequence
    WDTCSR = mode >= 0 ? bit(WDIE) | mode : 0;
    sei();
}

static void lowPower (byte mode) {
    // prepare to go into power down mode
    set_sleep_mode(mode);
    // disable the ADC
    byte prrSave = PRR, adcsraSave = ADCSRA;
    ADCSRA &= ~ bit(ADEN);
    PRR |=  bit(PRADC);
    // zzzzz...
    sleep_mode();
    // re-enable the ADC
    PRR = prrSave;
    ADCSRA = adcsraSave;
}

static byte loseSomeTime (word msecs) {
    // only slow down for periods longer than the watchdog granularity
    if (msecs >= 16) {
        // watchdog needs to be running to regularly wake us from sleep mode
        watchdogInterrupts(0); // 16ms
        for (word ticks = msecs / 16; ticks > 0; --ticks) {
            lowPower(SLEEP_MODE_PWR_DOWN); // now completely power down
            // adjust the milli ticks, since we will have missed several
            extern volatile unsigned long timer0_millis;
            timer0_millis += 16;
        }
        watchdogInterrupts(-1); // off
        return 1;
    }
    return 0;
}

// End of new power-saving code.

void setup() {

  Serial.begin(57600);
  Serial.print("\n[propaneDemo]");
    rf12_initialize(3, RF12_915MHZ, 212); // node 3, 915MHz, net group 212915
    analogReference(INTERNAL); // use the 1.1v internal reference voltage
      Sleepy::loseSomeTime(100);  //uC to sleep
}

void loop() {
    // spend most of the waiting time in a low-power sleep mode
    // note: the node's sense of time is no longer 100% accurate after sleeping
    rf12_sleep(RF12_SLEEP);          // turn the radio off //RF69 compatible?
   //  Sleepy::loseSomeTime(timer.remaining()); // go into a (controlled) comatose state
    while (!timer.poll(waitTime))
        Sleepy::loseSomeTime(timer.remaining()); // go into a (controlled) comatose state
        lowPower(SLEEP_MODE_IDLE);  // still not running at full power
    digitalWrite(5, HIGH); // enable the power pin to read
    payload.sequence++;
    pinMode(A0, INPUT); // for temperature
    pinMode(A3, INPUT); // for voltage
    pinMode(A1, INPUT); // for hall effect sensor
    payload.battVoltage = analogRead(A3)*10;
    payload.hallEffectValue = analogRead(A1);
    payload.temperature = analogRead(A0)*100;
    digitalWrite(5, LOW); // got our result, turn off power

 #ifdef DEBUG:
    Serial.print((int) payload.hallEffectValue);
    Serial.print("   ");
    Serial.print((int) payload.temperature);
    Serial.print("   ");
    Serial.println((int) payload.battVoltage); 
    Serial.flush();
#endif 
   
    rf12_sleep(RF12_WAKEUP);         // turn radio back on at the last moment
    
    MilliTimer wait;                 // radio needs some time to power up, why?
    while (!wait.poll(3)) {    //5
        rf12_recvDone();
        lowPower(SLEEP_MODE_IDLE);
    }
    
    rf12_sendNow(0, &payload, sizeof payload);
    rf12_sendWait(1); // sync mode!
}