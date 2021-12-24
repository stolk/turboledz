//
// turboledz.h
//
// (c)2021 Game Studio Abraham Stolk Inc.
//

extern int		opt_freq;

// Specified in config file: currently only "cpu" is implemented.
extern char		opt_mode[80];

// Specified in config file: how long do we wait before operations, to give udev daemon time to apply rules.
extern int		opt_launchpause;

// When paused, we don't collect data, nor send it to the device.
extern int		turboledz_paused;

// Set this to stop service.
extern int		turboledz_finished;

// The number of virtual cores in this system.
extern int 		turboledz_numcpu;

extern void turboledz_pause_all_devices(void);

extern void turboledz_cleanup(void);

extern int turboledz_read_config(void);

extern int turboledz_select_and_open_device( struct hid_device_info* devs );

extern int turboledz_service( void );

extern int turboledz_init(void);

