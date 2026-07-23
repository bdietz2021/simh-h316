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
#include <string.h>
#include <fcntl.h>    /* file open flags and open() */
#include <termios.h>
#include <unistd.h>
#include <pthread.h>

#include "cJSON.h" // JSON utilities
#include "async.h"
#include "FrontPanelH316.h"

int async_debug = 0; // flag settable by gdb
//
pthread_mutex_t fifo_mutex; // control access to fifo for pushbutton events
pthread_cond_t fifo_wait; // wait for fifo to go non-empty
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
//  define all buttons exceot Bit 1-16
//
struct button_type {
  int button_code;
  char button_name[24];
};
struct button_type button_tbl[] = { 
   999, "Input_waiting",  // not a button, but input recieved from stdin
   0, "DisplayUpdate",  // not a button, but send out the register values
   1, "Start",
   2, "RUN",
   3, "SI",
   4, "MA",
   5, "P+1",
   6, "Fetch",
   7, "M-clear",
   8, "M-reg",
   9, "P/Y",
   10, "OP",
   11, "B",
   12, "A",
   13, "SS4",
   14, "SS3",
   15, "SS2",
   16, "SS1",
   17, "CLR"
};
#define NBUTTONS (sizeof(button_tbl)/sizeof(struct button_type))
//
void fifo_init(struct fifo *fwork){
  fwork->in = fwork->out = 0;
}
int fifo_in(struct fifo *fwork,char *nin,int vin)
{
  int i;
  i = fwork->in;
  strcpy(fwork->block[i].name,nin); // store name
  fwork->block[i].value = vin;  // store value

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
  strcpy(nout,fwork->block[i].name);
  i++;
  if (i >= N_FIFO) i = 0;
  fwork->out = i;
  return(1);
};
int fifo_query(struct fifo *fwork)
{
  int i;
  i = fwork->in - fwork->out;
  if (i<0) i = N_FIFO -i;
  return(i);
}

//
//  look up front panel button
//
int look_up_button(char *bname, int *value)
{
  int i;
  for (i = 0; i < NBUTTONS; i++)
  {
    if (strcmp(bname, button_tbl[i].button_name) == 0)
    {
      *value = button_tbl[i].button_code;
      return (button_tbl[i].button_code);
    }
  }
  return (-1);
};

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
char* status_reg(const char *const , char[], int , char[] );

void* from_async(void* arg)
{
char buff[120];
int nchars;

/* read data received from hw front panel */
	while(1) {
	//	pthread_mutex_lock(&fifo_mutex);
    nchars = read(fd,buff,1);
		if (nchars < 0) {
    //  pthread_mutex_unlock(&fifo_mutex);
      break;
    }
		if (nchars > 0) {
        //  write(1,buff,nchars);  // echo data on console
         check_for_JSON(buff,nchars);
         // unlock
        // pthread_cond_signal(&fifo_wait);
      }
      // JSON processing
	};
	return(NULL);
};

int to_async()
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

int write_to_async(int nchars,char* buff) {

		if (nchars > 0) write(fd,buff,nchars);
      return(0); 
};

int async_start(int flag) 
{
// char buff[120];
// int nchars;
pthread_t thread1;

char portname[] = "/dev/ttyACM0";
char errormsg[120];
async_debug = flag;

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
void check_for_JSON(char *inbuf, int n)
{
  if (inbuf[n - 1] == '\n')
  {
   if (async_debug != 0)
    {
      write(1, front_panel_buff, front_panel_char_count); // echo data on console
      write(1, "\n", 1);
    }
    front_panel_buff[front_panel_char_count + 1] = 0;       // insure null termination
    process_json(front_panel_buff, front_panel_char_count); // process accumulated chars
    front_panel_char_count = 0;
  }
  else
  {
    front_panel_buff[front_panel_char_count++] = inbuf[0];
  }
};

/** @brief process a json command enclosed in <>
 * <{"name":"H316 Front Panel Status","A":668,"B":1024}> (test data)
 */
void process_json(char* inputx,int j)
{
  char* temp;
  char* json_start;  // start of JSON string
  int ival;  // value from json
  char sval[32]; // string

  if (front_panel_char_count < 2) return; // checkk for short input
  
  json_start = strchr(inputx,'{'); // find start of JSON string
  if (json_start == NULL) {
    printf("inputx = %s\n",inputx);
    return;
  }
  temp = status_reg(json_start, (char *)"Button", ival, sval);  // look for run button pushed
  if (temp > 0) {
  printf("received JSON %s\n",temp);
 // enqueue item for main thread processing
 pthread_mutex_lock(&fifo_mutex);
 fifo_in(&fifo1,sval,0); // enqueue data
  pthread_cond_signal(&fifo_wait); // signal not empty
  pthread_mutex_unlock(&fifo_mutex);
  //
}
  // if (temp >= 0)
  // {
  //   continue;
  // };
  //
};

/** @brief: process json command to set A register
 *
 * <{"Button": {"Name": "Start", "State": 1, "Value": 1465902417} }}>
 */
char* status_reg(const char *const jptr, char reg_name[], int ival, char sval[])
{
  const cJSON *a_ptr = NULL;
  const cJSON *name = NULL;
  char* status = NULL;

  cJSON *jptr_json = cJSON_Parse(jptr);
  if (jptr_json == NULL)
  {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != NULL)
    {
      fprintf(stderr, "Error before: %s\n", error_ptr);
    }
    status = NULL;
    goto end;
  }

  name = cJSON_GetObjectItemCaseSensitive(jptr_json, "Button"); // find button
  if (cJSON_IsString(name) && (name->valuestring != NULL))
  {
    printf("Found Button\n");
  }

  a_ptr = cJSON_GetObjectItemCaseSensitive(name, "Name"); // find name
  if (cJSON_IsString(a_ptr))
  {
    // copy string 
    strcpy(sval,a_ptr->valuestring);
    status = (char *) a_ptr->valuestring; // return address of 
    goto end;
  } 

end:
  cJSON_Delete(jptr_json);
  // Serial.print("json values ");
  // Serial.print(status);
  // Serial.print(reg_name);
  // Serial.println();
  return status;
};

//  get input routines
//  initialize key control variables
//
int event_input_start(){
  pthread_mutex_init(&fifo_mutex,NULL);
  pthread_cond_init(&fifo_wait,NULL);
};

//
//  this is the "input from stdin" thread
//  read a command from stdin and queue it for
//  processing by the main thread
//
int stdin_input_start()   // create thread
{
  pthread_t thread2;
  pthread_create(&thread2, NULL, from_stdin, NULL); // create thread to read from stdin
    return 0;
} 

//
//  read input and save in dedicated buffer
//
static char operator_input[256];  // last operator command
static int operator_input_flag = 0;

void* from_stdin(void* arg)
{
// char* get_input_event(char *buff, int max) - wrong 
/* input event - either console or front panel */
  char*  status;
  while (1) {
 status = fgets(operator_input, sizeof(operator_input) - 1, stdin);
//  if (!status)
//    return (status);
 // status = strncpy(buff,operator_input, sizeof(operator_input) - 1);
  operator_input_flag = 1; // command waiting
   // enqueue item for main thread processing
 pthread_mutex_lock(&fifo_mutex);
 fifo_in(&fifo1,"Input_waiting",0); // enqueue data
  pthread_cond_signal(&fifo_wait); // signal not empty
  pthread_mutex_unlock(&fifo_mutex);
  };
  //
  // return (status);
};

//
//  return command previously read from stdin
//
char* get_input_event(char *buff, int max)
{
  char* status;

  if (operator_input_flag != 0){
    operator_input_flag = 0;
    status = strncpy(buff,operator_input, sizeof(operator_input) - 1);
    return(status);
  }
  else return(NULL);
};