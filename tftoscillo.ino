
#include <SPI.h>
#include <TFT.h>  // Arduino LCD library

/**** DEFINES ****/

// TFT definitions
#define TFT_CS_PIN   10 // Chip select pin
#define TFT_DC_PIN    9 // Command / Display data pin
#define TFT_RST_PIN   8 // Reset pin
#define TFT_BL_PIN    6 // Backlight

#define TFT_WIDTH   160
#define TFT_HEIGHT  128

// Pot and scope input pins
#define POT_PIN      A0
#define SCOPE_PIN    A1

// Resolution for analog input and outputs
#define ANALOG_RES    10
#define ANALOG_MAX_VAL ((1 << ANALOG_RES) - 1)

// Timing definitions, in uS
#define SAMPLE_INTERVAL     1        // Sampling
#define REFRESH_INTERVAL    100000   // Clear display
#define BRIGTHNESS_INTERVAL 100000   // Update brightness from pot 

// Trigger must abort if values don't match
#define TRIGGER_TIMEOUT     1000000

// State machine
#define STATE_SAMPLE   0
#define STATE_REFRESH  1

/**** GLOBAL VARIABLES ****/

// Array of samples
int g_ys[TFT_WIDTH];

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

// Tft screen instance
TFT TFTscreen = TFT(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

void setup() {

  Serial.begin(115200);
  
  // Initialize LCD
  TFTscreen.begin();
  TFTscreen.background(255, 255, 255);

  // draw in red  
  TFTscreen.fill(255, 0, 0);
  TFTscreen.stroke(255, 0, 0);
  
  analogReadResolution(ANALOG_RES);
  analogWriteResolution(ANALOG_RES);
  
  pinMode(TFT_BL_PIN, OUTPUT);
  pinMode(POT_PIN, INPUT);
  
  // Set brightness based on pot
  analogWrite(TFT_BL_PIN, analogRead(POT_PIN));    
  
  g_nextSampleTime = micros();
}

void updatePwmFromPot()
{
  int potVal = analogRead(POT_PIN);
  if (potVal != g_potVal) {
    g_potVal = potVal;
    Serial.println(potVal);
    analogWrite(TFT_BL_PIN, potVal);  
  }
}

void displaySamples(bool bDisplay)
{
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
  
  // map values
  for (int i = 1; i < TFT_WIDTH; i++) {
    g_ys[i] = map(g_ys[i], 1, ANALOG_MAX_VAL, 1, TFT_HEIGHT - 1);
  }
  
  for (int i = 1; i < TFT_WIDTH; i++) {
    TFTscreen.line(i-1, g_ys[i-1], i, g_ys[i]);
  }  
}

bool triggered()
{
  int tStart;
 
  tStart = micros();
  
  while (analogRead(SCOPE_PIN) == 0) {
    if (micros() - tStart >= TRIGGER_TIMEOUT) {
      Serial.println("Timeout waiting for non 0 value");
      return true;
    }
  }

  tStart = micros();

  while (analogRead(SCOPE_PIN) != 0) {
    if (micros() - tStart >= TRIGGER_TIMEOUT) {
      Serial.println("Timeout waiting for 0 value");
      return true;
    }
  }

  g_x = 0;
  for (int i = 0; i < 50; i++) {
    g_ys[i] = 0;
    g_x++;
  }  
  
  return true;
}

void loop()
{
  int time = micros();
  int tStart;
  int tEnd;
  
  switch (g_state) {
    case STATE_SAMPLE :
      // Wait for trigger
      while (!triggered()) {};
      g_nextSampleTime = micros();
      tStart = g_nextSampleTime;
      while (g_state == STATE_SAMPLE) {
        if (micros() >= g_nextSampleTime) {
        //if (true) {

          g_ys[g_x] = analogRead(SCOPE_PIN);
          g_x++;
          g_nextSampleTime += g_sampleInterval;
          
          // time to display ?
          if (g_x >= TFT_WIDTH) {
            tEnd = micros();
            Serial.print("us spent sampling: ");
            Serial.print(tEnd - tStart);
            Serial.print(", expected: ");
            Serial.println(g_sampleInterval * TFT_WIDTH);
            TFTscreen.background(255, 255, 255);
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
        updatePwmFromPot();
      }
    break;
  }

}
git 
