//
// turboledz.c
//
// (c)2021 Game Studio Abraham Stolk Inc.
//

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <wchar.h>		// hidapi uses wide characters.
#include <inttypes.h>
#if defined(_WIN32)
#	include <Windows.h>
#else
#	include <unistd.h>
#	include <sysexits.h>
#endif
#include <string.h>
#include <float.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <hidapi/hidapi.h>

#include "cpuinf.h"

#if defined(_WIN32)
#	define EX_IOERR	EXIT_FAILURE
#	define EX_NOINPUT EXIT_FAILURE
#endif

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
	MODEL_88s,		// 8 bars of 8 segments, single dirver.
	MODEL_810c,		// 8 bars of 10 segments, colour LEDs.
	MODEL_COUNT
};

static const char* modelnames[ MODEL_COUNT ] =
{
	"unknown",
	"108m",
	"108",
	"810",
	"810s",
	"88s",
	"810c",
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
static int		turboledz_numcpu=0;

// Specified in config file: update frequency in Hertz.
int			opt_freq=10;

// Specified in config file: currently only "cpu" is implemented.
char			opt_mode[80];

// Specified in config file: how long do we wait before operations, to give udev daemon time to apply rules.
int			opt_launchpause;

// When paused, we don't collect data, nor send it to the device.
int			turboledz_paused=0;

// Set this to stop service.
int			turboledz_finished=0;


void turboledz_pause_all_devices(void)
{
	turboledz_paused = 1;
	fprintf( stderr, "Preparing to go to sleep...\n" );
#if defined(_WIN32)
	const uint8_t rep[8] = { 0x00, 0x40, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, };
	Sleep(40);
#else
	const uint8_t rep[2] = { 0x00, 0x40 };
	usleep(40000);
#endif

	for ( int i=0; i<numdevs; ++i )
	{
		hid_device* hd = hds[i];
		const int written = hid_write( hd, rep, sizeof(rep) );
		if (written<0)
			fprintf( stderr, "hid_write() failed for %zu bytes with: %ls\n", sizeof(rep), hid_error(hd) );
	}
#if defined(_WIN32)
	Sleep(40);
#else
	usleep(40000);
#endif
}


void turboledz_cleanup(void)
{
	if ( !turboledz_paused )
	{
		turboledz_pause_all_devices();
	}

	for ( int i=0; i<numdevs; ++i )
	{
		hid_device* hd = hds[i];
		hid_close(hd);
		hds[i] = 0;
	}
	hid_exit();
	numdevs=0;
}


static enum model get_model(const wchar_t* modelname)
{
	if ( !wcscmp( modelname, L"810c" ) )
		return MODEL_810c;
	if ( !wcscmp( modelname, L"810s" ) )
		return MODEL_810s;
	if ( !wcscmp( modelname, L"88s" ) )
		return MODEL_88s;
	if ( !wcscmp( modelname, L"810" ) )
		return MODEL_810;
	if ( !wcscmp( modelname, L"108m" ) )
		return MODEL_108m;
	if ( !wcscmp( modelname, L"108" ) )
		return MODEL_108;
	return MODEL_UNKNOWN;
}


#if !defined(_WIN32)
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
#endif


// We are passed a set of HID devices that matched our enumeration.
// Here, we should examine them, and select one to open.
int turboledz_select_and_open_device( struct hid_device_info* devs )
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
#if !defined(_WIN32)
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
				fprintf
				(
					stderr,
					"Error: No rw-permission for %s which has permission %04o. Retrying...\n",
					fname, bits
				);
				if ( attemptnr == 5 )
				{
					fprintf(stderr,"Giving up. The udev daemon did not fix permissions for us.\n");
					exit(EX_NOPERM);
				}
				attemptnr++;
				sleep(1);
			}
		}
#endif
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


// CPU Load stats.
static float usages[ CPUINF_MAX ];

// CPU Core Frequency stats.
static enum freq_stage stages[ CPUINF_MAX ];

int turboledz_service( void )
{
	const int delay = 1000000 / opt_freq;	// uSeconds to wait between writes.
	while ( !turboledz_finished )
	{
		if ( !turboledz_paused )
		{
			assert(turboledz_numcpu>0);
			// For 810c devices, we collect different stats (freqs) than other devices (loads.)
			int num810c = 0;
			for ( int i=0; i<numdevs; ++i )
				num810c += ( mod[i] == MODEL_810c ? 1 : 0 );
			int numother = numdevs - num810c;
			// Get CPU load.
			if ( numother > 0 )
				cpuinf_get_usages( 1, usages );
			// Get freq stages.
			int numfr = 0;
			if ( num810c > 0 )
				numfr = cpuinf_get_cur_freq_stages( stages, CPUINF_MAX );
			int frqoff = 0;

			for ( int i=0; i<numdevs; ++i )
			{
				hid_device* hd = hds[i];
				if ( mod[i] == MODEL_810c )
				{
					uint32_t grn=0;
					uint32_t red=0;
					uint32_t bit=1;
					for ( int j=0; j<10; ++j )
					{
						const int idx = (frqoff+j);
						if ( idx < numfr )
						{
							enum freq_stage s = stages[idx];
							grn |= ( (s==FREQ_STAGE_LOW || s==FREQ_STAGE_MID) ? bit : 0 );
							red |= ( (s==FREQ_STAGE_MID || s==FREQ_STAGE_MAX) ? bit : 0 );
							bit = bit<<1;
						}
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
						const char* modelnm = modelnames[ mod[i] ];
						fprintf( stderr, "hid_write to %s for %zu bytes failed with: %ls\n", modelnm, sizeof(rep), hid_error(hd) );
						turboledz_cleanup();
						exit(EX_IOERR);
					}
					frqoff += 10;
				}
				else
				{
					int bars = (int) ( 0.5f + ( (seg[i]-FLT_EPSILON) * usages[0] ) );
					uint8_t rep[2] = { 0x00, bars | 0x80 };
					const int written = hid_write( hd, rep, sizeof(rep) );
					if ( written < 0 )
					{
						fprintf( stderr, "hid_write for %zu bytes failed with: %ls\n", sizeof(rep), hid_error(hd) );
						turboledz_cleanup();
						exit(EX_IOERR);
					}
				}
			}
		}
#if defined(_WIN32)
		Sleep(delay / 1000);
#else
		usleep( delay );
#endif
	}
	return 0;
}


int turboledz_init(void)
{
	turboledz_finished = 0;
	turboledz_numcpu = cpuinf_init();
	if ( turboledz_numcpu <= 0 ) return 1;

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
		const int num = turboledz_select_and_open_device( devs_arduino );
		fprintf(stderr, "Opened %d devices.\n",num);
		hid_free_enumeration(devs_arduino);
	}

#if TRY_ADAFRUIT_DEVICES
	if ( devs_adafruit )
	{
		fprintf(stderr, "Examining adafruit devices...\n");
		const int num = turboledz_select_and_open_device( devs_adafruit );
		fprintf(stderr, "Opened %d devices.\n",num);
		hid_free_enumeration(devs_adafruit);
	}
#endif

	if ( numdevs== 0 )
	{
		fprintf(stderr,"Failed to select and open device.\n");
		return 1;
	}

	fprintf( stderr, "Mode=%s Freq=%d numcpu=%d\n", opt_mode, opt_freq, turboledz_numcpu );
	return 0;
}


