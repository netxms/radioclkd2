/*
 * status.h - diagnostic status file
 *
 * Periodically publishes the receiver state (signal level, recent pulses,
 * frame being collected, last decode result, PPS statistics, time guard
 * state) as a JSON document for external diagnostic tools. The file is
 * replaced atomically, so readers always see a complete document.
 */

#ifndef STATUS_H_
#define STATUS_H_

#include "clock.h"
#include "serial.h"

/* Minimal interval between two status file updates without pending events */
#define STATUS_WRITE_INTERVAL	(1.0)

void statusWrite ( const char* path, const char* name, int unit, const serDevT* dev, clkInfoT* clock );

#endif
