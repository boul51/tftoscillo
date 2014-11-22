
#ifndef _GEN_SIG_DMA_H_
#define _GEN_SIG_DMA_H_

#include <Arduino.h>
#include <stdarg.h>

// Uncomment to enable Serial debug messages
// Increases size, and needs Serial to be initialized
//#define GEN_SIG_DMA_DEBUG

#define GENSIGDMA_MAX_SAMPLE_RATE 2000000

#define GENSIGDMA_DACC_MIN_VAL ((int)(0))
#define GENSIGDMA_DACC_MAX_VAL ((int)(4095)) // 12 bits

#define DAC_DMA_MAX_TIMER_CHANNEL 2
#define DAC_DMA_DEF_TIMER_CHANNEL 0

typedef struct _GENSIGDMA_STATS {
	int64_t lastSamplesCount;
	int64_t lastIrqsCount;
	int     lastSec;
	int64_t totalSamplesCount;
	int64_t totalIrqsCount;
}GENSIGDMA_STATS;

typedef enum _GENSIGDMA_WAVEFORM {
	WAVEFORM_NONE,
	WAVEFORM_MIN,
	WAVEFORM_SQUARE,
	WAVEFORM_SINUS,
	WAVEFORM_SAW,
	WAVEFORM_TRIANGLE,
	WAVEFORM_MAX,
}GENSIGDMA_WAVEFORM;

class GenSigDma
{
public :
	GenSigDma();
	~GenSigDma();

	bool Start();
	bool Stop();
	bool SetWaveForm(GENSIGDMA_WAVEFORM wf, float freq, float *pActualFreq = NULL);

	bool SetMaxSampleRate(int rate);

	uint16_t *GetBufAddress(int bufIndex) {
		return m_buffers[bufIndex];
	}

	int GetSamplesPerBuffer() {
		return m_samplesPerBuffer;
	}

	int GetCurrBufIndex() {
		return m_currBuf;
	}

	void SetCurrBufIndex(int i) {
		m_currBuf = i;
	}

	int GetBufCount() {
		return m_bufCount;
	}

	bool SetTimerChannel(int timerChannel);

	void Loop(bool bResetStats);

	GENSIGDMA_STATS *GetStats() {
		return &m_stats;
	}

private :

	/* Private members */

	// State tracking
	bool  m_started;
	int	  m_sampleRate;
	int   m_maxSampleRate;
	float m_freq;
	GENSIGDMA_WAVEFORM m_waveform;
	GENSIGDMA_STATS    m_stats;

	// Buffers management
	uint16_t *m_buffers[2];
	int   m_bufSize;
	int   m_bufCount;
	int   m_currBuf;
	int   m_samplesPerBuffer;

	// DACC and Timer info
	int   m_daccChannel;
	int   m_timerChannel;
	int   m_daccMaxVal;
	int   m_daccMinVal;

	// Used by waveform generators
	int m_freqMult;
	int m_periodsPerBuffer;
	int m_samplesPerPeriod;


	/* Private functions */

	bool SetupDacc();
	bool SetupTimer();
	bool SetupDma();
	bool StartDma();
	bool StopDma();

	// Waveform generators
	void DuplicateBuffers();
	void DuplicatePeriods();
	bool GenSquare();
	bool GenSin();
	bool GenSaw();
	bool GenTriangle();

	// Stats
	void InitStats();
	void ResetStats();
	void DisplayStats(int sec);
};

// Debug declarations
void gensigdma_print(const char *fmt, ... );
char *gensigdma_floatToStr(float f, int precision);

#endif
