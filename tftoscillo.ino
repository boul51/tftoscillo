#include <GenSigDma.h>
#include <AdcDma.h>
#include <LibDbg.h>

#include <SerialCommand.h>

#include <SPI.h>
#include <TFT.h>  // Arduino LCD library

#include "tftoscillo.h"

/**** DEFINES ****/

#define DIMOF(x) (sizeof(x) / sizeof(x[0]))

// Debug zones
#define DBG_LOOP		false
#define DBG_VERBOSE		false
#define DBG_TEXT		false
#define DBG_POTS		false
#define DBG_DRAW		false
#define DBG_DRAWMODE	false
#define DBG_RX			false


// TFT definitions
#define TFT_CS_PIN   10 // Chip select pin
#define TFT_DC_PIN    9 // Command / Display data pin
#define TFT_RST_PIN   8 // Reset pin
#define TFT_BL_PIN    6 // Backlight

#define TFT_WIDTH   160
#define TFT_HEIGHT  128

// Pot and scope input pins
#define SCOPE_CHANNEL_1			7
#define SCOPE_CHANNEL_2			3
#define SCOPE_CHANNEL_3			2
#define SCOPE_CHANNEL_4			1
#define FREQ_CHANNEL			6
#define ADC_DMA_RATE_CHANNEL	5
#define TRIGGER_CHANNEL			4

// Pot definitions

#define POT_ANALOG_MAX_VAL (988*ANALOG_MAX_VAL/1000)	// This is used for pot input. Max value read is not 4096 but around 4050 ie 98.8%
#define POT_ANALOG_DIFF				20					// Min difference between two pot value to consider is was moved


#define TRIGGER_TIMEOUT				100

// Trigger definitions
#define TRIGGER_STATUS_DISABLED		0
#define TRIGGER_STATUS_WAITING		1
#define TRIGGER_STATUS_TRIGGERED	2
#define TRIGGER_STATUS_TIMEOUT		3

// Rate from which we switch to HS / LS mode
#define ADC_SAMPLE_RATE_LOW_LIMIT	300

// Tests show that ADC doesn't sample well with freq >= 1830000 Hz.
// (Not so bad since nominal max rate is 1MHz)
#define ADC_MIN_SAMPLE_RATE  100
#define ADC_MAX_SAMPLE_RATE 100000

//#define ADC_MIN_SAMPLE_RATE		10000
//#define ADC_MAX_SAMPLE_RATE		900000

//uint g_adcMinSampleRate = ADC_MIN_SAMPLE_RATE;
//uint g_adcMaxSampleRate = ADC_MAX_SAMPLE_RATE;
//uint g_adcSampleRate = ADC_MIN_SAMPLE_RATE;

#define DAC_MIN_FREQ    1
#define DAC_MAX_FREQ    1000
//#define DAC_MAX_FREQ    10000
uint g_dacMinFreq = DAC_MIN_FREQ;
uint g_dacMaxFreq = DAC_MAX_FREQ;
//uint g_dacFreq = g_dacMinFreq;
uint g_dacFreqMult = 1;
GenSigDma::WaveForm g_dacWaveForm = GenSigDma::WaveFormSinus;

#define TRIGGER_MIN_VAL    0
#define TRIGGER_MAX_VAL    ANALOG_MAX_VAL
//int g_triggerVal = TRIGGER_MAX_VAL / 2;
int g_zoom = 1;

uint g_frameRate = 0;		// Scope fps

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
#define TRIGGER_WAITING_COLOR	  0,   0, 255
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
uint16_t g_channels [] = {SCOPE_CHANNEL_1, SCOPE_CHANNEL_2};
//uint16_t g_channels [] = {SCOPE_CHANNEL_1};
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

SCOPE_STATE g_scopeState =
{
	(uint)-1,					// Trigger val
	ADC_MIN_SAMPLE_RATE,	// Min sample rate
	ADC_MAX_SAMPLE_RATE,	// Max sample rate
	(uint)-1,					// Sample rate
};

SIG_STATE g_sigState;

DRAW_STATE g_drawState;

bool rxHandler(uint16_t *buffer, int bufLen, bool bIsTrigger, int triggerIndex, bool bTimeout);

#define DRAW_MODE_SLOW		0
#define DRAW_MODE_FAST		1

POT_VAR g_potVars[] =
{
	{
		TRIGGER_CHANNEL,
		(uint)-1,
		0,
		ANALOG_MAX_VAL,
		&g_scopeState.triggerVal,
		20,
		false,
		"TRIG",
		{
			false,
			false,
			0,
			"",
			"",
			0,
			0,
		}
	},
	{
		ADC_DMA_RATE_CHANNEL,
		(uint)-1,
		g_scopeState.minSampleRate,
		g_scopeState.maxSampleRate,
		&g_scopeState.sampleRate,
		20,
		false,
		"RATE",
		{
			true,
			false,
			0,
			"SR:",
			"Hz",
			TFT_WIDTH / 2,
			TEXT_Y_OFFSET,
		}
	},
	{
		FREQ_CHANNEL,
		(uint)-1,
		g_dacMinFreq,
		g_dacMaxFreq,
		&g_sigState.freq,
		20,
		false,
		"FREQ",
		{
			true,
			false,
			0,
			"Fq:",
			"Hz",
			10,
			TEXT_Y_OFFSET,
		}
	}
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
	SCmd.addDefaultHandler(defaultHandler);

	// Initialize LCD
	TFTscreen.begin();
	TFTscreen.background(255, 255, 255);

	// SPI.setClockDivider(TFT_CS_PIN, 1);
	// Modified TFT library to set SPI clock divider to 1

	g_genSigDma = new GenSigDma();
	g_genSigDma->SetTimerChannel(1);

	uint16_t adcChannel = SCOPE_CHANNEL_1;
	g_adcDma = AdcDma::GetInstance();
	g_adcDma->SetAdcChannels(&adcChannel, 1);
	g_adcDma->SetTimerChannel(2);
	g_adcDma->SetRxHandler(rxHandler);

	// Call drawGrid to update g_minY and g_maxY
	drawGrid();

	updatePotsVars(NULL);

	g_genSigDma->SetWaveForm(g_dacWaveForm, (float)g_sigState.freq, NULL);
	g_adcDma->SetSampleRate(g_scopeState.sampleRate);
	g_adcDma->SetTrigger(g_scopeState.triggerVal, g_triggerMode, SCOPE_CHANNEL_1, 1000);

	initDrawState();

	g_drawState.bFinished = true;
	//g_drawState.drawMode = DRAW_MODE_SLOW;
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

	PF(true, "TODO\r\n");

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
		Serial.print(g_scopeState.sampleRate);
		Serial.print(" (");
		Serial.print(g_scopeState.minSampleRate);
		Serial.print(" - ");
		Serial.print(g_scopeState.maxSampleRate);
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

	g_scopeState.minSampleRate = rangeStart;
	g_scopeState.maxSampleRate = rangeEnd;

	PF(true, "TODO\r\n");

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

	PF(true, "TODO\r\n");
}

inline CHANNEL_DESC *getChannelDesc(int channel)
{
	for (int i = 0; i < g_channelsCount; i++) {
		if (g_channelDescs[i].channel == channel)
			return &g_channelDescs[i];
	}

	return NULL;
}

void mapBufferValues(int frameOffset, uint16_t *buf, int framesCount)
{
	uint16_t rawSample;
	uint16_t sample;
	uint16_t mappedVal;
	int channel;
	int channelsCount = g_adcDma->GetAdcChannelsCount();

	CHANNEL_DESC *channelDesc = &g_channelDescs[0];

	for (int iFrame = 0; iFrame < framesCount; iFrame++) {

		// Should not happen
		/*
		if (iFrame + frameOffset >= TFT_WIDTH)
			break;
			*/

		for (int iChannel = 0; iChannel < channelsCount; iChannel++) {
			rawSample = buf[iFrame * channelsCount + iChannel];

			sample = g_adcDma->sample(rawSample);
			channel = g_adcDma->channel(rawSample);

			PF(false, "Got channel %d, sample %d\r\n", channel, sample);

			channelDesc = getChannelDesc(channel);

			if (channelDesc) {
				mappedVal = map(sample, 0, SAMPLE_MAX_VAL, g_maxY, g_minY);
				channelDesc->curSamples[iFrame + frameOffset] = mappedVal;
			}
		}

		PF(false, "sf %d\r\n", g_drawState.mappedFrames);
		g_drawState.mappedFrames++;

		if (g_drawState.mappedFrames == TFT_WIDTH)
			break;
	}

	return;
}

void swapSampleBuffer()
{
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

void drawBegin()
{
}

void drawTriggerArrow(POT_VAR *potVar)
{
	// Redraw trigger line (always since signal may overwrite it)
	for (int i = 0; i < 2; i++) {
		int y;
		if (i == 0) {
			// Erase
			TFTscreen.stroke(BG_COLOR);
			y = potVar->display.prevValue;
		}
		else {
			// Draw
			TFTscreen.stroke(TRIGGER_COLOR);
			y = *potVar->value;
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
}

void drawPotVar(POT_VAR *potVar)
{
	char textBuf[40];

	for (int i = 0; i < 2; i++) {

		// Don't erase if not needed
		if (i == 0 && !potVar->display.bNeedsErase)
			continue;

		String s;
		s = String(potVar->display.prefix);
		if (i == 0) {
			// erase prev value
			s += String(potVar->display.prevValue);
			TFTscreen.stroke(BG_COLOR);
		}
		else {
			// draw new value
			s += String(*potVar->value);
			TFTscreen.stroke(TEXT_COLOR);
		}
		s += String(potVar->display.suffix);
		s.toCharArray(textBuf, 15);
		TFTscreen.text(textBuf, potVar->display.x, potVar->display.y);
	}

	potVar->display.prevValue = *potVar->value;
	potVar->display.bNeedsErase = true;
}

void drawTriggerStatus()
{
	char textBuf[40];

	// We don't need the loop here, but keep it for compat in case trigger text changes
	if (g_scopeState.bTriggerStatusChanged) {
		g_scopeState.bTriggerStatusChanged = false;

		// Don't display waiting state in fast mode since we don't have time to
		// see trigger state in this case
		if (g_scopeState.triggerStatus == TRIGGER_STATUS_WAITING &&
			g_drawState.drawMode == DRAW_MODE_FAST)
			return;

		for (int i = 1; i < 2; i++) {
			String s = String("T");
			if (i == 0) {
				TFTscreen.stroke(BG_COLOR);
			}
			else {
				switch (g_scopeState.triggerStatus) {
				case TRIGGER_STATUS_TIMEOUT :
					TFTscreen.stroke(TRIGGER_TIMEOUT_COLOR);
					break;
				case TRIGGER_STATUS_TRIGGERED :
					TFTscreen.stroke(TRIGGER_TRIGGERED_COLOR);
					break;
				case TRIGGER_STATUS_WAITING :
					TFTscreen.stroke(TRIGGER_WAITING_COLOR);
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
}

void drawGrid()
{
	PF(false, "++\r\n");
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

void drawEraseSamples(bool bDraw, bool bErase)
{
	PF(false, "g_drawState.drawnFrames %d, g_drawState.sampledFrames %d\r\n", g_drawState.drawnFrames, g_drawState.mappedFrames);
	int s_prevZoom = 1;

	// Draw current sample before drawing it (used in fast mode)
	if (bDraw && bErase) {
		uint16_t *oldSamples;
		uint16_t *newSamples;

		int iSample = 1;

		int lastXDraw = 0;

		// Erase first old sample

		for (int iChannel = 0; iChannel < g_channelsCount; iChannel++) {
			oldSamples = g_channelDescs[iChannel].oldSamples;
			TFTscreen.stroke(BG_COLOR);
			TFTscreen.line(0, oldSamples[0], s_prevZoom, oldSamples[1]);
		}

		int lastXErase = s_prevZoom;

		// Erase sample iSample+1 while drawing sample iSample
		// otherwise, new drawn line could be overwritten by erased line
		for (;;) {
			for (int iChannel = 0; iChannel < g_channelsCount; iChannel++) {
				oldSamples = g_channelDescs[iChannel].oldSamples;
				// Erase old sample
				if (iSample + 1 < TFT_WIDTH) {
					TFTscreen.stroke(BG_COLOR);
					TFTscreen.line(lastXErase, oldSamples[iSample], lastXErase + s_prevZoom, oldSamples[iSample + 1]);
				}
			}
			lastXErase += s_prevZoom;

			for (int iChannel = 0; iChannel < g_channelsCount; iChannel++) {
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
		g_drawState.drawnFrames = TFT_WIDTH - 1;
	}
	// Draw all currently samples framed (used in slow mode)
	else if (bDraw) {
		for (int xStart = g_drawState.drawnFrames; xStart + 1 < g_drawState.mappedFrames; xStart++) {

			PF(false, "x0 %d, x1 %d, df %d, sf %d\r\n", xStart, xStart + 1, g_drawState.drawnFrames, g_drawState.mappedFrames);

			for (int iChannel = 0; iChannel < g_channelsCount; iChannel++) {
				CHANNEL_DESC *pDesc = &g_channelDescs[iChannel];
				TFTscreen.stroke(pDesc->r, pDesc->g, pDesc->b);
				TFTscreen.line(xStart, pDesc->curSamples[xStart], xStart + 1, pDesc->curSamples[xStart + 1]);
			}

			g_drawState.drawnFrames++;

			if (g_drawState.drawnFrames == TFT_WIDTH - 1)
				break;
		}
	}
	// Erase all old samples (used in slow mode)
	else if (bErase) {
		TFTscreen.stroke(BG_COLOR);
		for (int iFrame = 0; iFrame < TFT_WIDTH; iFrame++) {
			for (int iChannel = 0; iChannel < g_channelsCount; iChannel++) {
				CHANNEL_DESC *pDesc = &g_channelDescs[iChannel];
				TFTscreen.line(iFrame - 1, pDesc->oldSamples[iFrame - 1], iFrame, pDesc->oldSamples[iFrame]);
			}
		}
	}

	return;
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

void enterScopeDrawMode(int drawMode)
{
	if (drawMode != g_drawState.drawMode) {
		PF(DBG_DRAWMODE, "Entering %s mode\r\n", drawMode == DRAW_MODE_SLOW ? "slow" : "fast");
		g_adcDma->Stop();
		g_drawState.bFinished = true;
		PF(true, "set bFinished\r\n");
	}

	g_drawState.drawMode = drawMode;
}

void initDrawState()
{
	PF(false, "++\r\n");
	g_drawState.drawnFrames = 0;
	g_drawState.mappedFrames = 0;
	g_drawState.rxFrames = 0;
}

void initScopeState()
{
	PF(false, "++\r\n");
	g_scopeState.triggerStatus = TRIGGER_STATUS_WAITING;
	g_scopeState.bTriggerStatusChanged = true;
}

void updateScopeFromPots()
{
}

void updateGensigFromPots()
{
}

void findTriggerSample(uint16_t *buffer, int buflen, int *triggerIndex)
{
	uint16_t rawSample, sample, channel;
	int triggerMode = g_triggerMode;
	int triggerVal = g_scopeState.triggerVal;
	bool bArmed = false;
	bool bFound = false;
	int prevTriggerIndex = *triggerIndex;

	for (int iSample = 0; iSample < buflen; iSample++) {
		rawSample = buffer[iSample];

		sample = g_adcDma->sample(rawSample);
		channel = g_adcDma->channel(rawSample);

		if (channel != SCOPE_CHANNEL_1)
			continue;

		if (triggerMode == AdcDma::RisingEdge) {
			if (!bArmed) {
				if (sample < triggerVal) {
					bArmed = true;
				}
			}
			else {
				if (sample >= triggerVal) {
					// Got trigger sample
					*triggerIndex = iSample;
					bFound = true;
					break;
				}
			}
		}
		else if (triggerMode == AdcDma::FallingEdge) {
			if (!bArmed) {
				if (sample > triggerVal) {
					bArmed = true;
				}
			}
			else {
				if (sample <= triggerVal) {
					// Got trigger sample
					*triggerIndex = iSample;
					bFound = true;
					break;
				}
			}
		}
	}

	if (!bFound) {
		PF(true, "Did not find trigger sample !\r\n");
	}

	if (prevTriggerIndex < *triggerIndex) {
		PF(true, "New trigger index is later !\r\n");
	}

	return;
}

bool rxHandler(uint16_t *buffer, int buflen, bool bIsTrigger, int triggerIndex, bool bTimeout)
{
	int channelsCount = g_adcDma->GetAdcChannelsCount();
	int framesInBuf;
	uint prevTriggerStatus = g_scopeState.triggerStatus;

	if (bIsTrigger || bTimeout) {
		PF(false, "trigger %d timeout %d\r\n", bIsTrigger, bTimeout);
	}

	PF(false, "len %d, tg %d, to %d, tgst %d\r\n", buflen, bIsTrigger, bTimeout, g_scopeState.triggerStatus);

	if (bTimeout && g_scopeState.triggerStatus == TRIGGER_STATUS_WAITING) {
		PF(false, "Trigger timeout\r\n");
		g_scopeState.triggerStatus = TRIGGER_STATUS_TIMEOUT;
	}
	else if (bIsTrigger) {

		// Trigger is not accurate in fast mode, since we're too slow
		// to get trigger sample at high sample rate.
		// Try to find it in current buffer
		if (g_drawState.drawMode == DRAW_MODE_FAST) {
			findTriggerSample(buffer, buflen, &triggerIndex);
		}

		g_scopeState.triggerStatus = TRIGGER_STATUS_TRIGGERED;
		buffer += triggerIndex;
		buflen -= triggerIndex;
	}

	if (g_scopeState.triggerStatus != prevTriggerStatus) {
		g_scopeState.bTriggerStatusChanged = true;
	}

	framesInBuf = buflen / channelsCount;

	g_drawState.rxFrames += framesInBuf;

	if (framesInBuf > TFT_WIDTH)
		framesInBuf = TFT_WIDTH;

	if (g_drawState.drawMode == DRAW_MODE_FAST) {
		if (g_scopeState.triggerStatus == TRIGGER_STATUS_WAITING) {
			PF(false, "Waiting\r\n");
			return true;
		}

		PF(false, "mapping %d frames\r\n", framesInBuf);
		// Debug: skip first frame, first sample is bad
		/*
		if (g_drawState.drawnFrames == 0) {
			buffer += g_channelsCount;
			framesInBuf--;
		}*/
		mapBufferValues(g_drawState.mappedFrames, buffer, framesInBuf);
	}
	else if (g_drawState.drawMode == DRAW_MODE_SLOW) {

		// Update pots values from last frame in buffer
		// Note: skip first samples since they are not very accurate
		if (g_drawState.rxFrames > 3) {
			updatePotsVars(buffer + buflen - channelsCount);
		}

		// Do nothing if we did not get trigger yet
		if (g_scopeState.triggerStatus == TRIGGER_STATUS_WAITING) {
			return true;
		}

		//PF(true, "Calling mapBufferValues, framesInBuf %d\r\n", framesInBuf);
		// Map values
		mapBufferValues(g_drawState.mappedFrames, buffer, framesInBuf);
		PF(false, "sampledFr frames is now %d (framesInBuf %d)\r\n", g_drawState.mappedFrames, framesInBuf);

		// Update scope and gensig states from read values
		updateScopeFromPots();
		updateGensigFromPots();
	}

	// Stop AdcDma if we have enough samples
	if (g_drawState.mappedFrames == TFT_WIDTH) {
		PF(DBG_RX, "Got enough frames, stopping AdcDma\r\n");
		g_adcDma->Stop();
	}

	PF(DBG_RX && DBG_VERBOSE, "Sampled %d frames\r\n", g_drawState.mappedFrames);

	return true;
}

void setupAdcDmaSlow()
{
	//g_adcDma->SetSampleRate(g_adcSampleRate);
}

POT_VAR *getPotVar(uint16_t channel)
{
	for (int i = 0; i < (int)DIMOF(g_potVars); i++) {
		if (g_potVars[i].adcChannel == channel)
			return &g_potVars[i];
	}
	return NULL;
}

#define ABS_DIFF(a, b) (a > b ? a - b : b - a)

void updatePotsVars(uint16_t *buffer)
{
	uint16_t rawSample;
	uint16_t potValue;
	uint16_t channel;
	uint value;
	uint diff;
	POT_VAR *potVar;

	int channelsCount = g_adcDma->GetAdcChannelsCount();
	int iChannel = 0;
	int iPotVar = 0;

	for (;;) {
		if (buffer) {
			rawSample = buffer[iChannel];
			channel = g_adcDma->channel(rawSample);
			potValue = g_adcDma->sample(rawSample);
			potVar = getPotVar(channel);
		}
		else {
			potVar = &g_potVars[iPotVar];
			g_adcDma->ReadSingleValue(potVar->adcChannel, &potValue);
		}

		if (potVar && !potVar->changed) {
			diff = ABS_DIFF(potVar->potValue, potValue);
			if (diff > potVar->margin) {
				value = map(potValue / 4, 0, POT_ANALOG_MAX_VAL / 4, potVar->minValue, potVar->maxValue);
				// Pot val might be out of bounds. In this case, crop value
				if (value < potVar->minValue)
					value = potVar->minValue;
				if (value > potVar->maxValue)
					value = potVar->maxValue;
				potVar->potValue = potValue;
				potVar->display.prevValue = *potVar->value;
				*potVar->value = value;
				potVar->changed = true;
				PF(DBG_POTS, "New value for potVar %s: %d (%d > %d)\r\n", potVar->name, *potVar->value, diff, potVar->margin);
			}
		}

		if (buffer) {
			iChannel++;
			if (iChannel == channelsCount)
				break;
		}
		else {
			iPotVar++;
			if (iPotVar == DIMOF(g_potVars))
				break;
		}
	}
}

void setupAdcDma()
{
	if (g_scopeState.sampleRate < ADC_SAMPLE_RATE_LOW_LIMIT) {
		int potChannels[] = {FREQ_CHANNEL, ADC_DMA_RATE_CHANNEL, TRIGGER_CHANNEL};
		int potChannelsCount = DIMOF(potChannels);
		int channelsCount = g_channelsCount + potChannelsCount;
		uint16_t channels[256];

		for (int i = 0; i < g_channelsCount; i++) {
			channels[i] = g_channels[i];
		}
		for (int i = 0; i < potChannelsCount; i++) {
			channels[g_channelsCount + i] = potChannels[i];
		}

		g_adcDma->SetAdcChannels(channels, channelsCount);
		g_adcDma->SetBuffers(10, channelsCount);
	}
	else {
		g_adcDma->SetAdcChannels(g_channels, g_channelsCount);

		// Calculate to have an IRQ every 1/10 s
		int buflen;
		// t = buflen / SR / sizeof(sample) / channelsCount => buflen = SR / 10 * sizeof(sample) * channelsCount
		buflen = g_scopeState.sampleRate / 10 * sizeof(uint16_t) * g_channelsCount;

		if (buflen > ADC_DMA_DEF_BUF_SIZE) {
			buflen = ADC_DMA_DEF_BUF_SIZE;
		}

		PF(false, "Using buflen %d\r\n", buflen);

		if (!g_adcDma->SetBuffers(5, buflen)) {
			PF(true, "Failed in g_adcDma->SetBuffers!\r\n");
		}
	}

	g_adcDma->SetTrigger(g_scopeState.triggerVal, g_triggerMode, SCOPE_CHANNEL_1, 1000);

}

POT_VAR *getPotVar(const char *name)
{
	for (uint i = 0; i < DIMOF(g_potVars); i++) {
		if (strcmp(g_potVars[i].name, name) == 0)
			return &g_potVars[i];
	}

	return NULL;
}

void processPotVars()
{
	POT_VAR *potVar;
	potVar = getPotVar("FREQ");
	if (potVar->changed) {
		g_genSigDma->Stop();
		g_genSigDma->SetWaveForm(g_dacWaveForm, (float)*potVar->value, NULL);
		g_genSigDma->Start();
		drawPotVar(potVar);
		potVar->changed = false;
	}
	potVar = getPotVar("RATE");
	if (potVar->changed) {
		drawPotVar(potVar);
		g_adcDma->SetSampleRate(g_scopeState.sampleRate);
		potVar->changed = false;
	}
	potVar = getPotVar("TRIG");
	if (potVar->changed) {
		drawTriggerArrow(potVar);
		drawGrid();
		// Force drawn samples to 0 to redraw all
		PF(false, "Clearing drawnFrames (was %d)\r\n", g_drawState.drawnFrames);
		g_drawState.drawnFrames = 0;
		drawEraseSamples(true, false);
		PF(false, "DrawnFrames is now %d\r\n", g_drawState.drawnFrames);
		g_adcDma->SetTrigger(g_scopeState.triggerVal, g_triggerMode, SCOPE_CHANNEL_1, 1000);
		potVar->changed = false;
	}
}

void loop()
{
	if ( (g_drawState.drawMode == DRAW_MODE_SLOW) ||
		 (g_drawState.drawMode == DRAW_MODE_FAST && g_drawState.mappedFrames == TFT_WIDTH) ) {

		// In slow mode, erase current samples if flag is set.
		// In fast mode, old samples are erased while the new ones are drawn
		if (g_drawState.bNeedsErase && g_drawState.drawMode == DRAW_MODE_SLOW)
		{
			drawEraseSamples(false, true);
			drawGrid();
			g_drawState.bNeedsErase = false;
		}

		if (g_drawState.drawMode == DRAW_MODE_FAST) {
			drawEraseSamples(true, true);
		}
		else {
			drawEraseSamples(true, false);
		}

		PF(false, "drawnFrames %d\r\n", g_drawState.drawnFrames);

		if (g_drawState.drawnFrames >= TFT_WIDTH - 1) {
			g_drawState.bFinished = true;
		}

		// Manually update pot vars in fast mode since it is not done while sampling
		if (g_drawState.drawMode == DRAW_MODE_FAST) {
			updatePotsVars(NULL);
		}

		// Process SR, freq and trigger pot values
		processPotVars();

		// Update trigger status
		drawTriggerStatus();
	}

	if (g_drawState.bFinished) {
		g_drawState.bFinished = false;

		// Reset drawn samples, sampled frames and trigger state
		initDrawState();
		initScopeState();

		g_drawState.bNeedsErase = true;

		// Update draw mode depending on sample rate
		if (g_scopeState.sampleRate < ADC_SAMPLE_RATE_LOW_LIMIT)
			g_drawState.drawMode = DRAW_MODE_SLOW;
		else
			g_drawState.drawMode = DRAW_MODE_FAST;

		swapSampleBuffer();

		drawTriggerStatus();
		drawTriggerArrow(getPotVar("TRIG"));
		drawGrid();

		setupAdcDma();

		PF(false, "Starting ADCDMA\r\n");
		g_adcDma->Start();
	}
}
