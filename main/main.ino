extern "C" {
  #include <stdlib.h>
  #include <string.h>
  #include <inttypes.h>
}

#include <Arduino.h>

#define TM1637_I2C_COMM1    0x40
#define TM1637_I2C_COMM2    0xC0
#define TM1637_I2C_COMM3    0x80


void setup(){
  // initialize serial communication at 9600 bits per second:
  Serial.begin(9600);
}

void loop(){
  
}
