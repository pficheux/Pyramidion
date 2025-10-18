#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

/****************************************************************
 * Constants
 ****************************************************************/

#define SYSFS_GPIO_DIR "/sys/class/gpio"
#define MAX_BUF 64
#define POLL_TIMEOUT (1000) /* 30 bpm = 60000/2/timeout */


/* global variables */
int gpio_out = 0;
int gpio_btn = 0;
int count_in = 0;
int verbose;

// GPIO

/****************************************************************
 * gpio_export
 ****************************************************************/
int gpio_export(unsigned int gpio)
{
  int fd, len;
  char buf[MAX_BUF];

  fd = open(SYSFS_GPIO_DIR "/export", O_WRONLY);
  if (fd < 0) {
    perror("gpio/export");
    return fd;
  }

  len = snprintf(buf, sizeof(buf), "%d", gpio);
  write(fd, buf, len);
  close(fd);

  return 0;
}

/****************************************************************
 * gpio_unexport
 ****************************************************************/
int gpio_unexport(unsigned int gpio)
{
  int fd, len;
  char buf[MAX_BUF];

  fd = open(SYSFS_GPIO_DIR "/unexport", O_WRONLY);
  if (fd < 0) {
    perror("gpio/export");
    return fd;
  }

  len = snprintf(buf, sizeof(buf), "%d", gpio);
  write(fd, buf, len);
  close(fd);
  return 0;
}

/****************************************************************
 * gpio_set_dir
 ****************************************************************/
int gpio_set_dir(unsigned int gpio, unsigned int out_flag)
{
  int fd, len;
  char buf[MAX_BUF];

  len = snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR  "/gpio%d/direction", gpio);

  fd = open(buf, O_WRONLY);
  if (fd < 0) {
    perror("gpio/direction");
    return fd;
  }

  if (out_flag)
    write(fd, "out", 4);
  else
    write(fd, "in", 3);

  close(fd);
  return 0;
}

/****************************************************************
 * gpio_set_value
 ****************************************************************/
int gpio_set_value(unsigned int gpio, unsigned int value)
{
  int fd, len;
  char buf[MAX_BUF];

  if (!gpio)
    return 0;

  len = snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%d/value", gpio);

  fd = open(buf, O_WRONLY);
  if (fd < 0) {
    perror("gpio/set-value");
    return fd;
  }

  if (value)
    write(fd, "1", 2);
  else
    write(fd, "0", 2);

  close(fd);
  return 0;
}

/****************************************************************
 * gpio_get_value
 ****************************************************************/
int gpio_get_value(unsigned int gpio, unsigned int *value)
{
  int fd, len;
  char buf[MAX_BUF];
  char ch;

  len = snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%d/value", gpio);

  fd = open(buf, O_RDONLY);
  if (fd < 0) {
    perror("gpio/get-value");
    return fd;
  }

  read(fd, &ch, 1);

  if (ch != '0') {
    *value = 1;
  } else {
    *value = 0;
  }

  close(fd);
  return 0;
}


/****************************************************************
 * gpio_set_edge
 ****************************************************************/

int gpio_set_edge(unsigned int gpio, char *edge)
{
  int fd, len;
  char buf[MAX_BUF];

  len = snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%d/edge", gpio);

  fd = open(buf, O_WRONLY);
  if (fd < 0) {
    perror("gpio/set-edge");
    return fd;
  }

  write(fd, edge, strlen(edge) + 1);
  close(fd);
  return 0;
}

/****************************************************************
 * gpio_fd_open
 ****************************************************************/

int gpio_fd_open(unsigned int gpio)
{
  int fd, len;
  char buf[MAX_BUF];

  len = snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%d/value", gpio);

  fd = open(buf, O_RDONLY | O_NONBLOCK );
  if (fd < 0) {
    perror("gpio/fd_open");
  }
  return fd;
}

/****************************************************************
 * gpio_fd_close
 ****************************************************************/

int gpio_fd_close(int fd)
{
  return close(fd);
}

void usage (void)
{
#ifdef USE_MOSQUITTO
  printf("\t-i <gpio-in-pin>\n\t-o <gpio-out-pin> \n\t-h <mqtt_host>\n\t-T <mqtt_topic> \n\t-v verbose \n\t-t <poll-timeout>\n\t-w <time> (wait 'time' before sending bpm)\n\n");
#else  
  printf("\t-i <gpio-in-pin>\n\t-o <gpio-out-pin> \n\t-v verbose \n\t-t <poll-timeout>\n\t\n\n");
#endif
  
  exit (1);
}

// signal handler
static void got_exit (int sig)
{
  /* Clear gpio out before exiting */
  gpio_set_value (gpio_out, 0);

  printf ("Got signal, exiting !\n");
  exit (0);
}


/****************************************************************
 * Main
 ****************************************************************/
int main(int ac, char **av)
{
  struct pollfd fdset[2];
  int nfds = 2;
  int gpio_fd, gpio_btn_fd, timeout, rc;
  char buf[MAX_BUF], *cp, mqtt_msg[MAX_BUF];
  unsigned int gpio = 0;
  int len;
  int val;
  unsigned int v_out = 0;
  int exit_v = 0;

  timeout = POLL_TIMEOUT;

  while (--ac) {
    if ((cp = *++av) == NULL)
      break;
    if (*cp == '-' && *++cp) {
      switch(*cp) {
#ifdef USE_MOSQUITTO	
      case 'h' :
	mqtt_host = *++av;
	break;
#endif
      case 'i' :
	gpio = atoi(*++av);
	break;

      case 'b' :
	gpio_btn = atoi(*++av);
	break;
	
      case 'o' :
	gpio_out = atoi(*++av);
	break;

#ifdef USE_MOSQUITTO	
      case 'T' :
	mqtt_topic = *++av;
	break;
#endif	

      case 't' :
	timeout = atoi(*++av);
	break;

      case 'v' :
	verbose = 1; break;

      default: 
	usage();
      }
    }
    else
      break;
  }

  if (!gpio || !gpio_out)
    usage();

  // GPIO in
  gpio_export(gpio);
  gpio_set_dir(gpio, 0);
  gpio_set_edge(gpio, "both");
  gpio_fd = gpio_fd_open(gpio);

  // GPIO out
  if (gpio_out) {
    gpio_export(gpio_out);
    gpio_set_dir(gpio_out, 1);
  }

  // GPIO button (in)
  if (gpio_btn) {
    gpio_export(gpio_btn);
    gpio_set_dir(gpio_btn, 0);
    gpio_btn_fd = gpio_fd_open(gpio_btn);
    printf ("fd= %d\n", gpio_btn_fd);
  }

  signal (SIGINT, got_exit);
  signal (SIGTERM, got_exit);

  printf ("default blinking= %d bpm\n", 30000/timeout);
  
  while (1) {
    memset((void*)fdset, 0, sizeof(fdset));

    fdset[0].fd = gpio_fd;
    fdset[0].events = POLLPRI;

    if (gpio_btn) {
      fdset[1].fd = gpio_btn_fd;
      fdset[1].events = POLLPRI;
    }

    rc = poll(fdset, nfds, timeout);

    if (rc < 0) {
      fprintf(stderr, "\n** Warning: poll() failed !\n");
      }

    // timeout -> default blinking
    if (rc == 0) {
      int r;

      if (verbose)
	printf(".");

      gpio_set_value (gpio_out, v_out);
      v_out = (v_out == 0 ? 1 : 0);
    }
    // rc > 0 => something happened on fds
    else {
      if (fdset[0].revents & POLLPRI) {
	lseek(fdset[0].fd, 0, SEEK_SET);
	len = read(fdset[0].fd, buf, MAX_BUF);

	printf ("len= %d on fdset 0\n", len);
      }
      else if (fdset[1].revents & POLLPRI) {
	lseek(fdset[1].fd, 0, SEEK_SET);
	len = read(fdset[1].fd, buf, MAX_BUF);
	printf ("len= %d on fdset 1\n", len);
      }
    }

    fflush(stdout);
  }

 the_end:
  gpio_fd_close(gpio_fd);
  if (gpio_btn)
    gpio_fd_close(gpio_btn_fd);
  
  return exit_v;
}
