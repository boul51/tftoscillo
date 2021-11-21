#include <Arduino.h>

#include <SerialCommand.h>

#include <TFT.h>  // Arduino LCD library

#include <GenSigDma.h>
#include <AdcDma.h>
#include <PrintStream.h>
#include <MemoryFree.h>

#include "oscdisplay.h"
#include "robotlcdoscdisplaydriver.h"

#include "tftoscillo.h"

#if HAS_LOGGER != 1
#define logdebug(...)
#define loginfo(...)
#define logwarning(...)
#define logerror(...)
#endif

#include "platform_logger.h"

/**** DEFINES ****/

#ifndef SERIAL_IFACE
#error "Please define SERIAL_IFACE to Serial (programming port) or SerialUSB (native port)"
#endif

// TFT screen definitions
#define TFT_WIDTH   160		// Screen width
#define TFT_HEIGHT  128		// Screen height

// LCD/SD outputs
#define TFT_BL_PIN    9		// Backlight
#define TFT_RST_PIN  10		// Reset pin
#define TFT_DC_PIN   11		// Command / Display data pin
#define USD_CS_PIN	 12	    // MicroSD chip select
#define TFT_CS_PIN   13		// Chip select pin

// Pot and scope inputs
#define SCOPE_CHANNEL_1			7
#define SCOPE_CHANNEL_2			6
#define SCOPE_CHANNEL_3			5
#define SCOPE_CHANNEL_4			4
#define FREQ_CHANNEL			3
#define ADC_RATE_CHANNEL		2
#define TRIGGER_CHANNEL			1
#define GAIN_CHANNEL_1			0

// Pot definitions

#define POT_ANALOG_MAX_VAL (988*ANALOG_MAX_VAL/1000)	// This is used for pot input. Max value read is not 4096 but around 4050 ie 98.8%
#define POT_ANALOG_DIFF				20					// Min difference between two pot values to consider is was moved

#define TRIGGER_TIMEOUT				1000

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

// Hardware gain used to calibrate
#define CAL_HW_GAIN 4

int g_zoom = 1;

uint32_t g_frameRate = 0;		// Scope fps

// Used to store pots value in slow mode
uint16_t g_rxBuffer[ADC_DMA_MAX_ADC_CHANNEL];

AdcDma::TriggerMode g_triggerMode = AdcDma::RisingEdge;

// In free run mode, we'll always be at 12 bits res
#define SAMPLE_MAX_VAL ((1 << 12) - 1)

/**** GLOBAL VARIABLES ****/

// Pointer on GenSigDma object
GenSigDma *g_genSigDma = NULL;
AdcDma *g_adcDma = NULL;

nboul::oscdisplay::OscDisplay* m_display = nullptr;

SerialCommand SCmd;

#include <CircularBufferLogger.h>
CircularLogBufferLogger<512> CLog;

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
#define VGRID_START			(TFT_WIDTH / 2)	// x-axis position of the first vertical line
#define VGRID_INTERVAL		50				// distance between vertical lines
#define VGRID_SUB_INTERVAL	25				// distance between sub divisions

#define HGRID_START			(TFT_HEIGHT / 2)// y-axis position of the first horizontal line
#define HGRID_INTERVAL		VGRID_INTERVAL	// distance between horizontal lines
#define HGRID_SUB_INTERVAL	VGRID_SUB_INTERVAL

#define HGRID_MARGIN	10					// distance between border and first horizontal line

// Tft screen instance
TFT TFTscreen = TFT(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

uint16_t g_scopeChannels [] = {SCOPE_CHANNEL_1, SCOPE_CHANNEL_2, SCOPE_CHANNEL_3, SCOPE_CHANNEL_4};

// Channel descriptors
CHANNEL_DESC *g_channelDescs;

int g_minY, g_maxY;

uint8_t g_scopeColors[][3] = {
	{255, 102,  51},
	{204,  51, 255},
	{ 51, 204, 255},
	{255,  51, 204},
};

// DacDma available frequencies
uint32_t g_dacFreqs[] = {
	1, 2, 5,
	10, 25, 50,
	50, 100, 250,
	500, 1000, 2500,
	5000, 10000, 25000,
	50000, 100000,
};

// Scope available rates, in µs/div
uint32_t g_scopeRates[] = {
	28,
	50, 100, 250,				// 50-250 µs
	500, 1000, 2500,			// 0.5-2.5 ms
	5000, 10000, 25000,			// 5-25 ms
	50000, 100000, 250000,		// 50-250 ms
	500000, 1000000, 2500000,	// 0.5-2.5 s
};

// Gain available values
/*
uint32_t g_scopeGains[] = {
	1,
	2,
	5,
	10,
	20,
	50,
	100,
};
*/
// Vertical resolutions, in V/div
float g_scopeVRes[] = {
	0.05,
	0.1,
	0.25,
	0.5,
	1.,
	2.5,
	5.,
	10.,
	25.,
	50.,
};

SCOPE_STATE g_scopeState =
{
	.sampleRate			= 0,
	.triggerChannel		= SCOPE_CHANNEL_1,
	.prevTriggerChannel	= SCOPE_CHANNEL_1,
	.triggerVal			= 2000,
	.minTriggerVal		= 0,
	.maxTriggerVal		= ANALOG_MAX_VAL,
	.triggerStatus		= TRIGGER_STATUS_DISABLED,
	.bTriggerStatusChanged = false,
	.prevScopeChannelsCount = 1,
	.scopeChannelsCount = 1,
	.newScopeChannelsCount = 1,
	.bScopeChannelsCountChanged = false,
	.blVal = 100,
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
		.prevPotValue = 0,
		.minValue	= NULL,
		.maxValue	= NULL,
		.value		= NULL,
		.prevValue	= 0,
		//.margin		= POT_ANALOG_DIFF,
		.margin		= 5,
		.forceRead	= true,
		.name		= "TRIG",
		.cbPotVarChanged = cbPotVarChangedTrig,
		.bHasVarDisplay = false,
		.display	= {
			.bNeedsErase	= false,
			.prefix			= "",
			.suffix			= "",
			.prevSuffix		= "",
			.value			= 0,
			.prevValue		= 0,
			.valuef			= 0.,
			.prevValuef		= 0.,
			.x				= 0,
			.y				= 0,
		}
	},
	{
		.adcChannel	= GAIN_CHANNEL_1,
		.potValue	= 0,
		.prevPotValue = 0,
		.minValue	= NULL,
		.maxValue	= NULL,
		.value		= NULL,
		.prevValue	= 0,
		.margin		= POT_ANALOG_DIFF,
		.forceRead	= true,
		.name		= "GAIN0",
		.cbPotVarChanged = cbPotVarChangedGain,
		.bHasVarDisplay = false,
		.display	= {
			.bNeedsErase	= false,
			.prefix			= "",
			.suffix			= "", // Will be set later
			.prevSuffix		= "", // Will be set later
			.value			= 0,
			.prevValue		= 0,
			.valuef			= 0.,
			.prevValuef		= 0.,
			.x				= 10,
			.y				= TEXT_DOWN_Y_OFFSET,
		}
	},
	{
		.adcChannel	= ADC_RATE_CHANNEL,
		.potValue	= 0,
		.prevPotValue = 0,
		.minValue	= NULL,
		.maxValue	= NULL,
		.value		= NULL,
		.prevValue	= 0,
		.margin		= POT_ANALOG_DIFF,
		.forceRead	= true,
		.name		= "RATE",
		.cbPotVarChanged = cbPotVarChangedRate,
		.bHasVarDisplay = true,
		.display	= {
			.bNeedsErase	= false,
			.prefix			= "SR:",
			.suffix			= "", // Will be set later
			.prevSuffix		= "", // Will be set later
			.value			= 0,
			.prevValue		= 0,
			.valuef			= 0.,
			.prevValuef		= 0.,
			.x				= TFT_WIDTH / 2,
			.y				= TEXT_UP_Y_OFFSET,
		}
	},
	{
		.adcChannel	= FREQ_CHANNEL,
        .potValue	= (uint32_t)-1,
		.prevPotValue = 0,
		.minValue	= &g_sigState.minFreq,
		.maxValue	= &g_sigState.maxFreq,
		.value		= NULL,
		.prevValue	= 0,
		.margin		= POT_ANALOG_DIFF,
		.forceRead	= true,
		.name		= "FREQ",
		.cbPotVarChanged = cbPotVarChangedFreq,
		.bHasVarDisplay = true,
		.display	= {
			.bNeedsErase	= false,
			.prefix			= "Fq:",
			.suffix			= "Hz",
			.prevSuffix		= "Hz",
			.value			= 0,
			.prevValue		= 0,
			.valuef			= 0.,
			.prevValuef		= 0.,
			.x				= 10,
			.y				= TEXT_UP_Y_OFFSET,
		}
	},
};

VAR_DISPLAY g_fpsVarDisplay =
{
	.bNeedsErase	= false,
	.prefix			= "Fps:",
	.suffix			= "", // None
	.prevSuffix		= "", // None
	.value			= 0,
	.prevValue		= 0,
	.valuef			= 0.,
	.prevValuef		= 0.,
	.x				= TFT_WIDTH / 2 + 20,
	.y				= TEXT_DOWN_Y_OFFSET,
};

void setup() {

	PlatformLogger::inst().echo(false);
	PlatformLogger::inst().auto_flush(false);
	loglevel(log_level_e::info);

	SERIAL_IFACE.begin(115200);
    while (!SERIAL_IFACE) {}
	printf_init(SERIAL_IFACE);

	loginfo("Entering setup, available memory %d\n", freeMemory());

	auto driver = new nboul::oscdisplaydriver::RobotLcdDisplayDriver();
	m_display = new nboul::oscdisplay::OscDisplay(TFT_WIDTH, TFT_HEIGHT, driver);

	g_channelDescs = (CHANNEL_DESC *)malloc(DIMOF(g_scopeChannels) * sizeof(CHANNEL_DESC));

	// Init channels descriptors
    for (uint32_t i = 0; i < DIMOF(g_scopeChannels); i++) {
		g_channelDescs[i].channel = g_scopeChannels[i];
		g_channelDescs[i].bufSize = TFT_WIDTH;
		g_channelDescs[i].swGain = 1.;
		g_channelDescs[i].hwGain = 1;

		for (int iGain = 0; iGain < ADC_DMA_HW_GAINS_COUNT; iGain++)
		{
			g_channelDescs[i].gndOffsets[iGain] = 0;
		}

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
	SCmd.addCommand("tgch", triggerChannelHandler);
	SCmd.addCommand("fr", freqRangeHandler);
	SCmd.addCommand("zoom", zoomHandler);
	SCmd.addCommand("form", formHandler);
	SCmd.addCommand("ch", channelCountHandler);
	SCmd.addCommand("bl", blHandler);
	SCmd.addCommand("gain", channelGainHandler);
	SCmd.addCommand("cal", calHandler);
	SCmd.addDefaultHandler(defaultHandler);

	// Initialize LCD
	// Note: TFT library was modified to set SPI clock divider to 4
	// We use setRotation to reverse the screen
	TFTscreen.begin();
	TFTscreen.setRotation(3);
	TFTscreen.background(255, 255, 255);
	analogWriteResolution(ANALOG_RES);
	analogWrite(TFT_BL_PIN, 0);

	g_genSigDma = new GenSigDma();
	g_genSigDma->SetTimerChannel(1);

	g_adcDma = AdcDma::GetInstance();
	g_adcDma->SetTimerChannel(2);
	g_adcDma->SetRxHandler(rxHandler);
	g_adcDma->SetTriggerPreBuffersCount(0);

	// This will update g_drawState.minY and g_drawState.maxY
	drawGrid(0, TFT_WIDTH);

	updatePotsVars(NULL);

	// Auto calibrate first channel
	calChannel(0);

	loginfo("Setup done, starting, available memory %d\n", freeMemory());

	logflush();
}

void calChannel(int chIdx)
{
	uint16_t gndValue;
	float avg = 0.;
	int avgCnt = 30;
	CHANNEL_DESC *ch;
	int hwGain;

	ch = &g_channelDescs[chIdx];

	AdcDma::CaptureState state = g_adcDma->GetCaptureState();
	g_adcDma->Stop();

	// Store hardware gain, we'll change it to find ground value
	hwGain = g_adcDma->GetChannelGain(ch->channel);

	for (int iGain = 0; iGain < ADC_DMA_HW_GAINS_COUNT; iGain++)
	{
		avg = 0.;
		int g = AdcDma::HwGainAtIndex(iGain);

		loginfo("Setting hwGain %d\n", g);
		g_adcDma->SetChannelGain(ch->channel, g);
		//g_adcDma->SetChannelGain(ch->channel, CAL_HW_GAIN);
		for (int i = 0; i < avgCnt; i++) {
			g_adcDma->ReadSingleValue(ch->channel, &gndValue);
			delay(10);
			avg += (float)gndValue;
		}
		avg /= (float)avgCnt;
		gndValue = (uint16_t)avg;
		//g_adcDma->ReadSingleValue(ch->channel, &gndValue);

		//ch->gndOffset = (gndValue - ANALOG_MAX_VAL / 2) / CAL_HW_GAIN;
		ch->gndOffsets[iGain] = (gndValue - ANALOG_MAX_VAL / 2);
	}

	g_adcDma->SetChannelGain(ch->channel, hwGain);

	if (state == AdcDma::CaptureStateStarted) {
		g_adcDma->Start();
	}
}

void defaultHandler()
{
	SERIAL_IFACE.println("Invalid command received\n");
}

void zoomHandler()
{
	char * strZoom;

	strZoom = SCmd.next();

	if (strZoom == NULL) {
	SERIAL_IFACE.println(g_zoom);
		return;
	}

	g_zoom = atoi(strZoom);
}

void calHandler()
{
    uint32_t chIdx;
	char * strCh;

	strCh = SCmd.next();

	if (strCh == NULL) {
	SERIAL_IFACE.println("calHandler: expecting channel index argument");
		return;
	}

	chIdx = (uint32_t)atoi(strCh);

	SERIAL_IFACE.print("Got channel index ");
	SERIAL_IFACE.println(chIdx);

	if (chIdx >= DIMOF(g_scopeChannels)) {
	SERIAL_IFACE.println("calHandler: invalid channel");
		return;
	}

	calChannel(chIdx);
}

void channelCountHandler()
{
	char * strCh;
	int chCnt;

	strCh = SCmd.next();

	if (strCh == NULL) {
	SERIAL_IFACE.println(g_scopeState.scopeChannelsCount);
		return;
	}

	chCnt = atoi(strCh);

	if (chCnt <= 0 || chCnt > (int)DIMOF(g_scopeChannels)) {
		SERIAL_IFACE.print("Invalid channels count ");
		SERIAL_IFACE.println(chCnt);
		return;
	}

	g_scopeState.newScopeChannelsCount = chCnt;
	g_scopeState.bScopeChannelsCountChanged = true;
}

void channelGainHandler()
{
	char * strCh, *strGain;
	int chIdx, gain;

	strCh = SCmd.next();

	if (strCh == NULL) {
		SERIAL_IFACE.println("Missing channel argument for gain command");
		return;
	}

	chIdx = atoi(strCh);
	if (chIdx < 0 || chIdx > ADC_DMA_MAX_ADC_CHANNEL) {
		SERIAL_IFACE.println("Invalid channel");
		return;
	}

	strGain = SCmd.next();

	if (strGain == NULL) {
		SERIAL_IFACE.print("Gain for channel ");
		SERIAL_IFACE.print(chIdx);
		SERIAL_IFACE.print(": ");
		SERIAL_IFACE.println(g_adcDma->GetChannelGain(chIdx));
		return;
	}

	gain = atoi(strGain);

	setChannelGlobalGain(chIdx, gain);
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
	SERIAL_IFACE.print(g_sigState.freq);
	SERIAL_IFACE.print(" (");
	SERIAL_IFACE.print(g_sigState.minFreq);
	SERIAL_IFACE.print(" - ");
	SERIAL_IFACE.print(g_sigState.maxFreq);
	SERIAL_IFACE.println(")");
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

void triggerModeHandler()
{
	char * strMode = SCmd.next();

	if (strMode == NULL) {
		switch (g_triggerMode) {
		case AdcDma::RisingEdge :
			SERIAL_IFACE.println("rising");
			break;

		case AdcDma::FallingEdge :
			SERIAL_IFACE.println("falling");
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

void triggerChannelHandler()
{
	char * strCh;
	CHANNEL_DESC *pChDesc = NULL;
	int chIdx;

	strCh = SCmd.next();

	if (strCh == NULL) {
	SERIAL_IFACE.println("Missing channel argument for trigger channel command");
		return;
	}

	chIdx = atoi(strCh);
	if (chIdx < 0 || chIdx > ADC_DMA_MAX_ADC_CHANNEL) {
	SERIAL_IFACE.println("Invalid channel");
		return;
	}

	pChDesc = &g_channelDescs[chIdx];

	g_scopeState.triggerChannel = pChDesc->channel;
	drawTriggerArrow(getPotVar("TRIG"), true);
}

void formHandler()
{
	char * strForm = SCmd.next();

	if (strForm == NULL) {
		switch (g_sigState.waveform) {
		case GenSigDma::WaveFormSaw :
			SERIAL_IFACE.println("saw");
			break;
		case GenSigDma::WaveFormSinus :
			SERIAL_IFACE.println("sinus");
			break;
		case GenSigDma::WaveFormSquare :
			SERIAL_IFACE.println("square");
			break;
		case GenSigDma::WaveFormTriangle :
			SERIAL_IFACE.println("triangle");
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

	g_genSigDma->Stop();
	g_genSigDma->SetWaveForm(g_sigState.waveform, g_sigState.freq, NULL);
	g_genSigDma->Start();
}

void blHandler()
{
	char * strBl = SCmd.next();
	int blVal;

	if (strBl == NULL) {
		SERIAL_IFACE.print(g_scopeState.blVal);
		SERIAL_IFACE.println("");
		return;
	}

	blVal = atol(strBl);

	if (blVal < 0 || blVal > 100) {
		SERIAL_IFACE.print("Invalid backlight value ");
		SERIAL_IFACE.println(blVal);
		return;
	}

	g_scopeState.blVal = blVal;
	SERIAL_IFACE.print("Setting backlight value: ");
	SERIAL_IFACE.println(blVal);

	blVal = map(blVal, 100, 0, 0, ANALOG_MAX_VAL);
	analogWrite(TFT_BL_PIN, blVal);

	return;
}

inline CHANNEL_DESC *getChannelDesc(int channel)
{
	for (uint32_t i = 0; i < DIMOF(g_scopeChannels); i++) {
		if (g_channelDescs[i].channel == channel)
			return &g_channelDescs[i];
	}

	return NULL;
}

inline int getChannelIndex(int adcChannel)
{
	for (uint32_t i = 0; i < DIMOF(g_scopeChannels); i++) {
		if (g_channelDescs[i].channel == adcChannel)
			return i;
	}

	return 0;
}

int groundOffsetForChannel(CHANNEL_DESC *pChDesc)
{
	int gainIdx = AdcDma::HwGainIndex(pChDesc->hwGain);
	return pChDesc->gndOffsets[gainIdx];
}

void mapBufferValues(int frameOffset, uint16_t *buf, int framesCount)
{
	uint16_t rawSample;
	uint16_t sample;
	int scaledSample;
	uint16_t mappedVal;
	int channel;
	int channelsCount = g_adcDma->GetAdcChannelsCount();
	CHANNEL_DESC *channelDesc;

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

			logdebug("Got channel %d, sample %d\n", channel, sample);

			// We have samples for input channels, but also for potentiometers, so channelDesc may be null
			if ((channelDesc = getChannelDesc(channel))) {

				sample -= groundOffsetForChannel(channelDesc);

				float gain = channelDesc->swGain;
				if (gain != 1.) {
					scaledSample = (uint32_t)((float)sample * gain - (gain - 1.) *
											  (float)SAMPLE_MAX_VAL / 2.);
					if (scaledSample < 0)
						scaledSample = 0;
					if (scaledSample > SAMPLE_MAX_VAL)
						scaledSample = SAMPLE_MAX_VAL;
					sample = scaledSample;
				}

				mappedVal = map(sample, 0, SAMPLE_MAX_VAL, g_maxY - 1, g_minY);
				channelDesc->curSamples[iFrame + frameOffset] = mappedVal;
			}
		}

		logdebug("sf %d\n", g_drawState.mappedFrames);
		g_drawState.mappedFrames++;

		if (g_drawState.mappedFrames == TFT_WIDTH)
			break;
	}

	return;
}

void swapSampleBuffer()
{
    for (uint32_t iChannel = 0; iChannel < DIMOF(g_scopeChannels); iChannel++) {
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

void drawTriggerArrow(POT_VAR *potVar, bool bErase)
{
	CHANNEL_DESC *ch;
	float gain;
	ch = getChannelDesc(g_scopeState.triggerChannel);
	// Update current and new value
    //g_scopeState.triggerVal = (uint32_t)((float)potVar->potValue / ch->swGain);
	g_scopeState.triggerVal = potVar->potValue;

	potVar->display.prevValue = potVar->display.value;

    //potVar->display.value = (uint32_t)((float)potVar->potValue * ch->swGain);

	gain = ch->swGain;

	int gndOffset = groundOffsetForChannel(ch);
	potVar->display.value = (uint32_t)((float)((int)potVar->potValue - gndOffset)*
								   gain - (gain - 1.) *
								   (float)SAMPLE_MAX_VAL / 2.);

	// Redraw trigger line (always since signal may overwrite it)
	for (int i = 0; i < 2; i++) {
		int y;
		if (i == 0 && bErase) {
			// Erase
			TFTscreen.stroke(BG_COLOR);
			y = potVar->display.prevValue;
		}
		else {
			// Draw
			TFTscreen.stroke(TRIGGER_COLOR);
			y = potVar->display.value;
		}
		y = map(y, 0, ANALOG_MAX_VAL, g_maxY, g_minY);

		if (y < g_minY)
			y = g_minY;
		if (y > g_maxY)
			y = g_maxY;

		TFTscreen.line(0, y, TRIGGER_ARROW_LEN, y);

		/* Use this to draw arrow towards right */
		// TFTscreen.line(TRIGGER_ARROW_LEN, y, TRIGGER_ARROW_LEN - TRIGGER_ARROW_HEIGHT, y - TRIGGER_ARROW_HEIGHT);
		// TFTscreen.line(TRIGGER_ARROW_LEN, y, TRIGGER_ARROW_LEN - TRIGGER_ARROW_HEIGHT, y + TRIGGER_ARROW_HEIGHT);

		/* Use this to draw arrow towards left */
		TFTscreen.line(0, y, TRIGGER_ARROW_HEIGHT, y - TRIGGER_ARROW_HEIGHT);
		TFTscreen.line(0, y, TRIGGER_ARROW_HEIGHT, y + TRIGGER_ARROW_HEIGHT);
	}
}

// Clone of dtostrf to avoid conflicts and missing include files
char *tftosc_dtostrf(float val, signed char width, unsigned char prec, char *sout) {
  char fmt[20];
  sprintf(fmt, "%%%d.%df", width, prec);
  sprintf(sout, fmt, val);
  return sout;
}

void drawVar(VAR_DISPLAY *var, VAR_TYPE type)
{
	char textBuf[40];
	String sValue;
	int value;
	float valuef;
	const char * suffix, *prefix;

	for (int i = 0; i < 2; i++) {

		// Don't erase if not needed
		if (i == 0 && !var->bNeedsErase)
			continue;

		if (i == 0) {
			// erase prev value
			prefix = var->prefix;
			suffix = var->prevSuffix;
			TFTscreen.stroke(BG_COLOR);
			value = var->prevValue;
			valuef = var->prevValuef;
		}
		else {
			// draw new value
			prefix = var->prefix;
			suffix = var->suffix;
			TFTscreen.stroke(TEXT_COLOR);
			value = var->value;
			valuef = var->valuef;
		}

		if (type == VAR_TYPE_FLOAT) {
			char strValue[10];
			tftosc_dtostrf(valuef, 0, 1, strValue);
			// dirty hack: remove trailing .0 if integer !
			if (strValue[strlen(strValue) - 1] == '0') {
				strValue[strlen(strValue) - 2] = 0;
			}
			sValue = String(strValue);
		}
		else {
			sValue = String(value);
		}

		String s;
		s  = String(prefix);
		s += String(sValue);
		s += String(suffix);
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

		for (int i = 0; i < 2; i++) {
			int tgAdcChannel;
			if (i == 0) {
				tgAdcChannel = g_scopeState.prevTriggerChannel;
				TFTscreen.stroke(BG_COLOR);
			}
			else {
				tgAdcChannel = g_scopeState.triggerChannel;
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
			sprintf(textBuf, "T(ch%d)", getChannelIndex(tgAdcChannel) + 1); // Draw channel index + 1
			logdebug("drawing text at %d:%d\n", 10, TEXT_DOWN_Y_OFFSET);
			TFTscreen.text(textBuf, TFT_WIDTH * 2 / 3, TEXT_DOWN_Y_OFFSET);
		}
	}

	g_scopeState.prevTriggerChannel = g_scopeState.triggerChannel;
}

void drawGrid(int minX, int maxX)
{
	// Min and max y for horizontal lines
	// We'll use it to start vertical lines from them
	int minY = 0, maxY = 0;

	// Use this to draw secondary lines
	int loop = 0;
	// Draw one line dark, one line light
	int secLoop = HGRID_INTERVAL / HGRID_SUB_INTERVAL;

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

		TFTscreen.line(minX,  yStart + yOffset, maxX, yStart + yOffset);
		TFTscreen.line(minX,  yStart - yOffset, maxX, yStart - yOffset);
		if (yStart + yOffset + margin < TFT_HEIGHT)
			maxY = yStart + yOffset;
		if (yStart - yOffset > margin)
			minY = yStart - yOffset;
		yOffset += HGRID_SUB_INTERVAL;
	}

	// Draw vertival grid (vertical lines)
	TFTscreen.stroke(VGRID_COLOR);
	int xStart = VGRID_START;
	int xOffset = 0;
	loop = 0;
	secLoop = VGRID_INTERVAL / VGRID_SUB_INTERVAL;
	while ( (xStart + xOffset <= maxX) || (xStart - xOffset >= minX) ) {

		if (loop % secLoop == 0) {
			TFTscreen.stroke(VGRID_COLOR);
		}
		else {
			TFTscreen.stroke(VGRID_SEC_COLOR);
		}
		loop++;

		if ( (xStart + xOffset >= minX) && (xStart + xOffset <= maxX)) {
			TFTscreen.line(xStart + xOffset, minY, xStart + xOffset, maxY);
		}
		if ( (xStart - xOffset >= minX) && (xStart - xOffset <= maxX)) {
			TFTscreen.line(xStart - xOffset, minY, xStart - xOffset, maxY);
		}
		xOffset += VGRID_SUB_INTERVAL;
	}

	g_minY = minY;
	g_maxY = maxY;
}

void drawEraseSamples(bool bDraw, bool bErase)
{
	logdebug("g_drawState.drawnFrames %d, g_drawState.sampledFrames %d\n", g_drawState.drawnFrames, g_drawState.mappedFrames);
	int s_prevZoom = 1;

	// Draw current sample before drawing it (used in fast mode)
	if (bDraw && bErase) {
		uint16_t *oldSamples;
		uint16_t *newSamples;

		int iSample = 1;

		int lastXDraw = 0;

		// Erase first old sample

        for (uint32_t iChannel = 0; iChannel < g_scopeState.scopeChannelsCount; iChannel++) {
			oldSamples = g_channelDescs[iChannel].oldSamples;
			TFTscreen.stroke(BG_COLOR);
			TFTscreen.line(0, oldSamples[0], s_prevZoom, oldSamples[1]);
		}

		int lastXErase = s_prevZoom;

		// Erase sample iSample+1 while drawing sample iSample
		// otherwise, new drawn line could be overwritten by erased line
		for (;;) {
            for (uint32_t iChannel = 0; iChannel < g_scopeState.prevScopeChannelsCount; iChannel++) {
				oldSamples = g_channelDescs[iChannel].oldSamples;
				// Erase old sample
				if (iSample + 1 < TFT_WIDTH) {
					TFTscreen.stroke(BG_COLOR);
					TFTscreen.line(lastXErase, oldSamples[iSample], lastXErase + s_prevZoom, oldSamples[iSample + 1]);
				}
			}

			// In this mode, we need to redraw part of the grid after samples were erased and before
			// new samples are drawn
			drawGrid(lastXErase, lastXErase + s_prevZoom);

			lastXErase += s_prevZoom;

            for (uint32_t iChannel = 0; iChannel < g_scopeState.scopeChannelsCount; iChannel++) {
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
	// Draw all currently sampled frames (used in slow mode)
	else if (bDraw) {
		for (int xStart = g_drawState.drawnFrames; xStart + 1 < g_drawState.mappedFrames; xStart++) {

			logdebug("x0 %d, x1 %d, df %d, sf %d\n", xStart, xStart + 1, g_drawState.drawnFrames, g_drawState.mappedFrames);

            for (uint32_t iChannel = 0; iChannel < g_scopeState.scopeChannelsCount; iChannel++) {
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
            for (uint32_t iChannel = 0; iChannel < g_scopeState.prevScopeChannelsCount; iChannel++) {
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
    uint32_t sec = millis() / 1000;
    static uint32_t s_prevSec = 0;
    static uint32_t s_loops = 0;

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
	g_drawState.drawnFrames = 0;
	g_drawState.mappedFrames = 0;
	g_drawState.rxFrames = 0;
}

void initScopeState()
{
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

	triggerVal = g_scopeState.triggerVal;

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
		logdebug("Did not find trigger sample !\n");
	}

	if (prevTriggerIndex < *triggerIndex) {
		logdebug("New trigger index is later !\n");
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
		// Copy data to buffer, we'll use them in loop() to update pots values
		memcpy(g_rxBuffer, buffer, channelsCount * sizeof(uint16_t));
	}

	if (g_scopeState.triggerStatus == TRIGGER_STATUS_WAITING) {
		return true;
	}

	mapBufferValues(g_drawState.mappedFrames, buffer, framesInBuf);

	// Stop AdcDma if we have enough samples
	if (g_drawState.mappedFrames == TFT_WIDTH) {
		logdebug("Got enough frames, stopping AdcDma\n");
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
    uint32_t value;
    uint32_t diff;
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

		if (potVar) {
			diff = ABS_DIFF(potVar->potValue, potValue);
			if (diff > potVar->margin || potVar->forceRead) {
				// Pot val might be out of bounds. In this case, crop value
				if (potVar->minValue && potVar->maxValue) {
					value = map(potValue / 4, 0, POT_ANALOG_MAX_VAL / 4, *potVar->minValue, *potVar->maxValue);
					if (value < *potVar->minValue)
						value = *potVar->minValue;
					if (value > *potVar->maxValue)
						value = *potVar->maxValue;
				}
				else {
					value = potValue;
				}
				potVar->prevPotValue = potVar->potValue;
				potVar->potValue = potValue;
				if (potVar->value) {
					potVar->prevValue = *potVar->value;
					*potVar->value = value;
				}
				potVar->forceRead = false;
				logdebug("New value for potVar %s: %d (%d > %d)\n", potVar->name, *potVar->value, diff, potVar->margin);
				if (potVar->cbPotVarChanged) {
					potVar->cbPotVarChanged(potVar);
				}
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
	int  buflen;				// Size of ADC DMA buffers
	int  bufcount = 5;			// Number of ADC DMA buffers
    uint32_t irqsPerSec;			// Number of ADC DMA IRQs/sec
    uint32_t maxIrqsPerSec = 500;	// Max number of ADC DMA IRQs/sec we want
	int  channelsCount = 0;
	uint16_t channels[ADC_DMA_MAX_ADC_CHANNEL];

	g_scopeState.prevScopeChannelsCount = g_scopeState.scopeChannelsCount;

	if (g_scopeState.bScopeChannelsCountChanged) {
		g_scopeState.scopeChannelsCount = g_scopeState.newScopeChannelsCount;
		g_scopeState.bScopeChannelsCountChanged = false;
	}

	// Add scope channels
    for (uint32_t i = 0; i < g_scopeState.scopeChannelsCount; i++) {
		channels[i] = g_scopeChannels[i];
		channelsCount++;
	}

	// Add pot channels in slow mode
	if (g_drawState.drawMode == DRAW_MODE_SLOW) {
		for (uint i = 0; i < DIMOF(g_potVars); i++)
		{
			channels[g_scopeState.scopeChannelsCount + i] = g_potVars[i].adcChannel;
			channelsCount++;
		}
	}

	if (g_drawState.drawMode == DRAW_MODE_SLOW) {
		buflen = sizeof(uint16_t) * channelsCount;
	}
	else {
		// Setup buflen to be sure we have enough samples to fill the screen
		buflen = TFT_WIDTH / (bufcount - 1) * sizeof(uint16_t) * channelsCount;

		// Adjust buffers length so that number of irqs is lower than the limit we fixed
		// Note: If sample rate is too high, we will be higher anyway since buffers size
		// is limited by ADC_MAX_MEM
		irqsPerSec = g_scopeState.sampleRate / buflen * sizeof(uint16_t) * channelsCount;
		if (irqsPerSec > maxIrqsPerSec) {
			logdebug("ajusting buflen, was %d\n", buflen);
			buflen = g_scopeState.sampleRate / maxIrqsPerSec * sizeof(uint16_t) * channelsCount ;
		}

		logdebug("Calculated buflen %d, irqs/s %d\n", buflen, g_scopeState.sampleRate / buflen * sizeof(uint16_t) * channelsCount);
	}

	if (buflen * bufcount > ADC_DMA_MAX_MEM) {
		buflen = ADC_DMA_MAX_MEM / bufcount;
	}

	if (buflen == 0) {
		buflen = sizeof(uint16_t) * channelsCount;
	}

	logdebug("buflen %d\n", buflen);

	g_adcDma->SetAdcChannels(channels, channelsCount);
	g_adcDma->SetBuffers(bufcount, buflen);
    uint32_t timeout = computeTimeout();
	g_adcDma->SetTrigger(g_scopeState.triggerVal, g_triggerMode, g_scopeState.triggerChannel, timeout);
}

uint32_t usecPerDivFromPotValue(uint32_t potValue)
{
	int idx = potValue * DIMOF(g_scopeRates) / ANALOG_MAX_VAL;
	idx = DIMOF(g_scopeRates) - idx - 1;
	uint32_t rate = g_scopeRates[idx];

	return rate;
}

uint32_t freqFromPotValue(uint32_t potValue)
{
	int idx = potValue * DIMOF(g_dacFreqs) / ANALOG_MAX_VAL;
	return g_dacFreqs[idx];
}

float vResFromPotValue(uint32_t potValue)
{
	int idx = potValue * DIMOF(g_scopeVRes) / ANALOG_MAX_VAL;
	return g_scopeVRes[DIMOF(g_scopeVRes) - idx - 1];
}

uint32_t computeTimeout()
{
    uint32_t timeout = 4 * TFT_WIDTH * 1000 / g_scopeState.sampleRate;
	if (timeout > 1000)
		timeout = 1000;
	else if (timeout == 0)
		timeout = 1;

	return timeout;
}

const char *sampleRateSuffix(uint32_t usecPerDiv, float *dispValue)
{
	const char *suffix;

	if (usecPerDiv >= 1000000) {
		*dispValue = (float)usecPerDiv / 1000000;
		suffix = "s/div";
	}
	else if (usecPerDiv >= 1000) {
		*dispValue = (float)usecPerDiv / 1000;
		suffix = "ms/div";
	}
	else {
		*dispValue = (float)usecPerDiv;
		suffix = "us/div";
	}

	return suffix;
}

POT_VAR *getPotVar(const char *name)
{
    for (uint32_t i = 0; i < DIMOF(g_potVars); i++) {
		if (strcmp(g_potVars[i].name, name) == 0)
			return &g_potVars[i];
	}

	return NULL;
}

void cbPotVarChangedRate(POT_VAR *potVar)
{
	// Get us/div
    uint32_t usecPerDiv = usecPerDivFromPotValue(potVar->potValue);
	// Convert it to frequency
    uint32_t scopeRate = VGRID_INTERVAL * 1000000 / usecPerDiv;
	if (scopeRate != g_scopeState.sampleRate) {
		g_scopeState.sampleRate = scopeRate;
		potVar->display.prevValuef = potVar->display.valuef;
		potVar->display.prevSuffix = potVar->display.suffix;
		potVar->display.suffix = sampleRateSuffix(usecPerDiv,
												  &potVar->display.valuef);
		drawVar(&potVar->display, VAR_TYPE_FLOAT);

		// If we go from slow to fast mode, we need to restart AdcDma
		if ( (g_drawState.drawMode == DRAW_MODE_SLOW) && (scopeRate >= ADC_SAMPLE_RATE_LOW_LIMIT) ) {
			drawEraseSamples(false, true);
			drawTriggerArrow(getPotVar("TRIG"), false);
			drawGrid(0, TFT_WIDTH);
			g_adcDma->Stop();
			g_drawState.bFinished = true;
		}
		g_adcDma->SetSampleRate(g_scopeState.sampleRate);
	}
}

void cbPotVarChangedFreq(POT_VAR *potVar)
{
    uint32_t freq = freqFromPotValue(potVar->potValue);
	if (freq != g_sigState.freq) {
		g_sigState.freq = freq;
		potVar->display.prevValue = potVar->display.value;
		potVar->display.value = freq;
		g_genSigDma->Stop();
		g_genSigDma->SetWaveForm(g_sigState.waveform, g_sigState.freq, NULL);
		g_genSigDma->Start();
		drawVar(&potVar->display, VAR_TYPE_INT);
	}
}

void cbPotVarChangedTrig(POT_VAR *potVar)
{
	//CHANNEL_DESC * ch;
	drawTriggerArrow(potVar, true);
	drawGrid(0, TFT_WIDTH);
	// Force drawn samples to 0 to redraw all
	g_drawState.drawnFrames = 0;
	drawEraseSamples(true, false);
    uint32_t timeout = computeTimeout();
	/*
	ch = getChannelDesc(g_scopeState.triggerChannel);
	potVar->display.prevValue = potVar->display.value;
    g_scopeState.triggerVal = (uint32_t)((float)potVar->potValue /
									 (float)ch->hwGain / ch->swGain);
	*/
	g_adcDma->SetTrigger(g_scopeState.triggerVal, g_triggerMode, g_scopeState.triggerChannel, timeout);
}

void cbPotVarChangedGain(POT_VAR *potVar)
{
	int chIdx;
	char c;
	float vres;		// voltage resolution (V/div)

	// Get channel index from potVar name.
	// We expect name to be something like "GAINX",
	// where X is the channel index
	c = potVar->name[strlen(potVar->name) - 1];
	chIdx = c - '0';

	vres = vResFromPotValue(potVar->potValue);

	setChannelGlobalGain(chIdx, potVar->potValue);

	potVar->display.prevValuef = potVar->display.valuef;
	potVar->display.prevSuffix = potVar->display.suffix;

	if (vres >= 1.) {
		potVar->display.suffix = "V/d";
		potVar->display.valuef = vres;
	}
	else {
		potVar->display.suffix = "mV/d";
		potVar->display.valuef = vres * 1000.;
	}

	drawVar(&potVar->display, VAR_TYPE_FLOAT);

	// Trigger position may change depending on gain, update it
	drawTriggerArrow(getPotVar("TRIG"), true);
}

// Set global gain on channel
bool setChannelGlobalGain(int chIdx, uint32_t potValue)
{
	float G = 54.6;	// input gain = Vin/Vout
	float gain;		// total display gain
	uint32_t nDiv;	// number of divisions
	float vres;		// voltage resolution (V/div)
	CHANNEL_DESC *pChDesc = &g_channelDescs[chIdx];

	if (potValue > ANALOG_MAX_VAL)
	{
		SERIAL_IFACE.print("Invalid pot value");
		SERIAL_IFACE.println(potValue);
		return false;
	}

	vres = vResFromPotValue(potValue);

	nDiv = (g_maxY - g_minY + 1) / VGRID_INTERVAL;

	/*
	 * vres is given by:
	 * vres = G * VREF * nDiv / gain;
	 * where : G is Vin/Vout
	 *         gain is the total display gain applied (hw*sw)
	 *         nDiv is the number of divisions
	 */

	gain = G * 3.3 / (float)nDiv / vres;

	if (gain >= 4) {
		pChDesc->hwGain = 4;
	}
	else if (gain >= 2) {
		pChDesc->hwGain = 2;
	}
	else {
		pChDesc->hwGain = 1;
	}
	pChDesc->swGain = (float)gain / (float)pChDesc->hwGain;

	logdebug("Channel %d, setting hwGain %d, swGain %f\n", chIdx, pChDesc->hwGain, pChDesc->swGain);

	g_adcDma->SetChannelGain(pChDesc->channel, pChDesc->hwGain);

	return true;
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
				drawGrid(0, TFT_WIDTH);
				drawTriggerArrow(getPotVar("TRIG"), true);
				g_drawState.bNeedsErase = false;
			}
		}

		if (g_drawState.drawMode == DRAW_MODE_FAST) {
			// In fast mode, grid is drawn by drawEraseSamples
			drawEraseSamples(true, true);
		}
		else {
			//int prevDrawnFrames = g_drawState.drawnFrames;
			drawEraseSamples(true, false);
		}

		if (g_drawState.drawnFrames >= TFT_WIDTH - 1) {
			g_drawState.bFinished = true;
		}

		// Manually update pot vars in fast mode since it is not done while sampling
		if (g_drawState.drawMode == DRAW_MODE_FAST) {
			updatePotsVars(NULL);
		}

		// Update trigger status
		drawTriggerStatus();

		SCmd.readSerial();
	}

	if (g_drawState.bFinished) {

		g_drawState.bFinished = false;

		// Reset drawn samples, sampled frames and trigger state
		initDrawState();
		initScopeState();

		if (g_drawState.drawMode == DRAW_MODE_SLOW) {
			g_drawState.bNeedsErase = true;
		}

		DRAW_MODE prevDrawMode = g_drawState.drawMode;

		// Update draw mode depending on sample rate
		if (g_scopeState.sampleRate < ADC_SAMPLE_RATE_LOW_LIMIT)
			g_drawState.drawMode = DRAW_MODE_SLOW;
		else
			g_drawState.drawMode = DRAW_MODE_FAST;

		// When draw mode changes, we need to erase prev samples
		if (prevDrawMode != g_drawState.drawMode) {
			g_drawState.bNeedsErase = true;
		}

		swapSampleBuffer();

		drawTriggerStatus();

		// In slow mode, this will be done when we get first samples
		if (g_drawState.drawMode == DRAW_MODE_FAST) {
			drawTriggerArrow(getPotVar("TRIG"), true);
		}

		setupAdcDma();

		logdebug("Starting ADCDMA\n");
		g_adcDma->Start();
	}

    if (g_drawState.drawMode == DRAW_MODE_SLOW)
    {
		// Update pot values in slow mode
		// Skip first frames since they are not very accurate
		if (g_drawState.mappedFrames > 3)
		{
			updatePotsVars(g_rxBuffer);
		}

		// If we're too slow in RW handler, capture might have been stopped.
		// Restart it in this case
		// Note: this should be handled more properly by using a bigger RX buffer
		// in slow mode
		if (g_adcDma->GetCaptureState() == AdcDma::CaptureStateStopped)
		{
			loginfo("Capture is stopped, restarting !\n");
			g_adcDma->Start();
		}
	}

	logflush();
}
