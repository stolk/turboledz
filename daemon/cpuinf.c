#include <assert.h>	// for assert()
#include <stdio.h>	// for fopen()
#include <stdlib.h>	// for atoi()
#include <unistd.h>	// for sysconf()

#include "cpuinf.h"

int	cpuinf_freq_min[CPUINF_MAX];
int	cpuinf_freq_bas[CPUINF_MAX];
int	cpuinf_freq_max[CPUINF_MAX];
int	cpuinf_coreid  [CPUINF_MAX];

FILE*	cpuinf_freq_cur_file[CPUINF_MAX];


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


int cpuinf_init(void)
{
	// How many cores in this system?
	const int num_cpus = sysconf( _SC_NPROCESSORS_ONLN );
	fprintf( stderr, "Found %d cpus.\n", num_cpus );
	assert( num_cpus <= CPUINF_MAX );

	for ( int i=0; i<num_cpus; ++i )
	{
		cpuinf_freq_min[i] = get_cpu_stat( i, "scaling_min_freq" );
		cpuinf_freq_max[i] = get_cpu_stat( i, "scaling_max_freq" );
		cpuinf_freq_bas[i] = get_cpu_stat( i, "base_frequency" );
		cpuinf_freq_cur_file[i] = get_cpu_stat_file( i, "scaling_cur_freq" );
		cpuinf_coreid[i] = get_cpu_coreid( i );
		fprintf
		(
			stderr, "cpu %d (core %d) minfreq: %d basefreq: %d maxfreq: %d\n", 
			i,
			cpuinf_coreid[i],
			cpuinf_freq_min[i],
			cpuinf_freq_bas[i],
			cpuinf_freq_max[i]
		);
	}

	return num_cpus;
}


int cpuinf_cur_freq( int cpunr )
{
	FILE* f = cpuinf_freq_cur_file[ cpunr ];
	assert( f );
	char line[128];
	const int numread = fread( line, 1, sizeof(line), f );
	assert( numread > 0 );
	rewind( f );
	return atoi( line );
}

