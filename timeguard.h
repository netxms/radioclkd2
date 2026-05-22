/*
 * timeguard.h - plausibility gate for decoded radio time
 *
 * Validates every decoded coarse timestamp before it is allowed to set the
 * system clock or be fed to chrony. Trust is anchored by a static lower bound
 * (firmware build time), an independent monotonic-clock rate check, and an
 * N-frame confirmation for the no-baseline bootstrap. See README / project
 * documentation for the rationale.
 */

#ifndef TIMEGUARD_H_
#define TIMEGUARD_H_

#include "timef.h"

/* Ceiling is derived as minTime + this many seconds (~25 years). */
#define TG_MAX_AGE_SECONDS	((time_f)(25.0 * 365.25 * 86400.0))

/* Guard state machine phase */
typedef enum
{
	TG_PHASE_BOOTSTRAP = 0,	/* no baseline yet - validate against system clock */
	TG_PHASE_CONFIRMING,	/* counting consecutive rate-consistent frames */
	TG_PHASE_LOCKED		/* baseline established - steady-state rate check */
} tgPhase;

/* Verdict returned to the caller */
typedef enum
{
	TG_ACCEPT_STEP = 0,	/* plausible, large offset - caller should step the clock */
	TG_ACCEPT_SLEW,		/* plausible, small offset - feed chrony, no step */
	TG_HOLD,		/* confirmation in progress - caller does nothing */
	TG_REJECT		/* implausible - drop frame, raise spoofing alert */
} tgVerdict;

typedef struct
{
	/* --- configuration (set once from command line) --- */
	int	enabled;	/* master switch (--set-system-time) */
	time_f	minTime;	/* hard floor: real time is never before this */
	time_f	maxTime;	/* hard ceiling: 0 disables the check */
	time_f	stepThreshold;	/* |offset| above this -> step instead of slew */
	time_f	bootstrapWindow;/* first-frame offset within this -> trust immediately */
	int	confirmCount;	/* consecutive consistent frames required to promote */
	time_f	rateTolerance;	/* absolute slack for the rate check */
	time_f	ratePpm;	/* crystal error allowance, ppm of the interval */

	/* --- runtime state --- */
	tgPhase	phase;
	int	haveBaseline;	/* prevRadioTime / prevMono are populated */
	time_f	prevRadioTime;	/* radio time of the last accepted/tentative frame */
	time_f	prevMono;	/* CLOCK_MONOTONIC at that frame */
	int	confirmed;	/* consecutive consistent frames seen so far */
} tgState;

/* Initialise runtime state; leaves configuration fields untouched. */
void tgInit ( tgState* tg );

/*
 * Evaluate one decoded frame.
 *   radioTime - decoded coarse time (UTC seconds)
 *   monoNow   - CLOCK_MONOTONIC reading taken with the frame
 *   sysNow    - CLOCK_REALTIME reading taken with the frame
 */
tgVerdict tgEvaluate ( tgState* tg, time_f radioTime, time_f monoNow, time_f sysNow );

/* Spoofing alert marker for the firmware (/tmp/clock-spoof). */
void tgRaiseSpoofMarker ( void );
void tgClearSpoofMarker ( void );

#endif
