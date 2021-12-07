//
// turboledzd.c
//
// Daemon for the Turbo LEDz devices.
// (c)2021 Game Studio Abraham Stolk Inc.
//

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <wchar.h>		// hidapi uses wide characters.
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <float.h>
#include <errno.h>
#include <sysexits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <hidapi/hidapi.h>

#include "cpuinf.h"


// So far, all Turbo LEDz devices made are based on Arduino Pro Micro, so we don't need to check for Adafruit SAMD devices.
// If this changes in the future, we should check for devices other than Arduino Pro Micro too.
#define TRY_ADAFRUIT_DEVICES	0

// More than 6 Turbo LEDz devices in a single PC would be silly.
#define MAXDEVS			6

enum model
{
	MODEL_UNKNOWN=0,
	MODEL_108m,		// 10 bars of 8 segments, mini size (3.5" drive bay.)
	MODEL_108,		// 10 bars of 8 segments.
	MODEL_810,		// 10 bars of 8 segments.
	MODEL_810s,		// 8 bars of 10 segments, single driver.
	MODEL_810c,		// 8 bars of 10 segments, colour LEDs.
};

// Howmany Turbo LEDz devices did we find?
static int		numdevs;

// Which Turbo LEDz devices did we find?
static hid_device*	hds[MAXDEVS];

// What model numbers are the devices that we found?
static enum model	mod[MAXDEVS];

// What number of segments do the LED bars have for this device?
static int		seg[MAXDEVS];

// Number of (virtual) cores this host PC has.
static int		numcpu=0;

// Specified in config file: update frequency in Hertz.
static int		opt_freq=10;

// Specified in config file: currently only "cpu" is implemented.
static char		opt_mode[80];

// Specified in config file: how long do we wait before operations, to give udev daemon time to apply rules.
static int		opt_launchpause;

// When paused, we don't collect data, nor send it to the device.
static int		paused=0;


static void cleanup(void)
{
	for ( int i=0; i<numdevs; ++i )
	{
		hid_device* hd = hds[i];
		hid_close(hd);
		hds[i] = 0;
	}
	hid_exit();
	numdevs=0;
}


// A systemd daemon needs to be able to re-read its config on SIGHUP, so we do that here.
static int read_config(void)
{
	const char* fname = "/etc/turboledz.conf";
	FILE* f = fopen( fname, "r" );
	if ( !f )
	{
		fprintf( stderr, "Config file '%s' not found.\n", fname );
		return 0;
	}
	char line[1024];
	int parsed=0;
	while ( 1 )
	{
		char* s = fgets( line, sizeof(line)-1, f );
		if ( !s )
		{
			fprintf( stderr, "Parsed %d options from config file.\n", parsed );
			return parsed;
		}
		if ( strstr( s, "=" ) )
		{
			if ( s[0] != '#' )
			{
				const size_t l = strlen(s);
				if ( l>0 && s[l-1]=='\n' )
					s[l-1] = 0;
				if ( !strncmp( s, "freq=", 5 ) )
				{
					int freq = atoi( s+5 );
					if ( freq > 0 && freq <= 100 )
						opt_freq = freq;
					parsed++;
				}
				if ( !strncmp( s, "mode=", 5 ) )
				{
					strncpy( opt_mode, s+5, sizeof(opt_mode)-1 );
					parsed++;
				}
				if ( !strncmp( s, "launchpause=", 12 ) )
				{
					opt_launchpause = atoi( s+12 );
					parsed++;
				}
			}
		}
	}
	return parsed;
}


static void sig_handler( int signum )
{
	if ( signum == SIGHUP )
	{
		// Re-read the configution file!
		read_config();
	}
	if ( signum == SIGTERM || signum == SIGINT )
	{
		// Shut down the daemon and exit cleanly.
		fprintf(stderr, "Attempting to close down gracefully...\n");
		cleanup();
		exit(0);
	}
	if ( signum == SIGUSR1 )
	{
		// This signals that the host is about to go to sleep / suspend.
		paused = 1;
		fprintf( stderr, "Preparing to go to sleep...\n" );
		const uint8_t rep[2] = { 0x00, 0x40 };
		usleep(40000);
		for ( int i=0; i<numdevs; ++i )
		{
			hid_device* hd = hds[i];
			const int written = hid_write( hd, rep, sizeof(rep) );
			if (written<0)
				fprintf( stderr,"hid_write() failed for %zu bytes with: %ls\n", sizeof(rep), hid_error(hd) );
		}
		usleep(40000);
	}
	if ( signum == SIGUSR2 )
	{
		fprintf( stderr, "Woken up.\n" );
		paused = 0;
	}
}


static enum model get_model(const wchar_t* modelname)
{
	if ( !wcscmp( modelname, L"810c" ) )
		return MODEL_810c;
	if ( !wcscmp( modelname, L"810s" ) )
		return MODEL_810s;
	if ( !wcscmp( modelname, L"810" ) )
		return MODEL_810;
	if ( !wcscmp( modelname, L"108m" ) )
		return MODEL_108m;
	if ( !wcscmp( modelname, L"108" ) )
		return MODEL_108;
	return MODEL_UNKNOWN;
}


static int get_permissions( const char* fname )
{
	struct stat statRes;
	const int r = stat( fname, &statRes );
	if ( r )
	{
		fprintf(stderr,"Error: stat() failed with %s on %s\n", strerror(errno), fname);
		exit(EX_NOINPUT);
	}
	const mode_t bits = statRes.st_mode;
	return bits;
}


// We are passed a set of HID devices that matched our enumeration.
// Here, we should examine them, and select one to open.
static int select_and_open_device( struct hid_device_info* devs )
{
	hid_device* handle = 0;
	struct hid_device_info* cur_dev = devs;
	int count=0;
	const char* filenames[16]={ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, };
	enum model models[16];
	int rv=0;

	while (cur_dev)
	{
		fprintf(stderr,"type: %04hx %04hx\n  path: %s\n  serial_number: %ls\n", cur_dev->vendor_id, cur_dev->product_id, cur_dev->path, cur_dev->serial_number);
		fprintf(stderr,"  Manufacturer: %ls\n", cur_dev->manufacturer_string);
		fprintf(stderr,"  Product:      %ls\n", cur_dev->product_string);
		fprintf(stderr,"  Release:      %hx\n", cur_dev->release_number);
		fprintf(stderr,"  Interface:    %d\n",  cur_dev->interface_number);
		fprintf(stderr,"  Usage (page): 0x%hx (0x%hx)\n", cur_dev->usage, cur_dev->usage_page);
		if ( cur_dev->product_string )
		{
			if ( !wcsncmp( cur_dev->product_string, L"Turbo LEDz", 10 ) )
			{
				const wchar_t* prodname = cur_dev->product_string+11;
				fprintf(stderr,"Detected model: %ls\n", prodname);
				if ( count<16 )
				{
					filenames[count] = cur_dev->path;
					models[count] = get_model(prodname);
					count++;
				}
			}
		}
		else
		{
			fprintf(stderr,"Skipped %s for lack of product name.\n", cur_dev->path);
		}
		cur_dev = cur_dev->next;
	}

	fprintf( stderr, "Found %d Turbo LEDz devices.\n", count );

	for ( int i=0; i<count; ++i )
	{
		// First check if permissions are good on the /dev/rawhidX file.
		const char* fname = filenames[i];
		int attemptnr=0;
		int valid=0;
		// We do this multiple times, because I find that sometimes the udev rule is too late during boot.
		while( !valid )
		{
			const mode_t bits = get_permissions( fname );
			if ( ( bits & S_IROTH ) && ( bits & S_IWOTH ) )
			{
				valid = 1;
			}
			else
			{
				if ( attemptnr == 3 )
				{
					fprintf
					(
						stderr,
						"Error: No rw-permission for %s which has permission %04o. Retrying...\n",
						fname, bits
					);
					exit(EX_NOPERM);
				}
				attemptnr++;
				sleep(1);
			}
		}
		// We can open it.
		handle = hid_open_path( filenames[i] );
		if (!handle)
		{
			fprintf(stderr,"Error: hid_open_path() on %s failed : %ls\n", fname, hid_error(handle));
			exit(EX_IOERR);
		}
		fprintf(stderr,"Opened hid device at %s\n", fname);
		// We like to be blocked.
		hid_set_nonblocking(handle, 0);
		if ( numdevs < MAXDEVS )
		{
			hds[ numdevs ] = handle;
			mod[ numdevs ] = models[i];
			seg[ numdevs ] = (models[i] == MODEL_108 || models[i] == MODEL_108m) ? 8 : 10;
			numdevs++;
			rv++;
		}
	}

	return rv;	// return the nr of devices we opened.
}


static uint32_t* prev=0;	// Per cpu, a set of 7 Jiffies counts.
static uint32_t* curr=0;	// Per cpu, a set of 7 Jiffies counts.

// Reads for each cpu: how many jiffies were spent in each state:
//   user, nice, system, idle, iowait, irq, softirq
static void get_usages(int num, float* usages)
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
		uint32_t totaldelta=0;
		for ( int i=0; i<7; ++i )
		{
			deltas[i] = cur[i] - prv[i];
			prv[i] = cur[i];
			if ( i != 2 )
				totaldelta += deltas[i];
		}
		const uint32_t user = deltas[0];
		const uint32_t syst = deltas[2];
		const uint32_t idle = deltas[3];
		uint32_t work = user + syst;
		usages[ cpu ] = work / (float) (user+syst+idle);
	}
}


int service( void )
{
	const int delay = 1000000 / opt_freq;	// uSeconds to wait between writes.
	while ( 1 )
	{
		if ( !paused )
		{
			float usages[ numcpu ];
			get_usages( 1, usages );
			for ( int i=0; i<numdevs; ++i )
			{
				hid_device* hd = hds[i];
				if ( mod[i] == MODEL_810c )
				{
					enum freq_stage stages[ CPUINF_MAX ];
					const int numfr = cpuinf_get_cur_freq_stages( stages, CPUINF_MAX );
					uint32_t grn=0;
					uint32_t red=0;
					uint32_t bit=1;
					for ( int i=0; i<numfr; ++i )
					{
						enum freq_stage s = stages[i];
						grn |= ( (s==FREQ_STAGE_LOW || s==FREQ_STAGE_MID) ? bit : 0 );
						red |= ( (s==FREQ_STAGE_MID || s==FREQ_STAGE_MAX) ? bit : 0 );
						bit = bit<<1;
					}
					uint8_t rep[5] =
					{
						0x00,
						((grn>>0) & 0x1f) | 0x80,
						((grn>>5) & 0x1f),
						((red>>0) & 0x1f),
						((red>>5) & 0x1f),
					};
					const int written = hid_write( hd, rep, sizeof(rep) );
					if ( written < 0 )
					{
						fprintf( stderr, "hid_write for %zu bytes failed with: %ls\n", sizeof(rep), hid_error(hd) );
						cleanup();
						exit(EX_IOERR);
					}
				}
				else
				{
					int bars = (int) ( 0.5f + ( (seg[i]-FLT_EPSILON) * usages[0] ) );
					uint8_t rep[2] = { 0x00, bars | 0x80 };
					const int written = hid_write( hd, rep, sizeof(rep) );
					if ( written < 0 )
					{
						fprintf( stderr, "hid_write for %zu bytes failed with: %ls\n", sizeof(rep), hid_error(hd) );
						cleanup();
						exit(EX_IOERR);
					}
				}
			}
		}
		usleep( delay );
	}
	return 0;
}


int main( int argc, char* argv[] )
{
	(void)argc;
	(void)argv;
	fprintf(stderr,"Turbo LEDZ daemon. (c) by GSAS Inc.\n");
	strncpy( opt_mode, "cpu", sizeof(opt_mode) );
	read_config();

	if ( opt_launchpause > 0 )
	{
		fprintf(stderr, "A %dms graceperiod for udevd to do its work starts now.\n", opt_launchpause);
		usleep( opt_launchpause * 1000 );
		fprintf(stderr, "Commencing...\n");
	}

	numcpu = cpuinf_init();
	if ( numcpu <= 0 ) return 1;

	signal( SIGINT,  sig_handler ); // For graceful exit.
	signal( SIGTERM, sig_handler );	// For graceful exit.
	signal( SIGHUP,  sig_handler );	// For re-reading configuration.
	signal( SIGUSR1, sig_handler );	// For going to sleep.
	signal( SIGUSR2, sig_handler );	// For waking up.

	if (hid_init())
	{
		fprintf( stderr,"hid_init() failed: %ls\n", hid_error(0) );
		return 1;
	}

	struct hid_device_info* devs_adafruit=0;
	struct hid_device_info* devs_arduino=0;
#if TRY_ADAFRUIT_DEVICES
	devs_adafruit = hid_enumerate( 0x239a, 0x801e );
#endif
	devs_arduino  = hid_enumerate( 0x2341, 0x8037 );

	if ( !devs_arduino && !devs_adafruit )
	{
		fprintf(stderr,"No Turbo LEDz devices were found.\n");
		return 1;
	}

	if ( devs_arduino )
	{
		fprintf(stderr, "Examining arduino devices...\n");
		const int num = select_and_open_device( devs_arduino );
		fprintf(stderr, "Opened %d devices.\n",num);
		hid_free_enumeration(devs_arduino);
	}

#if TRY_ADAFRUIT_DEVICES
	if ( devs_adafruit )
	{
		fprintf(stderr, "Examining adafruit devices...\n");
		const int num = select_and_open_device( devs_adafruit );
		fprintf(stderr, "Opened %d devices.\n",num);
		hid_free_enumeration(devs_adafruit);
	}
#endif

	if ( numdevs== 0 )
	{
		fprintf(stderr,"Failed to select and open device.\n");
		return 1;
	}

	fprintf( stderr, "Mode=%s Freq=%d numcpu=%d\n", opt_mode, opt_freq, numcpu );
	int rv = service();
	cleanup();
	return rv;
}

