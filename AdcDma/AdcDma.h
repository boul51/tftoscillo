
#ifndef _ADC_DMA_H_
#define _ADC_DMA_H

#define MCLK 84000000
#define ADC_DMA_MAX_VAL 4095

#define ADC_DMA_DEF_ADC_CHANNEL 0
#define ADC_DMA_MAX_ADC_CHANNEL 16

#define ADC_DMA_DEF_TIMER_CHANNEL 0
#define ADC_DMA_MAX_TIMER_CHANNEL 2

#define ADC_DMA_DEF_SAMPLE_RATE 100000
#define ADC_DMA_MAX_SAMPLE_RATE 2000000

#define ADC_DMA_DEF_BUF_SIZE	2000

#define ADC_DMA_DEF_BUF_COUNT	5
#define ADC_DMA_MAX_BUF_COUNT   100

#define	ADC_DMA_MAX_MEM			10000

#define ADC_DMA_HW_GAINS_COUNT  3 // Number of available hardware gains (1, 2, 4)

class AdcDma
{

/* Public types */

public :

	typedef bool (*RxCallback)(uint16_t *buffer, int bufLen, bool bIsTrigger, int triggerIndex, bool bTimeout);

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

	// Update StrTriggerState in AdcDma.cpp if TriggerState enum changes
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

	static AdcDma *GetInstance();
	void Start();
	void Stop();
	bool SetBuffers(int bufCount, int bufSize);
	bool SetTimerChannel(int timerChannel);
	bool SetAdcChannels(uint16_t *adcChannel, int adcChannelsCount);
	int  GetAdcChannelsCount();
	bool SetSampleRate(int sampleRate);
	CaptureState GetCaptureState();

	bool ReadSingleValue(int adcChannel, uint16_t *value);

	bool SetTrigger(int value, TriggerMode mode, int triggerChannel, int triggerTimeoutMs);
	bool SetTriggerPreBuffersCount(int triggerPreBuffersCount);
	bool SetTriggerPreSamplesCount(int triggerPreSamplesCount);
	bool TriggerEnable(bool bEnable);

	void HandleInterrupt();

	inline uint16_t channel(uint16_t rawSample) {if (m_adcChannelsCount == 1) return m_adcChannels[0]; else return (rawSample & 0xF000) >> 12;}
	inline uint16_t sample(uint16_t rawSample) {return (rawSample & 0x0FFF);}

/* Private methods and members */

	uint32_t SetBufferDuration(uint32_t durationMs);
	void SetRxHandler(RxCallback rxHandler);
	bool SetChannelGain(int adcChannel, int gain);
	int GetChannelGain(int adcChannel);
	static int HwGainAtIndex(int i);
	static int HwGainIndex(int hwGain);

private :

	RxCallback m_rxHandler;

	AdcDma();
	~AdcDma();
	static AdcDma *m_instance;
	bool allocateBuffers(int bufCount, int bufSize, bool bForceAllocate = false);
	bool deleteBuffers();

	uint16_t *m_buffers[ADC_DMA_MAX_BUF_COUNT];

	CaptureState m_captureState;
	int m_timerChannel;
	int m_sampleRate;
	int m_bufCount;
	int m_bufSize;
	int m_writeBufIndex;	// Current buffer being written
	int m_readBufIndex;		// Current buffer being read
	int m_readSampleIndex;	// Current sample being read
	int m_readBufCount;		// Total number of read buffers since start()
	int m_writeBufCount;	// Total number of written buffers since start()

	int m_adcChannels[ADC_DMA_MAX_ADC_CHANNEL];
	int m_adcChannelsCount;

	bool configureAdc(bool bSoftwareTrigger);
	void startAdc();
	void stopAdc();

	bool configureDma();
	void startDma();
	void stopDma();

	bool configureTimer();
	void startTimer();
	void stopTimer();

	void setCaptureState(CaptureState captureState);

	bool advanceReadBuffer();
	bool advanceWriteBuffer();

	void disableCompareMode();

	/* Trigger management */

	static const char *StrTriggerState[];

	int  m_triggerVal;
	int  m_triggerChannel;
	int  m_triggerTimeoutMaxInts;
	int  m_triggerBufferInts;
	int  m_triggerPreBuffersCount;
	int  m_triggerPreSamplesCount;
	int  m_triggerSampleBufIndex;
	int  m_triggerSampleIndex;
	bool m_bTriggerTimeout;

	TriggerMode  m_triggerMode;
	TriggerState m_triggerState;

	void triggerUpdateState(TriggerEvent *event);
	void triggerSetState(TriggerState state);
	void triggerEnterEnabled(TriggerEvent *event);
	void triggerEnterDisabled(TriggerEvent *event);
	void triggerEnterPreArmed(TriggerEvent *event);
	void triggerEnterArmed(TriggerEvent *event);
	void triggerEnterDone(TriggerEvent *event);
	void triggerEnterCapturing(TriggerEvent *event);
	void triggerDisableInterrupt();
	void triggerEnableInterrupt();
	void triggerHandleInterrupt(int bufIndex, int sampleIndex);
	void triggerFindTriggerSample();
	void updateAdcTimerChannel();
};

#endif
