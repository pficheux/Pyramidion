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
#ifdef USE_MOSQUITTO
#include <mosquitto.h>
#endif

/****************************************************************
 * Constants
 ****************************************************************/

#define SYSFS_GPIO_DIR "/sys/class/gpio"

#define DEFAULT_BPM_IDLE   30 /* bpm = 60000 / 2 / timeout */
#define MIN_BPM_IDLE       20
#define MAX_BPM_IDLE       200
#define BPM_IDLE_INC       5   /* press the button -> bpm_idle +/- 5 bpm */

#define MAX_BUF 64

/* global variables */
int gpio_out = 0;
int gpio_btn = 0;
int count_in = 0;
time_t t_start, t_cur, t_btn, t_btn_old;
int bpm, bpm_idle, bpm_temp;
int verbose;

pthread_t sensor_thread;

#ifdef USE_MOSQUITTO

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

#endif /* USE_MOSQUITTO */

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
  int fd;
  char buf[MAX_BUF];

  snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR  "/gpio%d/direction", gpio);

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
  int fd;
  char buf[MAX_BUF];

  if (!gpio)
    return 0;

  snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%d/value", gpio);

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
  int fd;
  char buf[MAX_BUF];
  char ch;

  snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%d/value", gpio);

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
  int fd;
  char buf[MAX_BUF];

  snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%d/edge", gpio);

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
  int fd;
  char buf[MAX_BUF];

  snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%d/value", gpio);

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
  printf("\t-i <gpio-in-pin>\n\t-o <gpio-out-pin> \n\t-g <btn-gpio>\n\t-h <mqtt_host>\n\t-T <mqtt_topic> \n\t-v verbose \n\t-b <idle-bpm>\n\t-w <time> (wait 'time' before sending bpm)\n\n");
#else  
  printf("\t-i <gpio-in-pin>\n\t-o <gpio-out-pin> \n\t-g <btn-gpio>\n\t-v verbose \n\t-b <idle-bpm>\n\t-w <time> (wait 'time' before sending bpm)\n\n");
#endif
  
  exit (1);
}

// thread for blinking as receiving data from the sensor
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
  char buf[MAX_BUF], *cp;
  unsigned int gpio = 0;
  unsigned int v_out = 0;
  int wait_time = 10, exit_v = 0;
  int skip_btn_event = 1;
  int bpm_inc = BPM_IDLE_INC;
#ifdef USE_MOSQUITTO  
  int mqtt_err;
  char mqtt_msg[MAX_BUF];
#endif  

  
  bpm_idle = DEFAULT_BPM_IDLE;
  
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

      case 'g' :
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

      case 'b' :
	bpm_idle = atoi(*++av);
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

  timeout = 30000 / bpm_idle;
  
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
    gpio_set_edge(gpio_btn, "falling");
    gpio_btn_fd = gpio_fd_open(gpio_btn);
  }

  signal (SIGINT, got_exit);
  signal (SIGTERM, got_exit);

#ifdef USE_MOSQUITTO
  mqtt_setup();
  sprintf (buf, "%d", bpm_idle);
  mqtt_err = mqtt_send (buf);
  if (mqtt_err != 0) 
    fprintf(stderr, "mqtt_send error= %d\n", mqtt_err);
#endif

  if (verbose)
    printf ("default blinking= %d bpm\n", bpm_idle);
  
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
      //      exit_v = 1;
      //      goto the_end;
#ifdef USE_MOSQUITTO      
      //      mqtt_err = mqtt_send ("30");
      //      if (mqtt_err != 0) 
      //         fprintf(stderr, "mqtt_send error= %d\n", mqtt_err);
      //      return -1;
#endif      
    }

    // timeout -> default blinking
    if (rc == 0) {
      int r;

      // default blinking
      if (bpm) {
	void *status;
#ifdef USE_MOSQUITTO	
        mqtt_send ("30");
#endif
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

      // Reset bpm to 0
      bpm = bpm_temp = 0;
      count_in = 0;
      t_start = t_cur = 0;

      if (verbose)
	printf(".");

      gpio_set_value (gpio_out, v_out);
      v_out = (v_out == 0 ? 1 : 0);
    }
    // rc > 0 => something happened on fds
    else {
      if (fdset[0].revents & POLLPRI) {
	lseek(fdset[0].fd, 0, SEEK_SET);
	if (read(fdset[0].fd, buf, MAX_BUF) < 0)
	  perror ("read / GPIO-in");

	// Start counting time and events
	if (t_start == 0)
	  t_start = time(0);
	t_cur = time(0);
      	count_in++;
      
	// bpm == 0 -> We need to get it !
	if (bpm == 0) {
	  // led off during calculation
	  gpio_set_value (gpio_out, 0);
	  // Wait some seconds (default is 10) before sending bpm because of sensor quality, then create the thread
	  if (t_cur - t_start >= wait_time) {
	    bpm = bpm_temp;
	    if (verbose)
	      printf (">>> final bpm = %d\n", bpm);
#ifdef USE_MOSQUITTO	  
	    sprintf (mqtt_msg, "%d", bpm);
	    mqtt_err = mqtt_send(mqtt_msg);
	    if (mqtt_err != 0) 
	      fprintf(stderr, "mqtt_send error= %d\n", mqtt_err);
#endif
	    // Create sensor thread with bpm value
	    if (pthread_create(&sensor_thread, NULL, threadfunc, (void *)bpm) < 0)
	      perror ("pthread_create");
	  }
	  else {
	    // get the bpm for the current interval
	    bpm_temp = (int)(30 * (double)count_in / (double)(t_cur - t_start));
	    if (verbose && (t_cur > t_start))
	      printf (">>> current bpm after %d seconds and %d event(s) = %d\n", (int)(t_cur-t_start), count_in, bpm_temp);
	  }
	}
	// we have bpm already
	else {
	  bpm_temp = 0;
	  //        if (verbose)
	  //	  printf (">>> thread is running, current bpm = %d\n", bpm);
	}
      }
      else if (fdset[1].revents & POLLPRI) {
	lseek(fdset[1].fd, 0, SEEK_SET);
	if (read(fdset[1].fd, buf, MAX_BUF) < 0)
	  perror ("read / btn");

	if (t_btn)
	  t_btn_old = t_btn;
	
	t_btn = time(0);

	if (t_btn - t_btn_old < 2 || skip_btn_event) {
	  skip_btn_event = 0;
	}
	else {
	  if (bpm == MIN_BPM_IDLE || bpm_idle == MAX_BPM_IDLE)
	    bpm_inc = -bpm_inc;

	  bpm_idle += bpm_inc;

	  timeout = 30000/bpm_idle;
	  
	  if (verbose)
	    printf ("new bpm= %d timeout=%d\n", bpm_idle, timeout);
	}
      }
    }

    fflush(stdout);
  }

  gpio_fd_close(gpio_fd);
  if (gpio_btn)
    gpio_fd_close (gpio_btn_fd);
  
#ifdef USE_MOSQUITTO  
  mqtt_err = mqtt_send ("30");
  if (mqtt_err != 0) 
    fprintf(stderr, "mqtt_send error= %d\n", mqtt_err);
#endif

  return exit_v;
}
