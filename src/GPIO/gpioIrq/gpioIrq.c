#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#ifdef USE_MOSQUITTO
#include <mosquitto.h>
#endif

/****************************************************************
 * Constants
 ****************************************************************/

#define SYSFS_GPIO_DIR "/sys/class/gpio"

#define DEFAULT_BPM_IDLE   30 /* bpm = 60000 / 2 / timeout */
#define MIN_BPM_IDLE       20
#define MAX_BPM_IDLE       150
#define BPM_IDLE_INC       10   /* press the button -> bpm_idle +/- 10 bpm */

#define MAX_BUF 64

/* global variables */
int gpio_in = 0;  /* sensor */
int gpio_out = 0; /* led */
int gpio_btn = 0; /* button */
int count_in = 0;
time_t t_btn, t_btn_old;
int bpm_idle;
int verbose;

// SysTimestamp() emulation
int64_t timespec_as_milliseconds(struct timespec ts)
{
    int64_t rv = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return rv;
}

int64_t sysTimestamp()
{
  struct timespec ts;

  clock_gettime (CLOCK_REALTIME, &ts);

  return timespec_as_milliseconds(ts);
}

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
  unsigned int v_out = 0;
  int exit_v = 0;
  int skip_btn_event = 1;
  int bpm_inc = BPM_IDLE_INC;
  int64_t ts_i = 0, ts_s = 0, ts_s_old = 0, ts_s_diff = 0;
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
	gpio_in = atoi(*++av);
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

      case 'v' :
	verbose = 1; break;

      default: 
	usage();
      }
    }
    else
      break;
  }

  if (!gpio_in || !gpio_out)
    usage();

  timeout = 30000 / bpm_idle;
  
  // GPIO in (sensor)
  gpio_export(gpio_in);
  gpio_set_dir(gpio_in, 0);
  gpio_set_edge(gpio_in, "both");
  gpio_fd = gpio_fd_open(gpio_in);

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

    // wait on fds
    rc = poll(fdset, nfds, timeout);

    if (rc < 0) {
      perror ("poll");
#ifdef USE_MOSQUITTO      
      mqtt_err = mqtt_send ("30");
      if (mqtt_err != 0) 
	fprintf(stderr, "mqtt_send error= %d\n", mqtt_err);
      return -1;
#endif      
    }
    // rc > 0 => something happened on fds (sensor or button)
    else if (rc > 0) {
      // Sensor
      if (fdset[0].revents & POLLPRI) {
	lseek(fdset[0].fd, 0, SEEK_SET);
	if (read(fdset[0].fd, buf, MAX_BUF) < 0)
	  perror ("read / sensor");

	//told = t;
	//	clock_gettime (CLOCK_REALTIME, &tr);
	//	t = (tr.tv_sec * 1000000000) + tr.tv_nsec;    

	ts_s_old = ts_s;
	ts_s = sysTimestamp();
	ts_s_diff = ts_s - ts_s_old;
	
	if (verbose) 
	  printf ("Copy sensor value %d to GPIO %d (%lld)\n", v_out, gpio_out, ts_s_diff);
	
	// copy the value to GPIO/out
	if (ts_s_diff > 20) {
	  gpio_set_value (gpio_out, v_out);
	  v_out = (v_out == 0 ? 1 : 0);
	}
      }
      // Button
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
	  if (bpm_idle == MIN_BPM_IDLE || bpm_idle == MAX_BPM_IDLE)
	    bpm_inc = -bpm_inc;

	  bpm_idle += bpm_inc;

	  timeout = 30000/bpm_idle;
	  
	  if (verbose)
	    printf ("new bpm= %d timeout=%d\n", bpm_idle, timeout);
	}
      }
    }
    // timeout -> default blinking
    else {
      // default blinking
#ifdef USE_MOSQUITTO	
        mqtt_send ("30");
#endif

	ts_i = sysTimestamp();

	if (ts_s_diff < 20 || (ts_i - ts_s > 3000)) {
	  if (verbose)
	    printf ("Idle activated (%lld) !\n", ts_i-ts_s);

	  gpio_set_value (gpio_out, v_out);
	  v_out = (v_out == 0 ? 1 : 0);
	}
	else {
	  if (verbose)
	    printf ("Idle ignored (%lld) !\n", ts_i-ts_s);
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
