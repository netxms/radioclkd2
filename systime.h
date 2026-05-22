#ifndef SYSTIME_H_
#define SYSTIME_H_

#include "config.h"

#include <time.h>

#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#if !HAVE_TIMEGM

time_t timegm(struct tm *_tm);

#endif

#endif
