#include "LibDbg.h"

#include <Arduino.h>
#include <stdarg.h>

#if LIBDBG_ENABLE == 1

void LibDbg::pf(bool doPrint, const char *caller, const char *fmt, ... )
{
	char buf[128]; // resulting string limited to 128 chars
	va_list args;

	//doPrint = true;

	//Serial.println("in LibDbg::p");
	if (!doPrint)
		return;

	va_start (args, fmt );
	vsnprintf(buf, 128, fmt, args);
	va_end (args);

	Serial.print(caller);
	Serial.print(": ");
	Serial.print(buf);
}

char * LibDbg::F2S(float f, int precision)
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

void LibDbg::pf(bool doPrint, const char * caller, const char *fmt, ... ) {}
char * LibDbg::F2S(float f, int precision) {return NULL;}

#endif
