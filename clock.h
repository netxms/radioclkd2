#ifndef CLOCK_H_
#define CLOCK_H_

#include "systime.h"
#include "timef.h"
#include "shm.h"
#include "timeguard.h"


#define	PPS_AVERAGE_COUNT		(60)

#define CLOCKTYPE_DCF77	0
#define CLOCKTYPE_MSF	1
#define CLOCKTYPE_WWVB	2

#define DCF77_TIMEZONE_CET   0
#define DCF77_TIMEZONE_UTC   1
#define DCF77_TIMEZONE_LOCAL 2

/* Reason of the last decode failure or frame discard (diagnostics) */
#define CLKERR_NONE             0
#define CLKERR_BAD_PULSE        1	//pulse length outside of any known width - frame discarded
#define CLKERR_BAD_GAP          2	//gap between pulses outside of any known width - frame discarded
#define CLKERR_OVERFLOW         3	//no minute marker within 120 pulses - frame discarded
#define CLKERR_FRAME_SHORT      4	//minute marker seen before a complete frame was collected
#define CLKERR_START_BIT        5	//DCF77 start-of-time bit (20) missing
#define CLKERR_PARITY_MINUTE    6
#define CLKERR_PARITY_HOUR      7
#define CLKERR_PARITY_DATE      8
#define CLKERR_PARITY_YEAR      9
#define CLKERR_PARITY_WEEKDAY   10
#define CLKERR_TZ_BITS          11	//DCF77 Z1/Z2 both set or both clear
#define CLKERR_RANGE            12	//decoded field out of range
#define CLKERR_CONVERSION       13	//decoded fields do not form a valid date

/* Flags of the last decoded frame */
#define CLKFLAG_DST             0x01	//summer time (CEST/BST)
#define CLKFLAG_DST_ANNOUNCE    0x02	//time zone change at the end of the hour
#define CLKFLAG_LEAP_ANNOUNCE   0x04	//leap second at the end of the hour
#define CLKFLAG_BACKUP_ANTENNA  0x08	//DCF77 transmitter on backup antenna

/* Last time guard verdict when guard is not enabled */
#define CLK_VERDICT_NONE        (-1)

/* Recent pulse history kept for diagnostics */
#define PULSE_HISTORY_SIZE      (60)

typedef struct
{
	time_f		start;		//pulse start (wall clock)
	short		pulseMs;	//pulse length, -1 if not finished yet
	short		gapMs;		//gap after the pulse, -1 if not finished yet
	signed char	value;		//classified pulse value (10ths of a second), -1 if invalid
} clkPulseT;

typedef struct
{
	unsigned long	edges;		//signal transitions seen
	unsigned long	pulsesGood;
	unsigned long	pulsesBad;
	unsigned long	gapsBad;
	unsigned long	framesDecoded;
	unsigned long	framesFailed;
	unsigned long	guardRejects;
	unsigned long	guardHolds;
	unsigned long	clockSteps;
} clkStatsT;

typedef struct clkInfoS clkInfoT;
struct clkInfoS
{
	clkInfoT*	next;

	int	inverted;	//if true, treat the signal as inverted...
	time_f	fudgeoffset;	//added to the recieved time - used to correct for recieve delays

	int	status;
	time_f	changetime;

	int     dcf77tz;        // Time zone used for decoding DCF77 messages

	//store 2 minutes of data - there will be a complete minute of data in here somewhere...
	signed char	data[120];
	int		numdata;

	int		msf_skip_b;	//set to 1 if we have a 100ms high after a 100ms low

	time_f		pctime;
	time_f		radiotime;
	int		radioleap;
	int		clocktype;	// The clock type. See CLOCKTYPE_

	int	secondssincetime;

	struct
	{
		time_f	pctime;
		time_f	radiotime;
	} ppslist[PPS_AVERAGE_COUNT];
	int	ppsindex;

	shmTimeT*	shm;

	int		rx_state;	//0=initial, 1=receiving, 2=lost

	tgState		guard;		//plausibility gate / system clock control

	//-- diagnostics
	int		level;		//current (non-inverted) line level
	time_f		lastEdge;	//wall clock time of the last transition, 0 if none yet
	int		frameSynced;	//data[] starts at a minute boundary
	clkPulseT	pulses[PULSE_HISTORY_SIZE];
	int		pulseIndex;	//next slot in pulses[]
	int		pulseCount;	//valid entries in pulses[]
	clkStatsT	stats;
	int		lastError;	//one of CLKERR_
	time_f		lastErrorTime;
	time_f		lastErrorValue;	//offending length for BAD_PULSE / BAD_GAP
	int		decodeError;	//set by decoders before returning failure
	time_f		lastDecodeTime;	//wall clock time of the last successful decode
	int		radioflags;	//CLKFLAG_ of the last successful decode
	int		lastVerdict;	//tgVerdict or CLK_VERDICT_NONE
	time_f		lastVerdictOffset;	//radio time - system time at that verdict
	time_f		lastFedTime;	//wall clock time of the last SHM update
	int		statusDirty;	//write status file at the next opportunity
};


void clkDumpData ( const clkInfoT* clock );

clkInfoT* clkCreate ( int inverted, int shmunit, time_f fudgeoffset, int clocktype, int dcf77tz, const tgState* guardCfg );

void clkDataClear ( clkInfoT* clock );

int clkPulseLength ( time_f timef, int clocktype );


void clkProcessStatusChange ( clkInfoT* clock, int Status, time_f timef );

void clkSendTime ( clkInfoT* clock );

void clkProcessPPS ( clkInfoT* clock, time_f timef );

//void clkDumpPPS ( clkInfoT* clock );

int clkCalculatePPSAverage ( clkInfoT* clock, time_f* paverage, time_f* pdeviation );

const char* clkErrorName ( int error );


#endif
