#include <Wire.h> // Include the I2C library (required)
#include <SX1508.h> // Include SX1508 library
#include "Keyboard.h"

// SX1508 I2C address (set by ADDR1 and ADDR0 (00 by default):
const byte SX1508_ADDRESS = 0x20;  // SX1508 I2C address
SX1508 io; // Create an SX1508 object to be used throughout

// SX1508 Pins:
const byte SX1508_BUTTON_PIN = 0; // IO 0 connected to button

// Arduino Pins (not SX1508!)
const byte ARDUINO_INT_PIN = 7; // SX1508 int output to D7

const byte FAN = 10;          //5v fan circuit control (pwm of a MOSFET)
const byte STORAGE = 5;       //Mass Storage subsystem power control
const byte PORT_1 = 8;        //USB 3 Port 1 power control
const byte PORT_2 = 12;       //USB 3 Port 2 power control
const int BUTTON_DELAY = 150; //ms to wait for a dual press detection
const int RESET_DELAY = 2000; //ms to wait before executing dual press function

// Global variables:
bool buttonPressed = false; // Track button press in ISR

long buttonTimer[6] = {0, 0, 0, 0, 0, 0};   //place to store the time of each new button press
long portResetTimeout = 0;                  //place to the time when the USB port power should be restored during a reset cycle
bool portResetting = false;                 //reset in progress boolean
byte timerMask = 0x00;                      //bits to denote whether a time recorded in the matching slot of buttonTimer is still relevant

bool fanCycling = false;

//Keycodes associated with each of the six buttons connected to the button board IO expander
byte keycode[6] = {KEY_DOWN_ARROW, KEY_RIGHT_ARROW, KEY_UP_ARROW, KEY_LEFT_ARROW, 0x5B, 0x5D};
byte keycode1[6] = {KEY_DOWN_ARROW, KEY_RIGHT_ARROW, KEY_UP_ARROW, KEY_LEFT_ARROW, KEY_RETURN, 0x20};
byte keycode2[6] = {0xE2, 0xE6, 0xE8, 0xE4, 0x5B, 0x5D};

                                                                
bool altKeys = false;

void setup()
{
  pinMode(ARDUINO_INT_PIN, INPUT_PULLUP);
  pinMode(FAN, OUTPUT);
  pinMode(PORT_1, OUTPUT);
  pinMode(PORT_2, OUTPUT);
  pinMode(STORAGE, OUTPUT);
  digitalWrite(PORT_2, LOW);
  digitalWrite(PORT_1, LOW);
  digitalWrite(STORAGE, HIGH);
  // Serial is used in this example to display the input
  // value of the SX1508_INPUT_PIN input:
  Serial.begin(9600);
  // Call io.begin(<address>) to initialize the SX1508. If
  // it successfully communicates, it'll return 1.
  if (!io.begin(SX1508_ADDRESS))
  {
    Serial.println("Failed to communicate.");
    while (1) ;
  }

  // io.configForNorthStarButtons();

  // Use io.pinMode(<pin>, <mode>) to set our button to an
  // input with internal pullup resistor activated:
  // io.writeByte(REG_INPUT_DISABLE, 0b11000000);
  // io.writeByte(REG_PULL_UP, 0b00111111);
  //  io.pinMode(SX1508_BUTTON_PIN, INPUT_PULLUP);
  for (byte i = 0; i < 6; i++) {
    io.pinMode(i, INPUT_PULLUP);
    delay(1);
  }

  // Use io.enableInterrupt(<pin>, <signal>) to enable an
  // interrupt on a pin. The <signal> variable can be either
  // FALLING, RISING, or CHANGE. Set it to falling, which will
  // mean the button was pressed:
  //  for (byte i = 0; i < 5; i++) {
  //    io.enableInterrupt(i, FALLING);
  //    delay(1);
  //  }
  io.writeByte(0x09, 0b11000000);
  io.writeByte(0x0A, 0b00001111);
  io.writeByte(0x0B, 0b11111111);

  // The SX1508 has built-in debounce features, so a single
  // button-press doesn't accidentally create multiple ints.
  // Use io.debounceTime(<time_ms>) to set the GLOBAL SX1508
  // debounce time.
  // <time_ms> can be either 0, 1, 2, 4, 8, 16, 32, or 64 ms.
  io.debounceTime(32); // Set debounce time to 32 ms.

  // After configuring the debounce time, use
  //debouncePin(<pin>) to enable debounce on an input pin.
  for (byte i = 0; i < 6; i++) {
    io.debouncePin(i);
  }

  // Attach an Arduino interrupt to the interrupt pin. Call
  // the button function, whenever the pin goes from HIGH to
  // LOW.
  attachInterrupt(digitalPinToInterrupt(ARDUINO_INT_PIN),
                  button, FALLING);
  digitalWrite(STORAGE, HIGH);
  analogWrite(FAN, 120);
  digitalWrite(PORT_2, HIGH);
  digitalWrite(PORT_1, HIGH);
  Keyboard.begin();
  Keyboard.releaseAll();
}

void loop()
{
  //Serial.println(".");
  if (buttonPressed) // If the button() ISR was executed
  {
    buttonPressed = false; // Clear the buttonPressed flag
    Serial.println("Int triggered");
    // read io.interruptSource() find out which pin generated
    // an interrupt and clear the SX1508's interrupt output.
    byte intStatus = io.interruptSource(true);
    byte buttonStatus = 0xFF ^ io.readData();
    // For debugging handiness, print the intStatus variable.
    // Each bit in intStatus represents a single SX1508 IO.
    Serial.println("intStatus = " + String(intStatus, BIN) + " buttonStatus = " + String(buttonStatus, BIN));

    // If the bit corresponding to our button IO generated
    // the input:

    byte pressed = intStatus | buttonStatus;
    byte buttonNum = 0;
    byte numPressed = 0;
    for (byte i = 0; i < 6; i++) {
      if (intStatus & (1 << i)) {
        buttonNum = i;
        Serial.print("Button " + String(buttonNum) + " was ");
        if (buttonStatus & (1 << i)) {
          Serial.println("pressed.");

          //Record time that the button was pressed
          buttonTimer[i] = millis();

          //Mark the timer as "active"
          timerMask |= (1 << i);

          //Send associated keycode
          //(For D-Pad. Key sending functions for A and B moved to timer delay to allow for dual press function)
          if (i < 4) {
            Keyboard.press(keycode[i]);
          }
        }
        else {
          //Button triggered interrupt but isn't held... must've been released!
          Serial.println("released. ");
          Keyboard.release(keycode[i]);
          //"Deactivate" timer
          if (i >3 && timerMask & (1 << i)) {
            Keyboard.press(keycode[i]);
            Keyboard.release(keycode[i]);
          }
          timerMask &= ~(1 << i);
          Serial.println("It was held for " + String(millis() - buttonTimer[i]) + "ms.");
        }
      }
      //How many buttons are held right now?
      if (buttonStatus & (1 << i)) {
        numPressed++;
      }
    }
    if (numPressed > 1) {
      Serial.println("There are " + String(numPressed) + " buttons being held.");
    }
    else {
      Serial.println();
    }
    Serial.println("TimerMask = " + String(timerMask, BIN));
  }
  timerActions();
}

void timerActions() {
  if (timerMask) {                  //does a button have an active timer?
    long currentTime = millis();    //let's just read the time once. No need to keep consulting the hardware clock here
    if ((0b00010000 & timerMask) && !(0b00100000 & timerMask)) {    //Is button A held and button B isnt?
      if (currentTime - BUTTON_DELAY > buttonTimer[4]) {            //Has is been held longer than the time allocated to detect a dual-button press?
        Keyboard.press(keycode[4]);                                 //Send the key!
        timerMask &= (~(1 << 4));                                   //Kill the timer!
        Serial.println("TimerMask = " + String(timerMask, BIN));    //Tell the story!
      }
    }
    else {
      if ((0b00100000 & timerMask) && !(0b00010000 & timerMask)) {  //Same as above... yadda yadda yadda
        if (currentTime - BUTTON_DELAY > buttonTimer[5]) {
          Keyboard.press(keycode[5]);
          timerMask &= (~(1 << 5));
          Serial.println("TimerMask = " + String(timerMask, BIN));
        }
      }
      else {
        //Check if A and B have been held together for two seconds
        if ((0b00100000 & timerMask) && (0b00010000 & timerMask)) { //Are both buttons pressed? Separate comparisons because 0b00110000 & timerMask would evaluate to true for both OR one.
          long initiatedTime = currentTime - RESET_DELAY;                  //Let's count the dual press from when the first button press was detected
          if ((initiatedTime > buttonTimer[4]) && (initiatedTime > buttonTimer[5])) {
                        digitalWrite(PORT_1, LOW);
                        digitalWrite(PORT_2, LOW);
                        portResetTimeout = millis() + 500;
                        portResetting = true;

            timerMask &= (~(0b11 << 4));
            Serial.println("TimerMask = " + String(timerMask, BIN) + "Port Resetting");
          }
        }
      }
    }
  }
  if (portResetting && millis() > portResetTimeout) {
    digitalWrite(PORT_1, HIGH);
    digitalWrite(PORT_2, HIGH);
    portResetting = false;
    Serial.println("Port Reset complete");
  }
}

void switchAltKeys(){
  //            if (altKeys) {
//              altKeys = false;
//              for (int i = 0; i < 6; i++) {
//                keycode[i] = keycode1[i];
//              }
//              digitalWrite(PORT_1, HIGH);
//              digitalWrite(PORT_2, HIGH);
//            }
//            else {
//              altKeys = true;
//              for (int i = 0; i < 6; i++) {
//                keycode[i] = keycode2[i];
//              }
//              digitalWrite(PORT_1, LOW);
//              digitalWrite(PORT_2, LOW);
//            }
}

// button() is an Arduino interrupt routine, called whenever
// the interrupt pin goes from HIGH to LOW.
void button()
{
  buttonPressed = true; // Set the buttonPressed flag to true
  // We can't do I2C communication in an Arduino ISR. The best
  // we can do is set a flag, to tell the loop() to check next
  // time through.
}
