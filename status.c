/*
 * status.c - diagnostic status file
 *
 * See status.h for the rationale.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "status.h"
#include "logger.h"

static const char*
statusTypeName ( const clkInfoT* clock )
{
	switch ( clock->clocktype )
	{
	case CLOCKTYPE_DCF77:
		switch ( clock->dcf77tz )
		{
		case DCF77_TIMEZONE_UTC:	return "gps";
		case DCF77_TIMEZONE_LOCAL:	return "timecode";
		}
		return "dcf77";
	case CLOCKTYPE_MSF:	return "msf";
	case CLOCKTYPE_WWVB:	return "wwvb";
	}
	return "unknown";
}

static const char*
statusRxStateName ( int state )
{
	switch ( state )
	{
	case 0:	return "initial";
	case 1:	return "receiving";
	case 2:	return "lost";
	}
	return "unknown";
}

static const char*
statusPhaseName ( tgPhase phase )
{
	switch ( phase )
	{
	case TG_PHASE_BOOTSTRAP:	return "bootstrap";
	case TG_PHASE_CONFIRMING:	return "confirming";
	case TG_PHASE_LOCKED:		return "locked";
	}
	return "unknown";
}

static const char*
statusVerdictName ( int verdict )
{
	switch ( verdict )
	{
	case TG_ACCEPT_STEP:	return "step";
	case TG_ACCEPT_SLEW:	return "slew";
	case TG_HOLD:		return "hold";
	case TG_REJECT:		return "reject";
	}
	return "none";
}

/*
 * Map stored frame value to a bit character
 */
static char
statusBitChar ( int clocktype, int value )
{
	switch ( clocktype )
	{
	case CLOCKTYPE_DCF77:
		switch ( value )
		{
		case 0:	return '-';	//missing second 59
		case 1:	return '0';
		case 2:	return '1';
		}
		break;
	case CLOCKTYPE_MSF:
		switch ( value )
		{
		case 1:		return '0';	//A=0 B=0
		case 2:		return '1';	//A=1 B=0
		case 11:	return '2';	//A=0 B=1
		case 3:		return '3';	//A=1 B=1
		case 5:		return 'M';	//minute marker
		}
		break;
	case CLOCKTYPE_WWVB:
		switch ( value )
		{
		case 2:	return '0';
		case 5:	return '1';
		case 8:	return 'M';	//marker
		}
		break;
	}
	return '?';
}

/*
 * Guess signal polarity from recent pulse/gap lengths. Correct polarity gives
 * short pulses followed by long gaps; inverted polarity gives the opposite.
 */
static const char*
statusPolarityHint ( const clkInfoT* clock )
{
	int	i, normal = 0, inverted = 0;

	for ( i=0; i<clock->pulseCount; i++ )
	{
		const clkPulseT* p = &clock->pulses[i];
		if ( p->pulseMs < 0 || p->gapMs < 0 )
			continue;
		if ( p->pulseMs <= 300 && p->gapMs >= 700 )
			normal++;
		else if ( p->pulseMs >= 700 && p->gapMs <= 300 )
			inverted++;
	}

	if ( normal + inverted < 10 )
		return "unknown";
	if ( inverted > 2 * normal )
		return "inverted";
	if ( normal > 2 * inverted )
		return "normal";
	return "unknown";
}

static void
statusWriteString ( FILE* f, const char* s )
{
	fputc ( '"', f );
	for ( ; *s != 0; s++ )
	{
		if ( *s == '"' || *s == '\\' )
			fprintf ( f, "\\%c", *s );
		else if ( (unsigned char)*s < 0x20 )
			fprintf ( f, "\\u%04x", *s );
		else
			fputc ( *s, f );
	}
	fputc ( '"', f );
}

static void
statusWriteAge ( FILE* f, const char* key, time_f t, time_f now )
{
	if ( t != 0 )
		fprintf ( f, "\"%s\":%.3f,", key, now - t );
	else
		fprintf ( f, "\"%s\":null,", key );
}

void
statusWrite ( const char* path, const char* name, int unit, const serDevT* dev, clkInfoT* clock )
{
	char		tmppath[256];
	FILE*		f;
	struct timeval	tv;
	struct timespec	ts;
	time_f		now, average, deviation;
	int		i, samples;

	clock->statusDirty = 0;

	snprintf ( tmppath, sizeof(tmppath), "%s.tmp", path );
	f = fopen ( tmppath, "w" );
	if ( f == NULL )
	{
		loggerf ( LOGGER_DEBUG, "status: cannot create %s\n", tmppath );
		return;
	}

	gettimeofday ( &tv, NULL );
	timeval2time_f ( &tv, now );

	fprintf ( f, "{\"version\":\"%s\",\"pid\":%d,\"timestamp\":%.6f,\"unit\":%d,\"device\":", VERSION, (int)getpid(), now, unit );
	statusWriteString ( f, name );
	fprintf ( f, ",\"type\":\"%s\",\"inverted\":%s,\"fudge\":%.6f,\"state\":\"%s\",\"level\":%d,",
		statusTypeName(clock), clock->inverted ? "true" : "false", clock->fudgeoffset,
		statusRxStateName(clock->rx_state), clock->level );
	fprintf ( f, "\"lastEdge\":%.6f,", clock->lastEdge );
	statusWriteAge ( f, "lastEdgeAge", clock->lastEdge, now );
	fprintf ( f, "\"glitches\":%lu,\"polarityHint\":\"%s\",", dev->glitches, statusPolarityHint(clock) );

	//frame being collected
	fprintf ( f, "\"frame\":{\"synced\":%s,\"position\":%d,\"bits\":\"", clock->frameSynced ? "true" : "false", clock->numdata );
	for ( i=0; i<clock->numdata; i++ )
		fputc ( statusBitChar ( clock->clocktype, clock->data[i] ), f );
	fprintf ( f, "\"}," );

	//last successful decode
	if ( clock->lastDecodeTime != 0 )
	{
		fprintf ( f, "\"lastDecode\":{\"time\":%.6f,", clock->lastDecodeTime );
		statusWriteAge ( f, "age", clock->lastDecodeTime, now );
		fprintf ( f, "\"radioTime\":%.6f,\"dst\":%s,\"dstAnnounce\":%s,\"leapAnnounce\":%s,\"backupAntenna\":%s},",
			clock->radiotime,
			(clock->radioflags & CLKFLAG_DST) ? "true" : "false",
			(clock->radioflags & CLKFLAG_DST_ANNOUNCE) ? "true" : "false",
			(clock->radioflags & CLKFLAG_LEAP_ANNOUNCE) ? "true" : "false",
			(clock->radioflags & CLKFLAG_BACKUP_ANTENNA) ? "true" : "false" );
	}
	else
	{
		fprintf ( f, "\"lastDecode\":null," );
	}

	//last error
	if ( clock->lastError != CLKERR_NONE )
	{
		fprintf ( f, "\"lastError\":{\"code\":%d,\"text\":\"%s\",\"time\":%.6f,", clock->lastError, clkErrorName(clock->lastError), clock->lastErrorTime );
		statusWriteAge ( f, "age", clock->lastErrorTime, now );
		fprintf ( f, "\"value\":%.6f},", clock->lastErrorValue );
	}
	else
	{
		fprintf ( f, "\"lastError\":null," );
	}

	fprintf ( f, "\"stats\":{\"edges\":%lu,\"pulsesGood\":%lu,\"pulsesBad\":%lu,\"gapsBad\":%lu,\"framesDecoded\":%lu,\"framesFailed\":%lu,"
		"\"guardRejects\":%lu,\"guardHolds\":%lu,\"clockSteps\":%lu},",
		clock->stats.edges, clock->stats.pulsesGood, clock->stats.pulsesBad, clock->stats.gapsBad,
		clock->stats.framesDecoded, clock->stats.framesFailed,
		clock->stats.guardRejects, clock->stats.guardHolds, clock->stats.clockSteps );

	//PPS offset statistics
	samples = 0;
	for ( i=0; i<PPS_AVERAGE_COUNT; i++ )
		if ( clock->ppslist[i].pctime != 0 && clock->ppslist[i].radiotime != 0 )
			samples++;
	fprintf ( f, "\"pps\":{\"samples\":%d,\"required\":%d,", samples, PPS_AVERAGE_COUNT );
	if ( clkCalculatePPSAverage ( clock, &average, &deviation ) == 0 )
		fprintf ( f, "\"offset\":%.6f,\"deviation\":%.6f},", average, deviation );
	else
		fprintf ( f, "\"offset\":null,\"deviation\":null}," );

	fprintf ( f, "\"lastFed\":%.6f,", clock->lastFedTime );
	statusWriteAge ( f, "lastFedAge", clock->lastFedTime, now );

	//time guard
	clock_gettime ( CLOCK_MONOTONIC_RAW, &ts );
	fprintf ( f, "\"guard\":{\"enabled\":%s,\"phase\":\"%s\",\"confirmed\":%d,\"confirmCount\":%d,\"lastVerdict\":\"%s\",",
		clock->guard.enabled ? "true" : "false", statusPhaseName(clock->guard.phase),
		clock->guard.confirmed, clock->guard.confirmCount, statusVerdictName(clock->lastVerdict) );
	if ( clock->lastVerdict != CLK_VERDICT_NONE )
		fprintf ( f, "\"lastOffset\":%.6f,", clock->lastVerdictOffset );
	else
		fprintf ( f, "\"lastOffset\":null," );
	if ( clock->guard.haveAnchor )
		fprintf ( f, "\"anchorAge\":%.3f},", ((time_f)ts.tv_sec + (time_f)ts.tv_nsec / 1e9) - clock->guard.anchorMono );
	else
		fprintf ( f, "\"anchorAge\":null}," );

	//recent pulses, oldest first: [start, pulse ms, gap ms, value]
	fprintf ( f, "\"pulses\":[" );
	for ( i=0; i<clock->pulseCount; i++ )
	{
		const clkPulseT* p = &clock->pulses[(clock->pulseIndex - clock->pulseCount + i + PULSE_HISTORY_SIZE) % PULSE_HISTORY_SIZE];
		fprintf ( f, "%s[%.3f,%d,%d,%d]", (i > 0) ? "," : "", p->start, p->pulseMs, p->gapMs, p->value );
	}
	fprintf ( f, "]}\n" );

	if ( fclose ( f ) != 0 || rename ( tmppath, path ) != 0 )
	{
		loggerf ( LOGGER_DEBUG, "status: cannot write %s\n", path );
		unlink ( tmppath );
	}
}
