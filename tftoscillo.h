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

#endif
