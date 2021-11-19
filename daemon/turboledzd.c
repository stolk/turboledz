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
#include <sysexits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <hidapi/hidapi.h>


static hid_device*	hd=0;

static int		numcpu=0;

// Specified in config file: update frequency in Hertz.
static int		opt_freq=10;

// Specified in config file: currently only "cpu" is implemented.
static char		opt_mode[80];

// Specified in config file: 
static int		opt_segm=8;

static int		paused=0;


static void cleanup(void)
{
	hid_close(hd);
	hd=0;
	hid_exit();
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
					strncpy( opt_mode, s+5, sizeof(opt_mode) );
					parsed++;
				}
				if ( !strncmp( s, "segm=", 5 ) )
				{
					int segm = atoi( s+5 );
					if ( segm == 8 || segm == 10 )
						opt_segm = segm;
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
		const int written = hid_write( hd, rep, sizeof(rep) );
		if (written<0)
			fprintf( stderr,"hid_write() failed for %zu bytes with: %ls\n", sizeof(rep), hid_error(hd) );
		usleep(40000);
	}
	if ( signum == SIGUSR2 )
	{
		fprintf( stderr, "Woken up.\n" );
		paused = 0;
	}
}


// We are passed a set of HID devices that matched our enumeration.
// Here, we should examine them, and select one to open.
static hid_device* select_and_open_device( struct hid_device_info* devs )
{
	hid_device* handle = 0;
	struct hid_device_info* cur_dev = devs;
	int count=0;
	const char* filenames[16]={ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, };

	while (cur_dev)
	{
		printf("Device Found\n  type: %04hx %04hx\n  path: %s\n  serial_number: %ls", cur_dev->vendor_id, cur_dev->product_id, cur_dev->path, cur_dev->serial_number);
		printf("\n");
		printf("  Manufacturer: %ls\n", cur_dev->manufacturer_string);
		printf("  Product:      %ls\n", cur_dev->product_string);
		printf("  Release:      %hx\n", cur_dev->release_number);
		printf("  Interface:    %d\n",  cur_dev->interface_number);
		printf("  Usage (page): 0x%hx (0x%hx)\n", cur_dev->usage, cur_dev->usage_page);
		printf("\n");

		if ( cur_dev->product_string )
		{
			if ( !wcsncmp( cur_dev->product_string, L"Turbo LEDz", 10 ) )
			{
				printf("Detected model: %ls\n", cur_dev->product_string+11);
				if ( count<16 )
					filenames[count++] = cur_dev->path;
			}
		}
		else
		{
			printf("Skipped %ls\n", cur_dev->product_string);
		}
		cur_dev = cur_dev->next;
	}

	fprintf( stderr, "Found %d Turbo LEDz devices.\n", count );

	if ( count>0 )
	{
		// First check if permissions are good on the /dev/rawhidX file.
		struct stat statRes;
		const char* fname = filenames[0];
		int r = stat( fname, &statRes );
		mode_t bits = statRes.st_mode;
		if ((bits & S_IRUSR) == 0)
		{
			fprintf(stderr,"Error: No user read-permission for %s\n", fname);
			exit(4);
		}
		if ((bits & S_IWUSR) == 0)
		{
			fprintf(stderr,"Error: No user write-permission for %s\n", fname);
			exit(EX_NOPERM);
		}
		handle = hid_open_path( filenames[0] );
		if (!handle)
		{
			fprintf(stderr,"Error: hid_open_path() on %s failed : %ls\n", fname, hid_error(handle));
			exit(EX_IOERR);
		}
		fprintf(stderr,"Opened hid device at %s\n", fname);
		// We like to be blocked.
		hid_set_nonblocking(handle, 0);
	}

	return handle;
}


static uint32_t* prev=0;	// Per cpu, a set of 7 Jiffies counts.
static uint32_t* curr=0;	// Per cpu, a set of 7 Jiffies counts.

// Reads for each cpu: how many jiffies were spent in each state:
//   user, nice, system, idle, iowait, irq, softirq
// Time spent in idle/iowait means that the cpu was not busy with work.
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

	FILE* f = fopen( "/proc/stat", "rb" );
	assert(f);
	char info[16384];
	const int numr = fread( info, 1, sizeof(info), f );
	fclose(f);

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
			totaldelta += deltas[i];
		}
		uint32_t work = deltas[0] + deltas[1] + deltas[5] + deltas[6];
		uint32_t idle = deltas[3] + deltas[4];
		(void) idle;
		usages[ cpu ] = work / (float) totaldelta;
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
			int bars = (int) ( 0.5f + ( (opt_segm-FLT_EPSILON) * usages[0] ) );
			uint8_t rep[2] = { 0x00, bars | 0x80 };
			const int written = hid_write( hd, rep, sizeof(rep) );
			if ( written < 0 )
			{
				fprintf( stderr, "hid_write for %zu bytes failed with: %ls\n", sizeof(rep), hid_error(hd) );
				cleanup();
				exit(EX_IOERR);
			}
		}
		usleep( delay );
	}
	return 0;
}


int main( int argc, char* argv[] )
{
	strncpy( opt_mode, "cpu", sizeof(opt_mode) );
	read_config();

	numcpu = (int) sysconf( _SC_NPROCESSORS_ONLN );
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

//	struct hid_device_info* devs_adafruit = hid_enumerate( 0x239a, 0x801e );
//	struct hid_device_info* devs_adafruit = hid_enumerate( 0x239a, 0 );
	struct hid_device_info* devs_adafruit = hid_enumerate( 0, 0x801e );
	struct hid_device_info* devs_arduino  = hid_enumerate( 0x2341, 0x8037 );

	if ( !devs_arduino && !devs_adafruit )
	{
		fprintf(stderr,"No suitable devices were found. Neither arduino based nor adafruit-samd based.\n");
		return 1;
	}

	if ( devs_arduino )
	{
		fprintf(stderr, "Looking at arduino devices...\n");
		hd = select_and_open_device( devs_arduino );
		hid_free_enumeration(devs_arduino);
	}

	if ( devs_adafruit )
	{
		fprintf(stderr, "Looking at adafruit devices...\n");
		hd = select_and_open_device( devs_adafruit );
		hid_free_enumeration(devs_adafruit);
	}

	if ( !hd )
	{
		fprintf(stderr,"Failed to select and open device.\n");
		return 1;
	}


	fprintf( stderr, "Mode=%s Freq=%d numcpu=%d\n", opt_mode, opt_freq, numcpu );
	int rv = service();
	cleanup();
	return rv;
}

