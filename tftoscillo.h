#ifndef _TFTOSCILLO_H_
#define _TFTOSCILLO_H_

// Resolution for analog input and outputs
#define ANALOG_RES    12
#define ANALOG_MAX_VAL ((1 << ANALOG_RES) - 1)
// This is used for pot input. Max value read is not 4096 but around 4050 ie 98.8%
#define POT_ANALOG_MAX_VAL (988*ANALOG_MAX_VAL/1000)

#endif
