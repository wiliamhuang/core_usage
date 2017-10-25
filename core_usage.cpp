// Compile: g++ -O2 -o core_usage core_usage.cpp -lX11 -lncurses
// Run:     ./a.out 0.3
//                  the time interval (in seconds) for info update
//          The GUI will show up if X11 is available. If not, the 
//          console version will run. If you want to run the console 
//          version even you have X11, 
//          ./a.out 0.3 txt_mode

// Written by Lei Huang at Texas Advanced Computing Center.
//
// A part of code is from, 
// http://www.linuxforums.org/forum/programming-scripting/117491-useful-timer-without-blocking-xevents.html
// which is partially based on code from http://www.linuxquestions.org/questions/programming-9/xnextevent-select-409355/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/time.h>
#include <time.h>
#include <ctype.h>
#include <curses.h>
#include <signal.h>

#define MAX_CORE	(1024)
#define MAX_SOCKET	(4)
#ifndef max(a,b)
#define max(a,b)	(((a)>(b))?(a):(b))
#endif

#ifndef timeradd
# define timeradd(a, b, result)							\
	do {													\
	(result)->tv_sec = (a)->tv_sec + (b)->tv_sec;		\
	(result)->tv_usec = (a)->tv_usec + (b)->tv_usec;	\
	if ((result)->tv_usec >= 1000000)					\
		{												\
		++(result)->tv_sec;								\
		(result)->tv_usec -= 1000000;						\
		}												\
	} while (0)
#endif
#ifndef timersub
# define timersub(a, b, result)						\
	do {													\
	(result)->tv_sec = (a)->tv_sec - (b)->tv_sec;		\
	(result)->tv_usec = (a)->tv_usec - (b)->tv_usec;	\
	if ((result)->tv_usec < 0) {						\
	--(result)->tv_sec;								\
	(result)->tv_usec += 1000000;					\
	}													\
	} while (0)
#endif

struct timeval tv;
struct timeval stv;
struct timeval tv_period;

Display *dis;
Window win;
int screen;
GC gc;

int nCore, nThread_per_Core, nCPU;
unsigned long long cur_user[MAX_CORE], cur_nice[MAX_CORE], cur_system[MAX_CORE], cur_idle[MAX_CORE], 
cur_iowait[MAX_CORE], cur_irq[MAX_CORE], cur_softirq[MAX_CORE], cur_steal[MAX_CORE];

unsigned long long old_user[MAX_CORE], old_nice[MAX_CORE], old_system[MAX_CORE], old_idle[MAX_CORE], 
old_iowait[MAX_CORE], old_irq[MAX_CORE], old_softirq[MAX_CORE], old_steal[MAX_CORE];
float Core_Usage[MAX_CORE];

int nSocket=0, nCore_Socket=0;
int SocketID[MAX_CORE];
int CoreID[MAX_CORE];	// which core this thread is located on. Terminal version. 
int ThreadID[MAX_CORE];	// store the thread index on the core it sits in. Terminal version. 


int bar_width, bar_height=200, extra=55, x0, y0, win_width, win_height;
char szHostName[256];

void timerFired();
void Init_Core_Stat();
void Read_Proc_Stat(void);
void Save_Core_Stat(void);
void Cal_Core_Usage(void);
void Setup_bar_width(void);

void Format_Two_Digital(int number, char szBuf[]);
static void Clean_up(int sig, siginfo_t *siginfo, void *ptr);	// Used by terminal version
void Extract_Thread_Mapping_Info(void);	// Used by terminal version


void Setup_bar_width(void)
{
	if(nCore <= 24)	{
		bar_width = 36;
	}
	else if(nCore <= 64)	{
		bar_width = 16;
	}
	else if(nCore <= 128)	{
		bar_width = 12;
	}
	else	{
		bar_width = 4;
	}
}

void Cal_Core_Usage(void)
{
	int i;
	unsigned long long cur_Idle, cur_NonIdle, old_Idle, old_NonIdle;
	
	Read_Proc_Stat();
	
	for(i=0; i<nCore; i++)	{
		cur_Idle = cur_idle[i] + cur_iowait[i];
		cur_NonIdle = cur_user[i] + cur_nice[i] + cur_system[i] + cur_irq[i] + cur_softirq[i] + cur_steal[i];
		
		old_Idle = old_idle[i] + old_iowait[i];
		old_NonIdle = old_user[i] + old_nice[i] + old_system[i] + old_irq[i] + old_softirq[i] + old_steal[i];
		
		Core_Usage[i] = 1.0*(cur_NonIdle - old_NonIdle)/(cur_Idle+cur_NonIdle - old_Idle - old_NonIdle);
		//		printf("Core %3d: %4.3f\n", i, Core_Usage[i]);
	}
	
	Save_Core_Stat();
}

class xtimer {	
	int dis;
	int x11_fd;
	fd_set in_fds;
	void (*tickFunc)(); 
	
public:
	// display, period in seconds 0.25 etc, 
	// and void function() pointer thats called each "tick"
	xtimer(Display *d,float p,void (*fp)()) {
		tickFunc=fp;
		dis=0;
		x11_fd=ConnectionNumber(d);
		tv_period.tv_sec = 0;
		tv_period.tv_usec = (int)(p*1000000); // 1000000us = 1000ms = 1.0s 
		tv.tv_sec = tv_period.tv_sec;       // Set tv=1 sec so select() will timeout.
		tv.tv_usec = tv_period.tv_usec;
		gettimeofday(&stv, 0);              // Get the time of day and
		timeradd(&stv, &tv_period, &stv);   // Trust my math for now.. :)
	}
	
	void check() {
		// Create a File Description Set containing x11_fd
		FD_ZERO(&in_fds);
		FD_SET(x11_fd, &in_fds);
		
        // Wait for X Event or a Timer, so you can only have 1 timer at a time...
		if (select(x11_fd+1, &in_fds, 0, 0, &tv)) {
			gettimeofday(&tv, 0);
			timersub(&stv, &tv, &tv);  // set tv = remaining time.
		}
		else {
			if (!dis) {
				tickFunc();
				// Initialize timer variables again.
				tv.tv_sec = tv_period.tv_sec;       // Set tv=1 sec so select() will timeout.
				tv.tv_usec = tv_period.tv_usec;
				gettimeofday(&stv, 0);
				timeradd(&stv, &tv_period, &stv);  // Trust my math for now.. :)
			}
        }
	}
	
	void disable() {
		dis=1;
		tv.tv_sec =1;
	}
	
	void enable() {
		dis=0;
		tv.tv_sec =0;
	}
	
	int running() {
		return 1-dis;
	}
};

xtimer *t;
float tInterval=0.3;

WINDOW * mainwin;

void Run_Terminal_version(void)
{
    int ch, i, j, iMax, nLine, nCol, cpu_idx, thread_idx, Width=32;
	time_t t;
	struct tm tm;
	char szNull[]="                                                                                         ";
	char szTime[128], szMonth[8], szDay[8], szHour[8], szMin[8], szSec[8];
	struct sigaction act;
	
	Extract_Thread_Mapping_Info();
	nCPU = nCore/nThread_per_Core;
	
    if ( (mainwin = initscr()) == NULL ) {
		fprintf(stderr, "Error initializing ncurses.\n");
		exit(EXIT_FAILURE);
    }
	
	start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
	attron(COLOR_PAIR(1));
	
	if(nCPU <= 32)	{
		nLine = nCPU;	nCol = 1;
	}
	else if(nCPU <=64)	{
		nLine = (nCPU+1)/2;		nCol = 2;
	}
	else	{
		nLine = (nCPU+2)/3;		nCol = 3;
	}
	
    noecho();                  /*  Turn off key echoing                 */
    keypad(mainwin, TRUE);     /*  Enable the keypad for non-char keys  */
	
    memset (&act, 0, sizeof(act));
    act.sa_sigaction = Clean_up;
    act.sa_flags = SA_SIGINFO;
	
    if (sigaction(SIGINT, &act, 0)) {
        perror("Error: sigaction");
        exit(1);
    }
	
	usleep(50000);
	
    while (1) {
		iMax = nLine + 4;
		for(i=1; i<=iMax;i++)	{
			mvprintw(i, 0, "%s", szNull);	// empty everything. Useful when resizing the terminal
		}
		
		Cal_Core_Usage();
		
		t = time(NULL);
		tm = *localtime(&t);
		Format_Two_Digital(tm.tm_mon + 1, szMonth);
		Format_Two_Digital(tm.tm_mday, szDay);
		Format_Two_Digital(tm.tm_hour, szHour);
		Format_Two_Digital(tm.tm_min, szMin);
		Format_Two_Digital(tm.tm_sec, szSec);
		
		sprintf(szTime, "Now: %s/%s/%d %s:%s:%s on node %s", szMonth, szDay, tm.tm_year + 1900, szHour, szMin, szSec, szHostName);
		mvprintw(0, 2, "%s", szTime);
		
		for(i=0; i<nCol; i++)	{	// loop over column
			for(j=0; j<nThread_per_Core; j++)	{
				mvprintw(2, 10 + Width*i + 5*j, "   T%d", j);
			}
		}
		
		for(i=0; i<nCPU; i++)	{
			mvprintw(3+(i%nLine), 1+Width*(i/nLine), "CORE %3d: ", i);
		}
		
		for(i=0; i<nCore; i++)	{
//			cpu_idx = CoreID[i];
			cpu_idx = CoreID[i] + SocketID[i]*nCore_Socket;
			thread_idx = ThreadID[i];
			if(Core_Usage[i] > 0.02)	attron(COLOR_PAIR(2));	// Use special color for non-idle core.
			mvprintw(3+(cpu_idx%nLine), 12 + 5*thread_idx + Width*(cpu_idx/nLine), "%3.2f", Core_Usage[i]);
			if(Core_Usage[i] > 0.02)	attron(COLOR_PAIR(1));	// Restore the default color.
		}
		mvprintw(nLine+5, 2, "Use Ctrl+c to quit.");
		mvprintw(0, 0, "");
		refresh();
		usleep((int)(1000000*tInterval));
    }
	
    return;
}

int main(int argc, char *argv[]) {
	int Run=1, GUI_On=1;
	XEvent ev;
	
	if(argc >= 2)	{
		tInterval = atof(argv[1]);
		if(argc == 3)	{	// Run the terminal version
			GUI_On = 0;
      printf("To run the console version after one second.\n");
		}
	}
	
	Init_Core_Stat();
	Read_Proc_Stat();
	Setup_bar_width();
	
	gethostname(szHostName, 255);
	
	dis = XOpenDisplay(NULL);
	if( (dis == NULL) || (GUI_On == 0) )	{
		if(dis == NULL) printf("Fail to open DISPALY. Did you set up X11 forwarding?\nThe terminal version will run.\n");
		sleep(1);
		Run_Terminal_version();
		return 0;
	}
	
	//	printf("display = %x\n", dis);
	screen = DefaultScreen(dis);
	//	printf("screen = %x\n", screen);
	win_width = bar_width*(nCore-1)+2*extra;
	win_height = bar_height+2*extra;
	win = XCreateSimpleWindow(dis, RootWindow(dis, 0), 1, 1, bar_width*(nCore-1)+2*extra, bar_height+2*extra, \
        0, WhitePixel(dis, 0), WhitePixel(dis, 0));
	
    // You don't need all of these. Make the mask as you normally would.
	XSelectInput(dis, win, 
		ExposureMask | KeyPressMask | KeyReleaseMask | PointerMotionMask |
		ButtonPressMask | ButtonReleaseMask  | StructureNotifyMask 
		);
	
	XMapWindow(dis, win);
	gc = DefaultGC(dis, screen);
	
	Atom WM_DELETE_WINDOW = XInternAtom(dis, "WM_DELETE_WINDOW", False); 
	XSetWMProtocols(dis, win, &WM_DELETE_WINDOW, 1);
	XFlush(dis);
	
	t=new xtimer(dis,tInterval,timerFired);
	
    // Main loop
	while(Run) {
		t->check(); // this blocks till time runs out or Xevent comes
		
		// Handle XEvents and flush the input, if timers stopped block for next event (probably map!)
		while(XPending(dis) || t->running()==0) {
			XNextEvent(dis, &ev);
			if (ev.type==UnmapNotify) { t->disable(); }
			else if (ev.type==MapNotify) { t->enable(); }
			else if (ev.type == ClientMessage) {
				Run = 0;
				break;
			}
		}
	}
	return(0);
}

void Format_Two_Digital(int number, char szBuf[])
{
	if(number <10)	{
		sprintf(szBuf,"0%d", number);
	}
	else	{
		sprintf(szBuf,"%d", number);
	}
}

void DrawLines(void)
{
	char szCoreIdx[5][64]={"0", "xx", "xx", "xx", "271"};
	const char *szUsage[]={"0%", "50%", "100%"};
	const char *szAxis[]={"Core Id", "Utilization"};
	char szMonth[8], szDay[8], szHour[8], szMin[8], szSec[8];
	XSegment line_list[4];
	int nMid, nMid_L, nMid_R;
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	char szTime[256];
	int nBufLen;
	
	line_list[0].x1 = extra;					line_list[0].y1 = bar_height+extra;	
	line_list[0].x2 = extra+bar_width*nCore;	line_list[0].y2 = bar_height+extra;	
	
	line_list[1].x1 = extra;					line_list[1].y1 = bar_height+extra;	
	line_list[1].x2 = extra;					line_list[1].y2 = extra;
	
	line_list[2].x1 = extra;					line_list[2].y1 = extra;	
	line_list[2].x2 = extra+bar_width*nCore;	line_list[2].y2 = extra;	
	
	line_list[3].x1 = extra;					line_list[3].y1 = extra+bar_height*0.5;	
	line_list[3].x2 = extra+bar_width*nCore;	line_list[3].y2 = extra+bar_height*0.5;	
	
	XSetForeground(dis, gc, 0x0);
	
	XDrawSegments(dis, win, gc, line_list, 4);
	
	nMid = (int)((nCore-1)/2);
	nMid_L = (int)((nMid)/2);
	nMid_R = (int)((nCore-1+nMid)/2);
	sprintf(szCoreIdx[1], "%d", nMid_L);
	sprintf(szCoreIdx[2], "%d", nMid);
	sprintf(szCoreIdx[3], "%d", nMid_R);
	sprintf(szCoreIdx[4], "%d", nCore-1);
	XDrawString(dis, win, gc, extra, extra+bar_height+14, szCoreIdx[0], strlen(szCoreIdx[0]));
	
	if(nCore>4) XDrawString(dis, win, gc, extra+(int)((nMid_L-1+0.5)*bar_width), extra+bar_height+14, szCoreIdx[1], strlen(szCoreIdx[1]));
	if(nCore>2) XDrawString(dis, win, gc, extra+(int)((nMid-1+0.5)*bar_width), extra+bar_height+14, szCoreIdx[2], strlen(szCoreIdx[2]));
	if(nCore>4) XDrawString(dis, win, gc, extra+(int)((nMid_R-1+0.5)*bar_width), extra+bar_height+14, szCoreIdx[3], strlen(szCoreIdx[3]));
	
	if(nCore>1) XDrawString(dis, win, gc, extra+(int)((nCore-0.5)*bar_width), extra+bar_height+14, szCoreIdx[4], strlen(szCoreIdx[4]));
	
	
	XDrawString(dis, win, gc, extra-13, extra+bar_height+4, szUsage[0], strlen(szUsage[0]));
	XDrawString(dis, win, gc, extra-19, extra+bar_height*0.5+4, szUsage[1], strlen(szUsage[1]));
	XDrawString(dis, win, gc, extra-25, extra+6, szUsage[2], strlen(szUsage[2]));
	
	XDrawString(dis, win, gc, extra+(int)((nCore-0.5)*bar_width-20), extra+bar_height+32, szAxis[0], strlen(szAxis[0]));	// X-Axis info
	XDrawString(dis, win, gc, extra-30, extra-15, szAxis[1], strlen(szAxis[1]));	// Y-Axis info
	
	Format_Two_Digital(tm.tm_mon + 1, szMonth);
	Format_Two_Digital(tm.tm_mday, szDay);
	Format_Two_Digital(tm.tm_hour, szHour);
	Format_Two_Digital(tm.tm_min, szMin);
	Format_Two_Digital(tm.tm_sec, szSec);
	
	sprintf(szTime, "Now: %s/%s/%d %s:%s:%s on node %s", szMonth, szDay, tm.tm_year + 1900, szHour, szMin, szSec, szHostName);
	nBufLen = strlen(szTime);
	XDrawString(dis, win, gc, max((int)(0.2*win_width), 70), extra-30, szTime, strlen(szTime));	// current time stamp
}

void timerFired()
{
	int i, height;
	
	Cal_Core_Usage();
	
	XSetForeground(dis, gc, 0xFFFFFF);
	XFillRectangle(dis, win, gc, 0, 0, win_width, win_height);
	
	XSetForeground(dis, gc, 0xFF);
	
	for(i=0; i<nCore; i++)	{
		height = (int)(bar_height * Core_Usage[i]);
		XFillRectangle(dis, win, gc, extra+i*bar_width, extra+(bar_height-height), bar_width, height);
	}
	DrawLines();
}

void Init_Core_Stat()
{
	int i;
	
	nCore = 0;
	
	memset(cur_user, 0, sizeof(unsigned long long)*MAX_CORE);
	memset(cur_nice, 0, sizeof(unsigned long long)*MAX_CORE);
	memset(cur_system, 0, sizeof(unsigned long long)*MAX_CORE);
	memset(cur_idle, 0, sizeof(unsigned long long)*MAX_CORE);
	memset(cur_iowait, 0, sizeof(unsigned long long)*MAX_CORE);
	memset(cur_irq, 0, sizeof(unsigned long long)*MAX_CORE);
	memset(cur_softirq, 0, sizeof(unsigned long long)*MAX_CORE);
	memset(cur_steal, 0, sizeof(unsigned long long)*MAX_CORE);
	
	memset(old_user, 0, sizeof(unsigned long long)*MAX_CORE);
	memset(old_nice, 0, sizeof(unsigned long long)*MAX_CORE);
	memset(old_system, 0, sizeof(unsigned long long)*MAX_CORE);
	memset(old_idle, 0, sizeof(unsigned long long)*MAX_CORE);
	memset(old_iowait, 0, sizeof(unsigned long long)*MAX_CORE);
	memset(old_irq, 0, sizeof(unsigned long long)*MAX_CORE);
	memset(old_softirq, 0, sizeof(unsigned long long)*MAX_CORE);
	memset(old_steal, 0, sizeof(unsigned long long)*MAX_CORE);
}

void Save_Core_Stat(void)
{
	int i;
	
	for(i=0; i<nCore; i++)	{
		old_user[i] = cur_user[i];
		old_nice[i] = cur_nice[i];
		old_system[i] = cur_system[i];
		old_idle[i] = cur_idle[i];
		old_iowait[i] = cur_iowait[i];
		old_irq[i] = cur_irq[i];
		old_softirq[i] = cur_softirq[i];
		old_steal[i] = cur_steal[i];
	}
}

void Read_Proc_Stat(void)
{
	FILE *fIn;
	char szLine[1024], szCoreIdx[256], *ReadLine;
	int i, ReadItem;
	
	fIn = fopen("/proc/stat", "r");
	if(fIn == NULL)	{
		printf("Fail to open file: /proc/stat\nQuit\n");
		exit(1);
	}
	
	fgets(szLine, 1024, fIn);
	
	if(nCore == 0)	{
		while(1)	{
			ReadLine = fgets(szLine, 1024, fIn);
			if(ReadLine == NULL)	{
				break;
			}
			else if(feof(fIn))	{
				break;
			}
			ReadItem = sscanf(szLine, "%s%llu%llu%llu%llu%llu%llu%llu%llu",
				szCoreIdx, &(cur_user[nCore]), &(cur_nice[nCore]), &(cur_system[nCore]), &(cur_idle[nCore]), 
				&(cur_iowait[nCore]), &(cur_irq[nCore]), &(cur_softirq[nCore]), &(cur_steal[nCore]));
			if(ReadItem != 9)	{
				printf("There are %d cores.\n", nCore);
				Save_Core_Stat();
				break;
			}
			else if(strncmp(szCoreIdx, "cpu", 3)==0)	{
				nCore++;
				if(nCore > MAX_CORE)	{
					fclose(fIn);
					printf("nCore > MAX_CORE\n");
					exit(1);
				}
			}
		}
	}
	else	{
		for(i=0; i<nCore; i++)	{
			fgets(szLine, 1024, fIn);
			ReadItem = sscanf(szLine, "%s%llu%llu%llu%llu%llu%llu%llu%llu",
				szCoreIdx, &(cur_user[i]), &(cur_nice[i]), &(cur_system[i]), &(cur_idle[i]), 
				&(cur_iowait[i]), &(cur_irq[i]), &(cur_softirq[i]), &(cur_steal[i]));
			if(ReadItem != 9)	{
				printf("Error to read record: %s\nQuit\n", szLine);
				fclose(fIn);
				exit(1);
			}
		}
	}
	fclose(fIn);
}


void Extract_Thread_Mapping_Info(void)
{
	FILE *fIn;
	char szLine[1024], *ReadLine;
	int i, j, nCoreRead=0, ReadItem, CoreCount;
	int ThreadCount[MAX_SOCKET][MAX_CORE], RealCoreID[MAX_SOCKET][MAX_CORE];
	int nThread_Socket=0;
	int MaxSocket=-1;
	
	memset(ThreadCount, 0, sizeof(int)*MAX_CORE*MAX_SOCKET);
	
	fIn = fopen("/proc/cpuinfo", "r");
	if(fIn == NULL)	{
		printf("Fail to open file: cpuinfo.\nQuit\n");
		exit(1);
	}
	
	while(1)	{
		ReadLine = fgets(szLine, 1024, fIn);
		if(ReadLine == NULL)	{
			break;
		}
		if(feof(fIn))	break;
		if(strncmp(szLine, "physical id", 11)==0)	{
			ReadItem = sscanf(szLine+14, "%d", &(SocketID[nCoreRead]));
			if(ReadItem != 1)	{
				printf("Error to read the physical id: %s\n", szLine);
			}
			else	{
				if(SocketID[nCoreRead] > MaxSocket)	{
					MaxSocket = SocketID[nCoreRead];
				}
			}
		}
		else if(strncmp(szLine, "siblings", 8)==0)	{
			ReadItem = sscanf(szLine+11, "%d", &nThread_Socket);
			if(ReadItem != 1)	{
				printf("Error to read siblings: %s\n", szLine);
			}
		}
		else if(strncmp(szLine, "core id", 7)==0)	{
			ReadItem = sscanf(szLine+10, "%d", &(CoreID[nCoreRead]));
			if(ReadItem == 1)	{
				ThreadID[nCoreRead] = ThreadCount[SocketID[nCoreRead]][CoreID[nCoreRead]];
				ThreadCount[SocketID[nCoreRead]][CoreID[nCoreRead]]++;
				nCoreRead++;
			}
			else	{
				printf("Error to read the core id: %s\n", szLine);
			}
		}
		else if(strncmp(szLine, "cpu cores", 9)==0)	{
			ReadItem = sscanf(szLine+12, "%d", &nCore_Socket);
			if(ReadItem != 1)	{
				printf("Error to read siblings: %s\n", szLine);
			}
		}
	}
	fclose(fIn);

	nSocket = MaxSocket + 1; 
	
	for(j=0; j<nSocket; j++)	{
		CoreCount = 0;
		for(i=0; i<MAX_CORE; i++)	{	// Assume every core has the same number of threads.
			if(ThreadCount[j][i] > 0)	{
				RealCoreID[j][i] = CoreCount;
				CoreCount++;
			}
		}
	}
	nThread_per_Core = nThread_Socket / nCore_Socket;
	for(i=0; i<nCoreRead; i++)	{
		CoreID[i] = RealCoreID[SocketID[i]][CoreID[i]];
	}
	return;
}

static void Clean_up(int sig, siginfo_t *siginfo, void *ptr)
{
	//	usleep(1500000);
    delwin(mainwin);
    endwin();
    refresh();
	
	exit(0);
}

