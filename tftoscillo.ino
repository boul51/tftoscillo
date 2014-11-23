//Just to have auto-completion working in qtCreator..
#include <GenSigDma.h>
#include <AdcDma.h>

#include <SerialCommand.h>

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

#define TRIGGER_TIMEOUT 100

// Tests show that ADC doesn't sample well with freq >= 1830000 Hz.
#define ADC_MIN_SAMPLE_RATE  100000
#define ADC_MAX_SAMPLE_RATE 1830000
uint g_adcMinSampleRate = ADC_MIN_SAMPLE_RATE;
uint g_adcMaxSampleRate = ADC_MAX_SAMPLE_RATE;
uint g_adcSampleRate = ADC_MIN_SAMPLE_RATE;

#define DAC_MIN_FREQ    5000
#define DAC_MAX_FREQ    800000
#define DAC_WAVEFORM    WAVEFORM_SINUS
int g_dacMinFreq = DAC_MIN_FREQ;
int g_dacMaxFreq = DAC_MAX_FREQ;
int g_dacFreq = g_dacMinFreq;

#define TRIGGER_MIN_VAL    0
#define TRIGGER_MAX_VAL    ANALOG_MAX_VAL
int g_triggerVal = TRIGGER_MAX_VAL / 2;

int g_zoom = 1;

AdcDma::TriggerMode g_triggerMode = AdcDma::RisingEdge;

// In free run mode, we'll always be at 12 bits res
#define SAMPLE_MAX_VAL ((1 << 12) - 1)

/**** GLOBAL VARIABLES ****/

// Pointer on GenSigDma object
GenSigDma *g_genSigDma = NULL;
AdcDma *g_adcDma = NULL;

SerialCommand SCmd;

// Tft screen instance
TFT TFTscreen = TFT(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

void setup() {

    Serial.begin(115200);

	SCmd.addCommand("tgmode", triggerModeHandler);
	SCmd.addCommand("fr", freqRangeHandler);
	SCmd.addCommand("sr", sampleRateRangeHandler);
	SCmd.addCommand("zoom", zoomHandler);
	SCmd.addDefaultHandler(defaultHandler);
  
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

void defaultHandler()
{
	Serial.println("Invalid command received\n");
}

void zoomHandler()
{
	char * strZoom;

	strZoom = SCmd.next();

	if (strZoom == NULL) {
		Serial.println(g_zoom);
		return;
	}

	g_zoom = atoi(strZoom);
}

void freqRangeHandler()
{
	// Expecting 2 parameters
	char * strRangeStart;
	char * strRangeEnd;

	int rangeStart;
	int rangeEnd;

	strRangeStart = SCmd.next();
	if (strRangeStart == NULL) {
		Serial.print(g_dacFreq);
		Serial.print(" (");
		Serial.print(g_dacMinFreq);
		Serial.print(" - ");
		Serial.print(g_dacMaxFreq);
		Serial.println(")");
		return;
	}

	strRangeEnd = SCmd.next();
	if (strRangeEnd == NULL) {
		return;
	}

	rangeStart = atoi(strRangeStart);
	rangeEnd   = atoi(strRangeEnd);

	if (rangeEnd < rangeStart) {
		return;
	}

	g_dacMinFreq = rangeStart;
	g_dacMaxFreq = rangeEnd;

	updateSignalFreq(true);

	return;
}

void sampleRateRangeHandler()
{
	// Expecting 2 parameters
	char * strRangeStart;
	char * strRangeEnd;

	int rangeStart;
	int rangeEnd;

	strRangeStart = SCmd.next();
	if (strRangeStart == NULL) {
		Serial.print(g_adcSampleRate);
		Serial.print(" (");
		Serial.print(g_adcMinSampleRate);
		Serial.print(" - ");
		Serial.print(g_adcMaxSampleRate);
		Serial.println(")");
		return;
	}

	strRangeEnd = SCmd.next();
	if (strRangeEnd == NULL) {
		return;
	}

	rangeStart = atol(strRangeStart);
	rangeEnd   = atol(strRangeEnd);

	if (rangeEnd < rangeStart) {
		return;
	}

	g_adcMinSampleRate = rangeStart;
	g_adcMaxSampleRate = rangeEnd;

	updateAdcSampleRate(true);

	return;
}

void triggerModeHandler()
{
	char * strMode = SCmd.next();

	if (strMode == NULL)
		return;

	if ( (strcmp(strMode, "rising") == 0) ||
		 (strcmp(strMode, "r") == 0) ) {
		g_triggerMode = AdcDma::RisingEdge;
	}
	else if ( (strcmp(strMode, "falling") == 0) ||
			  (strcmp(strMode, "f") == 0) ) {
		g_triggerMode = AdcDma::FallingEdge;
	}
	else {
		return;
	}
}

void mapBufferValues(uint16_t *buf, int count)
{
    for (int iSample = 0; iSample < count; iSample++) {
		buf[iSample] = map(buf[iSample] & 0x0FFF, 0, SAMPLE_MAX_VAL, TFT_HEIGHT - 1, 0);
    }
}

void updateSignalFreq(bool bForceUpdate)
{
    int potVal = 0;
    static int prevPotVal = -1000;
  
    float freq = 6000.;
  
    g_adcDma->ReadSingleValue(FREQ_CHANNEL, &potVal);
    
	if (bForceUpdate || (abs(potVal - prevPotVal) > 100) ) {
		freq = 10 * (int)map(potVal, 0, ANALOG_MAX_VAL, g_dacMinFreq / 10, g_dacMaxFreq / 10);
        prevPotVal = potVal;
        g_genSigDma->Stop();
        float actFreq;
        g_genSigDma->SetWaveForm(DAC_WAVEFORM, freq, &actFreq);
		g_dacFreq = actFreq;
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

void updateAdcSampleRate(bool bForceUpdate)
{
    int potVal = 0;
    static int prevPotVal = 0;
  
    g_adcDma->ReadSingleValue(ADC_RATE_CHANNEL, &potVal);

	if (bForceUpdate || (abs(potVal - prevPotVal) > 100) ) {
		g_adcSampleRate = 1000 * map(potVal, 0, ANALOG_MAX_VAL, g_adcMinSampleRate / 1000, g_adcMaxSampleRate / 1000);
		//g_adcSampleRate = 10;
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
    if (g_drawLastX == 0) {
        g_drawLastY = samples[0];
    }

	for (int iSample = 1; iSample < count / g_zoom; iSample++) {
		TFTscreen.line(g_drawLastX, g_drawLastY, g_drawLastX + g_zoom, samples[iSample]);
		g_drawLastX += g_zoom;
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

	SCmd.readSerial();

	// compute number of samples to have one trigger every 1s
	int bufSize = g_adcSampleRate * 1 / ADC_DMA_DEF_BUF_COUNT;

	if (bufSize <= 1) {
		bufSize = 2;
	}

	//p("Setting bufSize %d, sample rate %d\n", bufSize, g_adcSampleRate);

	//g_adcDma->SetBuffers(ADC_DMA_DEF_BUF_COUNT, bufSize);

    g_adcDma->SetSampleRate(g_adcSampleRate);
    g_adcDma->Start();
	g_adcDma->SetTrigger(g_triggerVal, g_triggerMode, SCOPE_CHANNEL, TRIGGER_TIMEOUT);
	g_adcDma->SetTriggerPreBuffersCount(2);
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

		//adcdma_print("Got read buffer 0x%08x, count %d\n", buf, count);

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

	updateSignalFreq(false);
    updateTriggerValue();
	updateAdcSampleRate(false);

    g_genSigDma->Loop(true);
}

