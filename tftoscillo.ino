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
uint g_dacMinFreq = DAC_MIN_FREQ;
uint g_dacMaxFreq = DAC_MAX_FREQ;
uint g_dacFreq = g_dacMinFreq;
GENSIGDMA_WAVEFORM g_dacWaveForm = WAVEFORM_SINUS;

#define TRIGGER_MIN_VAL    0
#define TRIGGER_MAX_VAL    ANALOG_MAX_VAL
int g_triggerVal = TRIGGER_MAX_VAL / 2;
int g_zoom = 1;

uint16_t g_samples0[TFT_WIDTH];
uint16_t g_samples1[TFT_WIDTH];

uint16_t *g_samples = g_samples0;


AdcDma::TriggerMode g_triggerMode = AdcDma::RisingEdge;

// In free run mode, we'll always be at 12 bits res
#define SAMPLE_MAX_VAL ((1 << 12) - 1)

/**** GLOBAL VARIABLES ****/

// Pointer on GenSigDma object
GenSigDma *g_genSigDma = NULL;
AdcDma *g_adcDma = NULL;

SerialCommand SCmd;

// Colors definitions
#define BG_COLOR         255, 255, 255
#define GRAPH_COLOR      255,   0,   0
#define TRIGGER_COLOR    0  , 255,   0
#define TEXT_COLOR       0  ,   0, 255

// Y position for texts
#define TEXT_Y_OFFSET 2

// Tft screen instance
TFT TFTscreen = TFT(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

void setup() {

	Serial.begin(115200);

	SCmd.addCommand("tgmode", triggerModeHandler);
	SCmd.addCommand("fr", freqRangeHandler);
	SCmd.addCommand("sr", sampleRateRangeHandler);
	SCmd.addCommand("zoom", zoomHandler);
	SCmd.addCommand("form", formHandler);
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
	// Expecting 2 parameters, if one is given, then set a one value range
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
		strRangeEnd = strRangeStart;
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
		strRangeEnd = strRangeStart;
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

	if (strMode == NULL) {
		switch (g_triggerMode) {
		case AdcDma::RisingEdge :
			Serial.println("rising");
			break;

		case AdcDma::FallingEdge :
			Serial.println("falling");
			break;

		default :
			break;

		return;
		}
	}

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

void formHandler()
{
	char * strForm = SCmd.next();

	if (strForm == NULL) {
		switch (g_dacWaveForm) {
		case WAVEFORM_SAW :
			Serial.println("saw");
			break;
		case WAVEFORM_SINUS :
			Serial.println("sinus");
			break;
		case WAVEFORM_SQUARE :
			Serial.println("square");
			break;
		case WAVEFORM_TRIANGLE :
			Serial.println("triangle");
			break;
		default :
			break;
		}
	}

	if (strcmp(strForm, "saw") == 0) {
		g_dacWaveForm = WAVEFORM_SAW;
	}
	else if (strcmp(strForm, "sinus") == 0) {
		g_dacWaveForm = WAVEFORM_SINUS;
	}
	else if (strcmp(strForm, "square") == 0) {
		g_dacWaveForm = WAVEFORM_SQUARE;
	}
	else if (strcmp(strForm, "triangle") == 0) {
		g_dacWaveForm = WAVEFORM_TRIANGLE;
	}
	else {
		return;
	}

	updateSignalFreq(true);
}

void mapBufferValues(uint16_t *buf, int count)
{
	for (int iSample = 0; iSample < count; iSample++) {
		g_samples[iSample] = map(buf[iSample], 0, SAMPLE_MAX_VAL, TFT_HEIGHT - 1, 0);
	}
}

void updateSignalFreq(bool bForceUpdate)
{
	int potVal = 0;
	static int prevPotVal = -1000;

	float freq = 6000.;

	g_adcDma->ReadSingleValue(FREQ_CHANNEL, &potVal);

	if (bForceUpdate || (abs(potVal - prevPotVal) > 100) ) {
		// Divide by 4 to avoid clipping
		freq = map(potVal / 4, 0, ANALOG_MAX_VAL / 4, g_dacMinFreq, g_dacMaxFreq);
		prevPotVal = potVal;
		g_genSigDma->Stop();
		float actFreq;
		g_genSigDma->SetWaveForm(g_dacWaveForm, freq, &actFreq);
		g_dacFreq = actFreq;
		g_genSigDma->Start();
		Serial.print("Set frequency: ");
		Serial.print(freq);
		Serial.print(", actual freq: ");
		Serial.println(actFreq);
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

	if (bForceUpdate || (abs(potVal - prevPotVal) > 10) ) {
		// Divide by 4 to avoid clipping
		g_adcSampleRate = map(potVal / 4, 0, ANALOG_MAX_VAL / 4, g_adcMinSampleRate, g_adcMaxSampleRate);
		prevPotVal = potVal;
		Serial.print("Setting ADC SR: ");
		Serial.println(g_adcSampleRate);
	}
}

void swapSampleBuffer()
{
	if (g_samples == g_samples0)
		g_samples = g_samples1;
	else
		g_samples = g_samples0;
}

uint16_t *getNewSamples()
{
	return g_samples;
}

uint16_t *getOldSamples()
{
	if (g_samples == g_samples0)
		return g_samples1;
	else
		return g_samples0;
}

void drawBegin()
{
	char textBuf[40];

	static int s_prevTriggerVal = -1;
	static uint s_prevDacFreq = -1;
	static uint s_prevSampleRate = 0;

	static uint32_t s_prevDrawTime = 0;

	while (millis() - s_prevDrawTime < 50) {
	}
	s_prevDrawTime = millis();

	// Redraw trigger line (always since signal may overwrite it)
	for (int i = 0; i < 2; i++) {
		int y;
		if (i == 0) {
			// Erase
			TFTscreen.stroke(BG_COLOR);
			y = s_prevTriggerVal;
		}
		else {
			// Draw
			TFTscreen.stroke(TRIGGER_COLOR);
			y = g_triggerVal;
		}
		y = map(y, 0, ANALOG_MAX_VAL, TFT_HEIGHT - 1, 0);
		TFTscreen.line(0, y, TFT_WIDTH, y);
	}

	// Print signal freq
	if (s_prevDacFreq != g_dacFreq) {
		for (int i = 0; i < 2; i++) {
			String s;
			s = String("Fq:");
			if (i == 0) {
				// erase prev value
				s += String(s_prevDacFreq);
				TFTscreen.stroke(BG_COLOR);
			}
			else {
				// draw new value
				s += String(g_dacFreq);
				TFTscreen.stroke(TEXT_COLOR);
			}
			s += String("Hz");
			s.toCharArray(textBuf, 15);
			TFTscreen.text(textBuf, 10, TEXT_Y_OFFSET);
		}
	}

	// Print sample rate
	if (s_prevSampleRate != g_adcSampleRate) {
		for (int i = 0; i < 2; i++) {
			String s = String("SR:");
			if (i == 0) {
				// erase prev value
				s += String(s_prevSampleRate);
				TFTscreen.stroke(BG_COLOR);
			}
			else {
				// draw new value
				s += String(g_adcSampleRate);
				TFTscreen.stroke(TEXT_COLOR);
			}
			s += String("Hz");
			s.toCharArray(textBuf, 15);
			TFTscreen.text(textBuf, TFT_WIDTH / 2, TEXT_Y_OFFSET);
		}
	}

	s_prevDacFreq = g_dacFreq;
	s_prevTriggerVal = g_triggerVal;
	s_prevSampleRate = g_adcSampleRate;
}

void drawSamples()
{
	static int s_prevZoom = 1;
	uint16_t *samples;

	for (int i = 0; i < 2; i++) {
		int zoom;
		if (i == 0) {
			// erase old samples
			TFTscreen.stroke(BG_COLOR);
			samples = getOldSamples();
			zoom = s_prevZoom;
		}
		else {
			// draw new samples
			TFTscreen.stroke(GRAPH_COLOR);
			samples = getNewSamples();
			zoom = g_zoom;
		}

		int lastX = 0;
		int lastY = samples[0];

		int iSample = 1;
		for (;;) {
			uint16_t sample = samples[iSample];
			TFTscreen.line(lastX, lastY, lastX + zoom, sample);
			lastX += zoom;
			lastY = sample;
			iSample++;
			// Need this for zoom
			if (iSample >= TFT_WIDTH)
				break;
			if (lastX >= TFT_WIDTH)
				break;
		}
	}

	s_prevZoom = g_zoom;
}

void drawEnd()
{
}

void loop()
{
	bool bTriggerTimeout;

	SCmd.readSerial();

	// compute buffer size to have one trigger every tTrig secs with bufCount buffers
	float tTrig = 0.1; // Try to have one trigger every 1/10s

	// tTrig = bufCount * bufSize / sampleRate
	// => bufSize = sampleRate * tTrig / bufCount

	int   bufCount = ADC_DMA_DEF_BUF_COUNT;
	float fBufSize = (float)g_adcSampleRate * tTrig / (float)bufCount;

	int bufSize = (int)ceil(fBufSize);

	//p("Setting bufSize %d, sample rate %d\n", bufSize, g_adcSampleRate);

	//g_adcDma->SetBuffers(ADC_DMA_DEF_BUF_COUNT, bufSize);

	g_adcDma->SetSampleRate(g_adcSampleRate);
	g_adcDma->SetBuffers(bufCount, bufSize);
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

	// Use new buffer before sampling
	swapSampleBuffer();

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

		if (drawnSamples + count > TFT_WIDTH)
			count = TFT_WIDTH - drawnSamples;

		adcdma_print("Will map %d values on buffer 0x%08x\n", count, buf);
		mapBufferValues(buf, count);

		drawnSamples += count;
	}

	drawBegin();
	drawSamples();
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

