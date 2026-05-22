/*
 * timeguard.c - plausibility gate for decoded radio time
 *
 * See timeguard.h for the rationale.
 */

#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#include "timeguard.h"
#include "logger.h"

#define SPOOF_MARKER_FILE	"/tmp/clock-spoof"

void
tgInit ( tgState* tg )
{
	tg->phase = TG_PHASE_BOOTSTRAP;
	tg->haveBaseline = 0;
	tg->prevRadioTime = 0;
	tg->prevMono = 0;
	tg->confirmed = 0;
}

/*
 * Record the current frame as the baseline for the next rate check.
 */
static void
tgSetBaseline ( tgState* tg, time_f radioTime, time_f monoNow )
{
	tg->prevRadioTime = radioTime;
	tg->prevMono = monoNow;
	tg->haveBaseline = 1;
}

/*
 * True if the radio time advanced consistently with the monotonic clock
 * since the last baseline. The monotonic clock is the device's own crystal,
 * independent of both the system clock and the RTC, so this catches step
 * jumps and rate anomalies (slow-walk spoofing) alike.
 */
static int
tgRateConsistent ( const tgState* tg, time_f radioTime, time_f monoNow )
{
	time_f expected, actual, tol;

	if ( !tg->haveBaseline )
		return 1;

	expected = monoNow - tg->prevMono;	/* local elapsed time */
	actual = radioTime - tg->prevRadioTime;	/* radio elapsed time */
	if ( expected < 0 )
		expected = 0;

	tol = tg->rateTolerance + tg->ratePpm * 1e-6 * expected;
	return ( fabs ( actual - expected ) <= tol );
}

tgVerdict
tgEvaluate ( tgState* tg, time_f radioTime, time_f monoNow, time_f sysNow )
{
	time_f offset;

	/* Static sanity bounds - enforced in every phase. */
	if ( radioTime < tg->minTime )
	{
		loggerf ( LOGGER_NOTE, "timeguard: radio time "TIMEF_FORMAT" precedes firmware build time "TIMEF_FORMAT"\n",
			radioTime, tg->minTime );
		return TG_REJECT;
	}
	if ( ( tg->maxTime > 0 ) && ( radioTime > tg->maxTime ) )
	{
		loggerf ( LOGGER_NOTE, "timeguard: radio time "TIMEF_FORMAT" is implausibly far in the future\n", radioTime );
		return TG_REJECT;
	}

	offset = radioTime - sysNow;

	switch ( tg->phase )
	{
	case TG_PHASE_BOOTSTRAP:
		if ( fabs ( offset ) <= tg->bootstrapWindow )
		{
			/* System clock is already close - trust this frame at once. */
			tgSetBaseline ( tg, radioTime, monoNow );
			tg->phase = TG_PHASE_LOCKED;
			return ( fabs ( offset ) > tg->stepThreshold ) ? TG_ACCEPT_STEP : TG_ACCEPT_SLEW;
		}
		/* Far from the current clock - require confirmation before jumping. */
		tgSetBaseline ( tg, radioTime, monoNow );
		tg->confirmed = 1;
		tg->phase = TG_PHASE_CONFIRMING;
		loggerf ( LOGGER_INFO, "timeguard: radio time differs from system clock by "TIMEF_FORMAT" s, confirming (1/%d)\n",
			offset, tg->confirmCount );
		return TG_HOLD;

	case TG_PHASE_CONFIRMING:
		if ( tgRateConsistent ( tg, radioTime, monoNow ) )
		{
			tg->confirmed++;
			tgSetBaseline ( tg, radioTime, monoNow );
			if ( tg->confirmed >= tg->confirmCount )
			{
				tg->phase = TG_PHASE_LOCKED;
				loggerf ( LOGGER_INFO, "timeguard: radio time confirmed over %d frames\n", tg->confirmed );
				return TG_ACCEPT_STEP;
			}
			loggerf ( LOGGER_INFO, "timeguard: confirming radio time (%d/%d)\n", tg->confirmed, tg->confirmCount );
			return TG_HOLD;
		}
		/* Confirmation run broken - start over from the system clock. */
		loggerf ( LOGGER_NOTE, "timeguard: inconsistent frame during confirmation - possible spoofing\n" );
		tg->phase = TG_PHASE_BOOTSTRAP;
		tg->haveBaseline = 0;
		tg->confirmed = 0;
		return TG_REJECT;

	case TG_PHASE_LOCKED:
	default:
		if ( tgRateConsistent ( tg, radioTime, monoNow ) )
		{
			tgSetBaseline ( tg, radioTime, monoNow );
			return ( fabs ( offset ) > tg->stepThreshold ) ? TG_ACCEPT_STEP : TG_ACCEPT_SLEW;
		}
		/* Rate anomaly - keep the last good baseline and drop this frame. */
		loggerf ( LOGGER_NOTE, "timeguard: rate anomaly (radio "TIMEF_FORMAT" vs monotonic) - possible spoofing\n",
			radioTime - tg->prevRadioTime );
		return TG_REJECT;
	}
}

void
tgRaiseSpoofMarker ( void )
{
	int fh = open ( SPOOF_MARKER_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644 );
	if ( fh >= 0 )
	{
		int64_t now = (int64_t) time ( NULL );
		write ( fh, &now, sizeof(int64_t) );
		close ( fh );
	}
}

void
tgClearSpoofMarker ( void )
{
	unlink ( SPOOF_MARKER_FILE );
}
