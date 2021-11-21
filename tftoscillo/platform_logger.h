#ifndef PLATFORM_LOGGER_H
#define PLATFORM_LOGGER_H

#include "ArduinoLogger.h"
#include "CircularBufferLogger.h"

using PlatformLogger = PlatformLogger_t<CircularLogBufferLogger<1024>>;

#endif  // PLATFORM_LOGGER_H
