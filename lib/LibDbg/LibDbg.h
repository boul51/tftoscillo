#ifndef _LIBDBG_H_
#define _LIBDBG_H_

// Set LIBDBG_ENABLE to 1 to enable debug

#ifndef SERIAL_IFACE
#error "Please define SERIAL_IFACE to Serial (programming port) or SerialUSB (native port)"
#endif

class LibDbg
{
public :
	static void pf(bool doPrint, const char* caller, const char *fmt, ... );
	static char *F2S(float f, int precision);
};

#if LIBDBG_ENABLE == 1
#define PF(doPrint, ...) LibDbg::pf(doPrint, __FUNCTION__, __VA_ARGS__)
#define P(doPrint, ...) LibDbg::pf(doPrint, NULL, __VA_ARGS__)
#else
#define PF(doPrint, ...) {}
#define P(doPrint, ...) {}
#endif

#endif
