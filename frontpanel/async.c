#include <stdio.h>  
#include <fcntl.h>    /* file open flags and open() */
#include <termios.h>
#include <unistd.h>
#include <pthread.h>

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

void* from_async(void* arg)
{
char buff[120];
int nchars;

/* read data */
	while(1) {
		nchars = read(fd,buff,1);
		if (nchars < 0) break;
		if (nchars > 0) write(1,buff,nchars);
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

pthread_create(&thread1, NULL, from_async, NULL);

/*	 end */
};

