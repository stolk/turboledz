
#define CPUINF_MAX	128

enum freq_stage
{
	FREQ_STAGE_MIN=0,	// minimal freq: no light.
	FREQ_STAGE_LOW,		// below nominal: green light.
	FREQ_STAGE_MID,		// nomimal, or higher: orange light.
	FREQ_STAGE_MAX		// turbo boost: red light.
};

// cpuinf data.

extern int	cpuinf_freq_min[CPUINF_MAX];
extern int	cpuinf_freq_bas[CPUINF_MAX];
extern int	cpuinf_freq_max[CPUINF_MAX];
extern int	cpuinf_coreid  [CPUINF_MAX];

extern FILE*	cpuinf_freq_cur_file[CPUINF_MAX];

extern int	cpuinf_num_virtual_cores;
extern int	cpuinf_num_physical_cores;


// Initialize the cpuinf system. Returns nr of virtual cores.
extern int cpuinf_init(void);

// Gets the current freq stage of all the physical cores.
extern int cpuinf_get_cur_freq_stages( enum freq_stage* stages, int sz );

