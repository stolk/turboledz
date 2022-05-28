// turboledzsim.c
//
// (c)2022 by Abraham Stolk.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>
#include <inttypes.h>
#include <errno.h>
#include <err.h>
#include <string.h>
#include <termios.h>

#include "grapher.h"
#include "cpuinf.h"

#define RINGBUFSZ	128
#define SUPERSAMPLES	4
typedef struct
{
	int head;
	int tail;
	uint32_t buf[RINGBUFSZ];
	uint8_t stagecounts[SUPERSAMPLES];
} ringbuf_t;

static ringbuf_t* ringbuffers=0;


static struct termios orig_termios;


static int update_image(void)
{
	if ( grapher_resized )
	{
		grapher_adapt_to_new_size();
	}

	grapher_update();
	return 0;
}


static void disableRawMode()
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}


static void enableRawMode()
{
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(disableRawMode);
	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ECHO);				// Don't echo key presses.
	raw.c_lflag &= ~(ICANON);			// Read by char, not by line.
	raw.c_cc[VMIN] = 0;				// No minimum nr of chars.
	raw.c_cc[VTIME] = 0;				// No waiting time.
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}


static void draw_samples(int numcores)
{
	const int numcol = imw/6;
	for (int core=0; core<numcores; ++core)
	{
		const int y0 = (numcores-1-core)*3+1;
		const int y1 = (numcores-1-core)*3+2;
		ringbuf_t* rb = ringbuffers + core;
		int len = rb->tail - rb->head;
		len = len < 0 ? len+RINGBUFSZ : len;
		if (y0 < imh && y1 < imh)
		{
			for (int col=0; col<numcol; ++col)
			{
				uint32_t colour = 0xff202020;
				if (col < len)
				{
					int i = (rb->tail - 1 - col);
					i = i < 0 ? i+RINGBUFSZ : i;
					colour = rb->buf[i];
				}
				for (int j=0; j<4; ++j)
				{
					int x = (numcol-1-col) * 6 + 1 + j;
					im[y0*imw + x] = im[y1*imw + x] = colour;
				}
			}
		}
	}
}

static int take_samples(int numcores)
{
	static uint32_t samplecount=0;
	samplecount++;
	static const uint32_t colours[4] = 
	{
		0xff202020,
		0xff00ff00,
		0xff0090e0,
		0xff0000ff,
	};
	int redraw=0;
	enum freq_stage stages[numcores];
	const int num = cpuinf_get_cur_freq_stages( stages, numcores );
	assert(num == numcores);
	for (int core=0; core<numcores; ++core)
	{
		ringbuf_t* rb = ringbuffers + core;
		rb->stagecounts[stages[core]]++;
		if (samplecount%SUPERSAMPLES==0)
		{
			uint8_t hival=0;
			uint8_t hiidx=0;
			for (int j=0; j<4; ++j)
				if (rb->stagecounts[j] >= hival)
				{
					hiidx = j;
					hival = rb->stagecounts[j];
				}
			const uint32_t colour = colours[hiidx];
			rb->buf[rb->tail] = colour;
			rb->tail = (rb->tail+1) % RINGBUFSZ;
			if (rb->tail == rb->head)
				rb->head = (rb->head+1) % RINGBUFSZ;
			memset(rb->stagecounts, 0, sizeof(rb->stagecounts));
			redraw=1;
		}
	}
	return redraw;
}


int main(int argc, char *argv[])
{
	(void) argc;
	(void) argv;
	const int numvirtcores = cpuinf_init();
	enum freq_stage stages[ numvirtcores ];
	const int numcores = cpuinf_get_cur_freq_stages( stages, numvirtcores );

	ringbuffers = (ringbuf_t*) malloc(sizeof(ringbuf_t) * numcores);

	int result = grapher_init();
	if ( result < 0 )
	{
		fprintf( stderr, "Failed to intialize grapher(), maybe we are not running in a terminal?\n" );
		exit(2);
	}

	enableRawMode();
	update_image();

	int done=0;
	const int delay = 100000 / SUPERSAMPLES;
	do
	{
		const int redraw = take_samples(numcores);
		if (redraw)
		{
			draw_samples(numcores);
			update_image();
		}
		char c=0;
		const int numr = read( STDIN_FILENO, &c, 1 );
		if ( numr == 1 && ( c == 27 || c == 'q' || c == 'Q' ) )
			done=1;
		usleep(delay);
	} while (!done);

	grapher_exit();
	exit(0);
}

