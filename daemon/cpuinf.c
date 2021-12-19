#include <assert.h>	// for assert()
#include <stdio.h>	// for fopen()
#include <stdlib.h>	// for atoi()
#include <unistd.h>	// for sysconf()
#include <inttypes.h>	// for uint32_t
#include <string.h>	// for memset()

#include "cpuinf.h"

int	cpuinf_freq_min[CPUINF_MAX];
int	cpuinf_freq_bas[CPUINF_MAX];
int	cpuinf_freq_max[CPUINF_MAX];
int	cpuinf_coreid  [CPUINF_MAX];

FILE*	cpuinf_freq_cur_file[CPUINF_MAX];

int	cpuinf_num_virtual_cores;
int	cpuinf_num_physical_cores;


static const char* get_cpu_stat_filename( int cpu, const char* name )
{
	static char fname[256];
	snprintf( fname, sizeof(fname), "/sys/devices/system/cpu/cpufreq/policy%d/%s", cpu, name );
	return fname;
}


static FILE* get_cpu_stat_file( int cpu, const char* name )
{
	const char* fname = get_cpu_stat_filename( cpu, name );
	FILE* f = fopen( fname, "rb" );
	return f;
}


static int get_cpu_stat( int cpu, const char* name )
{
	FILE* f = get_cpu_stat_file( cpu, name );
	if ( !f )
		return -1;
	char line[128];
	const int numread = fread( line, 1, sizeof(line), f );
	assert( numread > 0 );
	fclose( f );
	return atoi( line );
}


// NOTE: On a 8-core 16-thread hyperthreading machine, cpu15 has typically coreid 7.
static int get_cpu_coreid( int cpu )
{
	char fname[128];
	char line [128];
	snprintf( fname, sizeof(fname), "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu );
	FILE* f = fopen( fname, "rb" );
	if ( !f ) return -1;
	const int numread = fread( line, 1, sizeof(line), f );
	assert( numread > 0 );
	fclose(f);
	return atoi( line );
}


// Returns the number of virtual cores.
int cpuinf_init(void)
{
	// How many cores in this system?
	const int num_cpus = sysconf( _SC_NPROCESSORS_ONLN );
	assert( num_cpus <= CPUINF_MAX );

	int maxcoreid=-1;
	for ( int i=0; i<num_cpus; ++i )
	{
		cpuinf_freq_min[i] = get_cpu_stat( i, "scaling_min_freq" );
		cpuinf_freq_max[i] = get_cpu_stat( i, "scaling_max_freq" );
		cpuinf_freq_bas[i] = get_cpu_stat( i, "base_frequency" );
		cpuinf_freq_cur_file[i] = get_cpu_stat_file( i, "scaling_cur_freq" );
		cpuinf_coreid[i] = get_cpu_coreid( i );
		maxcoreid = maxcoreid < cpuinf_coreid[i] ? cpuinf_coreid[i] : maxcoreid;
		fprintf
		(
			stderr, "cpu %2d (core %2d)  minfreq: %4dMHz  basefreq: %4dMHz  maxfreq: %4dMHz\n", 
			i,
			cpuinf_coreid[i],
			cpuinf_freq_min[i]/1000,
			cpuinf_freq_bas[i]/1000,
			cpuinf_freq_max[i]/1000
		);
	}
	cpuinf_num_virtual_cores = num_cpus;
	cpuinf_num_physical_cores = maxcoreid+1;

	fprintf( stderr, "Number of virtual cores:  %2d\n", cpuinf_num_virtual_cores );
	fprintf( stderr, "Number of physical cores: %2d\n", cpuinf_num_physical_cores);

	return num_cpus;
}


static int cpuinf_get_cur_freq( int cpunr )
{
	FILE* f = cpuinf_freq_cur_file[ cpunr ];
	assert( f );
	char line[128];
	const int numread = fread( line, 1, sizeof(line), f );
	assert( numread > 0 );
	rewind( f );
	return atoi( line );
}


static int cpuinf_get_cur_freq_stage( int cpunr )
{
	const int lo = cpuinf_freq_min[cpunr];
	const int hi = cpuinf_freq_max[cpunr];
	const int range = hi - lo;
	const int th0 = lo + range/4;
	const int th1 = lo + range/2;
	const int th2 = hi - range/4;
	const int cur = cpuinf_get_cur_freq( cpunr );
	if ( cur >= th2 )
		return FREQ_STAGE_MAX;
	else if ( cur > th1 )
		return FREQ_STAGE_MID;
	else if ( cur > th0 )
		return FREQ_STAGE_LOW;
	else
		return FREQ_STAGE_MIN;
}


int cpuinf_get_cur_freq_stages( enum freq_stage* stages, int sz )
{
	int cnt = 0;
	for ( int i=0; i<cpuinf_num_virtual_cores; ++i )
		if ( cpuinf_coreid[i] == i && cnt<sz )
			stages[cnt++] = cpuinf_get_cur_freq_stage( i );
	return cnt;
}



static uint32_t* prev=0;	// Per cpu, a set of 7 Jiffies counts.
static uint32_t* curr=0;	// Per cpu, a set of 7 Jiffies counts.

// Reads for each cpu: how many jiffies were spent in each state:
//   user, nice, system, idle, iowait, irq, softirq
void cpuinf_get_usages( int num, float* usages )
{
	// First invokation, we should allocate buffers, sized to the number of CPUs in this system.
	if ( !prev || !curr )
	{
		const size_t sz = sizeof(uint32_t) * 7 * num;
		prev = (uint32_t*) malloc(sz);
		curr = (uint32_t*) malloc(sz);
		memset( prev, 0, sz );
		memset( curr, 0, sz );
	}

	static FILE* f = 0;
	if ( !f )
	{
		f = fopen( "/proc/stat", "rb" );
		assert(f);
	}
	char info[16384];
	const size_t numr = fread( info, 1, sizeof(info)-1, f );
	rewind(f);

	assert( numr < sizeof(info) );
	info[numr] = 0;

	for ( int cpu=0; cpu<num; ++cpu )
	{
		char tag[16];
		strncpy( tag,"cpu ", sizeof(tag) );

		uint32_t* prv = prev + cpu * 7;
		uint32_t* cur = curr + cpu * 7;

		// If num is larger than 1, we should ready per-cpu stats, instead of the aggragate stat.
		if ( num > 1 )
			snprintf( tag, sizeof(tag), "cpu%d", cpu );

		const char* s = strstr( info, tag );
		assert( s );

		if ( num > 1 )
		{
			// Read cpu specific stats.
			int cpunr;
			const int numscanned = sscanf( s, "cpu%d %u %u %u %u %u %u %u", &cpunr, cur+0, cur+1, cur+2, cur+3, cur+4, cur+5, cur+6 );
			assert( numscanned == 8 );
			assert( cpunr == cpu );
		}
		else
		{
			// Only read the aggregate stat, we don't need the break-out per cpu.
			const int numscanned = sscanf( s, "cpu %u %u %u %u %u %u %u", cur+0, cur+1, cur+2, cur+3, cur+4, cur+5, cur+6 );
			assert( numscanned == 7 );
		}

		uint32_t deltas[7];
		for ( int i=0; i<7; ++i )
		{
			deltas[i] = cur[i] - prv[i];
			prv[i] = cur[i];
		}
		const uint32_t user = deltas[0];
		const uint32_t syst = deltas[2];
		const uint32_t idle = deltas[3];
		uint32_t work = user + syst;
		usages[ cpu ] = work / (float) (user+syst+idle);
	}
}

