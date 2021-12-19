#include <assert.h>	// for assert()
#include <stdio.h>	// for fopen()
#include <stdlib.h>	// for atoi()
//#include <unistd.h>	// for sysconf()

#include "cpuinf.h"

int	cpuinf_freq_min[CPUINF_MAX];
int	cpuinf_freq_bas[CPUINF_MAX];
int	cpuinf_freq_max[CPUINF_MAX];
int	cpuinf_coreid  [CPUINF_MAX];

FILE*	cpuinf_freq_cur_file[CPUINF_MAX];

int	cpuinf_num_virtual_cores;
int	cpuinf_num_physical_cores;




// NOTE: On a 8-core 16-thread hyperthreading machine, cpu15 has typically coreid 7.
static int get_cpu_coreid( int cpu )
{
	return cpu;
}


// Returns the number of virtual cores.
int cpuinf_init(void)
{
	// How many cores in this system?
	const int num_cpus = 12; // TODO: replace
	assert( num_cpus <= CPUINF_MAX );

	int maxcoreid=-1;
	for ( int i=0; i<num_cpus; ++i )
	{
		cpuinf_freq_min[i] = 0;
		cpuinf_freq_max[i] = 0;
		cpuinf_freq_bas[i] = 0;
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
	return 0;
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


// Reads for each cpu: how many jiffies were spent in each state:
//   user, nice, system, idle, iowait, irq, softirq
void cpuinf_get_usages( int num, float* usages )
{
	usages[0] = 1.0f;
}

