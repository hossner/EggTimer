#include <avr/sleep.h>
#include <avr/wdt.h>
#include <TM1637Display.h>  // https://github.com/avishorp/TM1637
#include <HX711.h>          // https://github.com/bogde/HX711

#define DEBUG 1

// === Values ===
#define SHUTDOWN_TIME        5 // Nr of seconds, after which it should automatically turn off
#define SETUP_SHUTDOWN_TIME  5 // Nr of seconds during setup after which it should move between params
#define MIN_EGG_WEIGHT      20 // The minimum weight to be recognized as an egg
#define MAX_EGG_WEIGHT      90 // The maximum weight to be recognized as an egg
#define DEFAULT_BRIGHTNESS  0x0f
#define SCALE_SENSITIVITY    1 // The nr of grams the scale has to "stand still" before considered done
#define BATTERY_LOW         3.8
#define BATTERY_CRITICAL    3.7
#define SCALE_CALIBRATION   5850  // The calibration value of the scale - needs to be calibrated!

// === PINs ===
/*

*/
#define PIN_BTN_1        2       // The INT0 pin used for HW interrupt, PIN 4 on Atmega328P
#define PIN_LED_1       13      // PORTB5 (not working ) //13  // Atmega328 PCINT5?
#define PIN_TM1637_CLK   8  // Yellow wire, PIN 14 on Atmega328P
#define PIN_TM1637_DIO   9  // Green wire, PIN 15 on Atmega328P
#define PIN_HX71_SCK    5     // 11 on Atmega329P
#define PIN_HX71_DT     6     // 12 on Atmega329P


const uint8_t SEG_ERR[] = {
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,           // E
  SEG_E | SEG_G,                                   // r
  SEG_E | SEG_G                                    // r
};


// Global variables
TM1637Display display(PIN_TM1637_CLK, PIN_TM1637_DIO);
HX711 scale;
volatile byte ModeL1                  = 0;
byte          OldModeL1               = 0;
volatile bool WDT_handled             = true;
//volatile bool button_pressed          = false;
byte          shutdown_timer          = 0;
float         egg_weight              = 0;
float         last_egg_weight         = 0;
float         last_display_egg_weight = 0;
unsigned int  egg_boiling_time        = 0;
byte          param1                  = 225; // Min 200, max 250
byte          param2                  = 34;  // Min 25, max 40
byte          param3                  = 15;  // Min 0, max 240
bool          battery_warning_issued  = false;
bool          battery_critically_low  = false;

byte tmp_nr = 0;

void setup() {
  #ifdef DEBUG
  Serial.begin(9600);
  #endif
  StopWDT();
  pinMode(PIN_BTN_1, INPUT_PULLUP);
  //pinMode(PIN_BTN_1, INPUT);
  pinMode(PIN_LED_1, OUTPUT);
  scale.begin(PIN_HX71_DT, PIN_HX71_SCK);
}

void loop() {
  switch (ModeL1) {
    case 0:
      // Starting up, running tests...
      ModeL1 = initTest();
      break;
    case (1):
      // Everyting's fine, go to sleep
      // display.clear();
      battery_warning_issued = false;
      powerDown();
      OldModeL1 = 1;
      ModeL1 = 2;
      break;
    case (2):
      // We were sleeping, user pressed button to wake us up, external (button) interrupt is disabled
      // Start scale and monitor it for a stable reading that seems feseable (say 40 to 80 grams)
      // If it takes more than SHUTDOWN_TIME seconds, go to ModeL1 1
      if (OldModeL1 != 2){
        OldModeL1 = 2;
        if ((!battery_warning_issued) && (!BatteryOK())){
          ModeL1 = 20;
          break;
        }
        last_egg_weight = 0;
        shutdown_timer = 0;
        StartScale();
        StartWDT();
        StartDisplay(0,0);
        // Beep(1, 100);
      }
      if (!WDT_handled) {
        WDT_handled = true;
        shutdown_timer++;
        if (shutdown_timer >= SHUTDOWN_TIME) {
          StopWDT();
          StopScale();
          StopDisplay();
          shutdown_timer = 0;
          ModeL1 = 1; // Go to sleep
          break;
        }
      }
      egg_weight = ScaleReading();
      if (egg_weight < 0){ // Scale not ready
        break;
      }
      if (egg_weight != last_display_egg_weight){ 
        DisplayShow(egg_weight*10);
        last_display_egg_weight = egg_weight;
      }
      if ((egg_weight > MIN_EGG_WEIGHT) && (egg_weight < MAX_EGG_WEIGHT)){ // Valid egg weight...
        if (abs(egg_weight - last_egg_weight) > SCALE_SENSITIVITY){ // The scale is still moving
          shutdown_timer = 0;
        } else { // Should we have more than one consecutive stable reading before proceeding?
          StopWDT();
          StopScale();
          Beep(1, 20); // Make a bep...
          ModeL1 = 3;
        }
        last_egg_weight = egg_weight;
      }
      break;
    case (3):
      // We have a stable reading, calculate the time.
      // If user pushes button, toggle between weight and time
      // Monitor scale, if it goes down, move to next ModeL1
      // If it takes more than 30 seconds, go to ModeL1 1

      /* Test code follows... */
      //  blinkIt(3, 100, PIN_LED_1);
      if (OldModeL1 != 3){
        OldModeL1 = 3;
        shutdown_timer = 0;
      }
      StartWDT();
      ModeL1 = 4;
      /* --------- */
      break;
    case (4):
      // Waiting for user to push button to start count down
      // If it takes more than 30 seconds, go to ModeL1 1
      if (OldModeL1 != 4){
        OldModeL1 = 4;
      }
      if (!WDT_handled) {
        tmp_nr = tmp_nr + 1;
        WDT_handled = true;
        if (tmp_nr > 3) {
          StopWDT();
          tmp_nr = 0;
          ModeL1 = 1;
          int k;
          //display.setBrightness(0x0f);
          for (k = 0; k <= 4; k++) {
            display.showNumberDecEx(1234, (0x80 >> 1), true);
            delay(2000);
          }
        }
        BlinkIt(3, 100, PIN_LED_1);
      }
      break;
    case (5):
      // Counting down...
      // If user presses button, go to ModeL1 7
      // If counter reaches 0, go to ModeL1 6
      break;
    case (6):
      // Counter is finished, start beeping
      // When beeping is done, go to ModeL1 1
      break;
    case (7):
      // User pressed button during count down
      // Stop count down and blink the display
      // After 30 seconds or user presses button, go to ModeL1 1
      break;
    case (20): // Battery low mode
      if (OldModeL1 != 20){
        OldModeL1 = 20;
        battery_warning_issued = true;
      }
      // Here we should blink "BAtt" on display and beeping
      // When doen, if batt_critically_low, then ModeL1 = 1 (go to sleep), else ModeL1 = 2
      ModeL1 = 2;
      break;
    case (50): // Setup mode
      // User pressed button during boot...
      if (OldModeL1 != 50){
        OldModeL1 = 50;
        static byte setup_param = param1;
        shutdown_timer = 0;
        StartWDT(); // Should be with param for 4 sec watchdog
      }
      if (!WDT_handled) { // Timer triggered
        WDT_handled = true;
        shutdown_timer++;
        if (shutdown_timer >= SETUP_SHUTDOWN_TIME) {
          StopWDT();
          StopScale();
          StopDisplay();
          shutdown_timer = 0;
          ModeL1 = 1; // Go to sleep
          break;
        }
      }
      // Use a single param, set to param1, 2 and 3 consecutively
      // Listen for button press to cycle through values
      break;
    default:
      // Some error mode...
      break;
  }
}

bool BatteryOK(){
  // Measure voltage.
  // Level BATTERY_LOW; battery_critically_low = false, return true.
  // Level < BATTERY_LOW and > BATTERY_CRITICAL; battery_critically_low = false, return false.
  // Level < BATTERY_LOW and < BATTERY_CRITICAL; battery_critically_low = true, return false.
  battery_critically_low = false;
  return true;
}

void DisplayShow(int nr){
  #ifdef DEBUG
  Serial.print("In DisplayShow:\t");
  Serial.println(nr);
  #endif
  if ((nr > 999) || (nr < 0)){
    display.setSegments(SEG_ERR);
    return;
  }
  // Show 47.1
  display.showNumberDecEx(nr, 32, false);
  //display.setSegments(SEG_ERR);//display.showNumberDec(nr, 32, false);
}

void StartDisplay(byte nr1, byte nr2){
  display.setBrightness(DEFAULT_BRIGHTNESS);
  //display.showNumberDecEx(int(nr1*100+nr2), (0x80 >> 1), true);
}

float StopDisplay(){
  display.clear();
}


float ScaleReading(){
  #ifdef DEBUG
  Serial.println("In ScaleReading");
  #endif
  float weight;
  if (scale.is_ready()) {
    #ifdef DEBUG
    Serial.println("Scale is ready!");
    #endif
    weight = scale.get_units(10);
  } else {
    #ifdef DEBUG
    Serial.println("Scale not ready");
    #endif
  }

  #ifdef DEBUG
  Serial.print("Scale reading:\t");
  Serial.println(weight);
  #endif
  return weight;
/*
  if (scale.is_ready()) {
    float weight = scale.get_units(10);
    if ((weight < 0.5) && (weight > -0.5)){
      return 0;
    }
  }
  return 0;
*/
}

void StartScale(){
  // Power up the scale...
  scale.set_scale(SCALE_CALIBRATION);
  scale.tare();
}

void StopScale(){
  // Power down the scale...
}

byte initTest() {
  // Test if battery is OK
  // Test if there is weight on the scale
  #ifdef DEBUG
  BlinkIt(5, 100, PIN_LED_1);
  #endif
  if (digitalRead(PIN_BTN_1) == HIGH){ // User holding button during boot
    return 50;                         // Enter setup mode
  }
  return 1; // Go to ModeL1 1...
}

void powerDown() {
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

void watchdogSetup() {
  cli();  // disable all interrupts
  wdt_reset(); // reset the WDT timer
  MCUSR &= ~(1 << WDRF); // because the data sheet said to
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
  WDTCSR = (1 << WDCE) | (1 << WDE);
  // Set Watchdog settings: interrupte enable, 0110 for timer
  WDTCSR = (1 << WDIE) | (0 << WDP3) | (1 << WDP2) | (1 << WDP1) | (0 << WDP0);
  sei();
}

void StopWDT() {
  wdt_reset();                     // Just precaution...
  cli();                           // Disable interrupts while changing the registers
  MCUSR = 0;                       // Reset status register flags
  WDTCSR |= 0b00011000;            // Set WDCE and WDE to enter config mode
  WDTCSR =  0b00000000;            // set WDIE (interrupt disabled)
  sei();                           // re-enable interrupts
}

void StartWDT() {
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

void BlinkIt(byte times, unsigned int delayTime, byte pin) {
  if (times == 0) return;
  for (byte t = 1; t <= times; t++) {
    digitalWrite(pin, HIGH);
    delay(delayTime);
    digitalWrite(pin, LOW);
    if (t < times) delay(delayTime);
  }
}

// Watchdog interrupt routine
ISR(WDT_vect) {
  // not hanging, just waiting
  // reset the watchdog
  wdt_reset();
  WDT_handled = false;
}

// Hardware interrupt routine
ISR(INT0_vect) {
  EIMSK &= ~_BV(INT0);           // Disable the INT0 bit inte EIMSK register so only one interrupt is invoked
  // button_pressed = true;      // Maybe...
}
