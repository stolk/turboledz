#include <assert.h>	// for assert()
#include <stdio.h>	// for fopen()
#include <stdlib.h>	// for atoi()
#include <inttypes.h>	// for uint64_t;

#if defined(_WIN32)
#	include <Windows.h>
#	include <powerbase.h>	// For CallNtPowerInformation()

#else
#	include <unistd.h>	// for sysconf()
#endif

#include "cpuinf.h"

#if 1
#undef assert

_ACRTIMP void __cdecl _wassert(
	_In_z_ wchar_t const* _Message,
	_In_z_ wchar_t const* _File,
	_In_   unsigned       _Line
);

#define assert(expression) (void)(                                                       \
            (!!(expression)) ||                                                              \
            (_wassert(_CRT_WIDE(#expression), _CRT_WIDE(__FILE__), (unsigned)(__LINE__)), 0) \
        )
#endif

int	cpuinf_freq_min[CPUINF_MAX];
int	cpuinf_freq_bas[CPUINF_MAX];
int	cpuinf_freq_max[CPUINF_MAX];
int cpuinf_coreid  [CPUINF_MAX];


FILE*	cpuinf_freq_cur_file[CPUINF_MAX];

int	cpuinf_num_virtual_cores;
int	cpuinf_num_physical_cores;


static int cpuinf_examine_cores(FILE* f, int num_cpus)
{
	DWORD length = 0;
	GetLogicalProcessorInformationEx(RelationProcessorCore, 0, &length);
	assert(GetLastError() == ERROR_INSUFFICIENT_BUFFER);

	uint8_t buffer[length];
	SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* info = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)buffer;

	const int result = GetLogicalProcessorInformationEx(RelationProcessorCore, info, &length);
	assert(result);

	int num_phys = 0;
	size_t offset = 0;
	int smt[num_cpus];
	do 
	{
		const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* current_info =
			(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(buffer + offset);
		offset += current_info->Size;
		assert(current_info->Relationship == RelationProcessorCore);
		smt[num_phys] = current_info->Processor.Flags == LTP_PC_SMT ? 1 : 0;
		const int grpcnt = current_info->Processor.GroupCount;
		assert(grpcnt == 1);
		fflush(f);
		for (int g = 0; g < grpcnt; ++g)
		{
			int coreid = 64 * g;
			KAFFINITY aff = current_info->Processor.GroupMask[g].Mask;
			fprintf(f, "aff %d: 0x%llx\n", num_phys, aff);
			fflush(f);
			while (!(aff & 1))
			{
				aff = aff >> 1;
				coreid++;
			}
			cpuinf_coreid[coreid] = coreid;
		}
		++num_phys;
	} while (offset < length);

	// If we have a mixed system with both SMT cores and non-SMT cores, we give up!
	int totalsmt = 0;
	for (int i = 0; i < num_phys; ++i)
		totalsmt += smt[i];

	if (totalsmt > 0 && totalsmt < num_phys)
		fprintf(f, "Hmm... both HT and non-HT cores in this system!\n");

	return num_phys;
}


// Returns the number of virtual cores.
int cpuinf_init(void)
{
	FILE* f = 0;
#if !defined(STANDALONECPUINF)
	f = fopen("c:/temp/cpuinf.log", "wb");
#endif
	if (!f) f = stdout;

	// How many virtual cores in this system?
	SYSTEM_INFO sysinf;
	GetSystemInfo(&sysinf);
	const int num_cpus = sysinf.dwNumberOfProcessors;
	assert( num_cpus <= CPUINF_MAX );
	assert( num_cpus >= 1 );

	cpuinf_num_virtual_cores  = num_cpus;
	cpuinf_num_physical_cores = cpuinf_examine_cores(f, num_cpus);

	for ( int i=0; i<num_cpus; ++i )
	{
		cpuinf_freq_min[i] = 0;
		cpuinf_freq_max[i] = 0;
		cpuinf_freq_bas[i] = 0;
		fprintf
		(
			f, "cpu %2d (%s)\n", 
			i,
			cpuinf_coreid[i] == i ? "primary" : "secondary"
		);
	}

	fprintf( f, "Number of virtual cores:  %2d\n", cpuinf_num_virtual_cores );
	fprintf( f, "Number of physical cores: %2d\n", cpuinf_num_physical_cores);

	enum freq_stage stages[cpuinf_num_physical_cores];
	cpuinf_get_cur_freq_stages(stages, cpuinf_num_physical_cores);
#if !defined(STANDALONECPUINF)
	fclose(f);
#endif

	return num_cpus;
}


static int cpuinf_get_cur_freq_stage( ULONG low, ULONG cur, ULONG max )
{
	const ULONG range = max - low;
	const ULONG th0 = low + range/4;
	const ULONG th1 = low + range/2;
	const ULONG th2 = max - range/4;
	if ( cur >= th2 )
		return FREQ_STAGE_MAX;
	else if ( cur > th1 )
		return FREQ_STAGE_MID;
	else if ( cur > th0 )
		return FREQ_STAGE_LOW;
	else
		return FREQ_STAGE_MIN;
}


// Oh, Microsoft.
// "This will be corrected in the future."
// Well, the future is calling.
typedef struct _PROCESSOR_POWER_INFORMATION
{
	ULONG Number;
	ULONG MaxMhz;
	ULONG CurrentMhz;
	ULONG MhzLimit;
	ULONG MaxIdleState;
	ULONG CurrentIdleState;
} PROCESSOR_POWER_INFORMATION, * PPROCESSOR_POWER_INFORMATION;

static 	PROCESSOR_POWER_INFORMATION pinf[CPUINF_MAX];

int cpuinf_get_cur_freq_stages( enum freq_stage* stages, int sz )
{
	const NTSTATUS gotinfo = CallNtPowerInformation
	(
		ProcessorInformation,
		NULL,
		0,
		pinf,
		sizeof(pinf)
	);
	assert(gotinfo != ERROR_INSUFFICIENT_BUFFER);
	assert(gotinfo != ERROR_ACCESS_DENIED);
	assert(gotinfo == ERROR_SUCCESS);

	int cnt = 0;
	for ( int i=0; i<cpuinf_num_virtual_cores; ++i )
		if (cpuinf_coreid[i] == i && cnt < sz)
		{
			stages[cnt] = cpuinf_get_cur_freq_stage(0, pinf[i].CurrentMhz, pinf[i].MaxMhz);
#if defined(STANDALONECPUINF)
			fprintf(stdout, "virtual core %d cur:%luMHz stage:%d\n", i, pinf[i].CurrentMhz, stages[cnt]);
#endif
			cnt++;
		}
	return cnt;
}


// Reads for each cpu: how many jiffies were spent in each state:
//   user, nice, system, idle, iowait, irq, softirq
void cpuinf_get_usages( int num, float* usages )
{
	static uint64_t t_prev[3];
	static uint64_t t_curr[3];
	static int first = 1;

	int res = GetSystemTimes((FILETIME*)t_curr + 0, (FILETIME*)t_curr + 1, (FILETIME*)t_curr + 2);
	(void)res;
	assert(res);
	if (first)
	{
		memcpy(t_prev, t_curr, sizeof(t_prev));
		usages[0] = 0.0f;
		first = 0;
		return;
	}
	uint64_t deltas[3] =
	{
		t_curr[0] - t_prev[0],
		t_curr[1] - t_prev[1],
		t_curr[2] - t_prev[2],
	};
	uint64_t t_kern = deltas[1] - deltas[0];
	uint64_t t_user = deltas[2];
	uint64_t t_tota = deltas[1] + deltas[2];

	usages[0] = (t_kern + t_user) / (float) (t_tota);
	memcpy(t_prev, t_curr, sizeof(t_prev));
}

#if defined(STANDALONECPUINF)
int main(int argc, char* argv[])
{
	cpuinf_init();
	return 0;
}
#endif