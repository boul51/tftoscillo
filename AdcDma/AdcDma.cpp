
#include <Arduino.h>

#include "AdcDma.h"
#include <LibDbg.h>

#define DEBUG_ALL 0

// Debug zones
#if DEBUG_ALL != 1

#define DBG_INIT	false
#define DBG_IRQ		false
#define DBG_TIMER	false
#define DBG_TRIGGER	false
#define DBG_READ	false
#define DBG_WRITE	false
#define DBG_CHANNEL false
#define DBG_SAMPLE	false
#define DBG_WARN	true
#define DBG_ERROR	true

#else
#define DBG_INIT	true
#define DBG_IRQ		true
#define DBG_TIMER	true
#define DBG_TRIGGER	true
#define DBG_READ	true
#define DBG_WRITE	true
#define DBG_CHANNEL true
#define DBG_SAMPLE	true

#endif

typedef struct _ADCDMA_PRESCALER {
	uint32_t bitMask;
	uint32_t div;
}ADCDMA_PRESCALER;

ADCDMA_PRESCALER adcDmaPrescalers[] = {
	{
		TC_CMR_TCCLKS_TIMER_CLOCK1,
		2
	},
	{
		TC_CMR_TCCLKS_TIMER_CLOCK2,
		8
	},
	{
		TC_CMR_TCCLKS_TIMER_CLOCK3,
		32
	},
	{
		TC_CMR_TCCLKS_TIMER_CLOCK4,
		128
	},
};

AdcDma::AdcDma()
{
	m_sampleRate     = ADC_DMA_DEF_SAMPLE_RATE;
	m_timerChannel   = ADC_DMA_DEF_TIMER_CHANNEL;
	m_adcChannels[0] = ADC_DMA_DEF_ADC_CHANNEL;
	m_adcChannelsCount = 1;
	m_captureState = CaptureStateStopped;
	m_triggerState = TriggerStateDisabled;

	for (int i = 0; i < ADC_DMA_MAX_BUF_COUNT; i++) {
		m_buffers[i] = NULL;
	}

	allocateBuffers(ADC_DMA_DEF_BUF_COUNT, ADC_DMA_DEF_BUF_SIZE, true);

	SetTimerChannel(m_timerChannel);	// Will call ConfigureTimer
	configureAdc(false);
	configureDma();
}

AdcDma::~AdcDma()
{
	Stop();
	deleteBuffers();
}

AdcDma *AdcDma::GetInstance()
{
	if (AdcDma::m_instance == NULL) {
		m_instance = new AdcDma();
	}

	return m_instance;
}

void AdcDma::Start()
{
	m_writeBufIndex = 0;
	m_readBufIndex  = 0;
	m_readSampleIndex = 0;
	m_readBufCount = 0;
	m_writeBufCount = 0;

	m_bTriggerTimeout = false;

	TriggerEvent ev;
	ev.eventKind = TriggerEventKindEnable;
	triggerUpdateState(&ev);

	configureTimer();
	configureDma();
	startAdc();
	startDma();
	startTimer();

	setCaptureState(CaptureStateStarted);

	if (m_triggerState == TriggerStateEnabled) {
		triggerEnableInterrupt();
	}
}

void AdcDma::Stop()
{
	PF(DBG_INIT, "++\r\n");

	PF(DBG_INIT, "Will stop timer\r\n");
	stopTimer();
	PF(DBG_INIT, "Will stop DMA\r\n");
	stopDma();
	PF(DBG_INIT, "Will stop ADC\r\n");
	stopAdc();

	PF(DBG_INIT, "Will set timer state stopped\r\n");
	setCaptureState(CaptureStateStopped);

	TriggerEvent ev;
	ev.eventKind = TriggerEventKindDisable;
	triggerUpdateState(&ev);

	PF(DBG_INIT, "--\r\n");
}

bool AdcDma::SetBuffers(int bufCount, int bufSize)
{
	if (m_captureState != CaptureStateStopped) {
		PF(DBG_INIT, "not in stopped state\r\n");
		return false;
	}

	if (bufCount == m_bufCount && bufSize == m_bufSize) {
		return true;
	}

	if (bufCount > ADC_DMA_MAX_BUF_COUNT) {
		PF(DBG_WARN, "bufCount (%d) is bigger than ADC_DMA_MAX_BUF_COUNT (%d)\r\n", bufCount, ADC_DMA_MAX_BUF_COUNT);
		return false;
	}

	if (bufCount * bufSize > ADC_DMA_MAX_MEM) {
		PF(DBG_WARN, "total buf size (%d) is bigger than ADC_DMA_MAX_MEM (%d)\r\n", bufCount * bufSize, ADC_DMA_MAX_MEM);
		return false;
	}

	m_bufCount = bufCount;
	m_bufSize = bufSize;

	deleteBuffers();

	if (!allocateBuffers(bufCount, bufSize, true)) {
		PF(DBG_WARN, "failed in allocateBuffers !\r\n");
		return false;
	}

	return true;
}

void AdcDma::updateAdcTimerChannel()
{
	uint32_t trgSel;

	switch (m_timerChannel) {
	case 0 :
		trgSel = ADC_MR_TRGSEL_ADC_TRIG1;
		break;
	case 1:
		trgSel = ADC_MR_TRGSEL_ADC_TRIG2;
		break;
	case 2:
		trgSel = ADC_MR_TRGSEL_ADC_TRIG3;
		break;
	default :
		PF(DBG_WARN, "invalid timer channel %d !\r\n", m_timerChannel);
		return;
	}

	uint32_t mr = ADC->ADC_MR;

	mr &= ~ADC_MR_TRGSEL_Msk;
	mr |= trgSel;

	ADC->ADC_MR = mr;
}

bool AdcDma::SetTimerChannel(int timerChannel)
{
	if (timerChannel < 0 || timerChannel > ADC_DMA_MAX_TIMER_CHANNEL) {
		PF(DBG_WARN, "invalid channel %d !\r\n", timerChannel);
		return false;
	}

	if (m_captureState != CaptureStateStopped) {
		PF(DBG_WARN, "can't set timer channel while running !\r\n");
		return false;
	}

	m_timerChannel = timerChannel;

	// Update ADC trigger sel
	updateAdcTimerChannel();

	// Timer has to be setup again
	configureTimer();

	return true;
}

bool AdcDma::SetAdcChannels(uint16_t *adcChannels, int adcChannelsCount)
{
	if (adcChannelsCount > ADC_DMA_MAX_ADC_CHANNEL) {
		return false;
	}

	for (int i = 0; i < adcChannelsCount; i++) {
		if (adcChannels[i] > ADC_DMA_MAX_ADC_CHANNEL) {
			return false;
		}
	}

	if (m_captureState != CaptureStateStopped)
		return false;

	for (int i = 0; i < adcChannelsCount; i++) {
		m_adcChannels[i] = adcChannels[i];
		//SetChannelGain(m_adcChannels[i], 1);
	}

	m_adcChannelsCount = adcChannelsCount;

	// Enable channel tag in EMR if several channels are active
	if (m_adcChannelsCount > 1) {
		ADC->ADC_EMR |= ADC_EMR_TAG;
	}
	else {
		ADC->ADC_EMR &= ~ADC_EMR_TAG;
	}

	return true;
}

int AdcDma::GetAdcChannelsCount()
{
	return m_adcChannelsCount;
}

bool AdcDma::SetSampleRate(int sampleRate)
{
	if (sampleRate > ADC_DMA_MAX_SAMPLE_RATE) {
		PF(DBG_WARN, "sampleRate is bigger than ADC_DMA_MAX_SAMPLE_RATE (%d)\r\n", ADC_DMA_MAX_SAMPLE_RATE);
		return false;
	}

	PF(DBG_INIT, "Setting sample rate %d\r\n", sampleRate);

	m_sampleRate = sampleRate;

	return configureTimer();
}

bool AdcDma::SetChannelGain(int adcChannel, int gain)
{
	if (gain < 1 || gain > 4)
		return false;

	if (adcChannel < 0 || adcChannel > ADC_DMA_MAX_ADC_CHANNEL)
		return false;

	if (gain == 4)
		gain = 3;

	uint cgr = ADC->ADC_CGR;
	cgr &= ~(0x3 << (adcChannel*2) );
	cgr |= (gain << (adcChannel*2) );

	ADC->ADC_CGR = cgr;
	return true;
}

int AdcDma::GetChannelGain(int adcChannel)
{
	uint cgr;
	uint gain;

	if (adcChannel < 0 || adcChannel > ADC_DMA_MAX_ADC_CHANNEL)
		return -1;

	cgr = ADC->ADC_CGR;

	gain = ((cgr & 0x3) >> adcChannel*2);
	if (gain == 3)
		gain = 4;

	return gain;
}

AdcDma::CaptureState AdcDma::GetCaptureState()
{
	return m_captureState;
}

bool AdcDma::ReadSingleValue(int adcChannel, uint16_t *value)
{
	if (m_captureState != CaptureStateStopped)
		return false;

	if (adcChannel < 0 || adcChannel > ADC_DMA_MAX_ADC_CHANNEL)
		return false;

	if (value == NULL)
		return false;

	// Set configuration to software trigger
	configureAdc(true);
	stopTimer();

	// Disable all channels
	ADC->ADC_CHDR = 0x0000FFFF;

	// Read pending values to avoid reading an old sample
	while (ADC->ADC_ISR & (0x1 << adcChannel)) {
		ADC->ADC_CDR[adcChannel];
	}

	// Enable channel
	ADC->ADC_CHER = 0x1 << adcChannel;

	// Start conversion
	ADC->ADC_CR |= ADC_CR_START;

	// Wait until value is available
	while ( (ADC->ADC_ISR & 0x1 << adcChannel) == 0) {}

	*value = (uint16_t)(ADC->ADC_CDR[adcChannel]);

	// Restore adc configuration
	configureAdc(false);

	return true;
}

bool AdcDma::SetTrigger(int value, TriggerMode mode, int triggerChannel, int triggerTimeoutMs)
{
	if (value < 0 || value > ADC_DMA_MAX_VAL)
		return false;

	if (mode != RisingEdge && mode != FallingEdge)
		return false;

	if (triggerChannel < 0 || triggerChannel > ADC_DMA_MAX_ADC_CHANNEL)
		return false;

	m_triggerMode = mode;
	m_triggerVal = value;
	m_triggerChannel = triggerChannel;
	m_triggerPreSamplesCount = -1;
	m_triggerPreBuffersCount = 1;
	if (triggerTimeoutMs != 0) {
		// Compute number of interrupts for trigger timeout
		m_triggerTimeoutMaxInts = triggerTimeoutMs * m_sampleRate * m_adcChannelsCount / m_bufSize / 1000;
		// Force at least one period for timeout
		if (m_triggerTimeoutMaxInts == 0) {
			m_triggerTimeoutMaxInts = 1;
		}
	}
	else {
		// 0 is infinite timeout
		m_triggerTimeoutMaxInts = 0;
	}

	PF(DBG_TRIGGER, "Calculated m_triggerTimeoutMaxInts %d\r\n", m_triggerTimeoutMaxInts);

	uint16_t v = (uint16_t)value;

	// Set comparison window register
	ADC->ADC_CWR =
			v |			// Low threshold
			v << 16;	// High threshold

	return true;
}

uint32_t AdcDma::SetBufferDuration(uint32_t durationMs)
{
	if (durationMs == 0)
		return false;

	// Buffer duration is given by:
	// T = bufSize / sampleRate
	m_bufSize = durationMs * m_sampleRate / 1000;

	if (m_bufSize > ADC_DMA_DEF_BUF_SIZE) {
		m_bufSize = ADC_DMA_DEF_BUF_SIZE;
	}

	uint32_t actDurationMs = 1000 * m_bufSize / m_sampleRate;

	PF(DBG_INIT, "requested duration %d ms, actual %d ms\r\n", durationMs, actDurationMs);

	return actDurationMs;
}

bool AdcDma::SetTriggerPreBuffersCount(int triggerPreBuffersCount)
{
	if (triggerPreBuffersCount + 2 > m_bufCount) {
		PF(DBG_TRIGGER, "Too big value for triggerPreBuffersCount");
		return false;
	}

	if (triggerPreBuffersCount < 0) {
		PF(DBG_TRIGGER, "Negative value for triggerPreBuffersCount");
		return false;
	}

	m_triggerPreBuffersCount = triggerPreBuffersCount;

	return true;
}

bool AdcDma::SetTriggerPreSamplesCount(int triggerPreSamplesCount)
{
	if (triggerPreSamplesCount < 0) {
		PF(DBG_TRIGGER, "Negative value for triggerPreSamplesCount\r\n");
		return false;
	}

	if (!SetTriggerPreBuffersCount(triggerPreSamplesCount / m_bufSize + 1)) {
		PF(DBG_TRIGGER, "Failed in  SetTriggerPreBuffersCount\r\n");
		return false;
	}

	m_triggerPreSamplesCount = triggerPreSamplesCount;

	return true;
}

bool AdcDma::TriggerEnable(bool bEnable)
{
	TriggerEvent event;
	if (bEnable)
		event.eventKind = TriggerEventKindEnable;
	else
		event.eventKind = TriggerEventKindDisable;

	triggerUpdateState(&event);

	return true;
}

bool AdcDma::allocateBuffers(int bufCount, int bufSize, bool bForceAllocate)
{
	// If buffers are already allocated and size and count didn't change, do nothing
	if ( (!bForceAllocate) &&
		 (bufCount == m_bufCount) &&
		 (bufSize == m_bufSize)   &&
		 (m_buffers[0] != NULL) ) {
		return true;
	}

	// Check parameters:
	if ( (bufCount == 0) ||
		 (bufSize  == 0) ) {
		PF(DBG_INIT, "invalid parameters !\r\n");
		return false;
	}

	m_bufCount = bufCount;
	m_bufSize  = bufSize;

	for (int iBuf = 0; iBuf < m_bufCount; iBuf++) {
		m_buffers[iBuf] = (uint16_t *)malloc(m_bufSize * sizeof(uint16_t));
		if (m_buffers[iBuf] == NULL) {
			PF(DBG_INIT, "failed allocating buffer %d, size %d !\r\n", iBuf, m_bufSize);
			deleteBuffers();
			return false;
		}
		PF(DBG_INIT, "m_buffers[%d]: 0x%08x\r\n", iBuf, m_buffers[iBuf]);
	}

	return true;
}

bool AdcDma::deleteBuffers()
{
	for (int iBuf = 0; iBuf < m_bufCount; iBuf++) {
		if (m_buffers[iBuf] != NULL) {
			free(m_buffers[iBuf]);
			m_buffers[iBuf] = NULL;
		}
	}

	m_bufCount = 0;
	m_bufSize  = 0;

	return true;
}

bool AdcDma::configureAdc(bool bSoftwareTrigger)
{
	// Note concerning startup time:
	// Datasheet says we need a startup time of 40us.
	// Startup time is given in periods of ADCClock.
	// We use prescaler of 0, so ADCClock = MCLK / 2 = 41MHz
	// => We need 41 periods, smaller fitting value is 64, use it

	// Enable / disable hardware trigger
	uint32_t trgEn = (bSoftwareTrigger ? ADC_MR_TRGEN_DIS : ADC_MR_TRGEN_EN);

	ADC->ADC_MR =
			trgEn						|	// Enable/disable hardware trigger
			ADC_MR_LOWRES_BITS_12		|	// 12 bits resolution
			ADC_MR_SLEEP_NORMAL			|	// Disable sleep mode
			ADC_MR_FWUP_OFF				|	// Normal sleep mode (disabled by ADC_MR_SLEEP_NORMAL)
			ADC_MR_FREERUN_OFF			|	// Disable freerun mode
			ADC_MR_PRESCAL(0)			|	// ADCClock = MCLK / (2)
			ADC_MR_STARTUP_SUT64		|	// Startup Time (cf note above)
			ADC_MR_SETTLING_AST3		|	// We use same parameters for all channels, use minimal value
			ADC_MR_ANACH_ALLOWED		|	// Use different gain, offset and mode for all channels
			ADC_MR_TRACKTIM(0)			|	// Minimal value for tracking time
			ADC_MR_TRANSFER(0)			|	// Minimal value for transfer period
			ADC_MR_USEQ_NUM_ORDER;			// Don't use sequencer mode

	// Update timer channel used for trigger
	updateAdcTimerChannel();

	// All channels in single ended mode
	// Center before scaling
	ADC->ADC_COR = 0x0000FFFF;

	return true;
}

void AdcDma::startAdc()
{
	ADC->ADC_CHDR = 0x0000FFFF;			// Disable all ADC channels

	uint32_t cher = 0;

	for (int i = 0; i < m_adcChannelsCount; i++) {
		PF(DBG_INIT, "enabling channel %d\r\n", m_adcChannels[i]);
		cher |= (0x1 << m_adcChannels[i]);	// Enable ADC channel
	}

	ADC->ADC_CHER = cher;				// Update Channel Enable Register
}

void AdcDma::stopAdc()
{
	ADC->ADC_CHDR = 0x0000FFFF;			// Disable all ADC channels
}

bool AdcDma::configureDma()
{
	m_writeBufIndex = 0;
	m_readBufIndex = 0;

	ADC->ADC_IDR = 0xFFFFFFFF;			// Disable all interrupts

	ADC->ADC_IER =
			ADC_IER_GOVRE			|	// Enable interrupts on general overflow errors
			ADC_IER_ENDRX			|	// Enable interrupts on end of receive buffer
			ADC_IER_RXBUFF;				// Enable interrupts on receive buffer full

	ADC->ADC_ISR;						// Clear status bits by reading

	return true;
}

void AdcDma::startDma()
{
	ADC->ADC_ISR;						// Clear status bits by reading

	ADC->ADC_RPR =
			(uint32_t)m_buffers[0];		// Setup DMA receive pointer

	ADC->ADC_RCR =
			m_bufSize;					// Setup DMA size

	ADC->ADC_RNPR =
			(uint32_t)m_buffers[1];		// Setup DMA receive next pointer

	ADC->ADC_RNCR =
			m_bufSize;					// Setup DMA next size

	ADC->ADC_PTCR = ADC_PTCR_RXTEN;		// Enable RX

	NVIC_ClearPendingIRQ(ADC_IRQn);
	NVIC_EnableIRQ(ADC_IRQn);
}

void AdcDma::stopDma()
{
	ADC->ADC_PTCR = ADC_PTCR_RXTDIS;   // Disable RX

	NVIC_ClearPendingIRQ(ADC_IRQn);
	NVIC_DisableIRQ(ADC_IRQn);
}

bool AdcDma::configureTimer()
{
	// Try to find divider / rc combination to obtain sample rate
	ADCDMA_PRESCALER prescaler;
	int numPrescalers = sizeof(adcDmaPrescalers) / sizeof(adcDmaPrescalers[0]);

	bool bFound = false;
	for (int i = 0; i < numPrescalers; i++) {
		prescaler = adcDmaPrescalers[i];
		// Check if maximal RC fits
		int sr = MCLK / prescaler.div / (uint)(0xFFFFFFFF); // Max value for RC is
		// If sr is OK, keep this prescaler
		if (sr < m_sampleRate) {
			bFound = true;
			break;
		}
	}

	if (!bFound) {
		PF(DBG_TIMER, "No prescaler found !\r\n");
		return false;
	}

	// We got prescaler, now calculate value for rc
	uint32_t RC = MCLK / (m_sampleRate * prescaler.div);

	PF(DBG_TIMER, "AdcDma: Setting up timer with parameters:\r\n");
	PF(DBG_TIMER, " - Timer channel %d\r\n", m_timerChannel);
	PF(DBG_TIMER, " - Sample rate %d\r\n", m_sampleRate);
	PF(DBG_TIMER, " - Div %d\r\n", prescaler.div);
	PF(DBG_TIMER, " - RC %d\r\n", RC);

	// And write data to timer controller

	pmc_enable_periph_clk (TC_INTERFACE_ID + m_timerChannel) ;  // clock the TC0 channel for ADC

	TcChannel * t = &(TC0->TC_CHANNEL)[m_timerChannel];  // pointer to TC0 registers for its channel 0
	t->TC_CCR = TC_CCR_CLKDIS;              // disable internal clocking during setup
	t->TC_IDR = 0xFFFFFFFF;                 // disable interrupts
	t->TC_SR;                               // read int status reg to clear pending

	t->TC_CMR =
			prescaler.bitMask	|			// Prescaler
			TC_CMR_WAVE			|			// Waveform mode
			TC_CMR_WAVSEL_UP_RC	|			// Count-up PWM using RC as threshold
			TC_CMR_EEVT_XC0		|			// Set external events from XC0 (this setup TIOB as output)
			TC_CMR_ACPA_CLEAR	|
			TC_CMR_ACPC_CLEAR	|
			TC_CMR_BCPB_CLEAR	|
			TC_CMR_BCPC_CLEAR;

	t->TC_RC = RC;
	t->TC_RA = RC/2;

	t->TC_CMR = (t->TC_CMR & 0xFFF0FFFF) |
			TC_CMR_ACPA_CLEAR |
			TC_CMR_ACPC_SET ;  // set clear and set from RA and RC compares

	if (m_captureState == CaptureStateStarted)
		startTimer();

	return true;
}

void AdcDma::startTimer()
{
	TcChannel * t = &(TC0->TC_CHANNEL)[m_timerChannel];  // pointer to TC0 registers for its channel 0

	t->TC_CCR = TC_CCR_CLKEN |	// Enable counter clock
				TC_CCR_SWTRG ;  // Reset counter and start clock
}

void AdcDma::stopTimer()
{
	TcChannel * t = &(TC0->TC_CHANNEL)[m_timerChannel];  // pointer to TC0 registers for its channel 0

	t->TC_CCR = TC_CCR_CLKDIS;	// Disable counter clock
}

void AdcDma::setCaptureState(CaptureState captureState)
{
	m_captureState = captureState;
}

bool AdcDma::advanceReadBuffer()
{
	m_readBufIndex++;
	if (m_readBufIndex == m_bufCount)
		m_readBufIndex = 0;

	// Increase number of read buffers
	m_readBufCount++;

	PF(DBG_READ, "Advanced readBuffer, m_readBufIndex is now %d\r\n", m_readBufIndex);

	return true;
}

bool AdcDma::advanceWriteBuffer()
{
	// Use + 2 here since we give pointer and next pointer to DMA controller
	if ( ((m_writeBufIndex + 2) % m_bufCount) == m_readBufIndex) {
		PF(DBG_WRITE, "Would overwrite non read data (R %d, W %d)\r\n", m_readBufIndex, m_writeBufIndex);
		PF(DBG_WRITE, "BufCount! %d\r\n", m_bufCount);
		return false;
	}

	m_writeBufIndex++;
	if (m_writeBufIndex == m_bufCount)
		m_writeBufIndex = 0;

	// Current write buffer was automatically set by DMA controller from RNPR
	// Only update next pointer

	PF(DBG_WRITE, "Setting buffer %d to RNPR, m_writeBufIndex %d\r\n", (m_writeBufIndex + 1)% m_bufCount, m_writeBufIndex);

	ADC->ADC_RNPR = (uint32_t)m_buffers[(m_writeBufIndex + 1) % m_bufCount];
	ADC->ADC_RNCR = m_bufSize;

	return true;
}

void AdcDma::disableCompareMode()
{
	// Clear selected channel and 'all channels' mode
	ADC->ADC_EMR &= ~(ADC_EMR_CMPSEL_Msk | ADC_EMR_CMPALL);
}

void AdcDma::triggerUpdateState(TriggerEvent *event)
{
	// All state need to handle TriggerEventKindDisable:
	if (event->eventKind == TriggerEventKindDisable) {
		triggerEnterDisabled(event);
		return;
	}

	switch (m_triggerState) {

	case TriggerStateDisabled :
		switch (event->eventKind) {
		case TriggerEventKindEnable :
			triggerEnterEnabled(event);
			break;
		default :
			goto _unhandledEvent;
		}
		break;

	case TriggerStateEnabled :
		switch (event->eventKind) {
		case TriggerEventKindDisable :
			triggerEnterDisabled(event);
			break;
		case TriggerEventKindPreBuffersFull :
			triggerEnterPreArmed(event);
			break;
		default :
			goto _unhandledEvent;
		}
		break;

	case TriggerStatePreArmed :
		switch (event->eventKind) {
		case TriggerEventKindDisable :
			triggerEnterDisabled(event);
			break;
		case TriggerEventKindCompareInterrupt :
			triggerEnterArmed(event);
			break;
		case TriggerEventKindTimeout :
			triggerEnterCapturing(event);
			break;
		default :
			goto _unhandledEvent;
		}
		break;

	case TriggerStateArmed :
		switch (event->eventKind) {
		case TriggerEventKindCompareInterrupt :
			triggerEnterCapturing(event);
			break;
		case TriggerEventKindTimeout :
			triggerEnterCapturing(event);
			break;
		default :
			goto _unhandledEvent;
		}
		break;

	case TriggerStateCapturing :
		switch (event->eventKind) {
		case TriggerEventKindAllBuffersFull :
			triggerEnterDone(event);
			break;
		case TriggerEventKindDisable :
			triggerEnterDisabled(event);
			break;
		default :
			goto _unhandledEvent;
		}
		break;

	case TriggerStateDone :
		switch (event->eventKind) {
		case TriggerEventKindEnable :
			triggerEnterEnabled(event);
			break;
		case TriggerEventKindDisable :
			triggerEnterDisabled(event);
			break;
		default :
			goto _unhandledEvent;
		}
		break;

	default :
		PF(DBG_TRIGGER, "Invalid current state !\r\n");
		break;
	}

	return;

_unhandledEvent :

	PF(DBG_TRIGGER, "Unhandled event (kind %d) from state %s\r\n", event->eventKind, StrTriggerState[m_triggerState]);
	return;
}

void AdcDma::triggerSetState(TriggerState state)
{
	PF(DBG_TRIGGER, "Trigger state change %s -> %s%s\r\n", StrTriggerState[m_triggerState], StrTriggerState[state], m_bTriggerTimeout ? " (timeout)" : "");
	m_triggerState = state;
}

void AdcDma::triggerDisableInterrupt()
{
	ADC->ADC_IDR = ADC_IDR_COMPE;	// Disable compare interrupt
}

void AdcDma::triggerEnableInterrupt()
{
	// Enable interruptions on compare event
	ADC->ADC_IER = ADC_IER_COMPE;
}

void AdcDma::triggerEnterDisabled(TriggerEvent *)
{
	triggerSetState(TriggerStateDisabled);

	triggerDisableInterrupt();
}

void AdcDma::triggerEnterEnabled(TriggerEvent *)
{
	m_triggerBufferInts = 0;
	m_bTriggerTimeout = false;
	m_triggerSampleBufIndex = -1;
	m_triggerSampleIndex = -1;

	triggerSetState(TriggerStateEnabled);

	// If no prebuffers are required, jump directly to PreArmed state
	if (m_triggerPreBuffersCount == 0) {
		TriggerEvent ev;
		ev.eventKind = TriggerEventKindPreBuffersFull;
		triggerUpdateState(&ev);
	}

	// If already started, enable trigger interrupt
	// Otherwise it will be done in Start()
	if (m_captureState == CaptureStateStarted)
		triggerEnableInterrupt();
}

void AdcDma::triggerEnterPreArmed(TriggerEvent *)
{
	uint32_t cmpMode, emr;

	// We first need value to go out of window to arm the trigger
	switch (m_triggerMode) {
	case FallingEdge :
		cmpMode = ADC_EMR_CMPMODE_HIGH;
		break;
	case RisingEdge :
		cmpMode = ADC_EMR_CMPMODE_LOW;
		break;
	default :
		PF(DBG_TRIGGER, "invalid trigger mode !\r\n");
		return;
	}

	emr = ADC->ADC_EMR;
	emr &= ~(ADC_EMR_CMPMODE_Msk |
			 ADC_EMR_CMPSEL_Msk  |
			 ADC_EMR_CMPALL |
			 ADC_EMR_CMPFILTER_Msk);

	emr |= (cmpMode |							// Comparator mode
			ADC_EMR_CMPSEL(m_triggerChannel));	// ADC channel for comparator

	ADC->ADC_EMR = emr;

	triggerSetState(TriggerStatePreArmed);
}

void AdcDma::triggerEnterArmed(TriggerEvent *)
{
	uint32_t emr;

	// Change mode of interrupt
	switch (m_triggerMode) {
	case RisingEdge :
		emr = ADC->ADC_EMR & (~ADC_EMR_CMPMODE_Msk);
		ADC->ADC_EMR = emr | ADC_EMR_CMPMODE_HIGH;
		break;
	case FallingEdge :
		emr = ADC->ADC_EMR & (~ADC_EMR_CMPMODE_Msk);
		ADC->ADC_EMR = emr | ADC_EMR_CMPMODE_LOW;
		break;
	}

	triggerSetState(TriggerStateArmed);

	return;
}

void AdcDma::triggerEnterCapturing(TriggerEvent *event)
{
	// Capture started, we don't need compare interruptions anymore
	triggerDisableInterrupt();
	disableCompareMode();

	triggerSetState(TriggerStateCapturing);

	m_readBufIndex = m_writeBufIndex - m_triggerPreBuffersCount;
	if (m_readBufIndex < 0)
		m_readBufIndex += m_bufCount;

	//PF(DBG_TRIGGER, "Setting sampleBufIndex %d, sampleIndex %d\r\n", event->bufIndex, event->sampleIndex);

	m_triggerSampleBufIndex = event->bufIndex;
	m_triggerSampleIndex = event->sampleIndex;
}

void AdcDma::triggerEnterDone(TriggerEvent *)
{
	// All buffers were written, buf writeBufIndex was not updated since
	// RNPR (next pointer) was used
	// Update write buffer to last written buffer by DMA, that is ReadIndex
	m_writeBufIndex = m_readBufIndex;

	if (!m_bTriggerTimeout) {
		// Finding "manually" the first sample after trigger is a bit more accurate
		// than just relying on the interruption timing at high sample rate
		triggerFindTriggerSample();
	}

	triggerSetState(TriggerStateDone);
}

void AdcDma::triggerFindTriggerSample()
{
	int bufLoops = 0;
	bool bArmed = false;
	bool bFound = false;

	uint16_t rawSample;
	uint16_t sample;

	// Skip pre buffers for trigger sample
	int iBuf = (m_readBufIndex + m_triggerPreBuffersCount) % m_bufCount;

	while (bufLoops < m_bufCount) {
		for (int iSample = 0; iSample < m_bufSize; iSample++) {
			rawSample = m_buffers[iBuf][iSample];
			if (m_adcChannelsCount > 1) {
				if ( (rawSample & 0xF000) >> 12 != m_triggerChannel)
					continue;
			}

			sample = (rawSample & 0x0FFF) >> 0;

			if (m_triggerMode == RisingEdge) {
				if (!bArmed) {
					if (sample < m_triggerVal) {
						bArmed = true;
					}
				}
				else {
					if (sample >= m_triggerVal) {
						// Got trigger sample
						m_triggerSampleBufIndex = iBuf;
						m_triggerSampleIndex = iSample;
						bFound = true;
						break;
					}
				}
			}
			else if (m_triggerMode == FallingEdge) {
				if (!bArmed) {
					if (sample > m_triggerVal) {
						bArmed = true;
					}
				}
				else {
					if (sample <= m_triggerVal) {
						// Got trigger sample
						m_triggerSampleBufIndex = iBuf;
						m_triggerSampleIndex = iSample;
						bFound = true;
						break;
					}
				}
			}
		}
		if (bFound)
			break;
		iBuf = (iBuf + 1) % m_bufCount;
		bufLoops++;
	}

	if (!bFound) {
		PF(DBG_TRIGGER, "did not find trigger sample !\r\n");
		return;
	}

	// Trigger sample was found,
	// initialize sample and buffer read index based on trigger sample address
	// and required presamples (if set)
	// For prebuffers, this is already dealt by IRQ handleer

	if (m_triggerPreSamplesCount >= 0) {
		int firstBufIndex = m_triggerSampleBufIndex;
		int firstSampleIndex = m_triggerSampleIndex - m_triggerPreSamplesCount;
		while (firstSampleIndex < 0) {
			firstSampleIndex += m_bufSize;
			firstBufIndex--;
		}
		if (firstBufIndex < 0)
			firstBufIndex += m_bufCount;

		m_readSampleIndex = firstSampleIndex;
		m_readBufIndex = firstBufIndex;
	}

	return;
}

void AdcDma::HandleInterrupt()
{
	uint32_t isr = ADC->ADC_ISR;

	if (isr & ADC_ISR_COMPE) {

		PF(DBG_IRQ, "Got a ADC_ISR_COMPE interrupt\r\n");

		// Get current sample
		int bufIndex = m_writeBufIndex;
		//int sampleIndex = m_bufSize - ADC->ADC_RCR - 1;	// -1 since we want previous sample
		int sampleIndex = m_bufSize - ADC->ADC_RCR;	// -1 since we want previous sample

		// If sampleIndex in negative, it means sample is in previous buffer
		if (sampleIndex < 0) {
			bufIndex = m_writeBufIndex - 1;
			if (bufIndex < 0)
				bufIndex = m_bufCount - 1;
			sampleIndex = m_bufSize - 1;	// Last sample in previous buffer
		}

		triggerHandleInterrupt(bufIndex, sampleIndex);

		// A new compare interruption might have been raised before we changed mode of comparator,
		// so read again status register to avoid reacting to this interruption on next IRQ
		isr = ADC->ADC_ISR;
	}

	if (isr & ADC_ISR_RXBUFF) {

		PF(DBG_IRQ, "Got a ADC_ISR_RXBUFF interrupt\r\n");

		switch (m_triggerState) {

		case TriggerStateCapturing :
			TriggerEvent event;
			event.eventKind = TriggerEventKindAllBuffersFull;
			triggerUpdateState(&event);
			break;

		default :
			PF(true, "ADC_ISR_RX_BUFF is set, stopping acquisition !\r\n");
			break;
		}

		Stop();

		// Note:
		// If ADC_ISR_RXBUFF is set, ADC_ISR_ENDRX is set too,
		// even if we disabled the ADC_ISR_RXBUFF interrupt.
		// Let's handle it to update received samples
	}

	if (isr & ADC_ISR_ENDRX) {

		// Will be used for user callback
		int bufIndex = m_writeBufIndex;
		//PF(true, "Set bufIndex %d\r\n", bufIndex);

		PF(DBG_IRQ, "ADC_ISR_ENDRX, IMR 0x%08x, trigger state %s\r\n", ADC->ADC_IMR, StrTriggerState[m_triggerState]);

		// Increase number of written buffers, we'll use that to know if we still have data to read
		m_writeBufCount++;

		m_triggerBufferInts++;

		switch (m_triggerState) {
		case TriggerStateEnabled :
		  {
			advanceWriteBuffer();

			if (m_writeBufCount == m_triggerPreBuffersCount) {
				TriggerEvent preBuffersFullEvent;
				preBuffersFullEvent.eventKind = TriggerEventKindPreBuffersFull;
				triggerUpdateState(&preBuffersFullEvent);
			}
			break;
		  }

		case TriggerStatePreArmed :
		  {
			advanceWriteBuffer();
			break;
		  }

		case TriggerStateArmed :
		  {
			// Auto advance read buffer when waiting for a trigger event
			advanceWriteBuffer();
			break;
		  }

		case TriggerStateCapturing :
		  {
			// If we reached last buffer, disable ENDRX interrupt
			if (!advanceWriteBuffer()) {
				ADC->ADC_IDR = ADC_ISR_ENDRX;
			}
			break;
		  }

		case TriggerStateDisabled :
		  {
			// If we reached last buffer, disable ENDRX interrupt
			if (!advanceWriteBuffer()) {
				ADC->ADC_IDR = ADC_ISR_ENDRX;
			}
			break;
		  }

		default :
		  {
			break;
		  }
		}

		// Check timeout condition when waiting for an event
		switch (m_triggerState) {
		case TriggerStatePreArmed :
		case TriggerStateArmed :
			if (m_triggerBufferInts > 0 && (m_triggerBufferInts > m_triggerTimeoutMaxInts) ) {
				m_bTriggerTimeout = true;
				TriggerEvent timeoutEvent = {TriggerEventKindTimeout, 0, 0};
				triggerUpdateState(&timeoutEvent);
			}
			break;
		default :
			break;
		}

		if (m_rxHandler) {
			PF(DBG_IRQ, "Calling handler, bufIndex %d\r\n", bufIndex);
			if (m_rxHandler(m_buffers[bufIndex], m_bufSize, (m_triggerSampleBufIndex == bufIndex), m_triggerSampleIndex, m_bTriggerTimeout))
				advanceReadBuffer();
			// Avoid notifying handler again for this buffer index
			m_triggerSampleBufIndex = -1;
		}
	}
}

void AdcDma::SetRxHandler(RxCallback rxHandler)
{
	m_rxHandler = rxHandler;
}

void AdcDma::triggerHandleInterrupt(int bufIndex, int sampleIndex)
{
	TriggerEvent event;
	event.eventKind = TriggerEventKindCompareInterrupt;
	event.sampleIndex = sampleIndex;
	event.bufIndex = bufIndex;
	triggerUpdateState(&event);
}

void ADC_Handler()
{
	AdcDma::GetInstance()->HandleInterrupt();
}

AdcDma * AdcDma::m_instance = NULL;

/*****************************/
/*** Debug Functionalities ***/
/*****************************/

const char *AdcDma::StrTriggerState [] = {
	"Disabled",
	"Enabled",
	"PreArmed",
	"Armed",
	"Capturing",
	"Done",
};

/*

									State Machine used for trigger management

							   ______________________________________________________
							   |													|
							   V													|
						---------------												|
						|  TSDisabled |												|
						---------------												|
							   |													|
							   |													|
							  --- TEKEnable											|
							   |													|
							   |													|
							   V													|
						---------------												|
						|  TSEnabled  |												|
						---------------												|
							   |													|
							   |													|
							  --- TEKPreBuffersFull									|
							   |	_____________________________					|
							   |	|							|					|
							   V	V							|					|
						---------------							|					|
						| TSPreArmed  |							|					|
						---------------							|					|
		__________________|	   |								|					|
		|					   |								|					|
		|					  --- TEKCompareInterrupt			|					|
		|					   |								|					|
		|					   |								|					|
		|					   V								|					|
		|				---------------							|					|
		|				|   TSArmed   |							|					|
		|				---------------							|					|
		|_________________|	   |								|					|
		|					   |								|					|
	   --- TEKTimeout		  --- TEKCompareInterrupt			|					|
		|_________________	   |								|					|
						  |    |								|					|
						  V	   V								|					|
						---------------							|					|
						| TSCapturing |							|					|
						---------------							|					|
							   |								|					|
							   |							    |					|
							  --- TEKAllBuffersFull			   --- TEKEnable	   --- TEKDisable
							   |								|					|
							   |								|					|
							   V								|					|
						---------------							|					|
						|   TSDone    |							|					|
						---------------							|					|
							   |								|					|
							   |								|					|
							   |________________________________|___________________|

   Use cases:

	1/
	Start
	Read samples
	Stop

	2/
	Start
	Stop
	Read samples

	2/
	Start
	Set trigger
	Wait for TSTriggerDone
	Read samples

	3/
	Start
	Set trigger
	Wait for TSCapturing
	Read samples
	Stop Trigger

*/

