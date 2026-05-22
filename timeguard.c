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
	tg->haveAnchor = 0;
	tg->anchorRadio = 0;
	tg->anchorMono = 0;
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
 * Record the current frame as the fixed anchor for the drift check. Set once
 * when the guard locks and never overwritten afterwards, so the drift check
 * measures accumulated error against a stationary reference rather than the
 * previous frame.
 */
static void
tgSetAnchor ( tgState* tg, time_f radioTime, time_f monoNow )
{
	tg->anchorRadio = radioTime;
	tg->anchorMono = monoNow;
	tg->haveAnchor = 1;
}

/*
 * True if the radio time advanced consistently with the monotonic clock
 * since the previous frame. The monotonic clock is the device's own crystal,
 * independent of both the system clock and the RTC. This catches sudden step
 * jumps; the slack is the per-frame timestamp jitter plus the crystal error
 * over the (short) inter-frame interval.
 */
static int
tgFrameConsistent ( const tgState* tg, time_f radioTime, time_f monoNow )
{
	time_f expected, actual, tol;

	if ( !tg->haveBaseline )
		return 1;

	expected = monoNow - tg->prevMono;	/* local elapsed time */
	actual = radioTime - tg->prevRadioTime;	/* radio elapsed time */
	if ( expected < 0 )
		expected = 0;

	tol = tg->jitterTol + tg->ratePpm * 1e-6 * expected;
	return ( fabs ( actual - expected ) <= tol );
}

/*
 * True if the radio time has tracked the monotonic clock since the fixed
 * anchor was set. Because the anchor never moves, the deviation accumulates
 * on the left while the tolerance only grows at the allowed crystal rate -
 * so a slow-walk that stays under the per-frame step check is still caught
 * once its accumulated drift exceeds the rate envelope.
 */
static int
tgDriftConsistent ( const tgState* tg, time_f radioTime, time_f monoNow )
{
	time_f elapsed, drift, tol;

	if ( !tg->haveAnchor )
		return 1;

	elapsed = monoNow - tg->anchorMono;
	if ( elapsed < 0 )
		elapsed = 0;

	drift = ( radioTime - tg->anchorRadio ) - elapsed;
	tol = tg->rateTolerance + tg->ratePpm * 1e-6 * elapsed;
	return ( fabs ( drift ) <= tol );
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
			tgSetAnchor ( tg, radioTime, monoNow );
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
		if ( tgFrameConsistent ( tg, radioTime, monoNow ) )
		{
			tg->confirmed++;
			tgSetBaseline ( tg, radioTime, monoNow );
			if ( tg->confirmed >= tg->confirmCount )
			{
				tgSetAnchor ( tg, radioTime, monoNow );
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
		if ( !tgFrameConsistent ( tg, radioTime, monoNow ) )
		{
			/* Sudden step - keep the last good baseline and drop this frame. */
			loggerf ( LOGGER_NOTE, "timeguard: frame step "TIMEF_FORMAT" s vs monotonic - possible spoofing\n",
				( radioTime - tg->prevRadioTime ) - ( monoNow - tg->prevMono ) );
			return TG_REJECT;
		}
		if ( !tgDriftConsistent ( tg, radioTime, monoNow ) )
		{
			/* Accumulated drift vs the fixed anchor - slow-walk spoofing. */
			loggerf ( LOGGER_NOTE, "timeguard: drift "TIMEF_FORMAT" s vs anchor over "TIMEF_FORMAT" s - possible slow-walk spoofing\n",
				( radioTime - tg->anchorRadio ) - ( monoNow - tg->anchorMono ), monoNow - tg->anchorMono );
			return TG_REJECT;
		}
		tgSetBaseline ( tg, radioTime, monoNow );
		return ( fabs ( offset ) > tg->stepThreshold ) ? TG_ACCEPT_STEP : TG_ACCEPT_SLEW;
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
