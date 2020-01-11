// #include <TM1637Display.h>
#include <avr/sleep.h>

#define DEBUG 0

// === PINs ===
/*
#define PIN_TM1637_CLK 8
#define PIN_TM1637_DIO 9
#define PIN_HX71_SCK 12
#define PIN_HX71_DT 13
*/
#define PIN_BTN_1 4   // The INT0 pin used for HW interrupt, same as PIN 2 on Uno
#define PIN_LED_1 19  // Uno PIN 13

// Global variables
//TM1637Display display(PIN_TM1637_CLK, PIN_TM1637_DIO);
byte ModeL1 = 0;

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
    // Reset everything, disable all silent timers, 
    // Enable external (button) interrupt
    // Everyting's fine, go to sleep
    /* Test code follows... */
    if (digitalRead(PIN_BTN_1) == HIGH){
      blinkIt(5, 150, PIN_LED_1);
      powerDown();
      // Here we woke up, should go to ModeL1 2
    }
    blinkIt(1, 500, PIN_LED_1);
    delay(500);
    break;
  case(2):
    // We were sleeping, user pressed button to wake us up
    // Disable external (button) interrupt
    // Start scale and monitor it for a stable reading that seems feseable (say 40 to 80 grams)
    // If it takes more than 30 seconds, go to ModeL1 1
    break;
  case(3):
    // We have a stable reading, calculate the time.
    // If user pushes button, toggle between weight and time
    // Monitor scale, if it goes down, move to next ModeL1
    // If it takes more than 30 seconds, go to ModeL1 1
    break;
  case(4):
    // Waiting for user to push button to start count down
    // If it takes more than 30 seconds, go to ModeL1 1
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
  // Test if there
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

ISR(INT0_vect){
    EIMSK &= ~_BV(INT0);           // Disable the INT0 bit inte EIMSK register so only one interrupt is invoked
}
