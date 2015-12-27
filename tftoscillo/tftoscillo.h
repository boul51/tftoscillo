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
	float totalGain;
	float swGain;
	uint32_t  hwGain;
	int gndOffset;
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
	uint32_t sampleRate;
	uint32_t triggerChannel;
	uint32_t triggerVal;
	uint32_t minTriggerVal;
	uint32_t maxTriggerVal;
	uint32_t triggerStatus;
	bool bTriggerStatusChanged;
	uint32_t prevScopeChannelsCount;
	uint32_t scopeChannelsCount;
	uint32_t newScopeChannelsCount;
	bool bScopeChannelsCountChanged;
	uint32_t blVal;
}SCOPE_STATE;

typedef struct _SIG_STATE {
	uint32_t minFreq;
	uint32_t maxFreq;
	uint32_t freq;
	GenSigDma::WaveForm waveform;
}SIG_STATE;

typedef enum _VAR_TYPE {
	VAR_TYPE_INT = 1,
	VAR_TYPE_FLOAT = 2,
}VAR_TYPE;

struct _POT_VAR;
typedef void (*cbPotVarChanged_t)(struct _POT_VAR *);

void cbPotVarChangedRate(_POT_VAR *potVar);
void cbPotVarChangedFreq(_POT_VAR *potVar);
void cbPotVarChangedTrig(_POT_VAR *potVar);
void cbPotVarChangedGain(_POT_VAR *potVar);

typedef struct _VAR_DISPLAY {
	bool bNeedsErase;
	const char *prefix;
	const char *suffix;
	const char *prevSuffix;
	uint32_t value;
	uint32_t prevValue;
	float valuef;
	float prevValuef;
	int x;
	int y;
}VAR_DISPLAY;

typedef struct _POT_VAR {
	uint32_t adcChannel;
	uint32_t potValue;
	uint32_t prevPotValue;
	uint32_t *minValue;
	uint32_t *maxValue;
	uint32_t *value;
	uint32_t prevValue;
	uint32_t margin;
	bool forceRead;
	const char *name;
	cbPotVarChanged_t cbPotVarChanged;
	bool bHasVarDisplay;
	VAR_DISPLAY display;
}POT_VAR;

#endif
