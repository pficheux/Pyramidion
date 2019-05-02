//
//  How to access GPIO registers from C-code on the Raspberry-Pi
//  Example program
//  15-January-2012
//  Dom and Gert
//  Revised: 01-01-2013

// 
// PF: Fix mmap() error code + use POSIX.4 timer
//
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <libgen.h>
#include <string.h>

#define BCM2708_PERI_BASE   0x20000000
#define GPIO_BASE           (BCM2708_PERI_BASE + 0x200000) /* GPIO controler */

#define PAGE_SIZE (4*1024)
#define BLOCK_SIZE (4*1024)

int  mem_fd;
char *gpio_map;
timer_t my_timer;
int gpio_nr = 25; // default is GPIO #25
unsigned long period = 5000000; // default is 5 ms

unsigned long loop_prt;
int test_loops = 0;             /* outer loop count */
time_t t = 0, told = 0;
int ntest = 0, ntest_max;
int dry_run, verbose, quiet;

// I/O access
volatile unsigned *gpio;


// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x) 
#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))

#define GPIO_SET *(gpio+7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR *(gpio+10) // clears bits which are 1 ignores bits which are 0

// For GPIO# >= 32 (RPi B+)
#define GPIO_SET_EXT *(gpio+8)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR_EXT *(gpio+11) // clears bits which are 1 ignores bits which are 0

void gpio_set (int g)
{
  if (g >= 32)
    GPIO_SET_EXT = (1 << (g % 32));
  else
    GPIO_SET = (1 << g);
}

void gpio_clr (int g)
{
  if (g >= 32)
    GPIO_CLR_EXT = (1 << (g % 32));
  else
    GPIO_CLR = (1 << g);
}

//
// Set up a memory regions to access GPIO
//
void setup_io()
{
  /* open /dev/mem */
  if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
    printf("can't open /dev/mem \n");
    exit(-1);
  }

  /* mmap GPIO */
  gpio_map = (char *)mmap(
			  NULL,             //Any adddress in our space will do
			  BLOCK_SIZE,       //Map length
			  PROT_READ|PROT_WRITE,// Enable reading & writting to mapped memory
			  MAP_SHARED,       //Shared with other processes
			  mem_fd,           //File to map
			  GPIO_BASE         //Offset to GPIO peripheral
			  );

  close(mem_fd); //No need to keep mem_fd open after mmap

  if (gpio_map == MAP_FAILED) {
    printf("mmap error %d\n", (int)gpio_map);
    exit(-1);
  }

  // Always use volatile pointer!
  gpio = (volatile unsigned *)gpio_map;
}

void got_sigint (int sig) 
{
  printf ("Got SIGINT\n");

  if (timer_delete (my_timer) < 0) {
    perror ("timer_delete");
    exit (1);
  }

  exit (0);
}

void got_sigalrm (int sig)
{
  struct timespec tr;
  time_t jitter; 
  static time_t jitter_max = 0, jitter_avg = 0;

  told = t;
  clock_gettime (CLOCK_REALTIME, &tr);
  t = (tr.tv_sec * 1000000000) + tr.tv_nsec;    

  if (!dry_run) {
    if (test_loops % 2)
      gpio_set (gpio_nr);
    else
      gpio_clr (gpio_nr);
  }

  // Calculate jitter + display
  jitter = abs(t - told - period);
  jitter_avg += jitter;
  if (test_loops && (jitter > jitter_max))
    jitter_max = jitter;
  
  if (!quiet && test_loops && (!(test_loops % loop_prt) || verbose)) {
    jitter_avg /= loop_prt;
    printf ("Loop= %d sec= %ld nsec= %ld delta= %ld ns jitter cur= %ld ns avg= %ld ns max= %ld ns\n", test_loops,  tr.tv_sec, tr.tv_nsec, t-told, jitter, jitter_avg, jitter_max);
    jitter_avg = 0;

    if (++ntest == ntest_max) {
      if (timer_delete (my_timer) < 0) {
	perror ("timer_delete");
	exit (1);
      }

      printf ("Normal exiting.\n");
      exit (0);
    }
  }

  test_loops++;
}

void usage (char *s)
{
  fprintf (stderr, "Usage: %s [-p period (ns)] [-g gpio#] [-n loops] [-d] [-v]\n", s);
  exit (1);
}

int main(int ac, char **av)
{
  char *cp, *progname = (char*)basename(av[0]);
  struct itimerspec its, its_old;

  signal (SIGALRM, got_sigalrm);
  signal (SIGINT, got_sigint);

  while (--ac) {
    if ((cp = *++av) == NULL)
      break;
    if (*cp == '-' && *++cp) {
      switch(*cp) {
      case 'g' :
	gpio_nr = atoi(*++av);
	break;

      case 'p' :
	period = (unsigned long)atoi(*++av); break;

      case 'n' :
	ntest_max = (unsigned long)atoi(*++av); break;

      case 'd' :
	dry_run = 1; break;

      case 'q' :
	quiet = 1; break;

      case 'v' :
	verbose = 1; break;

      default: 
	usage(progname);
	break;
      }
    }
    else
      break;
  }

  // Display every 2 sec
  loop_prt = 2000000000 / period;
  
  printf ("Using GPIO %d and period %ld ns\n", gpio_nr, period);
  if (dry_run)
    printf ("** dry-run mode, no hardware access !! **\n");
  if (verbose)
    printf ("** verbose mode !! **\n");

  if (!dry_run) {
    // Set up io pointer for direct register access
    setup_io();

    // Set GPIO  as output
    OUT_GPIO(gpio_nr);
  }

  if (timer_create (CLOCK_REALTIME, NULL, &my_timer) < 0) {
    perror ("timer_create");
    exit (1);
  }

  its.it_value.tv_sec = 0;
  its.it_value.tv_nsec = 50000000;
  its.it_interval.tv_sec = period / 1000000000;
  its.it_interval.tv_nsec = period % 1000000000;
  //  its.it_interval.tv_sec = 0;
  //  its.it_interval.tv_nsec = period;

  if (timer_settime (my_timer, 0, &its, &its_old) < 0) {
    perror ("timer_settime");
    exit (1);
  }

  while (1)
    pause();

  return 0;
}
