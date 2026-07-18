/* FrontPanelH316.c: simulator frontpanel API sample

Test program for Honeywell H316 physial front panel interface to simh
Based on FrontPanelTest.c

      lpr -o "orientation-requested=4" (listing command)
      reformat - Option+shift+f

7/9/2024
7/11/2024
7/13/2024
7/28/2024
08/04/2024
08/13/2024
08/29/2024
09/09/2024 - rebuilt ubuntu
10/29/2024 - ill advised changes after getting trace to show commands send
10/30/2024
11/06/2024 - fixed problem near line 2272 - change e to s
02/03/2025 - restart debugging afer holiday break
02/06/2025 - add first command for front panel "bryan a <nnn>" where a is a register name
03/05/2025 - start experimentation with 2nd connection to panel
03/13/2025 - more experimentation with 2nd connection to panel
04/13/2025 - more experimentation with 2nd connection to panel
03/15/2025 - start merge of test program for 2nd connection
03/16/2025 - start merge of test program for 2nd connection

06/12/2025 - resume after working on front panel firmware
06/16/2025 - start adding JSON features
06/17/2025 = JSON works for A register. github complains about secrets.
06/12/2025 - resume after working on front panel firmware
06/25/2025 - add JSON message with register values
06/27/2025 - try clean up editing on mac with vsedit
07/19/2025 - extend commands, add pass through
08/15/2025 - continue work after summer vacation
08/17/2025 - start adding new commands
08/20/2025 - fixed bug in calls to "deposit instruction"
08/24/2025 - experimentation with command tests?
08/27/2025 - this may be starting to work
09/12/2025 - changes when sim is running
10/31/2025 - rewrite part of main where simh is left running - test case
02/25/2026 - resume testing - tweak long running program
05/26/2026 - after change to revert test command loop near 931 and GIT push
06/04/2026 - uncomented atP near line 427
06/06/2026 - changed "run" to start
06/06/2026 - Changed test code to check display_update and display regs
06/06/2026 - WORKS - displays updated H316 registers in real time on
              hardware front panel. Celebrations. 
06/24/2026 - update register display code
06/26/2026 - minor updates before adding option processing
07/05/2026 - main program now waits for thread to read console input (mutex/wait)

   Copyright (c) 2015, Mark Pizzolato

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   MARK PIZZOLATO BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Mark Pizzolato shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Mark Pizzolato.

   05-Feb-15    MP      Initial implementation

   This module demonstrates the use of the interface between a front panel
   application and a simh simulator.  Facilities provide ways to gather
   information from and to observe and control the state of a simulator.

*/

/* This program provides a basic test of the simh_frontpanel API. */

#include "sim_frontpanel.h"
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> // thread library
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <winerror.h>
#define usleep(n) Sleep(n / 1000)
#else
#include <unistd.h>
#undef BJD_HAVE_NCURSES
#if defined(BJD_HAVE_NCURSES)
#include <ncurses.h>
#define fgets(buf, n, f) (OK == getnstr(buf, n))
#define printf my_printf

/**************************************************/
/* static */
void my_printf(const char *fmt, ...)
{
  va_list arglist;
  int len;
  static char *buf = NULL;
  static int buf_size = 0;
  char *c;

  while (1)
  {
    va_start(arglist, fmt);
    len = vsnprintf(buf, buf_size, fmt, arglist);
    va_end(arglist);
    if (len < 0)
      return;
    if (len < buf_size)
      break;
    buf = realloc(buf, len + 2);
    buf_size = len + 1;
    buf[buf_size] = '\0';
  }
  while ((c = strstr(buf, "\r\n")))
    memmove(c, c + 1, strlen(c));
  printw("%s", buf);
}
//
//
//
#endif /* BJD_HAVE_NCURSES */
#endif

// #include "async.c" // BJD async library
#include "async.c"
#include "FrontPanelH316.h"

const char *sim_path =
#if defined(_WIN32)
    "vax.exe";
#else
    "./h316"; /* BJD changed to fix "cannot find vax" */
#endif

const char *sim_config = "H316-PANEL.ini";

/* Registers visible on the Front Panel */

static unsigned int P, A, B, X, atP;
static unsigned int PCQ[32];

int P_bits[16];
int PC_indirect_bits[32];
int PCQ_3_bits[32];
unsigned long long simulation_time;

int update_display = 1;
int run_state_loop = 0; // 0 == halt, 1 == run

int debug = 0;


static void DisplayCallback(PANEL *panel, unsigned long long sim_time,
                            void *context)
{
  simulation_time = sim_time;
  update_display = 1;
  // if (run_state_loop == 0) return;
  // enqueue item for later main thread processing
  pthread_mutex_lock(&fifo_mutex);
  if (fifo_query(&fifo1) < 3)  // do not flood the queue
  {                                     
    fifo_in(&fifo1, "DisplayUpdate", 0); // enqueue data
    pthread_cond_signal(&fifo_wait);     // signal not empty
  }
  pthread_mutex_unlock(&fifo_mutex);
  //
}

static void DisplayRegisters(PANEL *panel, int get_pos, int set_pos)
{
  char buf1[100], buf2[100], buf3[100], buf4[100];
  char jsonbuf[256]; // json
  static const char *states[] = {"Halt", "Run "};

  buf1[sizeof(buf1) - 1] = buf2[sizeof(buf2) - 1] = buf3[sizeof(buf3) - 1] =
      buf4[sizeof(buf4) - 1] = 0;
  sprintf(buf1, "%4s P: %06o    @P: %06o\n", states[sim_panel_get_state(panel)],
          P, atP);
  sprintf(buf2, "Instructions Executed: %lld\n", simulation_time);
  sprintf(buf3, "A:%06o  B:%06o  X:%06o  \n", A, B, X);
#if defined(_WIN32)
  if (1)
  {
    static HANDLE out = NULL;
    static CONSOLE_SCREEN_BUFFER_INFO info;
    static COORD origin = {0, 0};
    DWORD written;

    if (out == NULL)
      out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (get_pos)
      GetConsoleScreenBufferInfo(out, &info);
    SetConsoleCursorPosition(out, origin);
    WriteConsoleA(out, buf1, strlen(buf1), &written, NULL);
    WriteConsoleA(out, buf2, strlen(buf2), &written, NULL);
    WriteConsoleA(out, buf3, strlen(buf3), &written, NULL);
    if (set_pos)
      SetConsoleCursorPosition(out, info.dwCursorPosition);
  }
#else
  if (1)
  {
#if defined(BJD_HAVE_NCURSES)
    static int row, col;

    if (get_pos)
      getyx(stdscr, row, col);
    wmove(stdscr, 0, 0);
#else
#define ESC "\033"
#define CSI ESC "["
    /* this is the real code, after all conditional compilation */
    // get_pos = 0; /* bjd */
    if (get_pos)
    printf(CSI "H"); /* Save Cursor Position (was s)*/
                       /*	printf (CSI "H");   // Position to Top of Screen (1,1) */
    printf(CSI "0J");   /* erase to end of screen */
    /*    printf("\f");  /* erase to end of screen */

#endif /* BJD_HAVE_NCURSES */
    printf("\f%s", buf1);
    printf("%s", buf2);
    printf("%s", buf3);
    send_json_regs(jsonbuf);
#if defined(BJD_HAVE_NCURSES)
    if (set_pos)

      wmove(stdscr, row, col); /* Restore Cursor Position */
    wrefresh(stdscr);
#else
    if (set_pos)
    /* printf(CSI "u");  Restore Cursor Position (was s)*/
    printf("\r\n");
#endif /* BJD_HAVE_NCURSES */
  }
#endif
}

void send_json_regs(char jsonbuf[256])
{
  /*	send message to H316 front panel via async port */
  //  8/15/2025 - bug in H316 FW doesn't accept "RUN"
  /* sprintf(buf3, "A:%08o  B:%08o  X:%08o  \n", A, B, X); */
  /* compose JSON-formatted register contents message */
  sprintf(jsonbuf, "<{\"A\":%d,\"B\":%d,\"M-reg\":%d,\"P/Y\":%d}>", A, B, X, P);
  // sprintf(jsonbuf, "<{\"A\":%d,\"B\":%d,\"M-reg\":%d,\"P/Y\":%d}>", A, B, X, P);
  // sprintf(jsonbuf, "<{\"A\":%d,\"B\":%d,\"M-reg\":%d,\"P/Y\":%d,\"Run\":%s}>", A, B, X, P,
  //   states[sim_panel_get_state(panel)]);
  write_to_async(strlen(jsonbuf), jsonbuf);
}
static void CleanupDisplay(void)
{
#if (!defined(_WIN32)) && defined(BJD_HAVE_NCURSES)
  endwin();
#endif
}

static void InitDisplay(void)
{
#if defined(_WIN32)
  system("cls");
#else
#if defined(BJD_HAVE_NCURSES)
  int max_height = 0, max_width = 0;

  initscr();
  wclear(stdscr);
  scrollok(stdscr, 1);
  getmaxyx(stdscr, max_height, max_width);
  setscrreg(5, max_height - 1);
#else /* BJD_HAVE_NCURSES */
  printf(CSI "H");  /* Position to Top of Screen (1,1) */
  printf(CSI "2J"); /* Clear Screen */
#endif
#endif
  printf("\n\n\n\n");
  printf("^C to Halt, Commands: BOOT, CONT, EXIT, BREAK, NOBREAK, EXAMINE, "
         "HISTORY\n");
#if (!defined(_WIN32)) && defined(BJD_HAVE_NCURSES)
  wrefresh(stdscr);
#endif
  atexit(CleanupDisplay);
}

volatile int halt_cpu = 0;
PANEL *panel, *tape;

void halt_handler(int sig)
{
  signal(SIGINT, halt_handler); /* Re-establish handler for some platforms that
                                   implement ONESHOT signal dispatch */
  halt_cpu = 1;
  sim_panel_flush_debug(panel);
  return;
}

int panel_setup() /* called from main() */
{
  FILE *f;

  /* Create pseudo config file for a test */
  if ((f = fopen(sim_config, "w")))
  {
    if (debug)
    {
      /* BJD was conditional debug */
      fprintf(f, "set verbose\n");
      // fprintf(f, "set debug -n -a -p simulator.dbg\n");
      // fprintf(f, "set cpu simhalt\n");
      // fprintf(f, "set remote telnet=2226\n");
      // fprintf(f, "set rem-con debug=XMT;RCV;MODE;REPEAT;CMD\n");
      // fprintf(f, "set remote notelnet\n");
    }
    fprintf(f, "set cpu autoboot\n");
    fprintf(f, "set cpu 64\n");
    fprintf(f, "set cpu history=128\n");
    fprintf(f, "set console telnet=buffered\n");
    fprintf(f, "set console -u telnet=1927\n");
    fprintf(f, "set console log=\"logfile.txt\"\n");

    fprintf(f, "restore start\n"); /* BJD debug */

    /* Start a terminal emulator for the console port */
#if defined(_WIN32)
    fprintf(
        f,
        "set env "
        "PATH=%%PATH%%;%%ProgramFiles%%\\PuTTY;%%ProgramFiles(x86)%%\\PuTTY\n");
    fprintf(f, "! start PuTTY telnet://localhost:1927\n");
#elif defined(__linux) || defined(__linux__)
    fprintf(f, "! nohup xterm -e 'telnet localhost 1927' &\n");
#elif defined(__APPLE__)
    fprintf(f, "! osascript -e 'tell application \"Terminal\" to do script "
               "\"telnet localhost 1927; exit\"'\n");
#endif
    fclose(f);
  }

  signal(SIGINT, halt_handler);
  /* load h316 simulator program. this does not start anh h316 code, just the sim */
  panel = sim_panel_start_simulator_debug(sim_path, sim_config, 2,
                                          debug ? "frontpanel.dbg" : NULL);

  if (!panel)
  {
    printf("Error starting simulator %s with config %s: %s\n", sim_path,
           sim_config, sim_panel_get_error());
    goto Done;
  }

  if (debug)
  {
    sim_panel_set_debug_mode(panel, DBG_XMT | DBG_RCV | DBG_REQ | DBG_RSP |
                                        DBG_THR | DBG_APP);
  }
  sim_panel_debug(panel, "Starting Debug\n");
  /*	code to add tape drive removed */
  if (1)
  {
#ifdef bigtest // BJD ***
    /* unsigned int noop_noop_noop_halt = 0101000, addr0100 = 0100,
     * pc_value;*/
    /* unsigned long long int noop_noop_noop_halt = 8100810081000000ull; */
    unsigned long long int noop_noop_noop_halt = 0x0000008200820082ull;
    /*                                                 1234567890123456 */
    unsigned int addr0100 = 0100, pc_value;
    /* new code to place noops in memory */
    short int addrx = 0100; // changed 8/20 BJD
    int j;
    int *addrofi;

    /* static int inst[5] = {0101000, 0101000, 0101000, 0, 03100}; */
    static int inst[5] = {0101000, 0141206, 0101000, 0101000, 03100}; // BJD DEBUG BAD

    for (j = 0; j < 6; j++)
    {
      addrofi = &inst[j];
      sim_panel_mem_deposit(panel, sizeof(addrx), &addrx, sizeof(inst[0]),
                            addrofi);
      //  sim_panel_escape(panel, sizeof(addrx), &addrx, sizeof(inst[0]),
      //                       addrofi);
      addrx += 1;
    }

    int mstime = 0;

    /* if (sim_panel_mem_deposit (panel, sizeof(addr0100), &addr0100,
       sizeof(noop_noop_noop_halt), &noop_noop_noop_halt)) { printf ("Error
       setting 00000000 to %016llX: %s\n", noop_noop_noop_halt,
       sim_panel_get_error()); goto Done;
        } */
    if (sim_panel_gen_deposit(panel, "P", sizeof(addr0100), &addr0100))
    {
      printf("Error setting p to %08X: %s\n", addr0100, sim_panel_get_error());
      goto Done;
    }
    if (sim_panel_exec_start(panel))
    {
      printf("Error starting simulator execution: %s\n", sim_panel_get_error());
      goto Done;
    }
    while ((sim_panel_get_state(panel) == Run) && (mstime < 1000))
    {
      usleep(100000);
      mstime += 100;
    }
    if (sim_panel_get_state(panel) != Halt)
    {
      printf("Unexpected execution state not Halt: %d\n",
             sim_panel_get_state(panel));
      goto Done;
    }
    pc_value = 0;
    if (sim_panel_gen_examine(panel, "P", sizeof(pc_value), &pc_value))
    {
      printf("Unexpected error getting p value: %s\n", sim_panel_get_error());
      goto Done;
    }
    if (pc_value != addr0100 + 4)
    {
      printf("Unexpected error getting p value: %08X, expected: %08X\n",
             pc_value, addr0100 + 4);
      goto Done;
    }
#endif
  }

  if (sim_panel_add_register(panel, "P", NULL, sizeof(P), &P))
  {
    printf("Error adding register 'P': %s\n", sim_panel_get_error());
    goto Done;
  }
  // restire atP to be the instruction at the P register
  if (sim_panel_add_register_indirect(panel, "P", NULL, sizeof(atP), &atP))
  {
    printf("Error adding register indirect 'P': %s\n", sim_panel_get_error());
    goto Done;
  }
  if (sim_panel_add_register(panel, "A", NULL, sizeof(A), &A))
  {
    printf("Error adding register 'A': %s\n", sim_panel_get_error());
    goto Done;
  }
  if (sim_panel_add_register(panel, "B", NULL, sizeof(B), &B))
  {
    printf("Error adding register 'B': %s\n", sim_panel_get_error());
    goto Done;
  }
  if (sim_panel_add_register(panel, "X", NULL, sizeof(X), &X))
  {
    printf("Error adding register 'X': %s\n", sim_panel_get_error());
    goto Done;
  }
  if (sim_panel_get_registers(panel, NULL))
  {
    printf("Error getting register data: %s\n", sim_panel_get_error());
    goto Done;
  }
#ifdef bigtest // BJD ***
  if (1)
  {
    unsigned int deadbeef = 0123456, beefdead = 0123456, addr200 = 0x0000200,
                 beefdata;

    if (sim_panel_set_register_value(panel, "A", "123456"))
    {
      printf("Error setting A to 123456: %s\n", sim_panel_get_error());
      goto Done;
    }
    if (sim_panel_mem_deposit(panel, sizeof(addr200), &addr200,
                              sizeof(deadbeef), &deadbeef))
    {
      printf("Error setting 00000200 to 123456: %s\n", sim_panel_get_error());
      goto Done;
    }
    beefdata = 0;
    if (sim_panel_mem_examine(panel, sizeof(addr200), &addr200,
                              sizeof(beefdata), &beefdata))
    {
      printf("Error getting contents of memory location 0200: %s\n",
             sim_panel_get_error());
      goto Done;
    }
    beefdata = 0;
  }
  if (sim_panel_get_registers(panel, NULL))
  {
    printf("Error getting register data: %s\n", sim_panel_get_error());
    goto Done;
  }
#endif
  if (sim_panel_set_display_callback_interval(panel, &DisplayCallback, NULL,
                                              500000))  // slow down from 200 000
  {
    printf("Error setting automatic display callback: %s\n",
           sim_panel_get_error());
    goto Done;
  }
  sim_panel_clear_error();

#ifdef bigtest // BJD ***

  if (sim_panel_break_set(panel, "400"))
  {
    printf("Unexpected error establishing a breakpoint: %s\n",
           sim_panel_get_error());
    goto Done;
  }
  if (sim_panel_break_clear(panel, "400"))
  {
    printf("Unexpected error clearing a breakpoint: %s\n",
           sim_panel_get_error());
    goto Done;
  }
  if (sim_panel_break_output_set(panel, "\"32..31..30\""))
  {
    printf("Unexpected error establishing an output breakpoint: %s\n",
           sim_panel_get_error());
    goto Done;
  }
  if (sim_panel_break_output_clear(panel, "\"32..31..30\""))
  {
    printf("Unexpected error clearing an output breakpoint: %s\n",
           sim_panel_get_error());
    goto Done;
  }
  if (sim_panel_break_output_set(
          panel, "-P \"Normal operation not possible.\" SHOW QUEUE"))
  {
    printf("Unexpected error establishing an output breakpoint: %s\n",
           sim_panel_get_error());
    goto Done;
  }
  if (sim_panel_break_output_set(panel, "-P \"Device? [XQA0]: \""))
  {
    printf("Unexpected error establishing an output breakpoint: %s\n",
           sim_panel_get_error());
    goto Done;
  }
  if (sim_panel_break_output_set(panel, "-P \"(1..15): \" SEND \"4\\r\"; GO"))
  {
    printf("Unexpected error establishing an output breakpoint: %s\n",
           sim_panel_get_error());
    goto Done;
  }
#endif // BJD
  // BJD Debug - only set sampling parameters once
  // if (!sim_panel_set_sampling_parameters_ex(panel, 0, 0, 199))
  // {
  //   printf("Unexpected success setting sampling parameters to 0, 0, 199\n");
  //   goto Done;
  // }
  // if (!sim_panel_set_sampling_parameters_ex(panel, 199, 0, 0))
  // {
  //   printf("Unexpected success setting sampling parameters to 199, 0, 0\n");
  //   goto Done;
  // }
  // if (!sim_panel_set_sampling_parameters_ex(panel, 500, 40, 100))
  // {
  //   printf("Unexpected success setting sampling parameters to 500, 40, 100\n");
  //   goto Done;
  // }
  if (sim_panel_set_sampling_parameters_ex(panel, 500, 10, 100))
  {
    printf("Unexpected error setting sampling parameters to 500, 10, 100: %s\n",
           sim_panel_get_error());
    goto Done;
  }
  // if (sim_panel_add_register_indirect_bits(panel, "P", NULL, 32,
  //                                          PC_indirect_bits))
  // {
  //   printf("Error adding register 'P' indirect bits: %s\n",
  //          sim_panel_get_error());
  //   goto Done;
  // }
  // if (sim_panel_add_register_bits(panel, "P", NULL, 16, P_bits))
  // {
  //   printf("Error adding register 'P' bits: %s\n", sim_panel_get_error());
  //   goto Done;
  // }

#ifdef bigtest2
  if (1) // test loop of code
  {
    unsigned int noop_noop_noop_halt = 0x81008100, brb_self = 0x0600,
                 addr0100 = 0100, pc_value;
    int mstime;

    /* new code to place noops in memory */
    int addrx = 0100;
    int j;
    int *addrofi;

    static int inst[4] = {0101000, 0101000, 0101000, 0};

    for (j = 0; j < 4; j++)
    {
      addrofi = &inst[j];
      sim_panel_mem_deposit(panel, sizeof(addrx), &addrx, sizeof(inst[0]),
                            addrofi);
      addrx += 1;
    }

    /*
if (sim_panel_mem_deposit (panel, sizeof(addr0100), &addr0100,
sizeof(noop_noop_noop_halt), &noop_noop_noop_halt)) { printf ("Error setting
%08X to %08X: %s\n", addr0100, noop_noop_noop_halt, sim_panel_get_error()); goto
Done;
        }
*/
    if (sim_panel_gen_deposit(panel, "P", sizeof(addr0100), &addr0100))
    {
      printf("Error setting P to %08X: %s\n", addr0100, sim_panel_get_error());
      goto Done;
    }
    if (sim_panel_exec_run(panel))
    {
      printf("Error starting simulator execution: %s\n", sim_panel_get_error());
      goto Done;
    }
    if (!sim_panel_get_registers(panel, NULL))
    {
      printf("Unexpected success getting register data: %s\n",
             sim_panel_get_error());
      goto Done;
    }
    mstime = 0;
    while ((sim_panel_get_state(panel) == Run) && (mstime < 1000))
    {
      usleep(100000);
      mstime += 100;
    }
    if (sim_panel_get_state(panel) != Halt)
    {
      printf("Unexpected execution state not Halt\n");
      goto Done;
    }
    pc_value = 0;
    if (sim_panel_gen_examine(panel, "P", sizeof(pc_value), &pc_value))
    {
      printf("Unexpected error getting P value: %s\n", sim_panel_get_error());
      goto Done;
    }
    if (pc_value != addr0100 + 4)
    {
      printf("Unexpected P value after HALT: %06o, expected: %06o\n", pc_value,
             addr0100 + 4); // code started at 0100 - 3 nop s and hlt
      goto Done;
    }
    if (sim_panel_gen_deposit(panel, "P", sizeof(addr0100), &addr0100))
    {
      printf("Error setting P to %08X: %s\n", addr0100, sim_panel_get_error());
      goto Done;
    }
#ifdef bigtest
    if (sim_panel_exec_step(panel))
    {
      printf("Error executing a single step: %s\n", sim_panel_get_error());
      goto Done;
    }
    pc_value = 0;
    if (sim_panel_gen_examine(panel, "P", sizeof(pc_value), &pc_value))
    {
      printf("Unexpected error getting P value: %s\n", sim_panel_get_error());
      goto Done;
    }
    if (pc_value != addr0100 + 1)
    {
      printf("Unexpected P value after STEP: %060, expected: %06o\n", pc_value,
             addr0100 + 1);
      goto Done;
    }
#endif
    if (sim_panel_mem_deposit(panel, sizeof(addr0100), &addr0100,
                              sizeof(brb_self), &brb_self))
    {
      printf("Error setting %08X to %08X: %s\n", addr0100, brb_self,
             sim_panel_get_error());
      goto Done;
    }
    if (sim_panel_gen_deposit(panel, "P", sizeof(addr0100), &addr0100))
    {
      printf("Error setting P to %08X: %s\n", addr0100, sim_panel_get_error());
      goto Done;
    }
    if (sim_panel_exec_run(panel))
    {
      printf("Error starting simulator execution: %s\n", sim_panel_get_error());
      goto Done;
    }
    mstime = 0;
    while ((sim_panel_get_state(panel) == Run) && (mstime < 1000))
    {
      usleep(100000);
      mstime += 100;
    }
    if (sim_panel_exec_halt(panel))
    {
      printf("Error executing halt: %s\n", sim_panel_get_error());
      goto Done;
    }
    if (sim_panel_get_state(panel) != Halt)
    {
      printf("State not Halt after successful Halt\n");
      goto Done;
    }
  }
#endif
  sim_panel_clear_error();
  return 0;

  //  Error return

Done:
  sim_panel_destroy(panel); /* stop the h316 simulator program. */
  panel = NULL;

  /* Get rid of pseudo config file created above */
  (void)remove(sim_config);
  return -1;
}

/* BJD new commands */

int sim_go(PANEL *panel, const char *string, const char *device) /*****************DEBUG BJD ***** */
{
  /* 	new code to set register value "bryan <x> <yyy>" where x = a,b,c,p and yyy = octal string */
  /* new code for "GO <xxx"*/
  char *ptr;
  const char *reg;
  const char value; // xxx = value
  char string2[2];
  int addr;
  char *go;
  int16_t p_value; // set this to P
  char *valuex;
  char *stringtext; // points to command

  stringtext = (char *)string;      // move parameter to char* pointer
  go = strtok(stringtext, " \r\n"); // get pointer to "GO"
  valuex = strtok(NULL, " \n\r");   // get pointer to 2nd token (or NULL)
  if (valuex != NULL)
  {
    p_value = strtoul(valuex, NULL, 8); // if specified -
    if (sim_panel_gen_deposit(panel, "P", sizeof(p_value), &p_value))
    {
      printf("Error setting p to %06o: %s\n", value, sim_panel_get_error());
      // goto Done;
    }
  }
  sim_panel_debug(panel, "GO command\n");
  sim_panel_flush_debug(panel);

 // if (sim_panel_exec_start(panel))
  if (sim_panel_exec_run(panel))
  {
    printf("Error starting execution (GO command)\n");
  }; /* start execution */

  sim_panel_debug(panel, "GO command complete\n");
  sim_panel_flush_debug(panel);

  return (0);
}

int match_command(const char *command, const char *string, const char **arg)
{
  int match_chars = 0;
  size_t i;

  while (isspace(*string))
    ++string;
  for (i = 0; i < strlen(command); i++)
  {
    if (command[i] == (islower(string[i]) ? toupper(string[i]) : string[i]))
      continue;
    if (string[i] == '\0')
      break;
    if ((!isspace(string[i])) || (i == 0))
      return 0;
    break;
  }
  while (isspace(string[i]))
    ++i;
  if (arg)
    *arg = &string[i];
  return (i > 0) && (arg ? 1 : (string[i] == '\0'));
}

int process_front_panel_input(char cmd[])
{
  printf("process front panel pushbutton %s\n",cmd);
};

struct execution_breakpoint
{
  unsigned int addr;
  const char *desc;
  const char *extra;
} breakpoints[] = {
    {0x2004EAD3, "test 52 failure path"},
    {0x2004E6EC,
     "Test 52: de_programmable_timers.lis line 228 - Generic Error Dispatch",
     "SHOW HIST=10; EX SYSD STATE"},
    {0x2004E7F9,
     "Test 52: de_programmable_timers.lis line 381 - Interrupt Did Not Occur",
     "SHOW HIST=10; EX SYSD STATE"},
    {0x2004E97C,
     "Test 53: Subtest 05 - clock failed to tick within at least 100 ms. - "
     "de_toy.lis line 232",
     "SHOW HIST=10; EX SYSD STATE"},
    {0x2004E9BB,
     "Test 53: Subtest 07 - Time of year clock is not ticking - de_toy.lis "
     "line 274",
     "SHOW HIST=10; EX SYSD STATE"},
    {0x2004E9D3,
     "Test 53: Subtest 08 - Time of year clock is not ticking - de_toy.lis "
     "line 295",
     "SHOW HIST=10; EX SYSD STATE"},
    {0x2004EA2D, "Test 53: Subtest 09 - Running Slow - de_toy.lis line 359",
     "SHOW HIST=10; EX SYSD STATE"},
    {0x2004EA39, "Test 53: Subtest 0A - Running Fast - de_toy.lis line 366",
     "SHOW HIST=10; EX SYSD STATE"},
    {0x0, NULL}};

int main(int argc, char **argv) /********** main ************************** */
{
    int run_state = 0;  // Run vs. Halt (enum)
    int main_state = 0; // starting condition

    char jsonbuf[512]; //     send_json_regs(jsonbuf);

    async_start(async_debug);         // start thread to read USB connection to H316 hardware frontpanel FW
    event_input_start();   // start thread for input from h316 hw buttons
    stdin_input_start();  // start thread to read stdin and enque in fifo1
    int was_halted = 1, i; // was 1

  if ((argc > 1) && ((!strcmp("-d", argv[1])) || (!strcmp("-D", argv[1])) ||
                     (!strcmp("-debug", argv[1]))))
    debug = 1;
    // debug_opt.option1 = 0;
    // debug_opt.option2 = 0;

  //
  // bypass some testing
  //

  /* ************************************************* */
  /*	test operator commands			*/

  struct
  {
    unsigned short int addr;
    const char *instr;
  } long_running_program[] = {{0100, "aoa"}, {0101, "sze"}, {0102, "jmp 100"}, {0103, "irs 0"}, {0104, "jmp 100"}, {0105, "jmp 100"}, {0, NULL}};
  // } long_running_program[] = {{0100, "irs 0"}, {0101, "jmp 100"}, {0102, "aoa"}, {0103, "jmp 100"}, {0104, "nop"}, {0105, "jmp 100"}, {0, NULL}};
  // long_running_program[] = {{0100, "irs 0"}, {0101, "jmp 100"}, {0102, "aoa"}, {0103, "jmp 100"}, {0104, "hlt"}, {0105, "jmp 100"}, {0, NULL}};
  /*  irs 0
      jmp 100
      aoa
      jmp 100
      hlt
      jmp 100 */

  sim_panel_clear_error();
  InitDisplay();

  if (panel_setup())
    goto Done;
  sim_panel_debug(panel, "call panel_setup\n");
  sim_panel_flush_debug(panel);
  /*	remove breakpoint tests -(specific to VAX)---------------
  ----------------- */
  int16_t addr100 = 0100;

  sim_panel_debug(panel, "Testing with Command interface");
  DisplayRegisters(panel, 1, 1);

  /* DEBUG BJD - check this doloop */
  for (i = 0; long_running_program[i].instr; i++)
  {
    printf("deposit instruction %o = %s \n", long_running_program[i].addr, long_running_program[i].instr);
    if (sim_panel_mem_deposit_instruction(
            panel, sizeof(long_running_program[i].addr),
            &long_running_program[i].addr, long_running_program[i].instr))
    {
      printf("Error setting depositing instruction '%s' into memory at "
             "location %XA: %s\n",
             long_running_program[i].instr, long_running_program[i].addr,
             sim_panel_get_error());
      goto Done;
    }
  }

  if (sim_panel_gen_deposit(panel, "P", sizeof(addr100), &addr100))
  {
    printf("Error setting p to %06o: %s\n", addr100, sim_panel_get_error());
  }

  usleep(1000000); //
  static char cmd[512];

  // if (sim_panel_exec_run(panel)) // start execution
  if (sim_panel_exec_start(panel)) // start execution
    goto Done;

  // add delay

  usleep(1000000); //

  sim_panel_debug(panel, "start long-running loop\n");
  sim_panel_flush_debug(panel);

  
  const char *arg;
  int display_count = 0;
  int n; // value returned from fifo
  int ctr = 0; //

  // new long running loop code 7/9/2026 BJD

  while (1)
  {
    char cmd[512];

    run_state = sim_panel_get_state(panel);
    printf("main loop %d: run_state = %d, main_state = %d\n", ctr++, run_state, main_state);

    if ((run_state == Halt) || (main_state == 2))
    {
      switch (main_state)
      {
      case 0: // first time in Halt state
      {
        run_state_loop = 0; // set flag for displaying register on callback
        sim_panel_debug(panel, "Halted - Getting registers...");
        sim_panel_get_registers(panel, &simulation_time);
        if (!was_halted)
        {
          const char *haltmsg = sim_panel_halt_text(panel);
          const char *bpt;
          unsigned int Bpt_PC;

          usleep(100000); // delay

          DisplayRegisters(panel, 1, 1);
          if (*haltmsg)
            printf("%s", haltmsg);
          if ((bpt = strstr(haltmsg, "Breakpoint, PC: ")))
          {
            sscanf(bpt, "Breakpoint, PC: %X", &Bpt_PC);
            for (i = 0; breakpoints[i].addr; i++)
            {
              if (Bpt_PC == breakpoints[i].addr)
              {
                printf("Breakpoint at: %08X %s\n", breakpoints[i].addr, breakpoints[i].desc);
                break;
              }
            }
          }
        }
        //
        //
        was_halted = 1;
        printf("SIM> "); /*************************************************** */
        main_state = 1;
        break;
      case 1: // halt state after initial processing

        main_state = 2;
        break;
      case 2: // halt state awaiting SIM> response
              // process operator command in Halt state
        char xname[16];
        int xval; // dummy parms
        // wait until event is available
        pthread_mutex_lock(&fifo_mutex);

        while (fifo_query(&fifo1) == 0)
        {
          // take action when button is pushed
          printf("top wait run_state = %d\n",run_state);
          pthread_cond_wait(&fifo_wait, &fifo_mutex); // wait for signal
        }
        fifo_out(&fifo1, xname, &xval);
        pthread_mutex_unlock(&fifo_mutex);

        if (!get_input_event(cmd, sizeof(cmd) - 1)) /* input event - either console or front panel */
          break;                                           /* front panel event */
        while (strlen(cmd) && isspace(cmd[strlen(cmd) - 1]))
          cmd[strlen(cmd) - 1] = '\0';

        DisplayRegisters(panel, 1, 1);
        if (strcmp("Input_waiting",cmd) == 0) {
          process_front_panel_input(cmd); // a button was pushed
          main_state = 0;
          break;
        }
        else if (match_command("BOOT", cmd, &arg))
        {
          if (sim_panel_exec_boot(panel, arg))
            break;
        }
        else if (match_command("BREAK ", cmd, &arg))
        {
          if (sim_panel_break_set(panel, arg))
            printf("Error Setting Breakpoint '%s': %s\n", arg, sim_panel_get_error());
        }
        else if (match_command("NOBREAK ", cmd, &arg))
        {
          if (sim_panel_break_clear(panel, arg))
            printf("Error Clearing Breakpoint '%s': %s\n", arg, sim_panel_get_error());
        }
        else if (match_command("STEP", cmd, NULL))
        {
          if (sim_panel_exec_step(panel))
            break;
        }
        else if (match_command("GO", cmd, &arg))
        { /* go p 33000 */
          if (sim_go(panel, arg, NULL))
            break;
        }
        else if (match_command("CONT", cmd, NULL))
        {
          // if (sim_panel_exec_run (panel))
          //     break;
          sim_panel_exec_run(panel);
          main_state = 3; // 
          break;
        }
        else if (match_command("EXAMINE ", cmd, &arg))
        {
          int value;

          if (sim_panel_gen_examine(panel, arg, sizeof(value), &value))
            printf("Error EXAMINE %s: %s\n", arg, sim_panel_get_error());
          else
            printf("%s: %08X\n", arg, value);
        }
        else if (match_command("HISTORY ", cmd, &arg))
        {
          char history[10240];
          int count = atoi(arg);

          history[sizeof(history) - 1] = '\0';
          if (sim_panel_get_history(panel, count, sizeof(history) - 1, history))
            printf("Error retrieving instruction history: %s\n", sim_panel_get_error());
          else
            printf("%s\n", history);
        }
        else if (match_command("DEBUG ", cmd, &arg))
        {
          if (arg[0] == '-')
          {
            if (sim_panel_device_debug_mode(panel, NULL, 1, arg))
              printf("Error setting debug mode: %s\n", sim_panel_get_error());
          }
          else
          {
            if (sim_panel_device_debug_mode(panel, arg, 1, NULL))
              printf("Error setting debug mode: %s\n", sim_panel_get_error());
          }
        }
        else if ((match_command("EXIT", cmd, NULL)) || (match_command("QUIT", cmd, NULL)))
          goto Done;
        else
        {
          DisplayRegisters(panel, 1, 1); // final parm 1 = restore cursor posn
          printf("Huh? %s\r\n", cmd);
        }
      }
        main_state = 0; // back to normal
        break;

      case 3:
        break; // do nothing
      default:
        printf("Undefined state %d\n", main_state);
        main_state = 0;
      } /* end of switch(main_state) */
    }
    else if (run_state == Run)
    {
      char xname[16];
      int xval; // dummy parms
                // wait for event queue
                // input event sets main_state = 2
                // wait until event is available
      main_state = 0;
      pthread_mutex_lock(&fifo_mutex);

      while (fifo_query(&fifo1) == 0)
      {
        
        printf("bottom wait run_state = %d\n",run_state);// take action when button is pushed
        pthread_cond_wait(&fifo_wait, &fifo_mutex); // wait for signal
      }
      fifo_out(&fifo1, xname, &xval);
      pthread_mutex_unlock(&fifo_mutex);

      run_state = sim_panel_get_state(panel); // in case the state changed

      // process operator commands in Run state (few)

      // process all h316 front panel button push events (many)
      // check for type of event
      if (strcmp(xname, "B_Run") == 0)
      {
        printf("B_Run message removed from fifo %s %d\n", xname, xval);
        main_state = 0;   // set up for console input
        if (sim_panel_exec_halt(panel)) // Event: Run button pushed
        {
          printf("Error halting simulator execution: %s\n", sim_panel_get_error());
          goto Done;
        }
        //
        send_json_regs(jsonbuf); // make sure we display the latest registers
      }
      else if (strcmp(xname, "DisplayUpdate") == 0) //
      {
        send_json_regs(jsonbuf); // Event: time make sure we display the latest registers
      }
      else
      {
        printf("Unidentified message removed from fifo %s %d\n", xname, xval);
        // break;
      }
    }
  }

Done:
  DisplayRegisters(panel, 0, 1);
  sim_panel_destroy(panel); /* stop the h316 simulator */

  /* Get rid of pseudo config file created earlier */
  (void)remove(sim_config);
}

void super_get_input_event()
{
                                                           // process operator command in Halt state
                                                                 char xname[16];
      int xval; // dummy parms
                // wait for event queue
                // input event sets main_state = 2
                // wait until event is available
      pthread_mutex_lock(&fifo_mutex);

      while (fifo_query(&fifo1) == 0)
      {
        // take action when button is pushed
        pthread_cond_wait(&fifo_wait, &fifo_mutex); // wait for signal
      }
      fifo_out(&fifo1, xname, &xval);
      pthread_mutex_unlock(&fifo_mutex);
}
