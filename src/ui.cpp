#include "ui.h"

#include <elapsedMillis.h>

#include "LL_Lib.h"
//#include <FastLED.h>
#include "Freenove_WS2812_Lib_for_ESP32.h"

//CRGB fastleds[1];
Freenove_ESP32_WS2812 leds = Freenove_ESP32_WS2812(2, 5, 0, TYPE_GRB); // 2 led on GPIO 5

struct led_ani_s {
  int state;
  uint32_t rgbA;
  uint16_t msA;
  uint32_t rgbB;
  uint16_t msB;
  uint16_t steps;
  uint16_t stepCount;
  elapsedMillis eMs;
} led_ani;

struct button_eval_s {
  int act_button; // 0 none, 1 red, 2 yellow, 3 red+yellow
  elapsedMillis eMs;
  elapsedMillis lastStateChange;
  uint8_t counts[5]; // for each but state
  uint8_t readings;

  // set when act_button goes to 0, reset when read
  uint8_t  releasedButton;
  uint16_t releasedMs;

} button_eval;

void uiInit() {
  //FastLED.addLeds<NEOPIXEL, 5>(fastleds, 1); // 1 led on GPIO 5
  leds.begin();
  leds.setBrightness(20); 
  led_ani.state = 0;
}

// Get current pressed button and how long it is pressed
int uiGetPressedButton(uint16_t *sinceMs) {
  if(sinceMs != NULL) {
    *sinceMs = min((unsigned long)button_eval.lastStateChange, 60000UL);
  }
  return button_eval.act_button;
}

// Get (and clears) last released button and how long it was pressed
int uiGetReleasedButton(uint16_t *durationMs) {
  uint8_t lbut = button_eval.releasedButton;
  uint16_t dMs = min((unsigned long)button_eval.releasedMs, 60000UL);
  if(lbut == 0) dMs = 0;
  if(durationMs != NULL) *durationMs = dMs;
  button_eval.releasedButton = 0;
  return lbut;
}

void uiHandle() {

  // update button
  if(button_eval.eMs >= 2) {
    button_eval.eMs = 0;
    int a = analogRead(39);
    int but = 0;
    // No But: 4095, Red: 203, Yellow: 555, Both: 112 
    if(a < 700) but = 2;
    if(a < 370) but = 1;
    if(a < 150) but = 3;
    if(a < 50) but = 1;

    // filter out noise
    button_eval.counts[but]++;
    if(button_eval.readings++ > 10) {
      but = 0;
      // find which count is >= 7
      for(int i=0; i<4; i++) {
        if(button_eval.counts[i] >= 7) but = i;
      }
      if(button_eval.act_button != but) {
        if(button_eval.act_button != 0) {
          button_eval.releasedButton = button_eval.act_button;
          button_eval.releasedMs = min((unsigned long)button_eval.lastStateChange, 60000UL);
        }
        button_eval.act_button = but;
        button_eval.lastStateChange = 0;
      }
      // clear all
      for(int i=0; i<4; i++) {
        button_eval.counts[i] = 0;
      }     
      button_eval.readings = 0;
    }

    
  }

  // update LEDs
  switch(led_ani.state) {
    case 1:
      if(led_ani.eMs >= led_ani.msA) {
        led_ani.stepCount++;
        if(led_ani.steps) {
          if(led_ani.stepCount >= led_ani.steps) {
            led_ani.state = 0;
          }
        }
        if(led_ani.state != 0) {
          led_ani.state = 2;
          led_ani.eMs = 0;
          leds.setAllLedsColorData(led_ani.rgbB);
          leds.show();
        }
      }
      break;

    case 2:
      if(led_ani.eMs >= led_ani.msB) {
        led_ani.stepCount++;
        if(led_ani.steps) {
          if(led_ani.stepCount >= led_ani.steps) {
            led_ani.state = 0;
          }
        }
        if(led_ani.state != 0) {
          led_ani.state = 1;
          led_ani.eMs = 0;
          leds.setAllLedsColorData(led_ani.rgbA);
          leds.show();
        }
      }
      break;


  }  

}

void uiSetLED(uint32_t rgb) {
  leds.setAllLedsColorData(rgb);
  leds.show();
  led_ani.state = 0;
}

// Steps 0 = no stop, Steps 1 = stop with rgbA
void uiBlinkLED(uint32_t rgbA, uint16_t msA, uint32_t rgbB, uint16_t msB, uint16_t steps) {
  led_ani.rgbA = rgbA;
  led_ani.rgbB = rgbB;
  led_ani.msA = msA;
  led_ani.msB = msB;
  led_ani.eMs = 0;
  led_ani.steps = steps;
  led_ani.stepCount = 0;
  led_ani.state = 1;
  leds.setAllLedsColorData(rgbA);
  leds.show();
}

// No But: 4095, Red: 203, Yellow: 555, Both: 112
// analogRead(39);