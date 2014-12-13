
#ifndef _ADC_DMA_H_
#define _ADC_DMA_H

#include <Arduino.h>

#define MCLK 84000000
#define ADC_DMA_MAX_VAL 4095

#define ADC_DMA_DEF_ADC_CHANNEL 0
#define ADC_DMA_MAX_ADC_CHANNEL 16

#define ADC_DMA_DEF_TIMER_CHANNEL 0
#define ADC_DMA_MAX_TIMER_CHANNEL 2

#define ADC_DMA_DEF_SAMPLE_RATE 100000
#define ADC_DMA_MAX_SAMPLE_RATE 2000000

#define ADC_DMA_DEF_BUF_SIZE	1000

#define ADC_DMA_DEF_BUF_COUNT	5
#define ADC_DMA_MAX_BUF_COUNT   100

class AdcDma
{

/* Public types */

public :

	enum TriggerMode {
		RisingEdge,
		FallingEdge,
	};

	enum CaptureState {
		CaptureStateStopped = 0,
		CaptureStateStarted,
	};

/* Private types */

private :

	enum TriggerState {
		TriggerStateDisabled = 0,
		TriggerStateEnabled,
		TriggerStatePreArmed,
		TriggerStateArmed,
		TriggerStateCapturing,
		TriggerStateDone,
	};

	enum TriggerEventKind {
		TriggerEventKindEnable,
		TriggerEventKindPreBuffersFull,
		TriggerEventKindCompareInterrupt,
		TriggerEventKindTimeout,
		TriggerEventKindAllBuffersFull,
		TriggerEventKindDisable,
	};

	typedef struct _TriggerEvent {
		TriggerEventKind eventKind;
		int bufIndex;
		int sampleIndex;
	}TriggerEvent;

/* Public methods and members */

public :

	static AdcDma *m_instance;
	static AdcDma *GetInstance();
	void Start();
	void Stop();

	bool SetAdcChannels(int *adcChannel, int adcChannelsCount);
	bool SetSampleRate(int sampleRate);

	int  GetBufSize() {return m_bufSize;}

	bool AdvanceReadBuffer();
	uint16_t *GetReadBuffer();

	bool AllocateBuffers(int bufCount, int bufSize, bool bForceAllocate = false);
	bool DeleteBuffers();

	void HandleInterrupt();

	// Update StrTriggerState in AdcDma.cpp if TriggerState enum changes

	static const char *StrTriggerState[];

	bool SetTrigger(int value, TriggerMode mode, int triggerChannel, int triggerTimeoutMs);

	bool DidTriggerComplete(bool *pbTimeout = NULL);
	bool SetTimerChannel(int timerChannel);
	bool TriggerEnable(bool bEnable);

/* Private methods and members */

	bool SetTriggerPreBuffersCount(int triggerPreBuffersCount);
	bool GetTriggerSampleAddress(uint16_t **pBufAddress, int *pSampleIndex);
	bool ReadSingleValue(int adcChannel, int *value);
	bool SetBuffers(int bufCount, int bufSize);
	bool GetNextSample(uint16_t *sample, int *channel, CaptureState *state = NULL, bool *isTriggerSample = NULL);
	AdcDma::CaptureState GetCaptureState();
	bool SetTriggerPreSamplesCount(int triggerPreSamplesCount);
	bool EnableChannel(int channel, bool bEnable);
private :

	AdcDma();
	~AdcDma();

	uint16_t *m_buffers[ADC_DMA_MAX_BUF_COUNT];
	int m_bufCount;
	int m_bufSize;
	int m_writeBufIndex;	// Current buffer being written
	int m_readBufIndex;		// Current buffer being read
	int m_readSampleIndex;	// Current sample being read
	int m_startBufIndex;	// 1st pre-buffer after triggered
	int m_readBufCount;		// total number of read buffers since start()
	int m_writeBufCount;	// total number of written buffers since start()

	int m_adcChannels[ADC_DMA_MAX_ADC_CHANNEL];
	int m_adcChannelsCount;

	int m_triggerChannel;
	int m_triggerTimeoutMaxInts;
	int m_triggerBufferInts;
	int m_triggerPreBuffersCount;
	int m_triggerPreSamplesCount;
	int m_triggerSampleBufIndex;
	int m_triggerSampleIndex;
	bool m_bTriggerTimeout;

	bool ConfigureAdc(bool bSoftwareTrigger);
	void StartAdc();
	void StopAdc();

	bool ConfigureDma();
	void StartDma();
	void StopDma();

	bool ConfigureTimer();
	void StartTimer();
	void StopTimer();

	bool AdvanceWriteBuffer();

	/* Trigger management */

	int  m_triggerVal;
	TriggerMode m_triggerMode;
	TriggerState m_triggerState;

	void triggerUpdateState(TriggerEvent *event);
	void triggerSetState(TriggerState state);

	void triggerEnterEnabled(TriggerEvent *event);
	void triggerEnterDisabled(TriggerEvent *event);
	void triggerEnterArmed(TriggerEvent *event);
	void triggerEnterDone(TriggerEvent *event);
	//void triggerEnterUserReading(TriggerEvent *event);
	void triggerEnterCapturing(TriggerEvent *event);

	void triggerDisableInterrupt();
	void triggerEnableInterrupt();

	void triggerHandleInterrupt(int bufIndex, int sampleIndex);

	CaptureState m_captureState;

	int m_timerChannel;

	int m_sampleRate;
	void DisableCompareMode();
	void SetCaptureState(CaptureState captureState);
	//void triggerEnterReading(TriggerEvent *event);
	void findTriggerSample();
	void triggerEnterPreArmed(TriggerEvent *event);
};

#endif
