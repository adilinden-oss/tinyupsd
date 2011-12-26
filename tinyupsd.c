/* $Id: tinyupsd.c,v 1.7 2001-11-23 08:39:13 adi Exp $
 * 
 * Copyright (C) 2001 Adi Linden <adi@adis.on.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * -------------------------------------------------------------------------
 *
 * tinyupsd.c  Monitors the DCD line of a serial port connected
 *             UPS. If the power goes down an external script is
 *             run and the daemon quits.
 *
 * -------------------------------------------------------------------------
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/reboot.h>

/*
 * The following are the RS232 control lines    
 *                                               
 *                                            D D
 *                                            T C
 * Macro           English                    E E
 * ---------------------------------------------- 
 * TIOCM_DTR       DTR - Data Terminal Ready  --> 
 * TIOCM_RTS       RTS - Ready to send        --> 
 * TIOCM_CTS       CTS - Clear To Send        <-- 
 * TIOCM_CAR       DCD - Data Carrier Detect  <-- 
 * TIOCM_RNG       RI  - Ring Indicator       <-- 
 * TIOCM_DSR       DSR - Data Signal Ready    <-- 
 * TIOCM_ST        ST  - Data Xmit            -->
 */

/*
 * The following is the cable we use
 * 
 * UPS Side                           Serial Port Side
 * 9 Pin Male                         9 Pin Female
 * 
 * Shutdown UPS 1 <---------------> 3 TX  (high = kill power)
 * Line Fail    2 <---------------> 1 DCD (high = power fail)
 * Ground       4 <---------------> 5 GND
 * Low Battery  5 <----+----------> 6 DSR (low = low battery)
 *                     `---|  |---> 4 DTR (cable power)
 */

/* Are we building for SysV? */
/* #define SYSV */

#define LINE_KILL        TIOCM_ST
#define LINE_FAIL        TIOCM_CAR
#define LINE_SCRAM       TIOCM_DSR
#define LINE_POWER       TIOCM_DTR
#define LINE_CABLE       TIOCM_CTS

#define UPSSTAT          "/etc/upsstatus"
#define PWRSTAT          "/etc/powerstatus"
#define PIDFILE          "/var/run/tinyupsd.pid"

#define DEFAULT_DEVICE   "/dev/cuaa0"
#define DEFAULT_WAIT     240
#define UPS_KILLTIME     5

#define VERSION          "tinyupsd v0.7"

char *usage =
    VERSION "Daemon for controlling dumb ups's\n"
    "Usage: tinyupsd [-d device] [-w timeout] [-k]\n"
    "       tinyupsd {-h|-v}\n";

char *version =
    VERSION " Copyright (C) 2001 Adi Linden\n\n"
    "This program is free software; you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation; either version 2 of the License, or\n"
    "(at your option) any later version.\n\n"
    "This program is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU General Public License for more details.\n\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with this program; if not, write to the Free Software\n"
    "Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA\n";

void systemkill(int);    
void powerkill(int);
void powerfail(int);
void dodie();
u_int get_pageins(void);
    
/* Main program */
int main(int argc, char *argv[])
{
    int fd;
    int pkill = 0;
    int flags;
    int cntwait = 0;
    int cntstat = 0;
    int pstatus, poldstat = 1;  /* assume power is good to start */
    int bstatus, boldstat = 1;  /* assume battery is good to start */
    int wait = DEFAULT_WAIT;
    char *program = "tinyupsd";
    char *device = DEFAULT_DEVICE;
    char *cwait;
    FILE *fp;

    /* program = argv[0]; */

    /* Get any optional command line args */
    while(argc > 1) {
	if(!strcmp(argv[1], "-d")) { 
	    device = argv[2]; 
	}
	if(!strcmp(argv[1], "-w")) { 
	    cwait = argv[2]; 
	    wait = atoi(cwait);
	}
	if(!strcmp(argv[1], "-v")) { 
	    printf(version);
	    exit(0);
	}
	if(!strcmp(argv[1], "-h")) {
	    printf(usage);
	    exit(0);
	}
	if(!strcmp(argv[1], "-k")) {
	    pkill = 1;
	    argc -= 1; 
	    argv += 1;
	} else {
	    argc -= 2; 
	    argv += 2;
	}
    }

    /* Start syslog */
    openlog(program, LOG_CONS|LOG_PERROR, LOG_DAEMON);

    /* Open monitor device */
    if((fd = open(device, O_RDWR | O_NDELAY)) < 0) {
	syslog(LOG_ERR, "%s: %s", device, sys_errlist[errno]);
	closelog();
	exit(1);
    }

    /* We were asked to kill ups power */
    if(pkill) {
	/* We won't log to syslog, only to stderr */
	closelog();

	/* Kill power and exit (if the ups doesn't turn off */
        powerkill(fd);
	close(fd);
	exit(0);
    }

    /* See if we are already running */
    if(fp = fopen(PIDFILE, "r")) {
	syslog(LOG_ALERT, "fopen: %s: File already exists\n", PIDFILE);
	close(fd);
	closelog();
	exit(1);
    }

    /* Daemonize */
    switch(fork()) {
	case 0: /* Child */
	    closelog();
	    setsid();
	    break;
	case -1: /* Error */
	    syslog(LOG_ERR, "can't fork.");
	    closelog();
	    exit(1);
	default: /* Parent */
	    closelog();
	    exit(0);
    }

    /* Do some signal handling */
    signal(SIGQUIT, dodie);
    signal(SIGTERM, dodie);
    signal(SIGQUIT, dodie);

    /* Write our pid to file */
    unlink(PIDFILE);
    if((fp = fopen(PIDFILE, "w")) != 0) {
	fprintf(fp, "%d\n", getpid());
	fclose(fp);
    }
    
    /* Restart syslog */
    openlog(program, LOG_CONS, LOG_DAEMON);
    syslog(LOG_NOTICE, "started dev=%s wait=%i", device, wait);

    /* Adjust our timeout for the 2 second increment countdown */
    wait = wait / 2;
    
    /* Clear DTR and RTS */
    ioctl(fd, TIOCMBIC, TIOCM_RTS);
    ioctl(fd, TIOCMBIC, TIOCM_DTR);
    
    /* Apply power to our cable by setting DTR */
    ioctl(fd, TIOCMBIS, LINE_POWER);

    /* Begin our loop where we sample the line */
    while(1) {
        /* Get the status */
        ioctl(fd, TIOCMGET, &flags);

        /* Get ups condition */
        pstatus = !(flags & LINE_FAIL);   /* line high = power fail */
        bstatus = flags & LINE_SCRAM;     /* line low  = battery low */  

        /* Process the change if anything has changed */
        if(pstatus != poldstat || bstatus != boldstat) {
            /* Wait a little to ignore brownouts and false signals */
            cntstat++;
            if(cntstat < 4) {
                sleep(1);
                continue;
            }
	    
            #ifdef SYSV
	    /* See if we have a power condition */
	    if(pstatus != poldstat) {
		if(pstatus) {
		    /* Power has been restored */
		    syslog(LOG_ALERT, "power ok");
		    powerfail(0);
		} else {
		    /* Power has failed */
		    if(bstatus) {
			/* Battery is good */
			syslog(LOG_ALERT, "power has failed");
			powerfail(1);
		    } else {
			/* Battery is low */
			syslog(LOG_ALERT, 
			       "power has failed and battery is low");
			powerfail(2);
		    }
		}
	    }
	    #endif     
	}

        #ifndef SYSV
        if(!pstatus && !bstatus) {
            /* Low battery, shutting down now */
	    systemkill(fd);
		
        } else if(!pstatus) {
            /* No line power, count down now */
            if((cntwait == 0) || ((cntwait) % 30 == 0)) {
                /* Inform immediately and every minute */
                syslog(LOG_EMERG, 
		       "power failed, %d seconds to shutdown", 
		       ((wait-cntwait)*2));
            }
            if(cntwait > wait) {
                /* Timeout reached, shutting down */
                systemkill(fd);
            }
            cntwait++;
	
        } else {
            /* Power is ok, let everyone know, reset counter */
	    if(cntwait != 0) {
		syslog(LOG_EMERG, "power restored");
                cntwait = 0;
	    }
        }
	#endif     
	
	/* Reset count, remember status and sleep for 2 seconds */
	cntstat = 0;
	poldstat = pstatus;
	boldstat = bstatus;
        sleep(2);
    }

    
}

#ifndef SYSV
/* Take down the system for shutdown/reboot */
void systemkill(fd)
{
    int i;
    u_int pageins;

    /* Log a last syslog message */
    syslog(LOG_EMERG, "power failed, system is going down now");
    closelog();
	    
    /* 
     * Proceed to run down the system
     *
     * Sync disks. Followed by a SIGTSTP and SIGTERM
     * to init. SIGTSTP will prevent further logins and
     * SIGTERM will go to single-user mode. Send signals
     * to processes folowed by a last sync. Send the
     * kill signal to the ups followed by system reboot
     * in case the power returned.
     *
     * This sequence of events taken from reboot.c
     */
    sync();

    /* Just stop init */
    if(kill(1, SIGTSTP) == -1)
	fprintf(stderr, "SIGTSTP init failed\n");

    /* Ignore the SIGHUP we get when our parent shell dies */
    signal(SIGHUP, SIG_IGN);

    /* Send a SIGTERM first, a chance to save the buffers */
    if(kill(-1, SIGTERM) == -1)
	fprintf(stderr, "SIGTERM init failed\n");

    /*
     * After the processes receive the signal, start the 
     * rest of the buffers on their way.  Wait 5 seconds 
     * between the SIGTERM and the SIGKILL to give 
     * everybody a chance. If there is a lot of paging 
     * activity then wait longer, up to a maximum of 
     * approx 60 seconds.
     */
    sleep(2);
    for(i = 0; i < 20; i++) {
	pageins = get_pageins();
	sync();
	sleep(3);
	if(get_pageins() == pageins)
	    break;
    }

    for(i = 1;; ++i) {
	if(kill(-1, SIGKILL) == -1) {
	    break;
	}
	if(i > 5) {
	    fprintf(stderr, "some process(es) wouldn't die\n");
	    break;
	}
    }
    sleep(2 * i);
    sync();

    /* Send the kill signal to the ups */
    powerkill(fd);

    /* Fallthrough */
    fprintf(stderr, "forcing reboot\n");
    reboot(0);
}
#endif     

/* Send the kill signal to the ups */
void powerkill (int fd)
{
    int flags;

    /* 
     * Send the kill signal to the ups by sending a break (TX high)
     * and clearing break (TX low) after the wait time
     */
    ioctl(fd, TIOCSBRK, 10*UPS_KILLTIME);
    sleep(UPS_KILLTIME);
    ioctl(fd, TIOCCBRK, 10*UPS_KILLTIME);
	
    /* Get the status */
    ioctl(fd, TIOCMGET, &flags);

    if(!(flags & LINE_FAIL)) {
        /* ups reports power is on */ 
        fprintf(stderr, "kill failed - power is ok\n");
    } else {
        /* kill power unsupported or bad cable */
        fprintf(stderr, "kill failed - ups did not respond, bad cable?\n");
    }
}

#ifdef SYSV
/* Communicate power status */
void powerfail(int status)
{
    int fd;

    /* Create an info file for powerfail scripts. */
    unlink(UPSSTAT);
    if((fd = open(UPSSTAT, O_CREAT|O_WRONLY, 0644)) >= 0) {
        switch(status) {
      	    case 0: /* Power OK */
	    	       write(fd, "OK\n", 3);
		       break;
            case 1: /* Line Fail */
		       write(fd, "FAIL\n", 5);
		       break;
	    case 2: /* Line Fail and Low Batt */
		       write(fd, "SCRAM\n", 6);
		       break;
	    default: /* Should never get here */
		       write(fd, "SCRAM\n", 6);
        }
        close(fd);
    }

    /* Create an info file for init. */
    unlink(PWRSTAT);
    if ((fd = open(PWRSTAT, O_CREAT|O_WRONLY, 0644)) >= 0) {
    if(status) {
        /* Problem */
        write(fd, "FAIL\n", 5);
    } else {
        /* No problem */
        write(fd, "OK\n", 3);
    }
    close(fd);
    }

    /* Send signal to init */
    kill(1, SIGPWR);
}
#endif

/* Die cleanly */
void dodie()
{
    /* Remove the pid file */
    unlink(PIDFILE);

    /* Log our exit */
    syslog(LOG_NOTICE, "stopped");
    closelog();

    /* Die */
    exit(0);
}

/* From reboot.c */
u_int get_pageins()
{
    u_int pageins;
    size_t len;

    len = sizeof(pageins);
    if (sysctlbyname("vm.stats.vm.v_swappgsin", &pageins, &len, NULL, 0) != 0) {
	warnx("v_swappgsin");
	return (0);
    }
    return pageins;
}

