#ifndef _TFTOSCILLO_H_
#define _TFTOSCILLO_H_

// Macros
#define DIMOF(x) (sizeof(x) / sizeof(x[0]))
#define ABS_DIFF(a, b) (a > b ? a - b : b - a)

// Debug zones
#define DBG_LOOP		false
#define DBG_VERBOSE		false
#define DBG_TEXT		false
#define DBG_POTS		false
#define DBG_DRAW		false
#define DBG_DRAWMODE	false
#define DBG_RX			false
#define DBG_BUFFERS		false

// Resolution for analog input and outputs
#define ANALOG_RES    12
#define ANALOG_MAX_VAL ((1 << ANALOG_RES) - 1)

// Channel descriptor
typedef struct _CHANNEL_DESC {
	int channel;
	int bufSize;
	uint16_t *curSamples;
	uint16_t *oldSamples;
	uint16_t *samples[2];
	// color
	uint8_t r;
	uint8_t g;
	uint8_t b;
}CHANNEL_DESC;

typedef enum _DRAW_MODE {
	DRAW_MODE_SLOW = 0,
	DRAW_MODE_FAST = 1
}DRAW_MODE;

typedef struct _DRAW_STATE {
	bool bFinished;
	bool bNeedsErase;
	int  drawnFrames;
	int  mappedFrames;
	int  rxFrames;
	DRAW_MODE drawMode;
}DRAW_STATE;

typedef struct _SCOPE_STATE {
	uint sampleRate;
	uint minSampleRate;
	uint maxSampleRate;
	uint triggerChannel;
	uint triggerVal;
	uint minTriggerVal;
	uint maxTriggerVal;
	uint triggerStatus;
	bool bTriggerStatusChanged;
	uint prevScopeChannelsCount;
	uint scopeChannelsCount;
	uint newScopeChannelsCount;
	bool bScopeChannelsCountChanged;
}SCOPE_STATE;

typedef struct _SIG_STATE {
	uint minFreq;
	uint maxFreq;
	uint freq;
	GenSigDma::WaveForm waveform;
}SIG_STATE;

typedef struct _VAR_DISPLAY {
	bool bNeedsErase;
	const char *prefix;
	const char *suffix;
	uint value;
	uint prevValue;
	int x;
	int y;
}VAR_DISPLAY;

typedef struct _POT_VAR {
	uint adcChannel;
	uint potValue;
	uint *minValue;
	uint *maxValue;
	uint *value;
	uint prevValue;
	uint margin;
	bool changed;
	bool forceRead;
	char name[5];
	bool bHasVarDisplay;
	VAR_DISPLAY display;
}POT_VAR;

#endif
