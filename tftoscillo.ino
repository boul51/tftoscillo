#include <GenSigDma.h>
#include <AdcDma.h>
#include <LibDbg.h>

#include <SerialCommand.h>

#include <SPI.h>
#include <TFT.h>  // Arduino LCD library

#include "tftoscillo.h"

/**** DEFINES ****/

// TFT screen definitions
#define TFT_CS_PIN   10		// Chip select pin
#define TFT_DC_PIN    9		// Command / Display data pin
#define TFT_RST_PIN   8		// Reset pin
#define TFT_BL_PIN    6		// Backlight
#define TFT_WIDTH   160		// Screen width
#define TFT_HEIGHT  128		// Screen height

// Pot and scope input pins
#define SCOPE_CHANNEL_1			7
#define SCOPE_CHANNEL_2			3
#define SCOPE_CHANNEL_3			2
#define SCOPE_CHANNEL_4			1
#define FREQ_CHANNEL			6
#define ADC_RATE_CHANNEL		5
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
#define ADC_MIN_SAMPLE_RATE			100
#define ADC_MAX_SAMPLE_RATE			100000

#define DAC_MIN_FREQ				1
#define DAC_MAX_FREQ				1000

int g_zoom = 1;

uint g_frameRate = 0;		// Scope fps

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
#define TEXT_UP_Y_OFFSET		2
#define TEXT_DOWN_Y_OFFSET		TFT_HEIGHT - TEXT_UP_Y_OFFSET - TEXT_FONT_HEIGHT

// Grid definitions
#define VGRID_START		(TFT_WIDTH / 2)		// x-axis position of the first vertical line
#define VGRID_INTERVAL  25					// distance between vertical lines

#define HGRID_START     (TFT_HEIGHT / 2)	// y-axis position of the first horizontal line
#define HGRID_INTERVAL  VGRID_INTERVAL		// distance between horizontal lines

#define HGRID_MARGIN	10					// distance between border and first horizontal line

// Tft screen instance
TFT TFTscreen = TFT(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

uint16_t g_scopeChannels [] = {SCOPE_CHANNEL_1, SCOPE_CHANNEL_2, SCOPE_CHANNEL_3, SCOPE_CHANNEL_4};
uint16_t g_potChannels [] = {FREQ_CHANNEL, ADC_RATE_CHANNEL, TRIGGER_CHANNEL};
//int g_scopeChannelsCount = DIMOF(g_scopeChannels);
int g_potChannelsCount = DIMOF(g_potChannels);

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
	.sampleRate		= 100,
	.minSampleRate	= ADC_MIN_SAMPLE_RATE,
	.maxSampleRate	= ADC_MAX_SAMPLE_RATE,
	.triggerChannel	= SCOPE_CHANNEL_1,
	.triggerVal		= 2000,
	.minTriggerVal	= 0,
	.maxTriggerVal	= ANALOG_MAX_VAL,
	.triggerStatus	= TRIGGER_STATUS_DISABLED,
	.bTriggerStatusChanged = false,
	.prevScopeChannelsCount = 1,
	.scopeChannelsCount = 1,
	.newScopeChannelsCount = 1,
	.bScopeChannelsCountChanged = false,
};

SIG_STATE g_sigState =
{
	.minFreq		= 1,
	.maxFreq		= 10,
	.freq			= 10,
	.waveform		= GenSigDma::WaveFormSinus,
};

DRAW_STATE g_drawState =
{
	.bFinished		= true,
	.bNeedsErase	= false,
	.drawnFrames	= 0,
	.mappedFrames	= 0,
	.rxFrames		= 0,
	.drawMode		= DRAW_MODE_SLOW
};

bool rxHandler(uint16_t *buffer, int bufLen, bool bIsTrigger, int triggerIndex, bool bTimeout);

POT_VAR g_potVars[] =
{
	{
		.adcChannel	= TRIGGER_CHANNEL,
		.potValue	= 0,
		.minValue	= &g_scopeState.minTriggerVal,
		.maxValue	= &g_scopeState.maxTriggerVal,
		.value		= &g_scopeState.triggerVal,
		.prevValue	= 0,
		.margin		= POT_ANALOG_DIFF,
		.changed	= false,
		.forceRead	= true,
		.name		= "TRIG",
		.bHasVarDisplay = false,
		.display	= {
		}
	},
	{
		.adcChannel	= ADC_RATE_CHANNEL,
		.potValue	= 0,
		.minValue	= &g_scopeState.minSampleRate,
		.maxValue	= &g_scopeState.maxSampleRate,
		.value		= &g_scopeState.sampleRate,
		.prevValue	= 0,
		.margin		= POT_ANALOG_DIFF,
		.changed	= false,
		.forceRead	= true,
		.name		= "RATE",
		.bHasVarDisplay = true,
		.display	= {
			.bNeedsErase	= false,
			.prefix			= "SR:",
			.suffix			= "Hz",
			.value			= 0,
			.prevValue		= 0,
			.x				= TFT_WIDTH / 2,
			.y				= TEXT_UP_Y_OFFSET,
		}
	},
	{
		.adcChannel	= FREQ_CHANNEL,
		.potValue	= (uint)-1,
		.minValue	= &g_sigState.minFreq,
		.maxValue	= &g_sigState.maxFreq,
		.value		= &g_sigState.freq,
		.prevValue	= 0,
		.margin		= POT_ANALOG_DIFF,
		.changed	= false,
		.forceRead	= true,
		.name		= "FREQ",
		.bHasVarDisplay = true,
		.display	= {
			.bNeedsErase	= false,
			.prefix			= "Fq:",
			.suffix			= "Hz",
			.value			= 0,
			.prevValue		= 0,
			.x				= 10,
			.y				= TEXT_UP_Y_OFFSET,
		}
	}
};

VAR_DISPLAY g_fpsVarDisplay =
{
	.bNeedsErase	= false,
	.prefix			= "Fps:",
	.suffix			= "",
	.value			= 0,
	.prevValue		= 0,
	.x				= 10,
	.y				= TEXT_DOWN_Y_OFFSET,
};

void setup() {

	Serial.begin(115200);

	g_channelDescs = (CHANNEL_DESC *)malloc(DIMOF(g_scopeChannels) * sizeof(CHANNEL_DESC));

	// Init channels descriptors
	for (uint i = 0; i < DIMOF(g_scopeChannels); i++) {
		g_channelDescs[i].channel = g_scopeChannels[i];
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
	SCmd.addCommand("fr", freqRangeHandler);
	SCmd.addCommand("sr", sampleRateRangeHandler);
	SCmd.addCommand("zoom", zoomHandler);
	SCmd.addCommand("form", formHandler);
	SCmd.addCommand("ch", channelCountHandler);
	SCmd.addDefaultHandler(defaultHandler);

	// Initialize LCD
	// Note: TFT library was modified to set SPI clock divider to 4
	TFTscreen.begin();
	TFTscreen.background(255, 255, 255);

	g_genSigDma = new GenSigDma();
	g_genSigDma->SetTimerChannel(1);

	g_adcDma = AdcDma::GetInstance();
	g_adcDma->SetTimerChannel(2);
	g_adcDma->SetRxHandler(rxHandler);

	// This will update g_drawState.minY and g_drawState.maxY
	drawGrid();

	updatePotsVars(NULL);
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

void channelCountHandler()
{
	char * strCh;

	strCh = SCmd.next();

	if (strCh == NULL) {
		Serial.println(g_scopeState.scopeChannelsCount);
		return;
	}

	g_scopeState.newScopeChannelsCount = atoi(strCh);
	g_scopeState.bScopeChannelsCountChanged = true;
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
		Serial.print(g_sigState.freq);
		Serial.print(" (");
		Serial.print(g_sigState.minFreq);
		Serial.print(" - ");
		Serial.print(g_sigState.maxFreq);
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

	g_sigState.minFreq = rangeStart;
	g_sigState.maxFreq = rangeEnd;

	POT_VAR *potVar = getPotVar("FREQ");
	potVar->forceRead = true;

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

	POT_VAR *potVar = getPotVar("RATE");
	potVar->forceRead = true;

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
		switch (g_sigState.waveform) {
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
		g_sigState.waveform = GenSigDma::WaveFormSaw;
	}
	else if (strcmp(strForm, "sinus") == 0) {
		g_sigState.waveform = GenSigDma::WaveFormSinus;
	}
	else if (strcmp(strForm, "square") == 0) {
		g_sigState.waveform = GenSigDma::WaveFormSquare;
	}
	else if (strcmp(strForm, "triangle") == 0) {
		g_sigState.waveform = GenSigDma::WaveFormTriangle;
	}
	else {
		return;
	}

	PF(true, "TODO\r\n");
}

inline CHANNEL_DESC *getChannelDesc(int channel)
{
	for (uint i = 0; i < DIMOF(g_scopeChannels); i++) {
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
	for (uint iChannel = 0; iChannel < DIMOF(g_scopeChannels); iChannel++) {
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
			y = potVar->prevValue;
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
	potVar->display.value = *potVar->value;
	potVar->display.prevValue = potVar->prevValue;

	drawVar(&potVar->display);
}

void drawVar(VAR_DISPLAY *var)
{
	char textBuf[40];

	for (int i = 0; i < 2; i++) {

		// Don't erase if not needed
		if (i == 0 && !var->bNeedsErase)
			continue;

		String s;
		s = String(var->prefix);
		if (i == 0) {
			// erase prev value
			s += String(var->prevValue);
			TFTscreen.stroke(BG_COLOR);
		}
		else {
			// draw new value
			s += String(var->value);
			TFTscreen.stroke(TEXT_COLOR);
		}
		s += String(var->suffix);
		s.toCharArray(textBuf, 15);
		TFTscreen.text(textBuf, var->x, var->y);
	}

	var->bNeedsErase = true;
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

		for (uint iChannel = 0; iChannel < g_scopeState.scopeChannelsCount; iChannel++) {
			oldSamples = g_channelDescs[iChannel].oldSamples;
			TFTscreen.stroke(BG_COLOR);
			TFTscreen.line(0, oldSamples[0], s_prevZoom, oldSamples[1]);
		}

		int lastXErase = s_prevZoom;

		// Erase sample iSample+1 while drawing sample iSample
		// otherwise, new drawn line could be overwritten by erased line
		for (;;) {
			for (uint iChannel = 0; iChannel < g_scopeState.prevScopeChannelsCount; iChannel++) {
				oldSamples = g_channelDescs[iChannel].oldSamples;
				// Erase old sample
				if (iSample + 1 < TFT_WIDTH) {
					TFTscreen.stroke(BG_COLOR);
					TFTscreen.line(lastXErase, oldSamples[iSample], lastXErase + s_prevZoom, oldSamples[iSample + 1]);
				}
			}
			lastXErase += s_prevZoom;

			for (uint iChannel = 0; iChannel < g_scopeState.scopeChannelsCount; iChannel++) {
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

			for (uint iChannel = 0; iChannel < g_scopeState.scopeChannelsCount; iChannel++) {
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
			for (uint iChannel = 0; iChannel < g_scopeState.prevScopeChannelsCount; iChannel++) {
				CHANNEL_DESC *pDesc = &g_channelDescs[iChannel];
				TFTscreen.line(iFrame - 1, pDesc->oldSamples[iFrame - 1], iFrame, pDesc->oldSamples[iFrame]);
			}
		}
	}

	return;
}

bool computeFrameRate()
{
	bool bRet = false;
	uint sec = millis() / 1000;
	static uint s_prevSec = 0;
	static uint s_loops = 0;

	if (sec != s_prevSec) {
		s_prevSec = sec;
		g_fpsVarDisplay.prevValue = g_fpsVarDisplay.value;
		g_fpsVarDisplay.value = s_loops;
		if (g_fpsVarDisplay.prevValue != g_fpsVarDisplay.value) {
			g_fpsVarDisplay.bNeedsErase = true;
			bRet = true;
		}
		s_loops = 0;
	}
	else {
		s_loops++;
	}

	return bRet;
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

		if (channel != g_scopeState.triggerChannel)
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

	if (bTimeout && g_scopeState.triggerStatus == TRIGGER_STATUS_WAITING) {
		g_scopeState.triggerStatus = TRIGGER_STATUS_TIMEOUT;
		g_scopeState.bTriggerStatusChanged = true;
	}
	else if (bIsTrigger) {

		// Trigger is not accurate in fast mode, since we're too slow
		// to get trigger sample at high sample rate.
		// Try to find it in current buffer
		if (g_drawState.drawMode == DRAW_MODE_FAST) {
			findTriggerSample(buffer, buflen, &triggerIndex);
		}

		buffer += triggerIndex;
		buflen -= triggerIndex;

		g_scopeState.triggerStatus = TRIGGER_STATUS_TRIGGERED;
		g_scopeState.bTriggerStatusChanged = true;
	}

	framesInBuf = buflen / channelsCount;

	g_drawState.rxFrames += framesInBuf;

	if (framesInBuf > TFT_WIDTH)
		framesInBuf = TFT_WIDTH;

	if (g_drawState.drawMode == DRAW_MODE_SLOW) {
		// Update pots values from last frame in buffer
		// Note: skip first samples since they are not very accurate
		if (g_drawState.rxFrames > 3) {
			updatePotsVars(buffer + buflen - channelsCount);
		}
	}

	if (g_scopeState.triggerStatus == TRIGGER_STATUS_WAITING) {
		return true;
	}

	mapBufferValues(g_drawState.mappedFrames, buffer, framesInBuf);

	// Stop AdcDma if we have enough samples
	if (g_drawState.mappedFrames == TFT_WIDTH) {
		PF(DBG_RX, "Got enough frames, stopping AdcDma\r\n");
		g_adcDma->Stop();
	}

	return true;
}

POT_VAR *getPotVar(uint16_t channel)
{
	for (int i = 0; i < (int)DIMOF(g_potVars); i++) {
		if (g_potVars[i].adcChannel == channel)
			return &g_potVars[i];
	}
	return NULL;
}

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
			if (diff > potVar->margin || potVar->forceRead) {
				value = map(potValue / 4, 0, POT_ANALOG_MAX_VAL / 4, *potVar->minValue, *potVar->maxValue);
				// Pot val might be out of bounds. In this case, crop value
				if (value < *potVar->minValue)
					value = *potVar->minValue;
				if (value > *potVar->maxValue)
					value = *potVar->maxValue;
				potVar->potValue = potValue;
				potVar->prevValue = *potVar->value;
				*potVar->value = value;
				potVar->changed = true;
				potVar->forceRead = false;
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
	int buflen;
	int bufcount = 5;
	int channelsCount = 0;
	uint16_t channels[ADC_DMA_MAX_ADC_CHANNEL];

	g_scopeState.prevScopeChannelsCount = g_scopeState.scopeChannelsCount;

	if (g_scopeState.bScopeChannelsCountChanged) {
		g_scopeState.scopeChannelsCount = g_scopeState.newScopeChannelsCount;
		g_scopeState.bScopeChannelsCountChanged = false;
	}

	// Add scope channels
	for (uint i = 0; i < g_scopeState.scopeChannelsCount; i++) {
		channels[i] = g_scopeChannels[i];
		channelsCount++;
	}

	// Add pot channels in slow mode
	if (g_drawState.drawMode == DRAW_MODE_SLOW) {
		for (int i = 0; i < g_potChannelsCount; i++) {
			channels[g_scopeState.scopeChannelsCount + i] = g_potChannels[i];
			channelsCount++;
		}
	}

	if (g_drawState.drawMode == DRAW_MODE_SLOW) {
		buflen = sizeof(uint16_t) * channelsCount;
	}
	else {
		// Setup buflen for 1/10 sec duration
		buflen = g_scopeState.sampleRate / 10 * sizeof(uint16_t) * g_scopeState.scopeChannelsCount;
	}

	PF(true, "buflen %d\r\n", buflen);

	if (buflen > ADC_DMA_DEF_BUF_SIZE) {
		buflen = ADC_DMA_DEF_BUF_SIZE;
	}

	if (buflen == 0) {
		buflen = sizeof(uint16_t) * channelsCount;
	}

	g_adcDma->SetAdcChannels(channels, channelsCount);
	g_adcDma->SetBuffers(bufcount, buflen);
	g_adcDma->SetTrigger(g_scopeState.triggerVal, g_triggerMode, g_scopeState.triggerChannel, 1000);
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
		g_genSigDma->SetWaveForm(g_sigState.waveform, g_sigState.freq, NULL);
		g_genSigDma->Start();
		drawPotVar(potVar);
		potVar->changed = false;
	}
	potVar = getPotVar("RATE");
	if (potVar->changed) {
		drawPotVar(potVar);
		// If we go from slow to fast mode, we need to restart AdcDma
		if ( (g_drawState.drawMode == DRAW_MODE_SLOW) && (*potVar->value >= ADC_SAMPLE_RATE_LOW_LIMIT) ) {
			g_adcDma->Stop();
			g_drawState.bFinished = true;
		}
		g_adcDma->SetSampleRate(g_scopeState.sampleRate);
		potVar->changed = false;
	}
	potVar = getPotVar("TRIG");
	if (potVar->changed) {
		drawTriggerArrow(potVar);
		drawGrid();
		// Force drawn samples to 0 to redraw all
		g_drawState.drawnFrames = 0;
		drawEraseSamples(true, false);
		g_adcDma->SetTrigger(g_scopeState.triggerVal, g_triggerMode, g_scopeState.triggerChannel, 1000);
		potVar->changed = false;
	}
}

void loop()
{
	if ( (g_drawState.drawMode == DRAW_MODE_SLOW) ||
		 (g_drawState.drawMode == DRAW_MODE_FAST && g_drawState.mappedFrames == TFT_WIDTH) ) {

		// In slow mode, erase current samples if flag is set and we have something to draw
		// In fast mode, old samples are erased while the new ones are drawn
		if (g_drawState.bNeedsErase) {
			if ( (g_drawState.drawMode == DRAW_MODE_FAST) ||
				 (g_drawState.drawMode == DRAW_MODE_SLOW && g_drawState.mappedFrames > 0) ) {
				drawEraseSamples(false, true);
				drawGrid();
				g_drawState.bNeedsErase = false;
			}
		}

		if (g_drawState.drawMode == DRAW_MODE_FAST) {
			drawEraseSamples(true, true);
		}
		else {
			drawEraseSamples(true, false);
		}

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

		SCmd.readSerial();
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

		computeFrameRate();
		drawVar(&g_fpsVarDisplay);

		setupAdcDma();

		PF(false, "Starting ADCDMA\r\n");
		g_adcDma->Start();
	}
}
