// #include <TM1637Display.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#define DEBUG 0

// === PINs ===
/*
#define PIN_TM1637_CLK 8
#define PIN_TM1637_DIO 9
#define PIN_HX71_SCK 12
#define PIN_HX71_DT 13
*/
#define PIN_BTN_1 2   // The INT0 pin used for HW interrupt, same as PIN 2 on Uno
#define PIN_LED_1 13  // Atmega328 PCINT5?

// Global variables
//TM1637Display display(PIN_TM1637_CLK, PIN_TM1637_DIO);
byte ModeL1 = 0;
volatile bool WDT_handled = true;

byte tmp_nr = 0;

void setup(){
  pinMode(PIN_BTN_1, INPUT);
  pinMode(PIN_LED_1, OUTPUT);
  #ifdef DEBUG
  Serial.begin(9600);
  #endif
}

void loop(){
  switch(ModeL1){
  case 0:                 // Starting up, running tests...
    ModeL1 = initTest();
    break;
  case(1):
    // Everyting's fine, go to sleep
    powerDown();
    ModeL1 = 2;
    break;
  case(2):
    // We were sleeping, user pressed button to wake us up
    // Disable external (button) interrupt
    // Start scale and monitor it for a stable reading that seems feseable (say 40 to 80 grams)
    // If it takes more than 30 seconds, go to ModeL1 1
    for (byte i = 0; i < 5; i++){
      blinkIt(1, 500, PIN_LED_1);
      delay(500);
      ModeL1 = 3;
    }
    break;
  case(3):
    // We have a stable reading, calculate the time.
    // If user pushes button, toggle between weight and time
    // Monitor scale, if it goes down, move to next ModeL1
    // If it takes more than 30 seconds, go to ModeL1 1

    /* Test code follows... */
    //  blinkIt(3, 100, PIN_LED_1);

    WDTstart();
    ModeL1 = 4;
    /* --------- */
    break;
  case(4):
    // Waiting for user to push button to start count down
    // If it takes more than 30 seconds, go to ModeL1 1
    if (!WDT_handled){
      tmp_nr = tmp_nr+1;
      WDT_handled = true;
      if (tmp_nr > 3){
        WDTstop();
        ModeL1 = 1;
      }
      blinkIt(3, 100, PIN_LED_1);
    }
    break;
  case(5):
    // Counting down...
    // If user presses button, go to ModeL1 7
    // If counter reaches 0, go to ModeL1 6
    break;
  case(6):
    // Counter is finished, start beeping
    // When beeping is done, go to ModeL1 1
    break;
  case(7):
    // User pressed button during count down
    // Stop count down and blink the display
    // After 30 seconds, go to ModeL1 1
    break;
  default:
    // Some error mode...
    break;
  }
}

byte initTest(){
  // Test if battery is OK
  // Test if there is weight on the scale
  blinkIt(5, 100, PIN_LED_1);
  return 1; // Go to ModeL1 1...
}

void powerDown(){
  byte adcsra, mcucr1, mcucr2;
  sleep_enable();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  adcsra = ADCSRA;               // Save the ADC Control and Status Register A
  ADCSRA = 0;                    // Disable ADC
  cli();                         // Clear all interrupts to ensure the following can be done uninterrupted
  EIMSK |= _BV(INT0);            // Enable INT0 bit in the EIMSK register, enabling the ISR function at HW interrupt
  mcucr1 = MCUCR | _BV(BODS) | _BV(BODSE);  // Turn off the brown-out detector
  mcucr2 = mcucr1 & ~_BV(BODSE);
  MCUCR = mcucr1;
  MCUCR = mcucr2;
  sei();                         // Ensure interrupts enabled so we can wake up again
  sleep_cpu();                   // Go to sleep
  sleep_disable();               // Wake up here
  ADCSRA = adcsra;               // Restore ADCSRA
  //digitalWrite(PIN_LED_1, HIGH);
}

void watchdogSetup(){
  cli();  // disable all interrupts
  wdt_reset(); // reset the WDT timer
  MCUSR &= ~(1<<WDRF);  // because the data sheet said to
  /*
    WDTCSR configuration:
    WDIE = 1 :Interrupt Enable
    WDE = 1  :Reset Enable - I won't be using this on the 2560
    WDP3 = 0 :For 1000ms Time-out
    WDP2 = 1 :bit pattern is
    WDP1 = 1 :0110  change this for a different
    WDP0 = 0 :timeout period.
  */
  // Enter Watchdog Configuration mode:
  WDTCSR = (1<<WDCE) | (1<<WDE);
  // Set Watchdog settings: interrupte enable, 0110 for timer
  WDTCSR = (1<<WDIE) | (0<<WDP3) | (1<<WDP2) | (1<<WDP1) | (0<<WDP0);
  sei();
}

void WDTstop(){
  wdt_reset();                     // Just precaution...
  cli();                           // Disable interrupts while changing the registers
  MCUSR = 0;                       // Reset status register flags
  WDTCSR |= 0b00011000;            // Set WDCE and WDE to enter config mode
  WDTCSR =  0b00000000;            // set WDIE (interrupt disabled)
  sei();                           // re-enable interrupts 
}

void WDTstart(){
  cli();                           // Disable interrupts while changing the registers
  MCUSR = 0;                       // Reset status register flags
  WDTCSR |= 0b00011000;            // Set WDCE and WDE to enter config mode, or do "WDTCSR = (1<<WDCE) | (1<<WDE);"
  WDTCSR =  0b01000000 | 0b000110; // set WDIE (interrupt enabled) and clear WDE (to disable reset), and set delay interval, or do "WDTCSR = (1<<WDIE) | (1<<WDE) | (0<<WDP3) | (1<<WDP2) | (1<<WDP1) | (0<<WDP0);
  sei();                           // re-enable interrupts
  //  16 ms:     0b000000
  //  500 ms:    0b000101
  //  1 second:  0b000110
  //  2 seconds: 0b000111
  //  4 seconds: 0b100000
  //  8 seconds: 0b100001
}

void blinkIt(byte times, unsigned int delayTime, byte pin){
  if (times == 0) return;
  for (byte t = 1; t <= times; t++){
    digitalWrite(pin, HIGH);
    delay(delayTime);
    digitalWrite(pin, LOW);
    if (t < times) delay(delayTime);
  }
}

// Watchdog interrupt routine
ISR(WDT_vect){
  // not hanging, just waiting
  // reset the watchdog
  wdt_reset();
  WDT_handled = false;
}


// Hardware interrupt routine
ISR(INT0_vect){
    EIMSK &= ~_BV(INT0);           // Disable the INT0 bit inte EIMSK register so only one interrupt is invoked
}
