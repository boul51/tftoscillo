#ifndef _LIBDBG_H_
#define _LIBDBG_H_

class LibDbg
{
public :
	static void pf(bool doPrint, const char* caller, const char *fmt, ... );
	static char *F2S(float f, int precision);
};

#endif
