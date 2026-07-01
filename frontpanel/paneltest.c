//  paneltest.c     6/29/2026 - create .h file
//  paneltestt.c    6/8/2026 RPi program to test H316 front panel
#include <stdio.h>
#include <string.h>
#include <time.h>
//

#include "async.h" // BJD async library

int main(int argc, char **argv)

{
    int option = 0;
    struct timespec req, rem;
    req.tv_sec = 1;  // seconds
    req.tv_nsec = 0; // nanoseconds
    int r;
    char jsonbuf[256]; // json

    printf("paneltest: display values on LED displays\n");
    if (argc > 1)
        option = argc;
    r = async_start();

    // <string> {"A":15438,"B":0,"M-reg":43579,"P/Y":66}
    int nreps = 10000;
    int delay = 150;
    int i;

    int A, B, X, P;
    A = 1;
    B = 2;
    X = 3;
    P = 4;

    if (option != 0)
    {
        printf("output test to LEDs\ncntrl-c to exit\n");
        for (i = 0; i < nreps; i++)
        {
            // A++;

            /*	send message to H316 front panel via async port */
            //  8/15/2025 - bug in H316 FW doesn't accept "RUN"
            /* sprintf(buf3, "A:%08o  B:%08o  X:%08o  \n", A, B, X); */
            /* compose JSON-formatted register contents message */
            sprintf(jsonbuf, "<{\"A\":%d,\"B\":%d,\"M-reg\":%d,\"P/Y\":%d}>", A, B, X, P);
            // sprintf(jsonbuf, "<{\"A\":%d,\"B\":%d,\"M-reg\":%d,\"P/Y\":%d,\"Run\":%s}>", A, B, X, P,
            //   states[sim_panel_get_state(panel)]);
            write_to_async(strlen(jsonbuf), jsonbuf);
            A = A >> 1;
            if ((A & 0xffff) == 0)
                A = 0x8000;
            nanosleep(&req, &rem);
        }
    }
    else {
        printf("input test - push buttons on h316 front panel\nctrl-c to exit\n");
        while(1) ;
    }
}