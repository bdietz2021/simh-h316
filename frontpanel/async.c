// async.c 06/10/2026 - frontpanelh316 i/o routines for h316 hardware front panel
// This code opens an async connection via the USB to the connected custom hw
// Part of Bryan Dietz's retirement project(s)
// change variable portname if needed
//
// also, for now, decodes JSON messages from hw front panel
//
//  tweak to add \name
//
#include <stdio.h>  
#include <fcntl.h>    /* file open flags and open() */
#include <termios.h>
#include <unistd.h>
#include <pthread.h>

#include "cJSON.h" // JSON utilities

//
//  FIFO package
//  in = next index for storing
//  out = next index for retrieving
//  in = out --> empty fifo
struct fifo_block {
  char name[16];  // name of register or similar
  int value;
};
//
struct fifo {
  short in; 
  short out; 
  #define N_FIFO 16
  struct fifo_block block[N_FIFO];
};

struct fifo fifo1; // fifo for msgs from async input thread
//
void fifo_init(struct fifo *fwork){
  fwork->in = fwork->out = 0;
}
int fifo_in(struct fifo *fwork,char *nin,int vin)
{
  int i;
  i = fwork->in;
  fwork->block[i].value = vin;

  if (++i >= N_FIFO) i = 0;  // check for wrap 
  fwork->in = i; 
  return (0);
};
int fifo_out(struct fifo *fwork,char *nout,int *vout)
{
  int i;
  if (fwork->in == fwork->out) return(0);
  i = fwork->out;
  *vout = fwork->block[i].value;

  if (++i >= N_FIFO) i = 0;
  fwork->out = i;
  return(1);
};
int fifo_query(struct fifo *fwork)
{
  int i;
  i = fwork->in - fwork->out;
  if (i<0) i = -i;
  return(i);
}

struct termios serial_port_settings;
int option = 0;

/* https://www.man7.org/linux/man-pages/man3/termios.3.html */
//         tcflag_t c_iflag;      /* input modes */
//           tcflag_t c_oflag;      /* output modes */
//           tcflag_t c_cflag;      /* control modes */
//           tcflag_t c_lflag;      /* local modes */
//           cc_t     c_cc[NCCS];   /* special characters */
/* The settings of MIN (c_cc[VMIN]) and TIME
       (c_cc[VTIME]) determine the circumstances in which a read(2)
       completes; there are four distinct cases: */

int fd;	// file descriptor

// JSON input data
int front_panel_input;  // flag for data available
int front_panel_char_count;
char front_panel_buff[256];
void check_for_JSON(char* ,int );
void process_json(char* ,int );
int status_reg(const char *const , char *);

void* from_async(void* arg)
{
char buff[120];
int nchars;

/* read data received from hw front panel */
	while(1) {
		nchars = read(fd,buff,1);
		if (nchars < 0) break;
		if (nchars > 0) {
        //  write(1,buff,nchars);  // echo data on console
         check_for_JSON(buff,nchars);
      }
      // JSON processing
	};
	return(NULL);
};

int to_async(int fd)
{
char buff[120];
int nchars;
	while(1) {
		nchars = read(0,buff,1);
		if (nchars < 0) break;
		if (nchars > 0) write(fd,buff,nchars);
	};
	return(0);
}

int write_to_async(int fd, int nchars,char* buff) {

		if (nchars > 0) write(fd,buff,nchars);
      return(0); 
};

int async_start() 
{
// char buff[120];
// int nchars;
pthread_t thread1;

char portname[] = "/dev/ttyACM0";
char errormsg[120];

   // Replace /dev/ttyACM1 with the name of your Serial Port
   
   fd = open(portname, O_RDWR | O_NOCTTY); //open a connection to serialport
   
   if (fd == -1) 
   {
	sprintf(errormsg,"Failed to open serial port %s",portname);
       perror(errormsg); /*  to print system error messages */
       return 1;
   }
   else
   {
      printf("Connection to Port %s  Opened fd = %d \n",portname,fd);
   }
/* update terminal settings */
tcgetattr(fd,&serial_port_settings);

 serial_port_settings.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // Enable NON CANONICAL Mode for Serial Port Comm
  serial_port_settings.c_iflag &= ~(IXON | IXOFF | IXANY);         // Turn OFF software based flow control (XON/XOFF).
  
  serial_port_settings.c_cflag |=  CREAD | CLOCAL;         // Turn ON  the receiver of the serial port (CREAD)
  serial_port_settings.c_cflag &= ~CRTSCTS;                // Turn OFF Hardware based flow control RTS/CTS
   
   
  // Set 8N1 (8 bits, no parity, 1 stop bit)
  serial_port_settings.c_cflag &= ~PARENB;      // No parity
  serial_port_settings.c_cflag &= ~CSTOPB;      // One stop bit
  serial_port_settings.c_cflag &= ~CSIZE;       
  serial_port_settings.c_cflag |=  CS8;          // 8 bits

cfsetispeed(&serial_port_settings,B9600);
cfsetospeed(&serial_port_settings,B9600);

option = TCSANOW;
tcsetattr(fd,option,&serial_port_settings);

pthread_create(&thread1, NULL, from_async, NULL); // create thread to read from hw frontpanel
front_panel_input = 0;  // set no input received (yet)
front_panel_char_count = 0;   // nothing received yet

/*	 end */
   return(0);
};


// JSON processing code
//
void check_for_JSON(char* inbuf,int n)
{
    if (inbuf[n-1] == '\n') {
       write(1,front_panel_buff,front_panel_char_count);  // echo data on console
       write(1,"\n",1);
       front_panel_buff[front_panel_char_count+1] = 0;  // insure null termination
       process_json(front_panel_buff,front_panel_char_count); // process accumulated chars
       front_panel_char_count = 0;
    } else {
      front_panel_buff[front_panel_char_count++] = inbuf[0];
    }
};

/** @brief process a json command enclosed in <>
 * <{"name":"H316 Front Panel Status","A":668,"B":1024}> (test data)
 */
void process_json(char* inputx,int j)
{
  int temp;
  char* start;  // start of JSON string

  if (front_panel_char_count < 2) return; // checkk for short input
  
  start = strchr(inputx,'{'); // find start of JSON string
  if (start == NULL) {
    printf("inputx = %s\n",inputx);
    return;
  }
  temp = status_reg(start, (char *)"MA/SI/RUN");  // look for run button pushed
  if (temp >= 0) {
  printf("received JSON %d\n",temp);
  if (fifo_query(&fifo1) == 0)  fifo_in(&fifo1,"B_Run",temp); // enforce one at a time for now
}
  // if (temp >= 0)
  // {
  //   continue;
  // };
  //
};

/** @brief: process json command to set A register
 *
 */
int status_reg(const char *const monitor, char *reg_name)
{
  const cJSON *a_ptr = NULL;
  const cJSON *name = NULL;
  int status = -1;

  cJSON *monitor_json = cJSON_Parse(monitor);
  if (monitor_json == NULL)
  {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != NULL)
    {
      fprintf(stderr, "Error before: %s\n", error_ptr);
    }
    status = -1;
    goto end;
  }

  name = cJSON_GetObjectItemCaseSensitive(monitor_json, "name");
  if (cJSON_IsString(name) && (name->valuestring != NULL))
  {
    printf("Checking monitor \"%s\"\n", name->valuestring);
  }

  a_ptr = cJSON_GetObjectItemCaseSensitive(monitor_json, reg_name);
  if (cJSON_IsNumber(a_ptr))
  {
    status = a_ptr->valuedouble;
    goto end;
  }

end:
  cJSON_Delete(monitor_json);
  // Serial.print("json values ");
  // Serial.print(status);
  // Serial.print(reg_name);
  // Serial.println();
  return status;
};

