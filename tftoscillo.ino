#include <GenSigDma.h>
#include <AdcDma.h>
#include <LibDbg.h>

#include <SerialCommand.h>

#include <SPI.h>
#include <TFT.h>  // Arduino LCD library

#include "tftoscillo.h"

/**** DEFINES ****/
// Debug definitions
#define pf(doPrint, ...) LibDbg::pf(doPrint, __FUNCTION__, __VA_ARGS__)

#define DBG_LOOP	true
#define DBG_VERBOSE	false
#define DBG_TEXT	false


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

#define TRIGGER_TIMEOUT				100

// Rate from which we switch to HS / LS mode
#define ADC_SAMPLE_RATE_LOW_LIMIT	300

// Tests show that ADC doesn't sample well with freq >= 1830000 Hz.
#define ADC_MIN_SAMPLE_RATE  100
//#define ADC_MAX_SAMPLE_RATE 1825000
#define ADC_MAX_SAMPLE_RATE 1000
uint g_adcMinSampleRate = ADC_MIN_SAMPLE_RATE;
uint g_adcMaxSampleRate = ADC_MAX_SAMPLE_RATE;
uint g_adcSampleRate = ADC_MIN_SAMPLE_RATE;

#define DAC_MIN_FREQ    5000
#define DAC_MAX_FREQ    800000
uint g_dacMinFreq = DAC_MIN_FREQ;
uint g_dacMaxFreq = DAC_MAX_FREQ;
uint g_dacFreq = g_dacMinFreq;
uint g_dacFreqMult = 1000;
GenSigDma::WaveForm g_dacWaveForm = GenSigDma::WaveFormSinus;

#define TRIGGER_MIN_VAL    0
#define TRIGGER_MAX_VAL    ANALOG_MAX_VAL
int g_triggerVal = TRIGGER_MAX_VAL / 2;
int g_zoom = 1;

uint g_frameRate = 0;		// Scope fps

#define ERASE_MODE_ALL 0	// Erase all samples, then draw all samples
#define ERASE_MODE_ALT 1	// Alternate erasing and drawing samples
int g_eraseMode = ERASE_MODE_ALT;

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
#define TRIGGER_COLOR    50 , 196,  10
#define TEXT_COLOR       0  ,   0, 255
#define VGRID_COLOR      170, 170, 170
#define HGRID_COLOR      170, 170, 170

// Length of trigger arrow
#define TRIGGER_ARROW_LEN		8
#define TRIGGER_ARROW_HEIGHT	3

// Definitions for text placement and size
#define TEXT_FONT_HEIGHT		8
#define TEXT_FONT_WIDTH			5
#define TEXT_Y_OFFSET			2
#define TEXT_DOWN_Y_OFFSET		TFT_HEIGHT - TEXT_Y_OFFSET - TEXT_FONT_HEIGHT

// Grid definitions
#define VGRID_START		(TFT_WIDTH / 2)		// x-axis position of the first vertical line
#define VGRID_INTERVAL  50					// distance between vertical lines

#define HGRID_START     (TFT_HEIGHT / 2)	// y-axis position of the first horizontal line
#define HGRID_INTERVAL  VGRID_INTERVAL		// distance between horizontal lines

// Tft screen instance
TFT TFTscreen = TFT(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

void setup() {

	Serial.begin(115200);

	SCmd.addCommand("tgmode", triggerModeHandler);
	SCmd.addCommand("fr", freqMultHandler);
	SCmd.addCommand("sr", sampleRateRangeHandler);
	SCmd.addCommand("zoom", zoomHandler);
	SCmd.addCommand("form", formHandler);
	SCmd.addCommand("ermode", eraseModeHandler);
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

void freqMultHandler()
{
	// Expecting 1 parameter
	char * strFreqMult;

	strFreqMult = SCmd.next();
	if (strFreqMult == NULL) {
		Serial.print(g_dacFreqMult);
		Serial.println("");
		return;
	}

	g_dacFreqMult = atoi(strFreqMult) / 10;

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

void eraseModeHandler()
{
	char * strMode = SCmd.next();

	if (strMode == NULL) {
		switch (g_eraseMode) {
		case ERASE_MODE_ALL :
			Serial.println("all");
			break;

		case ERASE_MODE_ALT :
			Serial.println("alt");
			break;

		default :
			break;

		return;
		}
	}

	if (strcmp(strMode, "all") == 0) {
		g_eraseMode = ERASE_MODE_ALL;
	}
	else if (strcmp(strMode, "alt") == 0) {
		g_eraseMode = ERASE_MODE_ALT;
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
		case GenSigDma::WaveFormSaw :
			Serial.println("saw");
			break;
		case GenSigDma::WaveFormSinus :
			Serial.println("sinus");
			break;
		case GenSigDma::WaveFormSquare :
			Serial.println("square");
			break;
		case GenSigDma::WaveFormTriangle :
			Serial.println("triangle");
			break;
		default :
			break;
		}
	}

	if (strcmp(strForm, "saw") == 0) {
		g_dacWaveForm = GenSigDma::WaveFormSaw;
	}
	else if (strcmp(strForm, "sinus") == 0) {
		g_dacWaveForm = GenSigDma::WaveFormSinus;
	}
	else if (strcmp(strForm, "square") == 0) {
		g_dacWaveForm = GenSigDma::WaveFormSquare;
	}
	else if (strcmp(strForm, "triangle") == 0) {
		g_dacWaveForm = GenSigDma::WaveFormTriangle;
	}
	else {
		return;
	}

	updateSignalFreq(true);
}

void mapBufferValues(int offset, uint16_t *buf, int count)
{
	for (int iSample = 0; iSample < count; iSample++) {
		g_samples[iSample + offset] = map(buf[iSample], 0, SAMPLE_MAX_VAL, TFT_HEIGHT - 1, 0);
	}
}

void updateSignalFreq(bool bForceUpdate)
{
	int potVal = 0;
	static int prevPotVal = -1000;

	//float freq = 6000.;
	float freq = 1.;

	g_adcDma->ReadSingleValue(FREQ_CHANNEL, &potVal);

	// map won't do the clipping, do it here..
	if (potVal > POT_ANALOG_MAX_VAL)
		potVal = POT_ANALOG_MAX_VAL;

	if (bForceUpdate || (abs(potVal - prevPotVal) > 20) ) {
		// Map int 0-1000 range, then divide by 100 to obtain a value between 0 and 10
		//freq = (float)(map(potVal, 0, ANALOG_MAX_VAL, 0, 1000)) / 100.;
		freq = (float)(map(potVal, 0, POT_ANALOG_MAX_VAL, 0, 1000)) / 100.;
		freq *= (float)g_dacFreqMult;
		if (freq == 0.0)
			freq = 1.0;
		prevPotVal = potVal;
		g_genSigDma->Stop();
		float actFreq;
		g_genSigDma->SetWaveForm(g_dacWaveForm, freq, &actFreq);
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
	static uint32_t s_prevDrawTime = 0;

	while (millis() - s_prevDrawTime < 50) {
	}
	s_prevDrawTime = millis();
}

void drawTriggerArrow()
{
	static int s_prevTriggerVal = -1;

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
		TFTscreen.line(0, y, TRIGGER_ARROW_LEN, y);

		/* Use this to draw arrow towards right */
		// TFTscreen.line(TRIGGER_ARROW_LEN, y, TRIGGER_ARROW_LEN - TRIGGER_ARROW_HEIGHT, y - TRIGGER_ARROW_HEIGHT);
		// TFTscreen.line(TRIGGER_ARROW_LEN, y, TRIGGER_ARROW_LEN - TRIGGER_ARROW_HEIGHT, y + TRIGGER_ARROW_HEIGHT);

		/* Use this to draw arrow towards left */

		TFTscreen.line(0, y, TRIGGER_ARROW_HEIGHT, y - TRIGGER_ARROW_HEIGHT);
		TFTscreen.line(0, y, TRIGGER_ARROW_HEIGHT, y + TRIGGER_ARROW_HEIGHT);
	}

	s_prevTriggerVal = g_triggerVal;
}

void drawTexts(bool bForceDrawText)
{
	char textBuf[40];

	static uint s_prevDacFreq = -1;
	static uint s_prevSampleRate = 0;
	static uint s_prevFrameRate = 0;

	// Print signal freq
	if ( (s_prevDacFreq != g_dacFreq) || bForceDrawText ) {
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
	if ( (s_prevSampleRate != g_adcSampleRate) || bForceDrawText ) {
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

	// Print frame rate
	if ( (s_prevFrameRate != g_frameRate) || bForceDrawText ) {
		for (int i = 0; i < 2; i++) {
			String s = String("FR:");
			if (i == 0) {
				// erase prev value
				s += String(s_prevFrameRate);
				TFTscreen.stroke(BG_COLOR);
			}
			else {
				// draw new value
				s += String(g_frameRate);
				TFTscreen.stroke(TEXT_COLOR);
			}
			s += String("fps");
			s.toCharArray(textBuf, 15);
			pf(DBG_TEXT, "drawing text at %d:%d\r\n", 10, TEXT_DOWN_Y_OFFSET);
			TFTscreen.text(textBuf, 10, TEXT_DOWN_Y_OFFSET);
		}
	}

	s_prevDacFreq = g_dacFreq;
	s_prevSampleRate = g_adcSampleRate;
	s_prevFrameRate = g_frameRate;
}

inline bool isPointOnGrid(int x, int y)
{
	if ( (x % VGRID_INTERVAL) == VGRID_START)
		return true;

	if ( (y % HGRID_INTERVAL) == HGRID_START)
		return true;

	return false;
}

void drawGrid()
{
	// Draw vertival grid (vertical lines)
	TFTscreen.stroke(VGRID_COLOR);
	int xStart = VGRID_START;
	int xOffset = 0;
	while ( (xStart + xOffset < TFT_WIDTH) || (xStart - xOffset > 0) ) {
		TFTscreen.line(xStart + xOffset, 20, xStart + xOffset, TFT_HEIGHT);
		TFTscreen.line(xStart - xOffset, 20, xStart - xOffset, TFT_HEIGHT);
		xOffset += VGRID_INTERVAL;
	}

	// Draw horizontal grid (horizontal lines)
	TFTscreen.stroke(HGRID_COLOR);
	int yStart = HGRID_START;
	int yOffset = 0;
	while ( (yStart + yOffset < TFT_HEIGHT) || (yStart - yOffset > 0) ) {
		TFTscreen.line(0,  yStart + yOffset, TFT_WIDTH, yStart + yOffset);
		TFTscreen.line(0,  yStart - yOffset, TFT_WIDTH, yStart - yOffset);
		yOffset += HGRID_INTERVAL;
	}
}

void drawSamples()
{
	static int s_prevZoom = 1;

	if (g_eraseMode == ERASE_MODE_ALT) {

		uint16_t *oldSamples = getOldSamples();
		uint16_t *newSamples = getNewSamples();

		int iSample = 1;

		int lastXDraw = 0;
		int lastYDraw = newSamples[0];

		// Erase first old sample
		TFTscreen.stroke(BG_COLOR);
		TFTscreen.line(0, oldSamples[0], s_prevZoom, oldSamples[1]);
		int lastXErase = s_prevZoom;
		int lastYErase = oldSamples[1];

		// Erase sample iSample+1 while drawing sample iSample
		// otherwise, new drawn line could be overwritten by erased line
		for (;;) {
			uint16_t sample;

			// Erase old sample
			if (iSample + 1 < TFT_WIDTH) {
				sample = oldSamples[iSample + 1];
				TFTscreen.stroke(BG_COLOR);
				TFTscreen.line(lastXErase, lastYErase, lastXErase + s_prevZoom, sample);
				lastXErase += s_prevZoom;
				lastYErase = sample;
			}

			// Draw new sample
			sample = newSamples[iSample];
			TFTscreen.stroke(GRAPH_COLOR);
			TFTscreen.line(lastXDraw, lastYDraw, lastXDraw + g_zoom, sample);
			lastXDraw += g_zoom;
			lastYDraw = sample;

			iSample++;

			// Need this for zoom
			if (iSample >= TFT_WIDTH)
				break;
			if (lastXErase >= TFT_WIDTH && lastXDraw >= TFT_WIDTH)
				break;
		}
	}
	else {
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
	}

	s_prevZoom = g_zoom;
}

void erasePrevSamples()
{
	TFTscreen.stroke(BG_COLOR);

	for (int i = 0; i < TFT_WIDTH - 1; i++) {
		TFTscreen.line(i, g_samples[i], i+1, g_samples[i+1]);
	}
}

void drawSample(uint16_t sample, int iSample)
{
	static uint16_t prevSample;

	sample = map(sample, 0, SAMPLE_MAX_VAL, TFT_HEIGHT - 1, 0);

	if (iSample != 0) {
		TFTscreen.stroke(GRAPH_COLOR);
		TFTscreen.line(iSample - 1, prevSample, iSample, sample);
	}

	g_samples[iSample] = sample;

	prevSample = sample;
}

void computeFrameRate()
{
	uint sec = millis() / 1000;
	static uint s_prevSec = 0;
	static uint s_loops = 0;

	if (sec != s_prevSec) {
		s_prevSec = sec;
		g_frameRate = s_loops;
		s_loops = 0;
	}
	s_loops++;
}

void getAndDrawSampleSlow()
{
	static int s_iSample = 0;
	static bool s_bFirstCall = true;


	if (s_iSample == 0) {

		// compute buffer size to have one trigger every tTrig secs with bufCount buffers
		float tTrig = 0.1; // Try to have one trigger every 1/10s

		int   bufCount = ADC_DMA_DEF_BUF_COUNT;
		float fBufSize = (float)g_adcSampleRate * tTrig / (float)bufCount;

		int bufSize = (int)ceil(fBufSize);
		if (bufSize > ADC_DMA_DEF_BUF_SIZE)
			bufSize = ADC_DMA_DEF_BUF_SIZE;

		// Make sure we have enough samples to fill the screen..
		// use *2 to take x-position into account, todo: compute this precisely
		if (bufSize * bufCount < TFT_WIDTH * 2) {
			bufSize = TFT_WIDTH * 2 / bufCount;
		}

		pf(DBG_LOOP, "Setting bufSize %d, sample rate %d\r\n", bufSize, g_adcSampleRate);

		g_adcDma->SetSampleRate(g_adcSampleRate);
		g_adcDma->SetBuffers(bufCount, bufSize);
		g_adcDma->Start();

		//TFTscreen.background(BG_COLOR);
		if (s_bFirstCall) {
			s_bFirstCall = false;
		}
		else {
			erasePrevSamples();
		}

		drawTriggerArrow();
		drawGrid();
		drawTexts(false);
	}

	// Wait until next sample is available
	uint16_t sample;
	bool isTgSample;
	while (!g_adcDma->GetNextSample(&sample, NULL, &isTgSample)) {}
	drawSample(sample, s_iSample);

	s_iSample++;

	if (s_iSample == TFT_WIDTH) {
		g_adcDma->Stop();
		s_iSample = 0;

		updateSignalFreq(false);
		updateTriggerValue();
		updateAdcSampleRate(false);
	}
}

void getAndDrawSamplesFast()
{
	bool bTriggerTimeout;

	// compute buffer size to have one trigger every tTrig secs with bufCount buffers
	float tTrig = 0.1; // Try to have one trigger every 1/10s

	int   bufCount = ADC_DMA_DEF_BUF_COUNT;
	float fBufSize = (float)g_adcSampleRate * tTrig / (float)bufCount;

	int bufSize = (int)ceil(fBufSize);
	if (bufSize > ADC_DMA_DEF_BUF_SIZE)
		bufSize = ADC_DMA_DEF_BUF_SIZE;

	// Make sure we have enough samples to fill the screen..
	// use *2 to take x-position into account, todo: compute this precisely
	if (bufSize * bufCount < TFT_WIDTH * 2) {
		bufSize = TFT_WIDTH * 2 / bufCount;
	}

	pf(DBG_LOOP, "Setting bufSize %d, sample rate %d\r\n", bufSize, g_adcSampleRate);

	g_adcDma->SetSampleRate(g_adcSampleRate);
	g_adcDma->SetBuffers(bufCount, bufSize);
	g_adcDma->Start();
	g_adcDma->SetTrigger(g_triggerVal, g_triggerMode, SCOPE_CHANNEL, TRIGGER_TIMEOUT);
	g_adcDma->SetTriggerPreBuffersCount(0);
	g_adcDma->TriggerEnable(true);

	// Wait for trigger
	while (!g_adcDma->DidTriggerComplete(&bTriggerTimeout)){}

	uint16_t *triggerBufAddress = NULL;
	int triggerSampleIndex = 0;
	bool bDrawnTrigger = false;

	if (!bTriggerTimeout) {
		g_adcDma->GetTriggerSampleAddress(&triggerBufAddress, &triggerSampleIndex);
		pf(DBG_LOOP, "TriggerSample buf 0x%08x, index %d\r\n", triggerBufAddress, triggerSampleIndex);
	}
	else {
		Serial.println("Trigger timeout !");
		bDrawnTrigger = true;
	}

	int drawnSamples = 0;

	// Use new buffer before sampling
	swapSampleBuffer();

	int iLoop = 0;

	while (drawnSamples < TFT_WIDTH) {
		uint16_t *buf;
		int count = bufSize;

		buf = g_adcDma->GetReadBuffer();

		pf(DBG_LOOP && DBG_VERBOSE, "Got buffer 0x%08x at loop %d, size %d\r\n", buf, iLoop, bufSize);
		iLoop++;

		if (buf == NULL) {
			Serial.println("Got empty read buffer !");
			break;
		}

		if (!bDrawnTrigger) {
			if (buf != triggerBufAddress) {
				continue;
			}
			bDrawnTrigger = true;
			buf += triggerSampleIndex;
			count -= triggerSampleIndex;
		}

		if (drawnSamples + count > TFT_WIDTH)
			count = TFT_WIDTH - drawnSamples;

		mapBufferValues(drawnSamples, buf, count);
		drawnSamples += count;

		pf(DBG_LOOP && DBG_VERBOSE, "loop %d, drawnSamples: %d\r\n", iLoop, drawnSamples);
	}

	//g_adcDma->Stop();

	//drawBegin();
	drawTriggerArrow();
	drawSamples();
	drawGrid();
	drawTexts(false);

	// todo: not needed (?)
	// Read remaining buffers
	//while (g_adcDma->GetReadBuffer()) {
	//}

}

void loop()
{
	if (g_adcSampleRate < ADC_SAMPLE_RATE_LOW_LIMIT) {
		getAndDrawSampleSlow();
	}
	else {
		getAndDrawSamplesFast();
		updateSignalFreq(false);
		updateTriggerValue();
		updateAdcSampleRate(false);
	}

	computeFrameRate();
	SCmd.readSerial();
}
