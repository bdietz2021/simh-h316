//  paneltest.c     6/29/2026 - create .h file
//  paneltestt.c    6/8/2026 RPi program to test H316 front panel
//  9/24/2026 
//  11/08/2026
//  paneltest -run, -listen, -test
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h> 
#include <stdlib.h>
//

#include "async.h" // BJD async library

int main(int argc, char *argv[])

{
    int option = 0;
    struct timespec req, rem;
    req.tv_sec = 1;  // seconds
    req.tv_nsec = 0; // nanoseconds
    int r;
    int j;
    char jsonbuf[256]; // json

    printf("paneltest: display values on LED displays\n");

    for (j = 1; j < argc; j++)
    {
        printf("%d, %s \n", j, argv[j]);
        if (strcmp(argv[j],"-run")==0) option = 0;
        else if (strcmp(argv[j],"-listen")==0) option = 1; 
        else if (strcmp(argv[j],"-test")==0) option = 2;
        else {
            printf("usage: paneltest [-run, -listen, -test]\n");
            exit(-1);
        }
    };

    r = async_start(1);

    // <string> {"A":15438,"B":0,"M-reg":43579,"P/Y":66}
    int nreps = 10000;
    int delay = 150;
    int i;

    int A, B, X, P, ST;
    A = 1;
    B = 2;
    X = 3;
    P = 4;

    switch (option)
    {
    case 0:
        printf("output test to LEDs\ncntrl-c to exit\n");
        for (i = 0; i < nreps; i++)
        {
            // A++;

            /*	send message to H316 front panel via async port */
            //  8/15/2025 - bug in H316 FW doesn't accept "RUN"
            /* sprintf(buf3, "A:%08o  B:%08o  X:%08o  \n", A, B, X); */
            /* compose JSON-formatted register contents message */
            ST = i & 7;
            sprintf(jsonbuf, "<{\"A\":%d,\"B\":%d,\"M-reg\":%d,\"P/Y\":%d,\"Status\":%d}>", A, B, X, P, ST);
            // sprintf(jsonbuf, "<{\"A\":%d,\"B\":%d,\"M-reg\":%d,\"P/Y\":%d,\"Run\":%s}>", A, B, X, P,
            //   states[sim_panel_get_state(panel)]);
            write_to_async(strlen(jsonbuf), jsonbuf);
            printf("Sent: %s\n",jsonbuf);
            A = A >> 1;
            if ((A & 0xffff) == 0)
                A = 0x8000;
            sleep(1); // nanosleep(&req, &rem);
        }
        break;
    case 1:

        printf("input test - push buttons on h316 front panel\nctrl-c to exit\n");
        /* send one message to turn off firmware test (the A++) */
        sprintf(jsonbuf, "<{\"A\":%d,\"B\":%d,\"M-reg\":%d,\"P/Y\":%d}>", A, B, X, P);
        write_to_async(strlen(jsonbuf), jsonbuf);
        printf("Sent: %s\n",jsonbuf);
        while(1) ;
       //  break;
    case 2:
           A = 0177777;    // turn on all lights     
           ST = 7;
    while(1) {    
        A = A ^ 0177777 ;    // invert lights
        sprintf(jsonbuf, "<{\"A\":%d,\"B\":%d,\"M-reg\":%d,\"P/Y\":%d,\"Status\":%d}>", A, B, X, P, ST);
        write_to_async(strlen(jsonbuf), jsonbuf);
        printf("Sent: %s\n",jsonbuf);
        sleep(1); // nanosleep(&req, &rem);
        }
        // break;
    default:
        printf("unknown option\n");
    }
}