

// DAC and timer setup from:
// http://forum.arduino.cc/index.php?PHPSESSID=58ac5heg9q21h307sldn72jkm6&topic=205096.0

#define DACC_DMA_BUF_SIZE 4000
#define DACC_DMA_BUF_NUM  2

#define fDACC_DMA_BUF_SIZE ((float)(DACC_DMA_BUF_SIZE))
#define fDACC_DMA_BUF_NUM  ((float)(DACC_DMA_BUF_NUM))

#define MCLK 84000000
#define fMCLK ((float)(MCLK))

uint16_t g_daccSamples[DACC_DMA_BUF_NUM][DACC_DMA_BUF_SIZE];
int g_SamplesPerBuffer = DACC_DMA_BUF_SIZE;

int8_t g_curDaccBuf = 0;

void dac_setup ()
{
  pmc_enable_periph_clk (DACC_INTERFACE_ID) ; // start clocking DAC
  DACC->DACC_CR = DACC_CR_SWRST ;  // reset DAC


  DACC->DACC_MR = 
    DACC_MR_TRGEN_EN |    // Enable trigger
    DACC_MR_TRGSEL (1) |  // Trigger 1 = TIO output of TC0
    (0 << DACC_MR_USER_SEL_Pos) |  // select channel 0
    DACC_MR_REFRESH (0x01) |       // bit of a guess... I'm assuming refresh not needed at 48kHz
    (24 << DACC_MR_STARTUP_Pos) ;  // 24 = 1536 cycles which I think is in range 23..45us since DAC clock = 42MHz

/*

  DACC->DACC_MR = (0x1 << 0)  |   // Trigger enable
                  (0x1 << 1)  |   // Trigger on TCO TIO
                  (0x0 << 4)  |   // Half word transfer
                  (0x0 << 5)  |   // Disable sleep mode
                  (0x1 << 6)  |   // Fast wake up sleep mode
                  (0x1 << 8)  |   // Refresh period
                  (0x0 << 16) |   // Select channel 0
                  (0x0 << 20) |   // Disable tag mode
                  (0x0 << 21) |   // Max speed mode
                  (0x3F << 24);    // Startup time
                  */

}

void dac_dma_setup()
{
  DACC->DACC_TPR = (uint32_t)&g_daccSamples[0][0];
  DACC->DACC_TCR = g_SamplesPerBuffer;
  
  DACC->DACC_IDR = 0xFFFFFFFF ; // Clear interrupts
  DACC->DACC_IER = (0x1 << 2) | (0x1 << 3); // Enable ENDTX and TXBUFEMPTY
  
  DACC->DACC_CHER = DACC_CHER_CH0 << 0 ; // enable chan0
  }

void dac_dma_start()
{
  DACC->DACC_PTCR = (0x1 << 8);   // Enable TX

  NVIC_ClearPendingIRQ(DACC_IRQn);
  NVIC_EnableIRQ(DACC_IRQn);        // Enable DACC interrupt    
}

int g_samplesSent = 0;
int g_irqs = 0;

void DACC_Handler()
{
  //uint32_t *buf;
  void *buf;
  
  g_curDaccBuf = (++g_curDaccBuf) % DACC_DMA_BUF_NUM;
  
  //buf = &g_daccSamples[0][g_curDaccBuf];
  buf = &g_daccSamples[g_curDaccBuf][0];

/*
  Serial.print("Using buf ");
  Serial.print(g_curDaccBuf);
  Serial.print(", address: ");
  Serial.println((uint32_t)buf);
  */

  DACC->DACC_TPR = (uint32_t)buf;
  DACC->DACC_TCR = g_SamplesPerBuffer;
  
  g_samplesSent += g_SamplesPerBuffer;
  g_irqs++;
}

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

int g_npb = 1; // Number of signal periods per buffer (full buffer, ie (DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE) )
float g_freqMult = 1.;

#define MAX_IRQ_FREQ 1000

int g_sampleRate = 1000000.;
//int g_sampleRate = 1500000.;

bool timer_setup(float F)
{
  float fSampleRate = (float)g_sampleRate;
  
  while (fSampleRate / F > fDACC_DMA_BUF_SIZE) {
    fSampleRate -= 1.0;
  }
  
  // Try to find divider / rc combination to obtain sample rate
  PRESCALER prescaler;
  int numPrescalers = sizeof(prescalers) / sizeof(prescalers[0]);

  bool bFound = false;
  for (int i = 0; i < numPrescalers; i++) {
    prescaler = prescalers[i];
    // Check if maximal RC fits
    float sr = fMCLK / (float)prescaler.div / 16536.; // TODO, bad value !!
    // If sr is OK, keep this prescaler
    if (sr < fSampleRate) {
      bFound = true;
      break;
    }
  }
  
  if (!bFound) {
    Serial.println("No prescaler found !");
    return false;
  }
  
  Serial.print("Using prescaler with div: ");
  Serial.println(prescaler.div);
  Serial.print("Prescaler mask: ");
  Serial.println(prescaler.bitMask);
    
  // We got prescaler, now calculate value for rc
  float fRC = fMCLK / (fSampleRate * (float)prescaler.div);
  
  int RC = (int) fRC;
  
  // Calculate actual sample rate
  g_sampleRate = MCLK / (RC * prescaler.div);
  
  // Calculate number of samples in a buffer for one F period
  g_SamplesPerBuffer = (int) ((float)g_sampleRate / F);
  
  // Maximise number of samples in a buffer
  g_SamplesPerBuffer *= (DACC_DMA_BUF_SIZE / g_SamplesPerBuffer);
  
  // Calculate actual buf size
  //g_SamplesPerBuffer = (int)((float)g_npb * (float)g_sampleRate / F);
  
  // Update g_freqMult for generators
  g_freqMult = (float)g_sampleRate / F;
  
  Serial.print("timer_setup, freq: ");
  Serial.println(F);
  Serial.print("fSR: ");
  Serial.println(fSampleRate);
  Serial.print("SR: ");
  Serial.println(g_sampleRate);
  Serial.print("div: ");
  Serial.println(prescaler.div);
  Serial.print("fRC: ");
  Serial.println(fRC);
  Serial.print("rc: ");
  Serial.println(RC);
  Serial.print("g_npb: ");
  Serial.println(g_npb);
  Serial.print("g_samplesPerBuffer: ");
  Serial.println(g_SamplesPerBuffer);
  Serial.print("g_freqMult: ");
  Serial.println(g_freqMult);
  
  pmc_enable_periph_clk (TC_INTERFACE_ID + 0*3+0) ;  // clock the TC0 channel for DACC 0

  TcChannel * t = &(TC0->TC_CHANNEL)[0] ;    // pointer to TC0 registers for its channel 0
  t->TC_CCR = TC_CCR_CLKDIS ;  // disable internal clocking while setup regs
  t->TC_IDR = 0xFFFFFFFF ;     // disable interrupts
  t->TC_SR ;                   // read int status reg to clear pending

  t->TC_CMR = prescaler.bitMask | //TC_CMR_TCCLKS_TIMER_CLOCK1 |   // use TCLK1 (prescale by 128, = 656250 kHz)
              TC_CMR_WAVE |                  // waveform mode
              TC_CMR_WAVSEL_UP_RC |          // count-up PWM using RC as threshold
              TC_CMR_EEVT_XC0 |     // Set external events from XC0 (this setup TIOB as output)
              TC_CMR_ACPA_CLEAR | TC_CMR_ACPC_CLEAR |
              TC_CMR_BCPB_CLEAR | TC_CMR_BCPC_CLEAR ;

  t->TC_RC = RC;
  t->TC_RA = RC/2;
  
  t->TC_CMR = (t->TC_CMR & 0xFFF0FFFF) | TC_CMR_ACPA_CLEAR | TC_CMR_ACPC_SET ;  // set clear and set from RA and RC compares  
  t->TC_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG ;  // re-enable local clocking and switch to hardware trigger source.  
  
}

bool timer_setupOld2(float freq)
{
  // Sample vs signal period:
  // Tsample = Tsig / (DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE)
  
  PRESCALER prescaler;
  int numPrescalers = sizeof(prescalers) / sizeof(prescalers[0]);
  bool bFound = false;
  
  Serial.print("timer_setup, freq: ");
  Serial.println(freq);
  
  int i;
  
  // Fsample if given by:
  // Fsample = MCLK / div / rc
  // Fsample max val is 100MHz to fit DAC specifications
  
  // For now, always use fastest prescaler and rc
  prescaler = prescalers[0];
  
  // Number of periods per buffer is:
  float fnpb;
 
  float Fsample;
  float rc = 42.; // Min value to stay at 1MS/s
  float ra = rc / 2.;
  
  float Firq;

  for (;;) {
    Fsample = fMCLK / (float)prescaler.div / rc;  
    fnpb = fDACC_DMA_BUF_SIZE * fDACC_DMA_BUF_NUM * (float)freq / Fsample;
    if (fnpb >= 1.0)
      break;
    rc += 1.0;
  } 
  
  Serial.print("Calculated FSample: ");
  Serial.println(Fsample);

  Firq = Fsample / fDACC_DMA_BUF_SIZE;
  
  g_freqMult = Fsample / freq;
  
  Serial.print("Calculated IRQ freq: ");
  Serial.println(Firq);
  
  Serial.print("Using rc: ");
  Serial.print(rc);
  Serial.print(", ra: ");
  Serial.println(ra);
    
  // We need an integer value
  g_npb = (int)fnpb;
  
  // dma_buf_size = npb * Fsample / fDACC_DMA_BUF_NUM / freq
  
  // Re-calculate buf size based on new value:
  //g_SamplesPerBuffer = g_npb * (int)Fsample / DACC_DMA_BUF_NUM / freq;
  g_SamplesPerBuffer = (int) ((float)g_npb * Fsample / fDACC_DMA_BUF_NUM / freq);
  
  Serial.print("fnpb: ");
  Serial.print(fnpb);
  Serial.print("g_npb: ");
  Serial.println(g_npb);
  
  Serial.print("Calculated new buf size: ");
  Serial.println(g_SamplesPerBuffer);
  
  pmc_enable_periph_clk (TC_INTERFACE_ID + 0*3+0) ;  // clock the TC0 channel for DACC 0

  TcChannel * t = &(TC0->TC_CHANNEL)[0] ;    // pointer to TC0 registers for its channel 0
  t->TC_CCR = TC_CCR_CLKDIS ;  // disable internal clocking while setup regs
  t->TC_IDR = 0xFFFFFFFF ;     // disable interrupts
  t->TC_SR ;                   // read int status reg to clear pending

  t->TC_CMR = prescaler.bitMask | //TC_CMR_TCCLKS_TIMER_CLOCK1 |   // use TCLK1 (prescale by 128, = 656250 kHz)
              TC_CMR_WAVE |                  // waveform mode
              TC_CMR_WAVSEL_UP_RC |          // count-up PWM using RC as threshold
              TC_CMR_EEVT_XC0 |     // Set external events from XC0 (this setup TIOB as output)
              TC_CMR_ACPA_CLEAR | TC_CMR_ACPC_CLEAR |
              TC_CMR_BCPB_CLEAR | TC_CMR_BCPC_CLEAR ;

  t->TC_RC = (int)rc;
  t->TC_RA = (int)ra;
  
  t->TC_CMR = (t->TC_CMR & 0xFFF0FFFF) | TC_CMR_ACPA_CLEAR | TC_CMR_ACPC_SET ;  // set clear and set from RA and RC compares
  
  t->TC_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG ;  // re-enable local clocking and switch to hardware trigger source.  
  
  return true;
}

bool timer_setupOld(int freq)
{
  // Sample vs signal period:
  // Tsample = Tsig / (DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE)
  
  // Timer frequency must satisfy:
  // Ftc < Fsig / 2 * (DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE)g
  
  PRESCALER prescaler;
  int numPrescalers = sizeof(prescalers) / sizeof(prescalers[0]);
  bool bFound = false;
  
  Serial.print("timer_setup, freq: ");
  Serial.println(freq);
  
  int i;
  
  //for (i = numPrescalers - 1; i >= 0; i--) {
  for (i = 0; i < numPrescalers; i++) {// - 1; i >= 0; i--) {
    prescaler = prescalers[i];
    
    // Condition to fit is:
    //Ftc > Fsig * 2 * (DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE)
    
    int Ftc = MCLK / prescaler.div;
    if (Ftc > freq * 2 * (DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE)) {
      bFound = true;
      break;
    }

    //int rcTest = (int)((float)freq * (float)(DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE) * (float)prescaler.div / (float)MCLK); 
  }
  
  if (!bFound) {
    Serial.println("No prescaler found, using fastest one !");
    prescaler = prescalers[0];
    //return false;
  }
  
  if (prescaler.div == 0) {
  }
  
  Serial.print("Will use prescaler ");
  Serial.print(i);
  Serial.print(", mask ");
  Serial.print(prescaler.bitMask);
  Serial.print(", div ");
  Serial.println(prescaler.div);
  
  // We got the prescaler, now get values of RA and RC:
  //Tsample = Ttc * RC
  // => Ttc * RC = Tsample
  // => RC = Tsample / Ttc = Ftc / Fsample
  // Fsample = Fsig / (DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE)
  // => RC = Ftc * DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE / Fsig)
  
  // Tsample = Tsig / (DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE)
  
  // TC clock is running at MCLK / prescaler.div
  
  //int rc = (MCLK / prescaler.div) / freq * DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE;
  // RC = Fsig / Ftc * DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE;
  // Definition of RC is: RC * Ttc = Tsample
  // => RC = Tsample / Ttc = Tsample * Ftc = MCLK / prescaler.div / Fsig / (DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE)
  //int rc = (int)((float)freq * (float)(DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE) * (float)prescaler.div / (float)MCLK); 
  int rc = (int)( (float)(MCLK / prescaler.div) / (float)freq / (float)(DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE) );
  
  // if rc is too small (ie DACC_DMA_BUF_SIZE is too big), adjust DACC_DMA_BUF_SIZE
  if (rc < 1) {
    Serial.println("Forcing RC to 2");
    rc = 2;
  }
  
  // Based on rc, adjust number of samples to use
  // RC = Tsample / Ttc
  // Tsample = Tsig / (DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE)
  // RC = Tsig / (DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE) / Ttc
  // DACC_DMA_BUF_SIZE = Ftc / (Fsig * RC * DACC_DMA_BUF_NUM)
  g_SamplesPerBuffer = (int) ((float)(MCLK / prescaler.div) / (float)freq / (float)rc / (float)DACC_DMA_BUF_NUM);
  
  if (g_SamplesPerBuffer > DACC_DMA_BUF_SIZE)
    g_SamplesPerBuffer = DACC_DMA_BUF_SIZE;
  
  
  Serial.print("Using g_SamplesPerBuffer: ");
  Serial.println(g_SamplesPerBuffer);
  
  int ra = rc / 2;
  
  
  
  pmc_enable_periph_clk (TC_INTERFACE_ID + 0*3+0) ;  // clock the TC0 channel for DACC 0

  TcChannel * t = &(TC0->TC_CHANNEL)[0] ;    // pointer to TC0 registers for its channel 0
  t->TC_CCR = TC_CCR_CLKDIS ;  // disable internal clocking while setup regs
  t->TC_IDR = 0xFFFFFFFF ;     // disable interrupts
  t->TC_SR ;                   // read int status reg to clear pending

  t->TC_CMR = prescaler.bitMask | //TC_CMR_TCCLKS_TIMER_CLOCK1 |   // use TCLK1 (prescale by 128, = 656250 kHz)
              TC_CMR_WAVE |                  // waveform mode
              TC_CMR_WAVSEL_UP_RC |          // count-up PWM using RC as threshold
              TC_CMR_EEVT_XC0 |     // Set external events from XC0 (this setup TIOB as output)
              TC_CMR_ACPA_CLEAR | TC_CMR_ACPC_CLEAR |
              TC_CMR_BCPB_CLEAR | TC_CMR_BCPC_CLEAR ;

  t->TC_RC = rc;
  t->TC_RA = ra;
  
  Serial.print("Got rc: ");
  Serial.print(rc);
  Serial.print(", ra: ");
  Serial.println(ra);  
  
/*  
  t->TC_CMR = TC_CMR_TCCLKS_TIMER_CLOCK4 |   // use TCLK4 (prescale by 128, = 656250 kHz)
              TC_CMR_WAVE |                  // waveform mode
              TC_CMR_WAVSEL_UP_RC |          // count-up PWM using RC as threshold
              TC_CMR_EEVT_XC0 |     // Set external events from XC0 (this setup TIOB as output)
              TC_CMR_ACPA_CLEAR | TC_CMR_ACPC_CLEAR |
              TC_CMR_BCPB_CLEAR | TC_CMR_BCPC_CLEAR ;

g
  t->TC_RC = 656250 / 4;      // 4Hz
  t->TC_RA = t->TC_RC / 2;
*/

  //t->TC_RC =  875 ;     // counter resets on RC, so sets period in terms of 42MHz clock
  //t->TC_RA =  440 ;     // roughly square wave
  
  // Duration of rc is Ttc * rc = rc / Ftc
  // Num of used samples is 
  //g_usedSamples = 
  
  t->TC_CMR = (t->TC_CMR & 0xFFF0FFFF) | TC_CMR_ACPA_CLEAR | TC_CMR_ACPC_SET ;  // set clear and set from RA and RC compares
  
  t->TC_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG ;  // re-enable local clocking and switch to hardware trigger source.  
  
  return true;
}

void genSquare()
{
  static int prevVal = 12;
  //for (int i = 0; i < DACC_DMA_BUF_NUM * DACC_DMA_BUF_SIZE; i++) {
  for (int i = 0; i < DACC_DMA_BUF_NUM * g_SamplesPerBuffer; i++) {    
    int bufIndex    = i / g_SamplesPerBuffer;
    int sampleIndex = i % g_SamplesPerBuffer;
    
    float angle = (float)i * 2. / g_freqMult;
    
    if ( ((int)(angle)) % 2 == 0)    
      g_daccSamples[bufIndex][sampleIndex] = 0;
    else
      g_daccSamples[bufIndex][sampleIndex] = 0xFFFF;
  }
}

void genZero()
{
  for (int i = 0; i < DACC_DMA_BUF_NUM * g_SamplesPerBuffer; i++) {
    int bufIndex    = i / g_SamplesPerBuffer;
    int sampleIndex = i % g_SamplesPerBuffer;
    g_daccSamples[bufIndex][sampleIndex] = 0;
    //g_daccSamples[bufIndex][sampleIndex] = 0xFFFF;
  }
}

void genSin()
{
  int tStart = micros();
  
  for (int i = 0; i < DACC_DMA_BUF_NUM * g_SamplesPerBuffer; i++) {
    int bufIndex    = i / g_SamplesPerBuffer;
    int sampleIndex = i % g_SamplesPerBuffer;

    float angle = 2. * PI * (float)i / (float)g_freqMult;
    
    g_daccSamples[bufIndex][sampleIndex] = (int) ((sin(angle) + 1.0) / 2. * 4096. );

/*
    Serial.print("Generated sample for i: ");
    Serial.print(i);
    Serial.print(", bufIndex: ");
    Serial.print(bufIndex);
    Serial.print(", sampleIndex: ");
    Serial.print(sampleIndex);
    Serial.print(", angle: ");
    Serial.print(angle);
    Serial.print(", value: ");
    Serial.println(g_daccSamples[bufIndex][sampleIndex]);
*/
  }

  int tEnd = micros();  
  Serial.print("Generated sinus waveform in ");
  Serial.print(tEnd - tStart);
  Serial.print(" us");
}

void genSigDmaSetup() {
  
  timer_setup(500.);

  // Generation must be done after timer_setup since globals are initialized there  
  //genSquare();
  genSin();
  //genZero();
  
  dac_setup();
  dac_dma_setup();
  dac_dma_start();
  
}

void dac_write(int val)
{
  int isr = DACC->DACC_ISR;

  /*  
  Serial.print("isr: ");
  Serial.println(isr);
  */
  
  while ( (DACC->DACC_ISR & DACC_ISR_TXRDY) == 0) {
  }
  DACC->DACC_CDR = val & 0xFFF ;
}

void loop_genSigDma() {
  static int curSec = 0;
  int s = millis() / 1000;
  
  if (s != curSec) {
    int samples = g_samplesSent;
    int irqs = g_irqs;
    g_irqs = 0;
    g_samplesSent = 0;
    curSec = s;
    Serial.print("Sec: ");
    Serial.print(s);
    Serial.print(", samples sent: ");
    Serial.print(samples);
    Serial.print(", irqs: ");
    Serial.println(irqs);
  }
}
