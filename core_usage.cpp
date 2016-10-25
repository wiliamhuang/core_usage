// g++ core_usage.cpp -lX11
// Usage: ./a.out 0.2
// The parameter is the time interval of updating core unitlization data. Without it, 0.3 is used as default. 

// This contains the code of a GUI for showing core unitlizations
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

#define MAX_CORE	(1024)
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

int nCore;
unsigned long long cur_user[MAX_CORE], cur_nice[MAX_CORE], cur_system[MAX_CORE], cur_idle[MAX_CORE], 
cur_iowait[MAX_CORE], cur_irq[MAX_CORE], cur_softirq[MAX_CORE], cur_steal[MAX_CORE];

unsigned long long old_user[MAX_CORE], old_nice[MAX_CORE], old_system[MAX_CORE], old_idle[MAX_CORE], 
old_iowait[MAX_CORE], old_irq[MAX_CORE], old_softirq[MAX_CORE], old_steal[MAX_CORE];
float Core_Usage[MAX_CORE];

int bar_width, bar_height=200, extra=55, x0, y0, win_width, win_height;
char szHostName[256];

void timerFired();
void Init_Core_Stat();
void Read_Proc_Stat(void);
void Save_Core_Stat(void);
void Cal_Core_Usage(void);
void Setup_bar_width(void);

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

int main(int argc, char *argv[]) {
	int Run=1;
	XEvent ev;

	if(argc >= 2)	{
		tInterval = atof(argv[1]);
	}
	
	Init_Core_Stat();
	Read_Proc_Stat();
	Setup_bar_width();

	gethostname(szHostName, 255);

	dis = XOpenDisplay(NULL);
	if(dis == NULL)	{
		printf("Fail to open DISPALY. Did you set up X11 forwarding?\n");
		exit(1);
	}
	screen = DefaultScreen(dis);
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
	char szCoreIdx[5][64]={"1", "xx", "xx", "xx", "272"};
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

	nMid = (int)((nCore+1)/2);
	nMid_L = (int)((1+nMid)/2);
	nMid_R = (int)((nCore+nMid)/2);
	sprintf(szCoreIdx[1], "%d", nMid_L);
	sprintf(szCoreIdx[2], "%d", nMid);
	sprintf(szCoreIdx[3], "%d", nMid_R);
	sprintf(szCoreIdx[4], "%d", nCore);
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

