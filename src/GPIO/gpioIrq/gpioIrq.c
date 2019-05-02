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
#include <mosquitto.h>


/****************************************************************
 * Constants
 ****************************************************************/

#define SYSFS_GPIO_DIR "/sys/class/gpio"
//#define POLL_TIMEOUT (3 * 1000) /* 3 seconds */
#define POLL_TIMEOUT (1000) /* 30 bpm */
#define MAX_BUF 64

/* global variables */
int gpio_out = 0;
int count_in = 0;
time_t t_start, t_cur;
int bpm, bpm_temp;
int verbose;

pthread_t sensor_thread;

/************
 * MQTT
 ************/

struct mosquitto *mosq = NULL;
char *topic = NULL;
char *mqtt_host = NULL;
char *mqtt_topic = NULL;

void mosq_log_callback(struct mosquitto *mosq, void *userdata, int level, const char *str)
{
  /* Pring all log messages regardless of level. */
  
  switch(level){
    //case MOSQ_LOG_DEBUG:
    //case MOSQ_LOG_INFO:
    //case MOSQ_LOG_NOTICE:
  case MOSQ_LOG_WARNING:
  case MOSQ_LOG_ERR: {
    printf("%i:%s\n", level, str);
  }
  }
}

void mqtt_setup()
{
  int port = 1883;
  int keepalive = 60;
  bool clean_session = true;

  if (!mqtt_host || !mqtt_topic) 
    return;
  
  mosquitto_lib_init();
  mosq = mosquitto_new(NULL, clean_session, NULL);
  if(!mosq){
    fprintf(stderr, "Error: Out of memory.\n");
    mqtt_host = 0;
    return;
    //    exit(1);
  }
  
  mosquitto_log_callback_set(mosq, mosq_log_callback);
  
  if(mosquitto_connect(mosq, mqtt_host, port, keepalive)){
    fprintf(stderr, "Unable to connect.\n");
    mqtt_host = 0;
    return;
    //    exit(1);
  }

  int loop = mosquitto_loop_start(mosq);

  if(loop != MOSQ_ERR_SUCCESS){
    fprintf(stderr, "Unable to start loop: %i\n", loop);
    mqtt_host = 0;
    return;
    //    exit(1);
  }
}

int mqtt_send(char *msg)
{
  if (!mqtt_host || !mqtt_topic) 
    return 0;

  return mosquitto_publish(mosq, NULL, mqtt_topic, strlen(msg), msg, 0, 0);
}

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
  printf("\t-i <gpio-in-pin>\n\t-o <gpio-out-pin> \n\t-h <mqtt_host>\n\t-t <mqtt_topic> \n\t-v verbose \n\t-w <time> (wait 'time' before sending bpm)\n\n");
 
  exit (1);
}

// thread for blinking (bpm)
void *threadfunc(void *parm)
{
  struct timespec ts;
  unsigned int v_out = 0;
  unsigned long period;
  int bpm = (int)parm;

  if (verbose)
    printf (">> Thread started, bpm= %d\n", bpm);

  // period in ns
  period = (60000 / bpm / 2) * 1000000;
  ts.tv_sec = period / 1000000000;
  ts.tv_nsec = period % 1000000000;

  while (1) {
    // Change gpio out state
    gpio_set_value (gpio_out, v_out);
    v_out = (v_out == 0 ? 1 : 0);

    clock_nanosleep (CLOCK_MONOTONIC, 0, &ts, NULL);
  }
}

// signal handler
static void got_exit (int sig)
{
  /* Clear gpio out before exiting */
  gpio_set_value (gpio_out, 0);
}


/****************************************************************
 * Main
 ****************************************************************/
int main(int ac, char **av, char **envp)
{
  struct pollfd fdset[2];
  int nfds = 2;
  int gpio_fd, timeout, rc;
  char *buf[MAX_BUF], *cp, mqtt_msg[MAX_BUF];
  unsigned int gpio = 0;
  int len;
  int val;
  int mqtt_err;
  unsigned int v_out = 0;
  int wait_time = 10, exit_v = 0;
  
  while (--ac) {
    if ((cp = *++av) == NULL)
      break;
    if (*cp == '-' && *++cp) {
      switch(*cp) {
      case 'h' :
	mqtt_host = *++av;
	break;

      case 'i' :
	gpio = atoi(*++av);
	break;

      case 'o' :
	gpio_out = atoi(*++av);
	break;

      case 't' :
	mqtt_topic = *++av;
	break;

      case 'w' :
	wait_time = atoi(*++av);
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

  timeout = POLL_TIMEOUT;

  signal (SIGINT, got_exit);
  signal (SIGTERM, got_exit);

  mqtt_setup();
  mqtt_err = mqtt_send ("30");
  if (mqtt_err != 0) 
    fprintf(stderr, "mqtt_send error= %d\n", mqtt_err);


  while (1) {
    memset((void*)fdset, 0, sizeof(fdset));

    fdset[0].fd = gpio_fd;
    fdset[0].events = POLLPRI;

    rc = poll(fdset, nfds, timeout);

    if (rc < 0) {
      fprintf(stderr, "\npoll() failed, exiting!\n");
      exit_v = 1;
      goto the_end;
      //      mqtt_err = mqtt_send ("30");
      //      if (mqtt_err != 0) 
      //         fprintf(stderr, "mqtt_send error= %d\n", mqtt_err);
      //      return -1;
    }

    // timeout -> default blinking
    if (rc == 0) {
      int r;

      // default blinking for remote site
      if (bpm) {
	void *status;

        mqtt_send ("30");

	//  Stop current thread
	if (verbose)
	  printf (">> Cancelling thread\n");
	r = pthread_cancel(sensor_thread);
	if (verbose)
	  printf (">> pthread_cancel = %d\n", r);
	pthread_join(sensor_thread, &status);
	if (verbose)
	  printf (">> Thread cancelled !!\n");
      }

      bpm = bpm_temp = 0;
      count_in = 0;
      t_start = t_cur = 0;

      if (verbose)
	printf(".");

      gpio_set_value (gpio_out, v_out);
      v_out = (v_out == 0 ? 1 : 0);
    }
    // rc > 0 => something happened on fds
    else if (fdset[0].revents & POLLPRI) {
      lseek(fdset[0].fd, 0, SEEK_SET);
      len = read(fdset[0].fd, buf, MAX_BUF);
      if (t_start == 0)
	t_start = time(0);
      t_cur = time(0);
      count_in++;
      // We need to calcutate bpm !
      if (bpm == 0) {
	// led off during calculation
        gpio_set_value (gpio_out, 0);
        // Wait X s before sending bpm because of sensor quality
	if (t_cur - t_start >= wait_time) {
	  bpm = bpm_temp;
          if (verbose)
	    printf (">>> final bpm = %d\n", bpm);
	  sprintf (mqtt_msg, "%d", bpm);
	  mqtt_err = mqtt_send(mqtt_msg);
          if (mqtt_err != 0) 
            fprintf(stderr, "mqtt_send error= %d\n", mqtt_err);

	  // Create sensor thread with bpm value
          if (pthread_create(&sensor_thread, NULL, threadfunc, (void *)bpm) < 0)
	    perror ("pthread_create");
        }
	else {
	  // get bpm for current interval
	  bpm_temp = (int)(30 * (double)count_in / (double)(t_cur - t_start));
	  if (verbose && (t_cur > t_start))
            printf (">>> temporary bpm, t_start= %d t_cur= %d (%d) cnt= %d -> %d\n", t_start, t_cur, t_cur-t_start, count_in, bpm_temp);
	}
      }
      // we have bpm already
      else {
        bpm_temp = 0;
        if (verbose)
	  printf (">>> thread is running, current bpm = %d\n", bpm);
      }

      if (verbose)
	printf("\npoll() GPIO %d interrupt occurred ('%c')\n", gpio, buf[0]);
    }

    fflush(stdout);
  }

 the_end:
  gpio_fd_close(gpio_fd);
  mqtt_err = mqtt_send ("30");
  if (mqtt_err != 0) 
    fprintf(stderr, "mqtt_send error= %d\n", mqtt_err);


  return exit_v;
}
