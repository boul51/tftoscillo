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
}

/* Test mode selection */

#define TEST_MODE_TRIGGER_SAMPLE	0
#define TEST_MODE_TRIGGER_BUFFER	1
#define TEST_MODE_POSTREAD			2
#define TEST_MODE_PREREAD			3

#define TEST_MODE			TEST_MODE_TRIGGER_BUFFER


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

#endif
