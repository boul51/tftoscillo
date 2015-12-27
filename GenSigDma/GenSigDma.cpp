
#include <Arduino.h>

#include "GenSigDma.h"

// DAC and timer setup from:
// http://forum.arduino.cc/index.php?PHPSESSID=58ac5heg9q21h307sldn72jkm6&topic=205096.0

#ifdef p
#undef p
#endif
#define p(...) gensigdma_print(__VA_ARGS__)

#ifdef F2S
#undef F2S
#endif
#define F2S(f) gensigdma_floatToStr(f, 4)

#define DACC_DMA_BUF_SIZE 8000
#define DACC_DMA_BUF_NUM  1

#define fDACC_DMA_BUF_SIZE ((float)(DACC_DMA_BUF_SIZE))
#define fDACC_DMA_BUF_NUM  ((float)(DACC_DMA_BUF_NUM))

#define MCLK 84000000
#define fMCLK ((float)(MCLK))

static GenSigDma *g_instance;

#define DAC_CHANNEL 1 // TODO

typedef struct _PRESCALER {
	int bitMask;
	int div;
}PRESCALER;

PRESCALER prescalers[] = {
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

GenSigDma::GenSigDma()
{
	m_bufSize  = DACC_DMA_BUF_SIZE;
	m_bufCount = 2;
	m_samplesPerBuffer = 0;
	m_currBuf = 0;
	m_started = false;
	m_waveform = WaveFormNone;
	m_sampleRate = GENSIGDMA_MAX_SAMPLE_RATE;
	m_maxSampleRate = GENSIGDMA_MAX_SAMPLE_RATE;
	m_daccMaxVal = GENSIGDMA_DACC_MAX_VAL;
	m_daccMinVal = GENSIGDMA_DACC_MIN_VAL;
	m_timerChannel = DAC_DMA_DEF_TIMER_CHANNEL;

	g_instance = this;

	for (int i = 0; i < m_bufCount; i++) {
		m_buffers[i] = (uint16_t *)malloc(DACC_DMA_BUF_SIZE * sizeof(uint16_t));
	}

	InitStats();

	SetupDacc();
	SetupDma();
}

GenSigDma::~GenSigDma()
{
	for (int i = 0; i < m_bufCount; i++) {
		if (m_buffers[i] != NULL)
			delete m_buffers[i];
	}
}

bool GenSigDma::Start()
{
	if (m_started)
		return false;

	// If no valid waveform was set, abort
	if (m_waveform == WaveFormNone) {
		p("%s: No valid waveform is defined !\r\n", __FUNCTION__);
		return false;
	}

	if (!StartDma())
		return false;

	m_started = true;

	return true;
}

bool GenSigDma::Stop()
{
	if (!m_started)
		return false;

	if (!StopDma())
		return false;

	m_started = false;

	return true;
}

bool GenSigDma::SetTimerChannel(int timerChannel)
{
	if (timerChannel < 0 || timerChannel > DAC_DMA_MAX_TIMER_CHANNEL) {
		p("GenSigDma::SetTimerChannel: Invalid channel %d\r\n", timerChannel);
	}

	if (m_started) {
		p("GenSigDma::SetTimerChannel: Started, won't do anything\r\n");
		return false;
	}

	m_timerChannel = timerChannel;

	updateDacTimerChannel();

	SetupTimer();

	return true;
}

bool GenSigDma::SetWaveForm(WaveForm wf, float freq, float *pActualFreq)
{
	if (wf <= WaveFormMin || wf >= WaveFormMax) {
		p("GenSigDma::SetWaveForm: Invalid waveform !\r\n");
		return false;
	}

	if (freq == 0.) {
		p("GenSigDma::SetWaveForm: Invalid frequency 0.0 !\r\n");
		return false;
	}

	if (m_started) {
		p("GenSigDma::SetWaveForm: Already running !\r\n");
		return false;
	}

	if ( (m_waveform == wf) && (freq == m_freq) ) {
		if (pActualFreq) {
			*pActualFreq = m_freq;
		}
		return true;
	}

	m_freq = freq;
	m_waveform = wf;

	if (!SetupTimer()) {
		p("GenSigDma::SetupTimer: Failed in SetupTimer !\r\n");
		return false;
	}

	switch (wf) {
	case WaveFormSinus :
		GenSin();
		break;
	case WaveFormSquare :
		GenSquare();
		break;
	case WaveFormSaw :
		GenSaw();
		break;
	case WaveFormTriangle :
		GenTriangle();
		break;
	default :
		p("%s: Invalid waveform !\r\n", __FUNCTION__);
		return false;
	}

	if (pActualFreq) {
		*pActualFreq = m_freq;
	}

	return true;
}

bool GenSigDma::SetMaxSampleRate(int rate)
{
	if (m_started) {
		p("GenSigDma::SetMaxSampleRate: Already running !\r\n");
		return false;
	}

	if (rate > GENSIGDMA_MAX_SAMPLE_RATE) {
		p("Can't exceed sample rate %d !\r\n", GENSIGDMA_MAX_SAMPLE_RATE);
		return false;
	}

	if (m_maxSampleRate != rate) {
		// Clear waveform if sample rate changed to force user to re-generate before start
		m_waveform = WaveFormNone;
		m_maxSampleRate = rate;
	}

	return true;
}

#if 1
bool GenSigDma::SetupTimer()
{
	m_sampleRate = m_maxSampleRate;

// Don't sample more than MAX_SAMPLE_FACTOR values for a period
// (Makes signal generation faster)
#define fMAX_SAMPLE_FACTOR 300.

	if (m_freq * fMAX_SAMPLE_FACTOR < (float)m_sampleRate) {
		m_sampleRate = (int)(m_freq * fMAX_SAMPLE_FACTOR);
	}

	// Calculate the bigger sample rate for which buffer is big enough
	while ((float)m_sampleRate / m_freq > fDACC_DMA_BUF_SIZE) {
		m_sampleRate-=10;
	}

	// Calculate samples per period:
	float fSamplesPerPeriod = (float)m_sampleRate / m_freq;

	// Keep integer part of samples per period
	int samplesPerPeriod = (int)fSamplesPerPeriod;

	// Calculate actual sample rate:
	m_sampleRate = samplesPerPeriod * (int)m_freq;

	// Calculate actual generated freq:
	m_freq = m_sampleRate / samplesPerPeriod;

	// Try to find divider / rc combination to obtain sample rate
	PRESCALER prescaler;
	int numPrescalers = sizeof(prescalers) / sizeof(prescalers[0]);

	bool bFound = false;
	for (int i = 0; i < numPrescalers; i++) {
		prescaler = prescalers[i];
		// Check if maximal RC fits
		int sr = MCLK / prescaler.div / (int)(0xFFFFFFFF); // Max value for RC is
		// If sr is OK, keep this prescaler
		if (sr < m_sampleRate) {
			bFound = true;
			break;
		}
	}

	if (!bFound) {
		p("No prescaler found !");
		return false;
	}

	// We got prescaler, now calculate value for rc
	int RC = MCLK / (m_sampleRate * prescaler.div);

	// Calculate number of samples in a buffer for one F period
	m_samplesPerBuffer = (int) ((float)m_sampleRate / m_freq);

	// Maximise number of samples in a buffer,
	// that is put several signal periods in a buffer
	m_samplesPerBuffer *= (DACC_DMA_BUF_SIZE / m_samplesPerBuffer);

	// Update variables used by signal generators
	m_freqMult = int((float)m_sampleRate / m_freq);
	m_periodsPerBuffer = m_samplesPerBuffer / m_freqMult;
	m_samplesPerPeriod = m_samplesPerBuffer / m_periodsPerBuffer;

	p("Setting up timer with parameters:\r\n");
	p(" - Timer channel %d\r\n", m_timerChannel);
	p(" - Freq %s\r\n", F2S(m_freq));
	p(" - Sample rate %d\r\n", m_sampleRate);
	p(" - Div %d\r\n", prescaler.div);
	p(" - RC %d\r\n", RC);
	p(" - Samples per buffer %d\r\n", m_samplesPerBuffer);
	p(" - FreqMult %d\r\n", int((float)m_sampleRate / m_freq));

	// And write data to timer controller

	pmc_enable_periph_clk (TC_INTERFACE_ID + m_timerChannel) ;  // clock the TC0 channel for DACC 0

	TcChannel * t = &(TC0->TC_CHANNEL)[m_timerChannel];  // pointer to TC0 registers for its channel 0
	t->TC_CCR = TC_CCR_CLKDIS;              // disable internal clocking during setup
	t->TC_IDR = 0xFFFFFFFF;                 // disable interrupts
	t->TC_SR;                               // read int status reg to clear pending

	t->TC_CMR = prescaler.bitMask |         // prescaler
			TC_CMR_WAVE |					// waveform mode
			TC_CMR_WAVSEL_UP_RC |			// count-up PWM using RC as threshold
			TC_CMR_EEVT_XC0 |				// Set external events from XC0 (this setup TIOB as output)
			TC_CMR_ACPA_CLEAR |
			TC_CMR_ACPC_CLEAR |
			TC_CMR_BCPB_CLEAR |
			TC_CMR_BCPC_CLEAR;

	t->TC_RC = RC;
	t->TC_RA = RC/2;

	t->TC_CMR = (t->TC_CMR & 0xFFF0FFFF) |
			TC_CMR_ACPA_CLEAR |
			TC_CMR_ACPC_SET ;  // set clear and set from RA and RC compares

	t->TC_CCR = TC_CCR_CLKEN |
			TC_CCR_SWTRG ;  // re-enable local clocking and switch to hardware trigger source.

	return true;
}
#else

bool GenSigDma::SetupTimer()
{
	// Set default samples per period
	m_samplesPerPeriod = 100;
	int minSamplesPerPeriod = 4;

	// For now use integer frequencies
	int freq = floor(m_freq);

	if (freq * minSamplesPerPeriod > m_maxSampleRate) {
		p("%s: frequency is too high !\r\n", __FUNCTION__);
		return false;
	}

	if (freq * m_samplesPerPeriod > m_maxSampleRate) {
		p("%s: reducing samples per period\r\n", __FUNCTION__);
		m_samplesPerPeriod = m_maxSampleRate / freq;
	}

	//freq = m_maxSampleRate / m_samplesPerPeriod;
	m_freq = (float)freq;

	m_sampleRate = m_samplesPerPeriod * (int)m_freq;

	// Try to find divider / rc combination to obtain sample rate
	PRESCALER prescaler;
	int numPrescalers = sizeof(prescalers) / sizeof(prescalers[0]);

	bool bFound = false;
	for (int i = 0; i < numPrescalers; i++) {
		prescaler = prescalers[i];
		// Check if maximal RC fits
		int sr = MCLK / prescaler.div / (int)(10000); // Max value for RC is
		// If sr is OK, keep this prescaler
		if (sr < m_sampleRate) {
			bFound = true;
			break;
		}
	}

	if (!bFound) {
		p("No prescaler found !");
		return false;
	}

	// We got prescaler, now calculate value for rc
	int RC = MCLK / (m_sampleRate * prescaler.div);

	// Calculate number of samples in a buffer for one F period
	//m_samplesPerBuffer = m_sampleRate / freq;
	m_samplesPerBuffer = m_samplesPerPeriod;

	// Maximise number of samples in a buffer,
	// that is put several signal periods in a buffer
	//m_samplesPerBuffer *= (DACC_DMA_BUF_SIZE / m_samplesPerBuffer);

	// Update variables used by signal generators
	m_freqMult = m_sampleRate / freq;
	//m_periodsPerBuffer = m_samplesPerBuffer / m_freqMult;
	m_periodsPerBuffer = 1;
	//m_samplesPerPeriod = m_samplesPerBuffer / m_periodsPerBuffer;

	p("Setting up timer with parameters:\r\n");
	p(" - Timer channel %d\r\n", m_timerChannel);
	p(" - Freq %s\r\n", F2S(m_freq));
	p(" - Sample rate %d\r\n", m_sampleRate);
	p(" - Div %d\r\n", prescaler.div);
	p(" - RC %d\r\n", RC);
	p(" - Samples per buffer %d\r\n", m_samplesPerBuffer);
	p(" - FreqMult %d\r\n", int((float)m_sampleRate / freq));

	// And write data to timer controller

	pmc_enable_periph_clk (TC_INTERFACE_ID + 0*3+m_timerChannel) ;  // clock the TC0 channel for DACC 0

	TcChannel * t = &(TC0->TC_CHANNEL)[m_timerChannel];  // pointer to TC0 registers for its channel 0
	t->TC_CCR = TC_CCR_CLKDIS;              // disable internal clocking during setup
	t->TC_IDR = 0xFFFFFFFF;                 // disable interrupts
	t->TC_SR;                               // read int status reg to clear pending

	t->TC_CMR = prescaler.bitMask |         // prescaler
			TC_CMR_WAVE |					// waveform mode
			TC_CMR_WAVSEL_UP_RC |			// count-up PWM using RC as threshold
			TC_CMR_EEVT_XC0 |				// Set external events from XC0 (this setup TIOB as output)
			TC_CMR_ACPA_CLEAR |
			TC_CMR_ACPC_CLEAR |
			TC_CMR_BCPB_CLEAR |
			TC_CMR_BCPC_CLEAR;

	t->TC_RC = RC;
	t->TC_RA = RC/2;

	t->TC_CMR = (t->TC_CMR & 0xFFF0FFFF) |
			TC_CMR_ACPA_CLEAR |
			TC_CMR_ACPC_SET ;  // set clear and set from RA and RC compares

	t->TC_CCR = TC_CCR_CLKEN |
			TC_CCR_SWTRG ;  // re-enable local clocking and switch to hardware trigger source.

	return true;
}

#endif

void GenSigDma::updateDacTimerChannel()
{
	uint32_t mr = DACC->DACC_MR;

	mr &= ~DACC_MR_TRGSEL_Msk;
	mr |= DACC_MR_TRGSEL(m_timerChannel + 1);	// +1 since 0 is external trigger

	DACC->DACC_MR = mr;
}

bool GenSigDma::SetupDacc() {
	pmc_enable_periph_clk (DACC_INTERFACE_ID) ; // start clocking DAC
	DACC->DACC_CR = DACC_CR_SWRST ;  // reset DAC

	/*
	DACC->DACC_MR =
	DACC_MR_TRGEN_EN |    // Enable trigger
	DACC_MR_TRGSEL (1) |  // Trigger 1 = TIO output of TC0
	(0 << DACC_MR_USER_SEL_Pos) |  // select channel 0
	DACC_MR_REFRESH (0x01) |       // bit of a guess... I'm assuming refresh not needed at 48kHz
	(24 << DACC_MR_STARTUP_Pos) ;  // 24 = 1536 cycles which I think is in range 23..45us since DAC clock = 42MHz
	*/

	int dac_channel;
	switch (DAC_CHANNEL) {
	case 0 :
		dac_channel = DACC_MR_USER_SEL_CHANNEL0;
		break;
	case 1 :
		dac_channel = DACC_MR_USER_SEL_CHANNEL1;
		break;
	}

	DACC->DACC_MR = (0x1 << 0)  |   // Trigger enable
			(0x0 << 4)  |   // Half word transfer
			(0x0 << 5)  |   // Disable sleep mode
			(0x1 << 6)  |   // Fast wake up sleep mode
			(0x1 << 8)  |   // Refresh period
			dac_channel |	// Select DACC channel
			(0x0 << 20) |   // Disable tag mode
			(0x0 << 21) |   // Max speed mode
			(0x3F << 24);    // Startup time

	// Setup DACC_MR timer channel selection
	updateDacTimerChannel();

	return true;
}

bool GenSigDma::SetupDma()
{
	p("GenSigDma::SetupDma TODO: Set next pointer register !\r\n");

	DACC->DACC_TPR = (uint32_t)&m_buffers[0][0];
	DACC->DACC_TCR = m_samplesPerBuffer;

	DACC->DACC_IDR = 0xFFFFFFFF ; // Clear interrupts
	DACC->DACC_IER = (0x1 << 2) | (0x1 << 3); // Enable ENDTX and TXBUFEMPTY

	int cher;

	switch (DAC_CHANNEL) {
	case 0 :
		cher = DACC_CHER_CH0;
		break;
	case 1 :
		cher = DACC_CHER_CH1;
		break;
	}

	DACC->DACC_CHER = cher; // Enable selected channel

	return true;
}

bool GenSigDma::StartDma()
{
	DACC->DACC_PTCR = (0x1 << 8);   // Enable TX

	NVIC_ClearPendingIRQ(DACC_IRQn);
	NVIC_EnableIRQ(DACC_IRQn);        // Enable DACC interrupt

	return true;
}

bool GenSigDma::StopDma()
{
	DACC->DACC_PTCR &= (0x1 << 9);   // Disable TX

	NVIC_ClearPendingIRQ(DACC_IRQn);
	NVIC_DisableIRQ(DACC_IRQn);        // Enable DACC interrupt

	return true;
}

void DACC_Handler()
{
	void *buf;
	GenSigDma::Stats *pStats;
	int samplesPerBuffer = g_instance->GetSamplesPerBuffer();

	int curBuf = g_instance->GetCurrBufIndex();
	curBuf++;
	curBuf %= g_instance->GetBufCount();
	g_instance->SetCurrBufIndex(curBuf);

	buf = g_instance->GetBufAddress(curBuf);

	DACC->DACC_TPR = (uint32_t)buf;
	DACC->DACC_TCR = samplesPerBuffer;

	pStats = g_instance->GetStats();

	pStats->lastIrqsCount++;
	pStats->totalIrqsCount++;
	pStats->lastSamplesCount += samplesPerBuffer;
	pStats->totalSamplesCount += samplesPerBuffer;
}

void GenSigDma::DuplicateBuffers()
{
	for (int i = 1; i < m_bufCount; i++) {
		memcpy(m_buffers[i], m_buffers[0], m_samplesPerBuffer * sizeof(uint16_t));
	}
}

bool GenSigDma::GenSquare()
{
	for (int i = 0; i < m_samplesPerPeriod; i++) {
		int sampleIndex = i % m_samplesPerPeriod;

		float teta = (float)i * 2. / (float)m_freqMult;

		// %2 with float..
		teta = teta - (int)((int)teta / 2 * 2);

		int val;

		if (teta <= 1.) {
			val = m_daccMaxVal;
		}
		else {
			val = 0;
		}

		m_buffers[0][sampleIndex] = (int)val;
	}

	DuplicatePeriods();
	DuplicateBuffers();

	return true;
}

bool GenSigDma::GenTriangle()
{
	for (int i = 0; i < m_samplesPerPeriod; i++) {
		int sampleIndex = i % m_samplesPerPeriod;

		float teta = (float)i * 2. / (float)m_freqMult;

		// %2 with float..
		teta = teta - (int)((int)teta / 2 * 2);

		float val;

		if (teta <= 1.) {
			val = teta * (float)m_daccMaxVal;
		}
		else {
			teta -= 1.;
			val = (float)m_daccMaxVal - teta * (float)m_daccMaxVal;
		}

		m_buffers[0][sampleIndex] = (int)val;

		p("%d: %d\r\n", sampleIndex, (int)val);

		if (sampleIndex > 0) {
			int diff = m_buffers[0][sampleIndex] - m_buffers[0][sampleIndex - 1];
			if (diff > 100 || diff < -100) {
				p("Gap 1 at %d (%d)\r\n", sampleIndex, diff);
			}
		}
	}

	DuplicatePeriods();
	DuplicateBuffers();

	return true;
}

bool GenSigDma::GenSaw()
{
	for (int i = 0; i < m_samplesPerPeriod; i++) {
		int sampleIndex = i % m_samplesPerPeriod;

		float angle = (float)i / (float)m_freqMult;

		m_buffers[0][sampleIndex] = (int)(angle * (float)m_daccMaxVal);
	}

	DuplicatePeriods();
	DuplicateBuffers();

	return true;
}

void GenSigDma::DuplicatePeriods()
{
	for (int iPeriod = 0; iPeriod < m_periodsPerBuffer; iPeriod++) {
		memcpy(&m_buffers[0][iPeriod * m_samplesPerPeriod], &m_buffers[0][0], m_samplesPerPeriod * sizeof(uint16_t));
	}
}

bool GenSigDma::GenSin()
{
	int tStart = micros();

	for (int iSample = 0; iSample < m_samplesPerPeriod; iSample++) {

		float angle = 2. * PI * (float)iSample / (float)m_freqMult;

		m_buffers[0][iSample] = (int) ((cos(angle) + 1.0) / 2. * (float)m_daccMaxVal);
	}

	DuplicatePeriods();
	DuplicateBuffers();

	int tEnd = micros();
	p("Generated sinus waveform in %d us\r\n", tEnd - tStart);

	return true;
}

void GenSigDma::InitStats()
{
	m_stats.totalIrqsCount = 0;
	m_stats.totalSamplesCount = 0;
	ResetStats();
}

void GenSigDma::ResetStats()
{
	m_stats.lastIrqsCount = 0;
	m_stats.lastSamplesCount = 0;
	m_stats.lastSec = millis() / 1000;
}

void GenSigDma::DisplayStats(int sec)
{
	int samples = m_stats.lastSamplesCount;
	int irqs = m_stats.lastIrqsCount;
	p("Sec: %d, samples sent: %d, irqs: %d\r\n", sec, samples, irqs);
}

void GenSigDma::Loop(bool bResetStats)
{
	int s = millis() / 1000;

	if (s != m_stats.lastSec) {
		DisplayStats(s);
		if (bResetStats)
			ResetStats();
	}
}

#ifdef GEN_SIG_DMA_DEBUG
void gensigdma_print(const char *fmt, ... ) {
	char buf[128]; // resulting string limited to 128 chars
	va_list args;

	if (!Serial)
		return;

	va_start (args, fmt );
	vsnprintf(buf, 128, fmt, args);
	va_end (args);

	Serial.print(buf);
}

char *gensigdma_floatToStr(float f, int precision)
{
	int mult = 1;
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
void gensigdma_print(const char *fmt, ... ) {}
char *gensigdma_floatToStr(float f, int precision) {return NULL;}
#endif

#undef p
#undef F2S
