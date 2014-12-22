#include <GenSigDma.h>
#include <AdcDma.h>
#include <LibDbg.h>

#include <SerialCommand.h>

#include <SPI.h>
#include <TFT.h>  // Arduino LCD library

#include "tftoscillo.h"

/**** DEFINES ****/

// Debug zones
#define DBG_LOOP	false
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
#define SCOPE_CHANNEL_1     7
#define SCOPE_CHANNEL_2     3
#define SCOPE_CHANNEL_3     2
#define SCOPE_CHANNEL_4     1
#define FREQ_CHANNEL        6
#define ADC_RATE_CHANNEL    5
#define TRIG_CHANNEL        4

// Pot definitions

#define POT_ANALOG_MAX_VAL (988*ANALOG_MAX_VAL/1000)	// This is used for pot input. Max value read is not 4096 but around 4050 ie 98.8%
#define POT_ANALOG_DIFF				20					// Min difference between two pot value to consider is was moved


#define TRIGGER_TIMEOUT				100

// Trigger definitions
#define TRIGGER_STATUS_TIMEOUT		0
#define TRIGGER_STATUS_TRIGGERED	1
#define TRIGGER_STATUS_NONE			2
uint g_triggerStatus = TRIGGER_STATUS_NONE;

// Rate from which we switch to HS / LS mode
#define ADC_SAMPLE_RATE_LOW_LIMIT	300

#define SCOPE_DRAW_MODE_SLOW		0
#define SCOPE_DRAW_MODE_FAST		1
int g_scopeDrawMode = SCOPE_DRAW_MODE_FAST;

// Tests show that ADC doesn't sample well with freq >= 1830000 Hz.
// (Not so bad since nominal max rate is 1MHz)
#define ADC_MIN_SAMPLE_RATE  10
#define ADC_MAX_SAMPLE_RATE 1825000
//#define ADC_MAX_SAMPLE_RATE 1000
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
#define BG_COLOR				255, 255, 255
#define GRAPH_COLOR				255,   0,   0
#define TRIGGER_COLOR			 50, 196,  10
#define TEXT_COLOR				  0,   0, 255
#define VGRID_COLOR				170, 170, 170
#define VGRID_SEC_COLOR			230, 230, 230
#define HGRID_COLOR				170, 170, 170
#define HGRID_SEC_COLOR			230, 230, 230
#define TRIGGER_TIMEOUT_COLOR	255,   0,   0
#define TRIGGER_TRIGGERED_COLOR	  0, 255,   0
#define TRIGGER_NONE_COLOR		100, 100, 100

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
#define VGRID_INTERVAL  25					// distance between vertical lines

#define HGRID_START     (TFT_HEIGHT / 2)	// y-axis position of the first horizontal line
#define HGRID_INTERVAL  VGRID_INTERVAL		// distance between horizontal lines

#define HGRID_MARGIN	10					// distance between border and first horizontal line

// Tft screen instance
TFT TFTscreen = TFT(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

//int g_channels [] = {SCOPE_CHANNEL_1, SCOPE_CHANNEL_2, SCOPE_CHANNEL_3, SCOPE_CHANNEL_4};
int g_channels [] = {SCOPE_CHANNEL_1, SCOPE_CHANNEL_2};
int g_channelsCount = sizeof(g_channels) / sizeof(g_channels[0]);

// Channel descriptors
CHANNEL_DESC *g_channelDescs;

int g_minY, g_maxY;

uint8_t g_scopeColors[][3] = {
	{255, 102,  51},
	{204,  51, 255},
	{ 51, 204, 255},
	{255,  51, 204},
};

void setup() {

	Serial.begin(115200);

	g_channelDescs = (CHANNEL_DESC *)malloc(g_channelsCount * sizeof(CHANNEL_DESC));

	// Init channels descriptors
	for (int i = 0; i < g_channelsCount; i++) {
		g_channelDescs[i].channel = g_channels[i];
		g_channelDescs[i].bufSize = TFT_WIDTH;

		g_channelDescs[i].r = g_scopeColors[i][0];
		g_channelDescs[i].g = g_scopeColors[i][1];
		g_channelDescs[i].b = g_scopeColors[i][2];

		g_channelDescs[i].samples[0] = (uint16_t *)malloc(g_channelDescs[i].bufSize * sizeof(uint16_t));
		g_channelDescs[i].samples[1] = (uint16_t *)malloc(g_channelDescs[i].bufSize * sizeof(uint16_t));
		g_channelDescs[i].curSamples = g_channelDescs[i].samples[0];
		g_channelDescs[i].oldSamples = g_channelDescs[i].samples[1];

		memset(g_channelDescs[i].curSamples, 0, g_channelDescs[i].bufSize * sizeof(uint16_t));
		memset(g_channelDescs[i].oldSamples, 0, g_channelDescs[i].bufSize * sizeof(uint16_t));
	}

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
	g_genSigDma->SetTimerChannel(1);

	int adcChannel = SCOPE_CHANNEL_1;
	g_adcDma = AdcDma::GetInstance();
	g_adcDma->SetAdcChannels(&adcChannel, 1);
	g_adcDma->SetTimerChannel(2);

	// Call drawGrid to update g_minY and g_maxY
	drawGrid();

	enterScopeDrawMode(SCOPE_DRAW_MODE_SLOW);

	updateAdcSampleRate(true, -1);
	updateSignalFreq(true, -1);
	updateTriggerValue(-1);
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

	updateSignalFreq(true, -1);

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

	updateAdcSampleRate(true, -1);

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

	updateSignalFreq(true, -1);
}

inline CHANNEL_DESC *getChannelDesc(int channel)
{
	for (int i = 0; i < g_channelsCount; i++) {
		if (g_channelDescs[i].channel == channel)
			return &g_channelDescs[i];
	}

	return NULL;
}

void mapBufferValues(int offset, uint16_t *buf, int count)
{
	uint16_t rawSample;
	uint16_t sample;
	uint16_t mappedVal;
	int channel;
	int mappedSamples = 0;

	CHANNEL_DESC *channelDesc = &g_channelDescs[0];

	for (int iSample = 0; iSample < count; iSample++) {
		if (iSample / g_channelsCount + offset >= TFT_WIDTH)
			break;
		// Channel tag is set only if there are more than one channel
		if (g_channelsCount > 1) {
			rawSample = buf[iSample];
			channel = (rawSample & 0xF000) >> 12;
			sample  = (rawSample & 0x0FFF) >> 0;
			channelDesc = getChannelDesc(channel);
		}
		else {
			sample = buf[iSample];
		}
		mappedVal = map(sample, 0, SAMPLE_MAX_VAL, g_maxY, g_minY);

		channelDesc->curSamples[iSample / g_channelsCount + offset] = mappedVal;
		mappedSamples++;
	}

	return;
}

bool updateSignalFreq(bool bForceUpdate, int potVal)
{
	bool bFreqChanged = false;
	static int prevPotVal = -1000;

	float freq = 1.;

	if (potVal < 0) {
		g_adcDma->ReadSingleValue(FREQ_CHANNEL, &potVal);
	}

	// map won't do the clipping, do it here..
	if (potVal > POT_ANALOG_MAX_VAL)
		potVal = POT_ANALOG_MAX_VAL;

	if (bForceUpdate || (abs(potVal - prevPotVal) > POT_ANALOG_DIFF) ) {
		freq = (float)(map(potVal, 0, POT_ANALOG_MAX_VAL, 0, 1000)) / 100.;
		freq *= (float)g_dacFreqMult;
		if (freq < 1.0)
			freq = 1.0;
		prevPotVal = potVal;
		g_genSigDma->Stop();
		float actFreq;
		g_genSigDma->SetWaveForm(g_dacWaveForm, freq, &actFreq);
		g_dacFreq = actFreq;
		g_genSigDma->Start();

		bFreqChanged = true;
	}

	return bFreqChanged;
}

bool updateTriggerValue(int potVal)
{
	static int prevPotVal = 0;
	bool bTgChanged = false;

	if (potVal < 0)
		g_adcDma->ReadSingleValue(TRIG_CHANNEL, &potVal);

	if (abs(potVal - prevPotVal) > 100) {
		g_triggerVal = potVal;
		prevPotVal = potVal;
		bTgChanged = true;
	}

	return bTgChanged;
}

bool updateAdcSampleRate(bool bForceUpdate, int potVal)
{
	static int prevPotVal = 0;
	bool rateChanged = false;
	static int s_prevScopeDrawMode = SCOPE_DRAW_MODE_FAST;

	if (potVal < 0) {
		g_adcDma->ReadSingleValue(ADC_RATE_CHANNEL, &potVal);
	}

	if (bForceUpdate || (abs(potVal - prevPotVal) > POT_ANALOG_DIFF) ) {
		// Divide by 4 to avoid clipping
		g_adcSampleRate = map(potVal / 4, 0, POT_ANALOG_MAX_VAL / 4, g_adcMinSampleRate, g_adcMaxSampleRate);
		// Need to clip since value read might be a bit more than POT_ANALOG_MAX_VAL
		if (g_adcSampleRate > g_adcMaxSampleRate)
			g_adcSampleRate = g_adcMaxSampleRate;
		prevPotVal = potVal;

		rateChanged = true;
	}

	if (rateChanged) {

		if (g_adcSampleRate > ADC_SAMPLE_RATE_LOW_LIMIT) {
			g_scopeDrawMode = SCOPE_DRAW_MODE_FAST;
		}
		else {
			g_scopeDrawMode = SCOPE_DRAW_MODE_SLOW;
		}

		if (g_scopeDrawMode != s_prevScopeDrawMode) {
			enterScopeDrawMode(g_scopeDrawMode);
		}

		s_prevScopeDrawMode = g_scopeDrawMode;
	}

	return rateChanged;
}

void swapSampleBuffer()
{
#if 0
	if (g_samples == g_samples0)
		g_samples = g_samples1;
	else
		g_samples = g_samples0;
#endif
	for (int iChannel = 0; iChannel < g_channelsCount; iChannel++) {
		CHANNEL_DESC *pChannelDesc = &g_channelDescs[iChannel];
		if (pChannelDesc->curSamples == pChannelDesc->samples[0]) {
			pChannelDesc->curSamples = pChannelDesc->samples[1];
			pChannelDesc->oldSamples = pChannelDesc->samples[0];
		}
		else {
			pChannelDesc->curSamples = pChannelDesc->samples[0];
			pChannelDesc->oldSamples = pChannelDesc->samples[1];
		}
	}
}

#if 0
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
#endif

void drawBegin()
{
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
		y = map(y, 0, ANALOG_MAX_VAL, g_maxY, g_minY);
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
	static uint s_prevTriggerStatus = TRIGGER_STATUS_NONE;

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
			PF(DBG_TEXT, "drawing text at %d:%d\r\n", 10, TEXT_DOWN_Y_OFFSET);
			TFTscreen.text(textBuf, 10, TEXT_DOWN_Y_OFFSET);
		}
	}

	// Print trigger status
	// We don't need the loop here, but keep it for compat in case trigger text changes
	if (s_prevTriggerStatus != g_triggerStatus || bForceDrawText) {
		for (int i = 0; i < 2; i++) {
			String s = String("T");
			if (i == 0) {
				TFTscreen.stroke(BG_COLOR);
			}
			else {
				switch (g_triggerStatus) {
				case TRIGGER_STATUS_TIMEOUT :
					TFTscreen.stroke(TRIGGER_TIMEOUT_COLOR);
					break;
				case TRIGGER_STATUS_TRIGGERED :
					TFTscreen.stroke(TRIGGER_TRIGGERED_COLOR);
					break;
				default :
					TFTscreen.stroke(TRIGGER_NONE_COLOR);
					break;
				}
			}
			s.toCharArray(textBuf, 15);
			PF(DBG_TEXT, "drawing text at %d:%d\r\n", 10, TEXT_DOWN_Y_OFFSET);
			TFTscreen.text(textBuf, TFT_WIDTH / 2, TEXT_DOWN_Y_OFFSET);
		}
	}

	s_prevDacFreq = g_dacFreq;
	s_prevSampleRate = g_adcSampleRate;
	s_prevFrameRate = g_frameRate;
	s_prevTriggerStatus = g_triggerStatus;
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
	// Min and max y for horizontal lines
	// We'll use it to start vertical lines from them
	int minY = 0, maxY = 0;

	// Use this to draw secondary lines
	int loop = 0;
	// Draw one line dark, one line light
	int secLoop = 2;

	// Draw horizontal grid (horizontal lines)
	int yStart = HGRID_START;
	int yOffset = 0;
	int margin = HGRID_MARGIN;
	while ( (yStart + yOffset + margin < TFT_HEIGHT) || (yStart - yOffset > margin) ) {

		if (loop % secLoop == 0) {
			TFTscreen.stroke(HGRID_COLOR);
		}
		else {
			TFTscreen.stroke(HGRID_SEC_COLOR);
		}
		loop++;

		TFTscreen.line(0,  yStart + yOffset, TFT_WIDTH, yStart + yOffset);
		TFTscreen.line(0,  yStart - yOffset, TFT_WIDTH, yStart - yOffset);
		if (yStart + yOffset + margin < TFT_HEIGHT)
			maxY = yStart + yOffset;
		if (yStart - yOffset > margin)
			minY = yStart - yOffset;
		yOffset += HGRID_INTERVAL;
	}

	// Draw vertival grid (vertical lines)
	TFTscreen.stroke(VGRID_COLOR);
	int xStart = VGRID_START;
	int xOffset = 0;
	while ( (xStart + xOffset < TFT_WIDTH) || (xStart - xOffset > 0) ) {

		if (loop % secLoop == 0) {
			TFTscreen.stroke(VGRID_COLOR);
		}
		else {
			TFTscreen.stroke(VGRID_SEC_COLOR);
		}
		loop++;

		TFTscreen.line(xStart + xOffset, minY, xStart + xOffset, maxY);
		TFTscreen.line(xStart - xOffset, minY, xStart - xOffset, maxY);
		xOffset += VGRID_INTERVAL;
	}

	g_minY = minY;
	g_maxY = maxY;
}

void drawSamples()
{
	static int s_prevZoom = 1;

	// debug
	//int channelsCount = 1;
	int channelsCount = g_channelsCount;

	if (g_eraseMode == ERASE_MODE_ALT) {

		uint16_t *oldSamples;
		uint16_t *newSamples;

		int iSample = 1;

		int lastXDraw = 0;

		// Erase first old sample

		for (int iChannel = 0; iChannel < channelsCount; iChannel++) {
			oldSamples = g_channelDescs[iChannel].oldSamples;
			TFTscreen.stroke(BG_COLOR);
			TFTscreen.line(0, oldSamples[0], s_prevZoom, oldSamples[1]);
		}

		int lastXErase = s_prevZoom;

		// Erase sample iSample+1 while drawing sample iSample
		// otherwise, new drawn line could be overwritten by erased line
		for (;;) {
			for (int iChannel = 0; iChannel < channelsCount; iChannel++) {
				oldSamples = g_channelDescs[iChannel].oldSamples;
				// Erase old sample
				if (iSample + 1 < TFT_WIDTH) {
					TFTscreen.stroke(BG_COLOR);
					TFTscreen.line(lastXErase, oldSamples[iSample], lastXErase + s_prevZoom, oldSamples[iSample + 1]);
				}
			}
			lastXErase += s_prevZoom;

			for (int iChannel = 0; iChannel < channelsCount; iChannel++) {
				newSamples = g_channelDescs[iChannel].curSamples;
				// Draw new sample
				TFTscreen.stroke(g_channelDescs[iChannel].r, g_channelDescs[iChannel].g, g_channelDescs[iChannel].b);
				TFTscreen.line(lastXDraw, newSamples[iSample - 1], lastXDraw + g_zoom, newSamples[iSample]);
			}
			lastXDraw += g_zoom;

			iSample++;

			// Need this for zoom
			if (iSample >= TFT_WIDTH)
				break;
			if (lastXErase >= TFT_WIDTH && lastXDraw >= TFT_WIDTH)
				break;
		}
	}
#if 0
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
#endif

	s_prevZoom = g_zoom;
}

void erasePrevSamples(CHANNEL_DESC *pChannelDesc)
{
	/*
	static bool bFirstCall = true;

	// We have nothing to erase the first time we're called since g_samples was not written yet
	if (bFirstCall) {
		bFirstCall = false;
		return;
	}
	*/

	if (pChannelDesc->oldSamples == NULL)
		return;

	TFTscreen.stroke(BG_COLOR);

	for (int i = 0; i < TFT_WIDTH - 1; i++) {
		//TFTscreen.line(i, g_samples[i], i+1, g_samples[i+1]);
		TFTscreen.line(i, pChannelDesc->oldSamples[i], i+1, pChannelDesc->oldSamples[i+1]);
	}
}

void drawSample(uint16_t sample, int iSample, CHANNEL_DESC *pChannelDesc)
{
	//static uint16_t prevSample;

	sample = map(sample, 0, SAMPLE_MAX_VAL, g_maxY, g_minY);

	if (iSample != 0) {
		TFTscreen.stroke(pChannelDesc->r, pChannelDesc->g, pChannelDesc->b);
		TFTscreen.line(iSample - 1, pChannelDesc->curSamples[iSample - 1], iSample, sample);
	}

	//g_samples[iSample] = sample;
	pChannelDesc->curSamples[iSample] = sample;

	//prevSample = sample;
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

// Used by getAndDrawSampleSlow
int g_iSample = 0;

void enterScopeDrawMode(int drawMode)
{
	if (drawMode == SCOPE_DRAW_MODE_SLOW) {
		g_triggerStatus = TRIGGER_STATUS_NONE;
		g_iSample = 0;
		TFTscreen.background(BG_COLOR);
		drawTriggerArrow();
		drawGrid();
		drawTexts(true);
	}
}

void getAndDrawSampleSlow()
{
	int potChannels[] = {FREQ_CHANNEL, ADC_RATE_CHANNEL, TRIG_CHANNEL};
	int potChannelsCount = sizeof(potChannels) / sizeof(potChannels[0]);

	int channels[256];
	int channelsCount;
	for (int i = 0; i < g_channelsCount; i++) {
		channels[i] = g_channels[i];
	}
	for (int i = 0; i < potChannelsCount; i++) {
		channels[g_channelsCount + i] = potChannels[i];
	}
	channelsCount = g_channelsCount + potChannelsCount;

	g_adcDma->SetAdcChannels(channels, channelsCount);

	if (g_iSample == 0) {

		g_adcDma->Stop();

		g_adcDma->SetSampleRate(g_adcSampleRate);
		g_adcDma->SetBuffers(ADC_DMA_DEF_BUF_COUNT, ADC_DMA_DEF_BUF_SIZE);
		g_adcDma->SetTrigger(g_triggerVal, g_triggerMode, SCOPE_CHANNEL_1, 5000);
		g_adcDma->Start();
		g_adcDma->TriggerEnable(true);

		swapSampleBuffer();

		for (int i = 0; i < g_channelsCount; i++) {
			erasePrevSamples(&g_channelDescs[i]);
		}

		drawTriggerArrow();
		drawGrid();
		drawTexts(false);
	}

	// Wait until next sample is available
	uint16_t sample;
	bool isTgSample;
	int channel;
	int freqPotVal = -1;
	int srPotVal = -1;
	int tgPotVal = -1;

	bool bSrChanged = false;
	bool bFreqChanged = false;
	bool bTgChanged = false;

	static bool s_gotTriggerSample = false;

	if (g_iSample == 0) {
		s_gotTriggerSample = false;
		g_triggerStatus = TRIGGER_STATUS_NONE;
		drawTexts(true);
	}

	// Loop until we get a sample for each scope channel
	int obtainedChannels = 0;
	for (;;) {
		if (g_adcDma->GetNextSample(&sample, &channel, NULL, &isTgSample)) {
			CHANNEL_DESC *pChannelDesc = getChannelDesc(channel);
			if (pChannelDesc) {
				if (isTgSample) {
					g_iSample = 0;
					if (!s_gotTriggerSample) {
						g_triggerStatus = TRIGGER_STATUS_TRIGGERED;
						drawTexts(false);
					}
					s_gotTriggerSample = true;
				}

				if (s_gotTriggerSample) {
					drawSample(sample, g_iSample, pChannelDesc);
					obtainedChannels++;
				}
			}
			// First 2/3 samples may be slightly inaccurate, avoid them for pot inputs
			// Todo: check ADC config !
			else if (g_iSample < 3) {
			}
			else if (channel == FREQ_CHANNEL) {
				freqPotVal = sample;
				bFreqChanged = updateSignalFreq(false, freqPotVal);
			}
			else if (channel == ADC_RATE_CHANNEL) {
				srPotVal = sample;
				bSrChanged = updateAdcSampleRate(false, srPotVal);
			}
			else if (channel == TRIG_CHANNEL) {
				tgPotVal = sample;
				bTgChanged = updateTriggerValue(tgPotVal);
				if (bTgChanged)
					drawTriggerArrow();
			}
		}
		if (obtainedChannels == g_channelsCount)
			break;
	}

	g_iSample++;

	if (bSrChanged) {
		g_adcDma->SetSampleRate(g_adcSampleRate);
	}

	if (bSrChanged || bFreqChanged) {
		drawTexts(false);
	}

	if (g_iSample == TFT_WIDTH) {
		g_adcDma->Stop();
		g_iSample = 0;
	}
}

void getAndDrawSamplesFast()
{
	bool bTriggerTimeout;

	// compute buffer size to have one trigger every tTrig secs with bufCount buffers
	float tTrig = 0.1; // Try to have one trigger every 1/10s

	int   bufCount = ADC_DMA_DEF_BUF_COUNT;
	float fBufSize = (float)g_adcSampleRate * tTrig / (float)bufCount * (float)g_channelsCount;

	int bufSize = (int)ceil(fBufSize);
	if (bufSize > ADC_DMA_DEF_BUF_SIZE)
		bufSize = ADC_DMA_DEF_BUF_SIZE;

	// Make sure we have enough samples to fill the screen..
	// use *2 to take x-position into account, todo: compute this precisely
	if (bufSize * bufCount < TFT_WIDTH * 2 * g_channelsCount) {
		bufSize = TFT_WIDTH * 2 * g_channelsCount / bufCount;
	}

	// Make sure bufSize is a multiple of channels count
	while (bufSize % g_channelsCount) {
		bufSize--;
	}

	PF(DBG_LOOP, "Setting bufSize %d, sample rate %d\r\n", bufSize, g_adcSampleRate);

	g_adcDma->Stop();

	g_adcDma->SetAdcChannels(g_channels, g_channelsCount);
	g_adcDma->SetSampleRate(g_adcSampleRate);
	g_adcDma->SetBuffers(bufCount, bufSize);
	g_adcDma->Start();
	g_adcDma->SetTrigger(g_triggerVal, g_triggerMode, SCOPE_CHANNEL_1, TRIGGER_TIMEOUT);
	g_adcDma->SetTriggerPreBuffersCount(0);
	g_adcDma->TriggerEnable(true);

	// Wait for trigger

	while (!g_adcDma->DidTriggerComplete(&bTriggerTimeout)){}

	uint16_t *triggerBufAddress = NULL;
	int triggerSampleIndex = 0;

	if (!bTriggerTimeout) {
		g_adcDma->GetTriggerSampleAddress(&triggerBufAddress, &triggerSampleIndex);
		g_triggerStatus = TRIGGER_STATUS_TRIGGERED;
		PF(DBG_LOOP, "TriggerSample buf 0x%08x, index %d\r\n", triggerBufAddress, triggerSampleIndex);
	}
	else {
		g_triggerStatus = TRIGGER_STATUS_TIMEOUT;
		PF(true, "Trigger timeout !\r\n");
	}

	int x = 0;

	// Use new buffer before drawing
	swapSampleBuffer();

	int iBuf = 0;

	while (x < TFT_WIDTH) {
		uint16_t *buf;
		int samplesInBuf = bufSize;

		buf = g_adcDma->GetReadBuffer();

		if (buf == NULL) {
			PF(true, "Got null buffer !\r\n");
			break;
		}

		// We set pretrigger buffers to 0, so first buffer contains the trigger sample
		if (iBuf == 0) {
			buf += triggerSampleIndex;
			samplesInBuf -= triggerSampleIndex;
		}
		iBuf++;

		mapBufferValues(x, buf, samplesInBuf);

		x += samplesInBuf / g_channelsCount;
	}

	g_adcDma->Stop();

	drawBegin();
	drawTriggerArrow();
	drawSamples();
	drawGrid();
	drawTexts(false);
}

void loop()
{
	if (g_scopeDrawMode == SCOPE_DRAW_MODE_SLOW) {
		getAndDrawSampleSlow();
	}
	else {
		getAndDrawSamplesFast();
		updateSignalFreq(false, -1);
		updateTriggerValue(-1);
		updateAdcSampleRate(false, -1);
	}

	computeFrameRate();
	SCmd.readSerial();
}
