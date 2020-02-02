#include <avr/sleep.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include <TM1637Display.h>  // https://github.com/avishorp/TM1637
#include <HX711.h>          // https://github.com/bogde/HX711

#define DEBUG 1

// === Values ===
#define SHUTDOWN_TIME       15          // Nr of seconds, after which it should automatically turn off
#define SETUP_SHUTDOWN_TIME  5          // Nr of seconds during setup after which it should move between params
#define MIN_EGG_WEIGHT      200         // The minimum weight to be recognized as an egg
#define MAX_EGG_WEIGHT      900         // The maximum weight to be recognized as an egg
#define DEFAULT_BRIGHTNESS  0x0f        // Brightness of LCD screen
#define SCALE_SENSITIVITY   10.0        // The nr of grams (times 10) the scale has to "stand still" before considered done
#define BATTERY_LOW         3.4         // Voltage below which battery is considered low but scale still functions
#define BATTERY_CRITICAL    3.2         // Voltage below which battery is considered critically low and the scale won't function
#define SCALE_CALIBRATION   5850        // The calibration value of the scale - needs to be calibrated!
#define PIEZO_FREQ          2250        // The frequency of the piezo buzzer
#define PIEZO_DEF_DELAY     80          // Default delay and duration of tone

// === PINs ===
#define PIN_VOLTMETER   A2              // The PIN used to measure battery voltage
#define PIN_BTN_1        2       // The INT0 pin used for HW interrupt, PIN 4 on Atmega328P
#define PIN_LED_1       13      // PORTB5 (not working ) //13  // Atmega328 PCINT5?
#define PIN_PIEZO        7
#define PIN_TM1637_CLK   8  // Yellow wire, PIN 14 on Atmega328P
#define PIN_TM1637_DIO   9  // Green wire, PIN 15 on Atmega328P
#define PIN_HX71_SCK     5     // 11 on Atmega329P
#define PIN_HX71_DT      6     // 12 on Atmega329P

const uint8_t SEG_BATT[] = {
  SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,           // b
  SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,   // A
  SEG_D | SEG_E | SEG_F | SEG_G,                   // t
  SEG_D | SEG_E | SEG_F | SEG_G                    // t
};

const uint8_t SEG_ERR[] = {
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,           // E
  SEG_E | SEG_G,                                   // r
  SEG_E | SEG_G,                                   // r
  0
};

const uint8_t SEG_EGG[] = {
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,           // E
  SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,   // G
  SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,   // G
  0
};

const uint8_t SEG_WAIT[] = {
  SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,           // U
  SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,   // A
  SEG_B | SEG_C,                                   // I
  SEG_D | SEG_E | SEG_F | SEG_G                    // t
};

const uint8_t SEG_DONE[] = {
  SEG_B | SEG_C | SEG_D | SEG_E | SEG_G,           // d
  SEG_C | SEG_D | SEG_E | SEG_G,                   // o
  SEG_C | SEG_E | SEG_G,                           // n
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G            // E
};

// Global variables
TM1637Display display(PIN_TM1637_CLK, PIN_TM1637_DIO);
HX711         scale;
volatile byte mode                    = 0;
byte          last_mode               = 0;
volatile bool WDT_handled             = true;
volatile bool button_handled          = true;
byte          shutdown_timer          = 0;
bool          show_as_weight          = true;
float         egg_weight              = 0;
float         last_egg_weight         = 0;
unsigned int  time_left               = 0;
bool          timer_running           = false;
bool          display_blank           = false;
byte          params[3]               = {225, 34, 15};    // (200 - 250), (25 - 40), (0 - 240)
byte          paramsMin[3]            = {200, 25, 0};
byte          paramsMax[3]            = {250, 40, 240};
bool          battery_critically_low  = false;

void setup() {
  #ifdef DEBUG
  Serial.begin(9600);
  #endif
  StopWDT();
  pinMode(PIN_BTN_1, INPUT_PULLUP);
  pinMode(PIN_LED_1, OUTPUT);
  pinMode(PIN_PIEZO, OUTPUT);
  scale.begin(PIN_HX71_DT, PIN_HX71_SCK);
  display.clear();
}

// === State handling loop ===

void loop() {

  switch (mode) {

    case 0:                             // Boot mode, initializing & test
      mode = InitTest();
      GetParams();
      break;

    case (1):                           // Sleep mode
      if (last_mode != 1){
        last_mode = 1;
      }
      battery_critically_low = false;
      last_mode = 1;
      PowerDown();                      // Sleeping here...
      button_handled = true;
      StartDisplay();
      display.setSegments(SEG_WAIT);
      if (BatteryOK()){             // Either sleep or continue
        mode = 2;
      } else {
        delay(100);
        mode = 20;
      }
      break;

    case (2):                           // Weighing the egg...
      if (last_mode != 2){
        last_mode = 2;
        last_egg_weight = 0;
        shutdown_timer = 0;
        StartScale();
        StartWDT();
        StartDisplay();
        display.setSegments(SEG_EGG);        
        Beep(1, PIEZO_DEF_DELAY);
      }
      if (!WDT_handled) {
        WDT_handled = true;
        shutdown_timer++;
        if (shutdown_timer >= SHUTDOWN_TIME) {
          StopWDT();
          StopScale();
          StopDisplay();
          shutdown_timer = 0;
          mode = 1;                     // Go to sleep
          break;
        }
      }
      if (!ScaleReading()){             // Scale not ready
        break;
      }
      if (egg_weight != last_egg_weight){ 
        DisplayShow(egg_weight, true);
      }
      if ((egg_weight > MIN_EGG_WEIGHT) && (egg_weight < MAX_EGG_WEIGHT)){
        if (abs(egg_weight - last_egg_weight) > SCALE_SENSITIVITY){             // The scale is still moving
          shutdown_timer = 0;
        } else {
          StopWDT();
          StopScale();
          Beep(2, PIEZO_DEF_DELAY);
          mode = 3;
          break;
        }
        last_egg_weight = egg_weight;
      }
      break;

    case (3):                           // Waiting for user to push button, toggle weight and time
      if (last_mode != 3){
        last_mode = 3;
        shutdown_timer = 0;
        time_left = EggWeightToTime();
        DisplayShow(time_left, false);
        StartWDT();
        EnableButton();
      }
      if (!WDT_handled) {
        WDT_handled = true;
        shutdown_timer++;
        if (shutdown_timer >= (SHUTDOWN_TIME*2)) {
          StopWDT();
          StopDisplay();
          mode = 1;
          break;
        }
        if (show_as_weight){
          DisplayShow(egg_weight, true);
        } else {
          DisplayShow(time_left, false);
        }
        show_as_weight = !show_as_weight;
      }
      if (!button_handled){             // User pushed button to start count-down
        button_handled = true;
        shutdown_timer = 0;
        StopWDT();
        mode = 4;
  #ifdef DEBUG
  Serial.println("Button pressed (3)...");
  #endif
      }
      break;

    case (4):                           // Count-down
      if (last_mode != 4){
        last_mode = 4;
        shutdown_timer = 0;
        timer_running = true;
        DisplayShow(time_left, false);
        StartWDT();
        button_handled = true;
      }
      if (!WDT_handled) {
        EnableButton();
        WDT_handled = true;
        if (!timer_running){            // We are paused...
          shutdown_timer++;
          if (shutdown_timer >= (SHUTDOWN_TIME*2)){
            StopWDT();
            StopDisplay();
            mode = 1;
            break;
          }
          display_blank = !display_blank;
          display.setBrightness(DEFAULT_BRIGHTNESS, display_blank);
          DisplayShow(time_left, false);// Need to update LCD to blink...
        } else {                        // We are counting down...
          time_left--;
          DisplayShow(time_left, false);
          if (time_left <= 0){          // Time's up, go to alarm
            StopWDT();
            mode = 5;
            break;
          }
        }
      }
      //if (digitalRead(PIN_BTN_1) == LOW){                                       // User pressed button
      if (!button_handled){
  #ifdef DEBUG
  Serial.println("Button pressed...");
  #endif
          button_handled = true;
          timer_running = !timer_running;
          if (timer_running){
            display.setBrightness(DEFAULT_BRIGHTNESS, true);                    // Make sure LCD is on
          }
      }
      break;

    case (5):                           // Alarm...
      if (last_mode != 5){
        last_mode = 5;
        shutdown_timer = 0;
        StartWDT();
        EnableButton();
      }
      if (!WDT_handled){
        WDT_handled = true;
        shutdown_timer++;
        display_blank = !display_blank;
        display.setBrightness(DEFAULT_BRIGHTNESS, display_blank);
        display.setSegments(SEG_DONE);  // Update LCD to blink
        Beep(4, PIEZO_DEF_DELAY);
      }
      if ((!button_handled) || (shutdown_timer >= (SHUTDOWN_TIME * 3))){
        StopWDT();
        StopDisplay();
        mode = 1;
        delay(500);
      }
      break;

    case (20):                          // Battery low mode
      if (last_mode != 20){
        last_mode = 20;
        shutdown_timer = 0;
        Beep(3, PIEZO_DEF_DELAY/2);
        StartWDT();
      }
      if (!WDT_handled){
        WDT_handled = true;
        shutdown_timer++;
        display_blank = !display_blank;
        display.setBrightness(DEFAULT_BRIGHTNESS, display_blank);
        display.setSegments(SEG_BATT);
      }
      if (shutdown_timer >= SETUP_SHUTDOWN_TIME){
        StopWDT();
        StopDisplay();
        if (battery_critically_low){
          mode = 1;                     // Go to sleep...
        } else {
          mode = 2;                     // Continue...
        }
      }
      break;

    case (50):                          // Setup mode
      static byte param_nr;
      static byte param_val;
      static unsigned int deb;
      if (last_mode != 50){
        StartDisplay();
        last_mode = 50;
        shutdown_timer = 0;
        param_nr = 0;
        param_val = params[param_nr];
        display.showNumberDecEx((param_nr+1)*1000+param_val, 128, false);
        StartWDT();
        EnableButton();
        deb = millis();
  #ifdef DEBUG
  Serial.println("In 50...");
  Serial.println((param_nr+1)*1000+param_val);
  #endif
      }
      if (!WDT_handled) {
        WDT_handled = true;
        shutdown_timer++;
        //EnableButton();
        if (shutdown_timer >= SETUP_SHUTDOWN_TIME) {
          shutdown_timer = 0;
          params[param_nr] = param_val;
          param_nr++;
          if (param_nr >= 3){
            StopWDT();
            StopDisplay();
            StoreParams();
            mode = 1; // Go to sleep
            break;
          }
          param_val = params[param_nr];
          display.showNumberDecEx((param_nr+1)*1000+param_val, 128, false);
  #ifdef DEBUG
  Serial.println((param_nr+1)*1000+param_val);
  #endif
        }
      }
      if (!button_handled){
        button_handled = true;
        shutdown_timer = 0;
        param_val++;
        if (param_val > paramsMax[param_nr]){
          param_val = paramsMin[param_nr];
        }
        display.showNumberDecEx((param_nr+1)*1000+param_val, 128, false);
  #ifdef DEBUG
  Serial.println((param_nr+1)*1000+param_val);
  #endif
      }
      if ((millis()-deb)> 200){
        EnableButton();
      }
      break;

    default:
      break;
  }
}

// === Supporting functions ===
/*
  BatteryOK   Used to determine if we can continue or go to sleep due to low battery. Variable "battery_critically_low"
              is set if the battery is too low.
*/
bool BatteryOK(){
  //float voltage = 3.39;
  return false;
  float voltage = 0.0;
  for (byte v = 0; v < 20; v++) {
    voltage += analogRead(PIN_VOLTMETER);
    delay(10);
  }
  voltage = (voltage/20.0 * 5.015) / 1024.0;
  if (voltage > BATTERY_LOW){ // 3.5V
    return true;
  }
  if (voltage > BATTERY_CRITICAL){ // 3.4V
    return false;
  }
  battery_critically_low = true;
  return false;
}


/*
  Beep          Plays a number of beeps with a certain duration
    "times"     The number of beeps
    "duration"  The duration of each beep in tenth of seconds, i.e. 10 equals 1 second, 1 equals 1/10 of a second etc.
*/
void Beep(byte times, byte duration){
  if (times <= 0) return;
  if ((times * duration) > 1000) return;
  for (byte i = 0; i < times; i++){
    //tone(PIN_PIEZO, PIEZO_FREQ, duration);
    tone(PIN_PIEZO, PIEZO_FREQ);
    delay(duration);
    noTone(PIN_PIEZO);
    if (i < (times - 1)){
      delay(duration);
    }
/*
    if (i < (times - 1)){
      delay(duration);
      noTone(PIN_PIEZO);
    }
    noTone(PIN_PIEZO);
*/
  }
}


/*
  DisplayShow   Shows a number on the LCD screen
    "nr"        The number to display. Note that this has to be an integer even if it is to be displayed as a float.
                If "isWeight" is true, then "nr" should be times 10, otherwise in seconds (which will be displayed as minutes and seconds)
    "isWeight"  If true, the "nr" is displayed as a float with one decimal point, otherwise it will be interpreted as seconds and 
                converted to minutes and seconds
*/
void DisplayShow(int nr, bool isWeight){
  if ((nr > 999) || (nr < 0)){
    display.setSegments(SEG_ERR);
    return;
  }
  if (isWeight){
    display.showNumberDecEx(nr, 32, false); // 32 is binary for displaying dot at last position, i.e. one decimal nr
  } else {
    int s;
    byte dot = 64;
    if (nr < 60) dot = 0;
    s = nr % 60;
    nr = (nr - s)/60;
    nr = nr % 60;
    nr = nr*100+s;
    display.showNumberDecEx(nr, dot, false); // 64 is binary for displaying dot at second to last position, i.e. two decimal nr
  }
}


/*

*/
unsigned int EggWeightToTime(){
#ifdef DEBUG
  return 20;
#endif
  return ((egg_weight * params[0] / params[1])/10) + params[2];
}


/*

*/
void EnableButton(){
  cli();
  EIMSK |= _BV(INT0);            // Enable INT0 bit in the EIMSK register, enabling the ISR function at HW interrupt
  sei();
  button_handled = true;
}


/*

*/
void GetParams(){
  byte val;
  for (byte nr = 0; nr < 3; nr++){
    val = EEPROM.read(nr);
    if ((val > paramsMax[nr]) || (val < paramsMin[nr])){
      val = params[nr];
    }
    params[nr] = val;
  }
}


/*

*/
byte InitTest() {
  #ifdef DEBUG
  Serial.print("Button is:\t");
  Serial.println(digitalRead(PIN_BTN_1));
  #endif
  // Test if there is weight on the scale
  if (digitalRead(PIN_BTN_1) == LOW){ // User holding button during boot
    return 50;                         // Enter setup mode
  }
  return 1; // Go to mode 1...
}


/*
  Watchdog timer interrupt routine
*/
ISR(WDT_vect) {
  // Reset the watchdog
  wdt_reset();
  WDT_handled = false;
}


/*
  Hardware interrupt routine
*/
ISR(INT0_vect) {
  EIMSK &= ~_BV(INT0);           // Disable the INT0 bit inte EIMSK register so only one interrupt is invoked
  button_handled = false;
}


/*

*/
void PowerDown() {
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


/*

*/
bool ScaleReading(){
  if (scale.is_ready()) {
    egg_weight = scale.get_units(10) * 10;
    return true;
  }
  return false;
}


/*

*/
void StartDisplay(){
  display.setBrightness(DEFAULT_BRIGHTNESS, true);
  //display.showNumberDecEx(int(nr1*100+nr2), (0x80 >> 1), true);
}


/*

*/
void StartScale(){
  // Power up the scale...
  scale.set_scale(SCALE_CALIBRATION);
  scale.tare();
}


/*

*/
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


/*

*/
float StopDisplay(){
  display.clear();
}


/*

*/
void StopScale(){
  // Power down the scale...
}


/*

*/
void StopWDT() {
  wdt_reset();                     // Just precaution...
  cli();                           // Disable interrupts while changing the registers
  MCUSR = 0;                       // Reset status register flags
  WDTCSR |= 0b00011000;            // Set WDCE and WDE to enter config mode
  WDTCSR =  0b00000000;            // set WDIE (interrupt disabled)
  sei();                           // re-enable interrupts
}


/*

*/
void StoreParams(){
  for (byte nr = 0; nr < 3; nr++){
    EEPROM.update(nr, params[nr]);
  }
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
