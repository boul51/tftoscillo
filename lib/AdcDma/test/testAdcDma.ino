#include <Arduino.h>
#include <LibDbg.h>
#include <AdcDma.h>

#define DBG_SETUP	false
#define DBG_LOOP	true

#define IN_CHANNEL 6

AdcDma *g_adcDma;

void setup()
{
	Serial.begin(115200);

	delay(1000);

	PF(DBG_SETUP, "++\r\n");

	g_adcDma = AdcDma::GetInstance();

	setupAdcDma();
}

/* Test mode selection */

#define TEST_MODE_TRIGGER_SAMPLE	0
#define TEST_MODE_TRIGGER_BUFFER	1
#define TEST_MODE_POSTREAD			2
#define TEST_MODE_PREREAD			3
#define TEST_MODE_BUFFER_DURATION	4
#define TEST_MODE_RX_HANDLER		5

#define TEST_MODE			TEST_MODE_RX_HANDLER


/* Start, trigger, wait for trigger done and read buffers */

#if TEST_MODE == TEST_MODE_TRIGGER_BUFFER

void loop()
{
	bool timeout;
	uint16_t channel = IN_CHANNEL;
	int bufSize = 5;
	int bufCount = 4;

	PF(true, "Entering loop\r\n");

	g_adcDma->SetTimerChannel(1);
	g_adcDma->SetAdcChannels(&channel, 1);
	g_adcDma->SetBuffers(bufCount, bufSize);
	g_adcDma->SetSampleRate(5);
	g_adcDma->Start();
	g_adcDma->SetTrigger(2000, AdcDma::RisingEdge, channel, 5000);
	g_adcDma->SetTriggerPreBuffersCount(2);
	g_adcDma->TriggerEnable(true);

	PF(true, "Will wait for trigger complete\r\n");

	// Acquisition will stop when all buffers are full
	while (!g_adcDma->DidTriggerComplete(&timeout)) {}

	PF(true, "Trigger complete, timeout %d\r\n", timeout);

	uint16_t *buf;
	bool isTriggerSample;

	int tgSampleIdx;
	uint16_t *tgBufAddr;

	g_adcDma->GetTriggerSampleAddress(&tgBufAddr, &tgSampleIdx);

	// Just for debug
	int bufIdx = 0;
	int tgBufIdx = -1;

	while ( (buf = g_adcDma->GetReadBuffer()) ) {
		for (int i = 0; i < bufSize; i++) {
			if ( (!timeout) && (tgBufAddr == buf) && (tgSampleIdx == i)) {
				isTriggerSample = true;
				tgBufIdx = bufIdx;
			}
			else {
				isTriggerSample = false;
			}
			uint16_t sample = buf[i];
			PF(true, "%c Got sample %d\r\n", (isTriggerSample ? '*' :' '), sample);
		}
		bufIdx++;
	}

	if (!timeout) {
		PF(true, "Trigger sample: buf %d, sample %d\r\n", tgBufIdx, tgSampleIdx);
	}
	else {
		PF(true, "Trigger timeout\r\n");
	}
}

/* Start, trigger, wait for trigger done and read samples */

#elif TEST_MODE == TEST_MODE_TRIGGER_SAMPLE

void loop()
{
	bool timeout;
	uint16_t channel = IN_CHANNEL;
	g_adcDma->SetAdcChannels(&channel, 1);
	g_adcDma->SetTimerChannel(0);
	g_adcDma->SetBuffers(4, 5);
	g_adcDma->SetSampleRate(5);
	g_adcDma->Start();
	g_adcDma->SetTrigger(2000, AdcDma::RisingEdge, channel, 5000);
	g_adcDma->SetTriggerPreSamplesCount(3);
	g_adcDma->TriggerEnable(true);

	// Acquisition will stop when all buffers are full
	while (!g_adcDma->DidTriggerComplete(&timeout)) {}

	PF(true, "Trigger complete, timeout %d\r\n", timeout);

	uint16_t sample;
	bool isTriggerSample;

	AdcDma::CaptureState state;

	for (;;) {
		if (g_adcDma->GetNextSample(&sample, &channel, &state, &isTriggerSample)) {
			PF(DBG_LOOP, "%c Got sample %d\r\n", (isTriggerSample ? '*' :' '), sample);
		}
		else {
			break;
		}
	}
}

/* Start, stop and read */

#elif TEST_MODE == TEST_MODE_POSTREAD

void loop()
{
	int timerChannel = 2;

	uint16_t channel = IN_CHANNEL;
	g_adcDma->SetAdcChannels(&channel, 1);
	g_adcDma->SetTimerChannel(timerChannel);
	g_adcDma->SetBuffers(4, 5);
	g_adcDma->SetSampleRate(10);

	g_adcDma->Start();

	// Acquisition will stop when all buffers are full
	while (g_adcDma->GetCaptureState() != AdcDma::CaptureStateStopped) {}

	uint16_t sample;
	int iSample = 0;

	while (g_adcDma->GetNextSample(&sample, &channel)) {
		PF(DBG_LOOP, "Got sample %d\r\n", sample);
		iSample++;
	}

	PF(DBG_LOOP, "Got %d samples\r\n", iSample);
}

/* Start, read and stop */

#elif TEST_MODE == TEST_MODE_PREREAD

void loop()
{
	uint16_t channel = IN_CHANNEL;
	g_adcDma->SetAdcChannels(&channel, 1);
	g_adcDma->SetTimerChannel(0);
	g_adcDma->SetBuffers(4, 10);
	g_adcDma->SetSampleRate(10);

	g_adcDma->Start();

	uint16_t sample;

	AdcDma::CaptureState state;

	for (;;) {
		if (g_adcDma->GetNextSample(&sample, &channel, &state)) {
			PF(DBG_LOOP, "Got sample %d\r\n", sample);
		}
		if (state == AdcDma::CaptureStateStopped)
			break;
	}
}

#elif TEST_MODE == TEST_MODE_BUFFER_DURATION

void loop()
{
	int bufCount = 0;
	uint32_t actDuration;
	uint16_t channel = IN_CHANNEL;
	g_adcDma->SetAdcChannels(&channel, 1);
	g_adcDma->SetSampleRate(48000);
	actDuration = g_adcDma->SetBufferDuration(1000);

	PF(true, "Got actual duration %d ms\r\n", actDuration);

	g_adcDma->Start();
	//while (g_adcDma->GetCaptureState() != AdcDma::CaptureStateStopped) {}

	for (;;) {
		if (g_adcDma->GetReadBuffer()) {
			bufCount++;
			PF(true, "Got %d buffers\r\n", bufCount);
			if (bufCount >= 10) {
				break;
			}
		}
	}

	g_adcDma->Stop();

}

#elif TEST_MODE == TEST_MODE_RX_HANDLER

bool g_done = true;
bool g_triggered = false;
int g_reqSamples = 10000000;
int g_readSamples = 0;
int g_readBuffers = 0;

bool RxHandler(uint16_t *buffer, int bufLen, bool bIsTrigger, int triggerIndex, bool bTimeout)
{
	PF(false, "%d\r\n", g_readSamples);

	char c;

	g_readSamples += bufLen;
	g_readBuffers++;

	PF(false, "bIsTrigger %d, triggerIndex %d\r\n", bIsTrigger, triggerIndex);

	for (int iSample = 0; iSample < bufLen; iSample++) {
		if (bTimeout)
			c = 'T';
		else if (bIsTrigger && (iSample == triggerIndex) )
			c = '*';
		else
			c = ' ';

		P(true, "%c Channel %d, Sample %d\r\n", c, (buffer[iSample] & 0xF000) >> 12, buffer[iSample] & 0x0FFF);
	}

	if (g_readSamples >= g_reqSamples || bIsTrigger || bTimeout) {
		PF(true, "Got %d samples (%d buffers), stopping AdcDma\r\n", g_readSamples, g_readBuffers);
		g_adcDma->Stop();
		g_readSamples = 0;
		g_triggered = false;
		g_done = true;
	}
	else if (g_adcDma->GetCaptureState() == AdcDma::CaptureStateStopped) {
		PF(true, "AdcDma is stopped, got %d buffers\r\n", g_readBuffers);
	}

	return true;
}

void setupAdcDma()
{
	uint16_t channel = IN_CHANNEL;

	g_adcDma->SetAdcChannels(&channel, 1);
	g_adcDma->SetSampleRate(5);
	g_adcDma->SetBuffers(10, 1);
	g_adcDma->SetRxHandler(RxHandler);
	g_adcDma->SetTrigger(2000, AdcDma::RisingEdge, channel, 5000);
	g_adcDma->TriggerEnable(true);
}

void loop()
{
	if (!g_done)
		return;

	g_done = false;

	PF(true, "Starting AdcDma\r\n");
	g_adcDma->Start();
}

#endif
