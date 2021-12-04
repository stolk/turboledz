
#define CPUINF_MAX	128

extern int	cpuinf_freq_min[CPUINF_MAX];
extern int	cpuinf_freq_bas[CPUINF_MAX];
extern int	cpuinf_freq_max[CPUINF_MAX];

extern int	cpuinf_coreid  [CPUINF_MAX];

extern FILE*	cpuinf_freq_cur_file[CPUINF_MAX];

extern int cpuinf_init(void);

extern int cpuinf_get_cur_freq(int cpunr);

