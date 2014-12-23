#ifndef _TFTOSCILLO_H_
#define _TFTOSCILLO_H_

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

typedef struct _DRAW_STATE {
	bool bFinished;
	bool bNeedsErase;
	int  drawnFrames;
	int  mappedFrames;
	int  rxFrames;
	int  drawMode;
}DRAW_STATE;

typedef struct _SCOPE_STATE {
	uint triggerVal;
	uint minSampleRate;
	uint maxSampleRate;
	uint sampleRate;
	uint triggerStatus;
	bool bTriggerStatusChanged;
}SCOPE_STATE;

typedef struct _SIG_STATE {
	uint freq;
	GenSigDma::WaveForm waveform;
}SIG_STATE;

typedef struct _POT_VAR_DISPLAY {
	bool bValid;
	bool bNeedsErase;
	int  prevValue;
	const char *prefix;
	const char *suffix;
	int x;
	int y;
}POT_VAR_DISPLAY;

typedef struct _POT_VAR {
	uint adcChannel;
	uint potValue;
	uint minValue;
	uint maxValue;
	uint *value;
	uint margin;
	bool changed;
	char name[5];
	POT_VAR_DISPLAY display;
}POT_VAR;

#endif
