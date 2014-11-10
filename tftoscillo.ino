
#include <SPI.h>
#include <TFT.h>  // Arduino LCD library

#include "tftoscillo.h"

/**** DEFINES ****/

// TFT definitions
#define TFT_CS_PIN   10 // Chip select pin
#define TFT_DC_PIN    9 // Command / Display data pin
#define TFT_RST_PIN   8 // Reset pin
#define TFT_BL_PIN    6 // Backlight

#define TFT_WIDTH   160
#define TFT_HEIGHT  128

// Pot and scope input pins
#define POT_PIN      A1
#define SCOPE_PIN    A0

// In free run mode, we'll always be at 12 bits res
#define SAMPLE_MAX_VAL ((1 << 12) - 1)

// Timing definitions, in uS
#define SAMPLE_INTERVAL     0        // Sampling
#define REFRESH_INTERVAL    100000   // Clear display
#define BRIGTHNESS_INTERVAL 100000   // Update brightness from pot 

// Trigger must abort if values don't match
#define TRIGGER_TIMEOUT     500000
//#define TRIGGER_TIMEOUT     0
#define TRIGGER_DIR_UP      0
#define TRIGGER_DIR_DOWN    1

// State machine
#define STATE_SAMPLE   0
#define STATE_REFRESH  1

/**** GLOBAL VARIABLES ****/

// Array of samples
int g_ys1[TFT_WIDTH];
int g_ys2[TFT_WIDTH];
int *g_ys;
int g_yInUse = 1;

// Intervals may vary
int g_sampleInterval     = SAMPLE_INTERVAL;
int g_refreshInterval    = REFRESH_INTERVAL;
int g_brightnessInterval = BRIGTHNESS_INTERVAL;

// Store next time for actions
int g_nextSampleTime = 0;
int g_nextRefreshTime = 0;
int g_nextBrightnessTime = 0;

// Store pot value to avoid useless updates
int g_potVal = 0;

// X position of current sample
int g_x = 0;

// Current state machine state
int g_state = STATE_SAMPLE;

// Trigger
int g_triggerVal = 900;
int g_triggerDir = TRIGGER_DIR_UP;

// Tft screen instance
TFT TFTscreen = TFT(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

void genSignal();
void genSigDmaSetup();

void setup() {

  Serial.begin(115200);
  
  while (!Serial.available()) {}
  
  //SPI.begin(TFT_CS_PIN);
  // Initialize LCD
  TFTscreen.begin();
  TFTscreen.background(255, 255, 255);
  
  //SPI.setClockDivider(TFT_CS_PIN, 255);
  
  // draw in red  
  TFTscreen.fill(255, 0, 0);
  TFTscreen.stroke(255, 0, 0);
  
  analogReadResolution(ANALOG_RES);
  analogWriteResolution(ANALOG_RES);
  
  pinMode(SCOPE_PIN, INPUT);

  // these lines set free running mode on adc 7 (pin A0)
  int t = analogRead(0);
  ADC->ADC_MR |= 0x80;
  ADC->ADC_CR = 2;
  ADC->ADC_CHER = 0x80;
  
  pinMode(TFT_BL_PIN, OUTPUT);
  //pinMode(POT_PIN, INPUT);
  
  // Set brightness based on pot
  //analogWrite(TFT_BL_PIN, analogRead(POT_PIN));    
  //analogWrite(TFT_BL_PIN, ANALOG_MAX_VAL / 2);
  
  // Initialize samples arrays with invalid values
  for (int i = 0; i < TFT_WIDTH; i++) {
    g_ys1[i] = SAMPLE_MAX_VAL + 1;
    g_ys2[i] = SAMPLE_MAX_VAL + 1;
  }
  
  g_nextSampleTime = micros();
  
  //genInit();
  genSigDmaSetup();
}

void updatePwmFromPot()
{
  return;
  int potVal = analogRead(POT_PIN);
  if (potVal != g_potVal) {
    g_potVal = potVal;
    Serial.println(potVal);
    analogWrite(TFT_BL_PIN, potVal);  
  }
}

void displaySamples(bool bDisplay)
{
  int *ys;
  int *oldYs;
  bool bUseBackground = false;
  bool bDrawAlways = true;
  
  if (!bDisplay)
    return;
  
  if (bDisplay) {
    if (g_yInUse == 1) {
      ys = g_ys1;
      oldYs = g_ys2;
    }
    else {
      ys = g_ys2;
      oldYs = g_ys1;
    }
  }
  else {
    if (g_yInUse == 2)
      ys = g_ys1;
    else
      ys = g_ys2;
  }
  
  if (bDisplay) {
    // draw in red  
    TFTscreen.fill(255, 0, 0);
    TFTscreen.stroke(255, 0, 0);
    
  }
  else {
    // draw with background color
    TFTscreen.fill(255, 255, 255);
    TFTscreen.stroke(255, 255, 255);
  }
  
  // draw with background color
  TFTscreen.fill(255, 255, 255);
  TFTscreen.stroke(255, 255, 255);

  for (int i = 1; i < TFT_WIDTH; i++) {
    if (oldYs[i-1] != ys[i-1] ||
        oldYs[i] != ys[i] ||
        bDrawAlways) {

      // Clear prev line
      TFTscreen.line(i-1, oldYs[i-1], i, oldYs[i]);
    }
  }

  // draw with foreground color
  TFTscreen.fill(255, 0, 0);
  TFTscreen.stroke(255, 0, 0);

  for (int i = 1; i < TFT_WIDTH; i++) {
    if (oldYs[i-1] != ys[i-1] ||
        oldYs[i] != ys[i] ||
        bDrawAlways) {
          
      // Draw new line
      TFTscreen.line(i-1, ys[i-1], i, ys[i]);
    }
  }

/*
  TFTscreen.fill(255, 0, 0);
  TFTscreen.stroke(255, 0, 0);

  if (bDisplay || !bUseBackground) {
    for (int i = 1; i < TFT_WIDTH; i++) {
      if (oldYs[i-1] != ys[i-1] ||
          oldYs[i] != ys[i]) {
        // Clear prev line, draw new line
        
        // draw with background color
        TFTscreen.fill(255, 255, 255);
        TFTscreen.stroke(255, 255, 255);
        
        TFTscreen.line(i-1, oldYs[i-1], i, oldYs[i]);

        TFTscreen.fill(255, 0, 0);
        TFTscreen.stroke(255, 0, 0);

        TFTscreen.line(i-1, ys[i-1], i, ys[i]);
      }
    }
  }
  
  if (!bDisplay && bUseBackground) {
    TFTscreen.background(255, 255, 255);
  }
  */
}

void mapValues()
{
  //Serial.print("Mapping, ANALOG_MAX_VAL: ");
  //Serial.println(ANALOG_MAX_VAL);
  
  for (int i = 0; i < TFT_WIDTH; i++) {
    g_ys[i] = map(g_ys[i], 0, SAMPLE_MAX_VAL, TFT_HEIGHT - 1, 0);
  }
}

bool trigger()
{
  int tStart;
  int sample = 0;
  int prevSample = ANALOG_MAX_VAL;
  bool bArmed = false;
  
  tStart = micros();
  
  g_yInUse = (g_yInUse == 1 ? 2 : 1);
  if (g_yInUse == 1)
    g_ys = g_ys1;
  else
    g_ys = g_ys2;
    
  for (;;) {
    //sample = analogRead(SCOPE_PIN);
    sample = freeRunAnalogRead();
    
/*    
    Serial.print("Got sample: ");
    Serial.print(sample);
    Serial.print(", trigger: ");
    Serial.print(g_triggerVal);
    Serial.print(", armed: ");
    Serial.println(bArmed);
*/  
    
    // Got a value under trigger, we are armed now
    if (sample < g_triggerVal) {
      bArmed = true;
    }
    // Value went above trigger and was armed, OK
    else if (bArmed) {
      break;
    }
      
    if (micros() - tStart >= TRIGGER_TIMEOUT) {
      Serial.println("Trigger timeout");
      return false;
    }
  }
  
  g_ys[g_x++] = sample;
  
  //Serial.println("Triggered");
  
  g_nextSampleTime = micros() + SAMPLE_INTERVAL;
  
  return true;
}

inline int freeRunAnalogRead()
{
    while((ADC->ADC_ISR & 0x80)==0); // wait for conversion
    return ADC->ADC_CDR[7];   
}

void loop_genSigDma();

void loop()
{
  int time = micros();
  int tStart;
  int tEnd;
  
  //return;
  
  switch (g_state) {
    case STATE_SAMPLE :
      g_nextSampleTime = micros();
      // Wait for trigger
      trigger();
      tStart = micros();
      while (g_state == STATE_SAMPLE) {
        if (micros() >= g_nextSampleTime) {
        //if (true) {
          //g_ys[g_x] = analogRead(SCOPE_PIN);
          g_ys[g_x] = freeRunAnalogRead();
          g_x++;
          g_nextSampleTime += g_sampleInterval;
          
          // time to display ?
          if (g_x >= TFT_WIDTH) {
            tEnd = micros();
            Serial.print("us spent sampling: ");
            Serial.print(tEnd - tStart);
            Serial.print(", expected: ");
            Serial.println(g_sampleInterval * TFT_WIDTH);
            //TFTscreen.background(255, 255, 255);
            mapValues();
            displaySamples(false);
            displaySamples(true);
            g_x = 0;
            g_state = STATE_REFRESH;
            g_nextRefreshTime = micros() + g_refreshInterval;
          }
        }
      }
    break;
    
    case STATE_REFRESH :
      if (time >= g_nextRefreshTime) {
        //TFTscreen.background(255, 255, 255);
        //displaySamples(false);
        g_state = STATE_SAMPLE;
        g_nextSampleTime = time + g_sampleInterval;
        //updatePwmFromPot();
      }
    break;
  }

  loop_genSigDma();
  //genSignal();
}

