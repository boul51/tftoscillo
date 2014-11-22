
#include "AdcDma.h"

typedef struct _ADCDMA_PRESCALER {
	int bitMask;
	int div;
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

	AllocateBuffers(ADC_DMA_DEF_BUF_COUNT, ADC_DMA_DEF_BUF_SIZE, true);

	SetTimerChannel(m_timerChannel);	// Will call ConfigureTimer
	ConfigureAdc(true);					// Enable hardware trigger
	ConfigureDma();
}

AdcDma::~AdcDma()
{
	Stop();

	DeleteBuffers();
}

AdcDma *AdcDma::GetInstance()
{
	if (AdcDma::m_instance == NULL) {
		m_instance = new AdcDma();
	}

	return m_instance;
}

void AdcDma::findTriggerSample()
{
	int bufLoops = 0;
	int iBuf = m_readBufIndex;
	bool bArmed = false;

	while (bufLoops < m_bufCount) {
		for (int iSample = 0; iSample < m_bufSize; iSample++) {
			if (m_triggerMode == RisingEdge) {
				if (!bArmed) {
					if (m_buffers[iBuf][iSample] < m_triggerVal) {
						bArmed = true;
					}
				}
				else {
					if (m_buffers[iBuf][iSample] >= m_triggerVal) {
						// Got trigger sample
						m_triggerSampleBufIndex = iBuf;
						m_triggerSampleIndex = iSample;
						return;
					}
				}
			}
			else if (m_triggerMode == FallingEdge) {
				if (!bArmed) {
					if (m_buffers[iBuf][iSample] > m_triggerVal) {
						bArmed = true;
					}
				}
				else {
					if (m_buffers[iBuf][iSample] < m_triggerVal) {
						// Got trigger sample
						m_triggerSampleBufIndex = iBuf;
						m_triggerSampleIndex = iSample;
						return;
					}
				}
			}
		}
		iBuf++;
		bufLoops++;
	}

	p("%s: did not find trigger sample !\n", __FUNCTION__);

	return;
}

bool AdcDma::SetBuffers(int bufCount, int bufSize)
{
	if (m_captureState != CaptureStateStopped) {
		return false;
	}

	if (bufCount > ADC_DMA_DEF_BUF_COUNT) {
		return false;
	}

	if (bufSize > ADC_DMA_DEF_BUF_SIZE) {
		return false;
	}

	m_bufCount = bufCount;
	m_bufSize = bufSize;

	return true;
}

bool AdcDma::SetTimerChannel(int timerChannel)
{
	if (timerChannel > ADC_DMA_MAX_TIMER_CHANNEL) {
		p("%s: invalid channel %d !\n", __FUNCTION__, timerChannel);
		return false;
	}

	if (m_captureState != CaptureStateStopped) {
		p("%s: can't set timer channel while running !\n", __FUNCTION__);
		return false;
	}

	m_timerChannel = timerChannel;

	/* Timer channel selection in ADC_MR */
	int trgSel;
	switch (m_timerChannel) {
	case 0 :
		trgSel = ADC_TRIG_TIO_CH_0;
		break;
	case 1 :
		trgSel = ADC_TRIG_TIO_CH_1;
		break;
	case 2 :
		trgSel = ADC_TRIG_TIO_CH_2;
		break;
	default :
		p("%s: invalid timer channel %d !\n", m_timerChannel);
		return false;
	}

	int mr = ADC->ADC_MR & ~ADC_MR_TRGSEL_Msk;
	ADC->ADC_MR = mr | trgSel;

	// Timer has to be setup again
	ConfigureTimer();

	return true;
}

void AdcDma::SetCaptureState(CaptureState captureState)
{
	m_captureState = captureState;
}

void AdcDma::Start()
{
	m_writeBufIndex = 0;
	m_readBufIndex  = 0;

	StartAdc();
	StartDma();
	StartTimer();

	SetCaptureState(CaptureStateStarted);
}

void AdcDma::Stop()
{
	StopTimer();
	StopDma();
	StopAdc();

	SetCaptureState(CaptureStateStopped);
}

bool AdcDma::ReadSingleValue(int adcChannel, int *value)
{
	if (m_captureState != CaptureStateStopped)
		return false;

	// Set configuration to software trigger
	ConfigureAdc(true);
	StopTimer();

	ADC->ADC_CHDR = 0x0000FFFF;

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
	ConfigureAdc(false);

	return true;
}

bool AdcDma::SetTrigger(int value, TriggerMode mode, int triggerChannel, int triggerTimeoutMs)
{
	if (value < 0 || value > ADC_DMA_MAX_VAL)
		return false;

	if (mode != RisingEdge && mode != FallingEdge)
		return false;

	if (triggerChannel > ADC_DMA_MAX_ADC_CHANNEL)
		return false;

	m_triggerMode = mode;
	m_triggerVal = value;
	m_triggerChannel = triggerChannel;
	m_triggerPreBuffersCount = 1;
	if (triggerTimeoutMs != 0) {
		// Compute number of interrupts for trigger timeout
		m_triggerTimeoutMaxInts = triggerTimeoutMs * m_sampleRate / m_bufSize / 1000;
		// Force at least one period for timeout
		if (m_triggerTimeoutMaxInts == 0) {
			m_triggerTimeoutMaxInts = 1;
		}
	}
	else {
		// 0 is infinite timeout
		m_triggerTimeoutMaxInts = 0;
	}

	p("Calculated m_triggerTimeoutMaxInts %d\n", m_triggerTimeoutMaxInts);

	uint16_t v = (uint16_t)value;

	ADC->ADC_CWR =
			v |			// Low threshold
			v << 16;	// High threshold

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
}

void AdcDma::triggerSetState(TriggerState state)
{
	p("Trigger state change %s -> %s%s\n", StrTriggerState[m_triggerState], StrTriggerState[state], m_bTriggerTimeout ? " (timeout)" : "");
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

bool AdcDma::DidTriggerComplete(bool *pbTimeout)
{
	if (pbTimeout)
		*pbTimeout = m_bTriggerTimeout;

	return (m_triggerState == TriggerStateDone);
}

void AdcDma::triggerUpdateState(TriggerEvent *event)
{
	switch (m_triggerState) {

	case TriggerStateDisabled :
		switch (event->eventKind) {
		case TriggerEventKindEnable :
			triggerEnterEnabled(event);
		}
		break;

	case TriggerStateEnabled :
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
		}
		break;

	case TriggerStateCapturing :
		switch (event->eventKind) {
		case TriggerEventKindAllBuffersFull :
			triggerEnterDone(event);
			break;
		}
		break;

	case TriggerStateDone :
		switch (event->eventKind) {
		case TriggerEventKindOneBufferRead :
			triggerEnterReading(event);
			break;
		}

	case TriggerStateReading :
		switch (event->eventKind) {
		case TriggerEventKindAllBuffersRead :
			triggerEnterDisabled(event);
		}

		break;
	}
}

void AdcDma::triggerEnterDisabled(TriggerEvent *event)
{
	triggerSetState(TriggerStateDisabled);

	triggerDisableInterrupt();
}

void AdcDma::triggerEnterEnabled(TriggerEvent *event)
{
	m_triggerBufferInts = 0;
	m_bTriggerTimeout = false;

	int cmpMode;

	// We first need value to go out of window to arm the trigger
	switch (m_triggerMode) {
	case FallingEdge :
		cmpMode = ADC_EMR_CMPMODE_HIGH;
		break;
	case RisingEdge :
		cmpMode = ADC_EMR_CMPMODE_LOW;
		break;
	default :
		p("%s: invalid trigger mode !\n", __FUNCTION__);
		return;
	}

	int emr = ADC->ADC_EMR;
	emr &= ~(ADC_EMR_CMPMODE_Msk |
			 ADC_EMR_CMPSEL_Msk  |
			 ADC_EMR_CMPALL |
			 ADC_EMR_CMPFILTER_Msk);

	emr |= (cmpMode |						// Comparator mode
			ADC_EMR_CMPSEL(m_triggerChannel));	// ADC channel for comparator

	ADC->ADC_EMR = emr;

	triggerSetState(TriggerStateEnabled);

	triggerEnableInterrupt();

	// Restart DMA in case if was stopped at the end of previous trigger
	StartDma();
}

void AdcDma::triggerEnterArmed(TriggerEvent *event)
{
	int emr;

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
	DisableCompareMode();

	triggerSetState(TriggerStateCapturing);

	m_triggerSampleBufIndex = event->bufIndex;
	m_triggerSampleIndex = event->sampleIndex;
}

void AdcDma::triggerEnterDone(TriggerEvent *event)
{
	// All buffers were written, buf writeBufIndex was not updated since
	// RNPR (next pointer) was used
	// Update write buffer to last written buffer by DMA, that is ReadIndex
	m_writeBufIndex = m_readBufIndex;

	if (!m_bTriggerTimeout) {
		findTriggerSample();
	}

	triggerSetState(TriggerStateDone);
}

void AdcDma::triggerEnterReading(TriggerEvent *event)
{
	p("%s++\n", __FUNCTION__);
	triggerSetState(TriggerStateReading);
}

bool AdcDma::AllocateBuffers(int bufCount, int bufSize, bool bForceAllocate)
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
		p("AdcDma::AllocateBuffers: Invalid parameters !\n");
		return false;
	}

	m_bufCount = bufCount;
	m_bufSize  = bufSize;

	for (int iBuf = 0; iBuf < m_bufCount; iBuf++) {
		m_buffers[iBuf] = (uint16_t *)malloc(m_bufSize * sizeof(uint16_t));
		if (m_buffers[iBuf] == NULL) {
			p("AdcDma::AllocateBuffers: Failed allocating buffer %d, size %d !\n", iBuf, m_bufSize);
			DeleteBuffers();
			return false;
		}
		p("AdcDma::AllocateBuffers: m_buffers[%d]: 0x%08x\n", iBuf, m_buffers[iBuf]);
	}

	return true;
}

bool AdcDma::DeleteBuffers()
{
	for (int iBuf = 0; iBuf < m_bufCount; iBuf++) {
		if (m_buffers[iBuf] != NULL) {
			free(m_buffers[iBuf]);
			m_buffers[iBuf] = NULL;
		}
	}

	m_bufCount = 0;
	m_bufSize  = 0;
}

bool AdcDma::SetAdcChannels(int *adcChannels, int adcChannelsCount)
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

bool AdcDma::SetSampleRate(int sampleRate)
{
	if (sampleRate > ADC_DMA_MAX_SAMPLE_RATE)
		return false;

	if (m_captureState != CaptureStateStopped)
		return false;

	m_sampleRate = sampleRate;

	return ConfigureTimer();
}

void AdcDma::StopTimer()
{
	TcChannel * t = &(TC0->TC_CHANNEL)[m_timerChannel];  // pointer to TC0 registers for its channel 0

	t->TC_CCR = TC_CCR_CLKDIS;	// Disable counter clock
}

void AdcDma::StartTimer()
{
	TcChannel * t = &(TC0->TC_CHANNEL)[m_timerChannel];  // pointer to TC0 registers for its channel 0

	t->TC_CCR = TC_CCR_CLKEN |	// Enable counter clock
				TC_CCR_SWTRG ;  // Reset counter and start clock
}

bool AdcDma::ConfigureTimer()
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
		p("No prescaler found !\n");
		return false;
	}

	// We got prescaler, now calculate value for rc
	int RC = MCLK / (m_sampleRate * prescaler.div);

	p("AdcDma: Setting up timer with parameters:\n");
	p(" - Timer channel %d\n", m_timerChannel);
	p(" - Sample rate %d\n", m_sampleRate);
	p(" - Div %d\n", prescaler.div);
	p(" - RC %d\n", RC);

	// And write data to timer controller

	pmc_enable_periph_clk (TC_INTERFACE_ID + 0*3 + m_timerChannel) ;  // clock the TC0 channel for ADC

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

	return true;
}

bool AdcDma::ConfigureAdc(bool bSoftwareTrigger)
{
	// Note concerning startup time:
	// Datasheet says we need a startup time of 40us.
	// Startup time is given in periods of ADCClock.
	// We use prescaler of 0, so ADCClock = MCLK / 2 = 41MHz
	// => We need 41 periods, smaller fitting value is 64, use it

	// Read current trigger since this is set by SetTimerChannel
	int chSelect = ADC->ADC_MR & ADC_MR_TRGSEL_Msk;

	// Select trigger based on value of bSoftwareTrigger is requi
	int trgSelect = (bSoftwareTrigger ? ADC_MR_TRGEN_DIS : ADC_MR_TRGEN_EN);

	// Clear current trigger from MR
	ADC->ADC_MR &= ~ADC_MR_TRGEN;

	ADC->ADC_MR =
			trgSelect					|	// Enable hardware trigger
			chSelect					|	// Trigger channel selection
			ADC_MR_LOWRES_BITS_12		|	// 12 bits resolution
			ADC_MR_SLEEP_NORMAL			|	// Disable sleep mode
			ADC_MR_FWUP_OFF				|	// Normal sleep mode (disabled by ADC_MR_SLEEP_NORMAL)
			ADC_MR_FREERUN_OFF			|	// Disable freerun mode
			ADC_MR_PRESCAL(0)			|	// ADCClock = MCLK / (2)
			ADC_MR_STARTUP_SUT64		|	// Startup Time (cf note above)
			ADC_MR_SETTLING_AST3		|	// We use same parameters for all channels, use minimal value
			ADC_MR_ANACH_NONE			|	// Use Diff0, Gain0 and Off0 for all channels
			ADC_MR_TRACKTIM(0)			|	// Minimal value for tracking time
			ADC_MR_TRANSFER(0)			|	// Minimal value for transfer period
			ADC_MR_USEQ_NUM_ORDER;			// Don't use sequencer mode
}

void AdcDma::StopAdc()
{
	ADC->ADC_CHDR = 0x0000FFFF;		// Disable all ADC channels
}

void AdcDma::StartAdc()
{
	ADC->ADC_CHDR = 0x0000FFFF;		// Disable all ADC channels

	int cher = 0;

	for (int i = 0; i < m_adcChannelsCount; i++) {
		cher |= (0x1 << m_adcChannels[i]);	// Enable ADC channel
	}

	ADC->ADC_CHER = cher;	// Update Channel Enable Register
}

bool AdcDma::ConfigureDma()
{
	m_writeBufIndex = 0;
	m_readBufIndex = 0;

	ADC->ADC_IDR =
			0xFFFFFFFF;					// Disable all interrupts

	ADC->ADC_IER =
			ADC_IER_GOVRE			|	// Enable interrupts on general overflow errors
			ADC_IER_ENDRX			|	// Enable interrupts on end of receive buffer
			ADC_IER_RXBUFF;				// Enable interrupts on receive buffer full

	ADC->ADC_ISR;						// Clear status bits by reading

	return true;
}

void AdcDma::StartDma()
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

	ADC->ADC_PTCR = (0x1 << 0);			// Enable RX

	NVIC_ClearPendingIRQ(ADC_IRQn);
	NVIC_EnableIRQ(ADC_IRQn);
}

void AdcDma::StopDma()
{
	ADC->ADC_PTCR &= (0x1 << 1);   // Disable RX

	NVIC_ClearPendingIRQ(ADC_IRQn);
	NVIC_DisableIRQ(ADC_IRQn);
}

uint16_t *AdcDma::GetReadBuffer()
{
	uint16_t *buf = NULL;

	switch (m_triggerState) {
	case TriggerStateDisabled :
	case TriggerStateReading :
		//p("Write index %d, read index %d\n", m_writeBufIndex, m_readBufIndex);
		if (m_readBufIndex != m_writeBufIndex) {
			buf = m_buffers[m_readBufIndex];
		}
		break;

	case TriggerStateDone :
		// No buffer was read at this point, but data are ready
		buf = m_buffers[m_readBufIndex];
		// Notify trigger machine state that one buffer was read to enter state TriggerStateReading
		TriggerEvent event = {TriggerEventKindOneBufferRead, 0, 0};
		triggerUpdateState(&event);
		break;
	}

	return buf;
}

bool AdcDma::AdvanceReadBuffer()
{
	m_readBufIndex++;
	if (m_readBufIndex == m_bufCount)
		m_readBufIndex = 0;

	// All data since trigger were read, re-enable trigger
	if (m_readBufIndex == m_writeBufIndex && m_triggerState == TriggerStateReading) {
		TriggerEvent event = {TriggerEventKindAllBuffersRead, 0, 0};
		triggerUpdateState(&event);
	}

	//p("Advanced readBuffer, m_readBufIndex is now %d\n", m_readBufIndex);

	return true;
}

bool AdcDma::AdvanceWriteBuffer()
{
	// Use + 2 here since we give pointer and next pointer to DMA controller
	if ( ((m_writeBufIndex + 2) % m_bufCount) == m_readBufIndex) {
		//p("WriteBufIndex: Would overwrite non read data (R %d, W %d)\n", m_readBufIndex, m_writeBufIndex);
		return false;
	}

	m_writeBufIndex++;
	if (m_writeBufIndex == m_bufCount)
		m_writeBufIndex = 0;

	//p("Advanced writeBuffer, m_writeBufIndex is now %d\n", m_writeBufIndex);

	//p("AdcDma::AdvanceWriteBuffer: Setting next RX pointer %d (0x%08x)\n", m_writeBufIndex, (uint32_t)m_buffers[m_writeBufIndex]);

	// Current write buffer was automatically set by DMA controller.
	// Only update next pointer

	ADC->ADC_RNPR = (uint32_t)m_buffers[(m_writeBufIndex + 1) % m_bufCount];
	ADC->ADC_RNCR = m_bufSize;

	return true;
}

bool AdcDma::SetTriggerPreBuffersCount(int triggerPreBuffersCount)
{
	if (triggerPreBuffersCount + 2 > m_bufCount)
		return false;

	if (triggerPreBuffersCount < 1)
		return false;

	m_triggerPreBuffersCount = triggerPreBuffersCount;

	return true;
}

bool AdcDma::GetTriggerSampleAddress(uint16_t **pBufAddress, int *pSampleIndex)
{
	if (m_bTriggerTimeout) {
		*pBufAddress = NULL;
		*pSampleIndex = 0;
		return false;
	}

	*pBufAddress  = m_buffers[m_triggerSampleBufIndex];
	*pSampleIndex = m_triggerSampleIndex;

	return true;
}

void AdcDma::HandleInterrupt()
{
	int isr = ADC->ADC_ISR;

	if (isr & ADC_ISR_COMPE) {
		p("ADC_ISR_COMPE\n");

		// Get current sample
		int bufIndex = m_writeBufIndex;
		int sampleIndex = m_bufSize - ADC->ADC_RCR - 1;	// -1 since we want previous sample

		// If sampleIndex in negative, it means sample is in previous buffer
		if (sampleIndex < 0) {
			bufIndex = m_writeBufIndex - 1;
			if (bufIndex < 0)
				bufIndex = m_bufCount - 1;
			sampleIndex = m_bufSize - 1;	// Last sample in previous buffer
		}

		triggerHandleInterrupt(bufIndex, sampleIndex);
	}

	if (isr & ADC_ISR_ENDRX) {
		//p("ADC_ISR_ENDRX\n");

		m_triggerBufferInts++;

		switch (m_triggerState) {
		case TriggerStateEnabled :
		case TriggerStateArmed :
			// Auto advance read buffer when waiting for a trigger event
			if (m_triggerBufferInts >= m_triggerPreBuffersCount)
				AdvanceReadBuffer();
			break;
		}

		AdvanceWriteBuffer();

		// Check timeout condition when waiting for an event
		switch (m_triggerState) {
		case TriggerStateEnabled :
		case TriggerStateArmed :
			if (m_triggerBufferInts > 0 && (m_triggerBufferInts > m_triggerTimeoutMaxInts) ) {
				m_bTriggerTimeout = true;
				TriggerEvent timeoutEvent = {TriggerEventKindTimeout, 0, 0};
				triggerUpdateState(&timeoutEvent);
			}
			break;
		}
	}

	if (isr & ADC_ISR_RXBUFF) {

		p("ADC_ISR_RXBUFF");

		switch (m_triggerState) {

		case TriggerStateCapturing :
			TriggerEvent event;
			event.eventKind = TriggerEventKindAllBuffersFull;
			triggerUpdateState(&event);
			break;

		default :
			p("%s: ADC_ISR_RX_BUFF is set, stopping acquisition !\n", __FUNCTION__);
			break;
		}

		Stop();
	}
}

void AdcDma::DisableCompareMode()
{
	ADC->ADC_EMR &= ~ADC_EMR_CMPSEL_Msk;
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
	"Armed",
	"Capturing",
	"Done",
	"Reading",
};

#ifdef ADC_DMA_DEBUG
void adcdma_print(const char *fmt, ... ) {
	char buf[128]; // resulting string limited to 128 chars
	va_list args;

	if (!Serial)
		return;

	va_start (args, fmt );
	vsnprintf(buf, 128, fmt, args);
	va_end (args);

	Serial.print(buf);
}

char *adcdma_floatToStr(float f, int precision)
{
	int mult = 1;
	int multRes;
	int digit[32];
	static char s_ftstrbuf[32];
	sprintf(s_ftstrbuf, "%d.", (int)f);

	for (int i = 0; i < precision; i++) {
		mult *= 10;
		digit[i] = (int)(f * (float)mult) % 10;
		s_ftstrbuf[strlen(s_ftstrbuf) + 1] = 0;
		s_ftstrbuf[strlen(s_ftstrbuf)] = digit[i] + '0';
	}

	return s_ftstrbuf;
}

#else
void adcdma_print(const char *fmt, ... ) {}
char *adcdma_floatToStr(float f, int precision) {}
#endif
