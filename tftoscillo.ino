#include <GenSigDma.h>
#include <AdcDma.h>

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
#define SCOPE_CHANNEL       7
#define FREQ_CHANNEL        6
#define ADC_RATE_CHANNEL    5
#define TRIG_CHANNEL        4

#define TRIGGER_TIMEOUT 1000

#define ADC_MIN_SAMPLE_RATE 5000
#define ADC_MAX_SAMPLE_RATE 800000
uint g_adcSampleRate = ADC_MIN_SAMPLE_RATE;

#define DAC_MIN_FREQ    3000
#define DAC_MAX_FREQ    200000
#define DAC_WAVEFORM    WAVEFORM_SINUS
int g_dacFreq = DAC_MIN_FREQ;

#define TRIGGER_MIN_VAL    0
#define TRIGGER_MAX_VAL    ANALOG_MAX_VAL
int g_triggerVal = TRIGGER_MAX_VAL / 2;

// In free run mode, we'll always be at 12 bits res
#define SAMPLE_MAX_VAL ((1 << 12) - 1)

/**** GLOBAL VARIABLES ****/

// Pointer on GenSigDma object
GenSigDma *g_genSigDma = NULL;
AdcDma *g_adcDma = NULL;

// Tft screen instance
TFT TFTscreen = TFT(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

void setup() {

    Serial.begin(115200);
  
    //while (!Serial.available()) {}
  
    // Initialize LCD
    TFTscreen.begin();
    TFTscreen.background(255, 255, 255);
  
    // SPI.setClockDivider(TFT_CS_PIN, 1);
    // Modified TFT library to set SPI clock divider to 1

    g_genSigDma = new GenSigDma();
    //g_genSigDma->SetTimerChannel(1);

    int adcChannel = SCOPE_CHANNEL;
    g_adcDma = AdcDma::GetInstance();
    g_adcDma->SetAdcChannels(&adcChannel, 1);
    g_adcDma->SetTimerChannel(1);
}

void mapBufferValues(uint16_t *buf, int count)
{
    for (int iSample = 0; iSample < count; iSample++) {
        int prevSample = buf[iSample] & 0x0FFF;
        buf[iSample] = map(buf[iSample] & 0x0FFF, 0, SAMPLE_MAX_VAL, TFT_HEIGHT - 1, 0);
    }
}

void updateSignalFreq()
{
    int potVal = 0;
    static int prevPotVal = -1000;
  
    float freq = 6000.;
    static int waveform = (int)WAVEFORM_MIN + 1;
  
    g_adcDma->ReadSingleValue(FREQ_CHANNEL, &potVal);
    
    if (abs(potVal - prevPotVal) > 100) {
        freq = 10 * (int)map(potVal, 0, ANALOG_MAX_VAL, DAC_MIN_FREQ / 10, DAC_MAX_FREQ / 10);
        prevPotVal = potVal;
        g_genSigDma->Stop();
        float actFreq;
        g_genSigDma->SetWaveForm(DAC_WAVEFORM, freq, &actFreq);
        g_genSigDma->Start();
        Serial.print("Set frequency: ");
        Serial.print((int)freq);
        Serial.print(", actual freq: ");
        Serial.println((int)actFreq);
    }
}

void updateTriggerValue()
{
    int potVal = 0;
    static int prevPotVal = 0;

    g_adcDma->ReadSingleValue(TRIG_CHANNEL, &potVal);

    if (abs(potVal - prevPotVal) > 100) {
        g_triggerVal = potVal;
        prevPotVal = potVal;
        Serial.print("Setting trigger: ");
        Serial.println(g_triggerVal);
    }
}

void updateAdcSampleRate()
{
    int potVal = 0;
    static int prevPotVal = 0;
  
    g_adcDma->ReadSingleValue(ADC_RATE_CHANNEL, &potVal);

    if (abs(potVal - prevPotVal) > 100) {
        g_adcSampleRate = 1000 * map(potVal, 0, ANALOG_MAX_VAL, ADC_MIN_SAMPLE_RATE / 1000, ADC_MAX_SAMPLE_RATE / 1000);
        prevPotVal = potVal;
        Serial.print("Setting ADC SR: ");
        Serial.println(g_adcSampleRate);
    }
}

int g_drawLastX = 0;
int g_drawLastY = 0;

void drawBegin()
{
    g_drawLastX = 0;
    g_drawLastY = 0;

    TFTscreen.background(255, 255, 255);

    // draw in red
    TFTscreen.fill(255, 0, 0);
    TFTscreen.stroke(255, 0, 0);
}

void drawSamples(uint16_t *samples, int count)
{
    int zoom = 4;

    if (g_drawLastX == 0) {
        g_drawLastY = samples[0];
    }

    for (int iSample = 1; iSample < count / zoom; iSample++) {
        TFTscreen.line(g_drawLastX, g_drawLastY, g_drawLastX + zoom, samples[iSample]);
        g_drawLastX += zoom;
        g_drawLastY = samples[iSample];

        if (g_drawLastX >= TFT_WIDTH)
            return;
    }
}

void drawEnd()
{
    // draw in green
    TFTscreen.fill(0, 255, 0);
    TFTscreen.stroke(0, 255, 0);

    int y = map(g_triggerVal, 0, ANALOG_MAX_VAL, TFT_HEIGHT - 1, 0);

    TFTscreen.line(0, y, TFT_WIDTH, y);
}

void loop()
{
    bool bTriggerTimeout;

    g_adcDma->SetSampleRate(g_adcSampleRate);
    g_adcDma->Start();
    g_adcDma->SetTrigger(g_triggerVal, AdcDma::RisingEdge, SCOPE_CHANNEL, TRIGGER_TIMEOUT);
    g_adcDma->SetTriggerPreBuffersCount(4);
    g_adcDma->TriggerEnable(true);
    while (!g_adcDma->DidTriggerComplete(&bTriggerTimeout)){}
    uint16_t *triggerBufAddress = NULL;
    int triggerSampleIndex = 0;

    bool bDrawnTrigger = false;

    if (!bTriggerTimeout) {
        g_adcDma->GetTriggerSampleAddress(&triggerBufAddress, &triggerSampleIndex);
        adcdma_print("TriggerSample buf 0x%08x, index %d\n", triggerBufAddress, triggerSampleIndex);
    }
    else {
        Serial.println("Trigger timeout !");
        bDrawnTrigger = true;
    }

    int drawnSamples = 0;

    drawBegin();

    while (drawnSamples < TFT_WIDTH) {
        uint16_t *buf;
        int count = g_adcDma->GetBufSize();
        buf = g_adcDma->GetReadBuffer();
        g_adcDma->AdvanceReadBuffer();

        adcdma_print("Got read buffer 0x%08x, count %d\n", buf, count);

        if (!bDrawnTrigger) {
            if (buf != triggerBufAddress) {
                adcdma_print("Not trigger buffer (0x%08x != 0x%08x)\n", buf, triggerBufAddress);
                continue;
            }
            /*
            adcdma_print("Got trigger buffer\n");
            Serial.print("Trigger sample: ");
            Serial.println(buf[triggerSampleIndex]);
            Serial.print("Trigger sample index: ");
            Serial.println(triggerSampleIndex);
            */
            bDrawnTrigger = true;
            buf += triggerSampleIndex;
            count -= triggerSampleIndex;
        }

        adcdma_print("Will map %d values on buffer 0x%08x\n", count, buf);
        mapBufferValues(buf, count);
        drawSamples(buf, count);

        drawnSamples += count;
    }

    drawEnd();

    // Read remaining buffers
    while (g_adcDma->GetReadBuffer()) {
        g_adcDma->AdvanceReadBuffer();
    }

    updateSignalFreq();
    updateTriggerValue();
    updateAdcSampleRate();

    g_genSigDma->Loop(true);
}

