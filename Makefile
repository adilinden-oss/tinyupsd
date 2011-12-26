# $Id: Makefile,v 1.5 2001-11-23 08:09:08 adi Exp $
#
# Makefile for tinyupsd
# Written by Adi Linden
#

BINDIR		= /sbin
SCRIPTDIR	= /usr/local/etc/rc.d
OWNER		= root
GROUP		= wheel

CC        	= gcc
LIBS      	=
INCLUDE   	= 
CFLAGS    	= $(INCLUDE) -O2 -Wall

all: 		tinyupsd

tinyupsd:	tinyupsd.c 

install:	tinyupsd
		cp -f tinyupsd ${BINDIR}
		strip ${BINDIR}/tinyupsd
		chmod 754 ${BINDIR}/tinyupsd
		chgrp ${GROUP} ${BINDIR}/tinyupsd
		chown ${OWNER} ${BINDIR}/tinyupsd
		mkdir -p ${SCRIPTDIR}
		cp -f tinyupsd.sh ${SCRIPTDIR}/tinyupsd.sh
		chmod 754 ${SCRIPTDIR}/tinyupsd.sh
		chgrp ${GROUP} ${SCRIPTDIR}/tinyupsd.sh
		chown ${OWNER} ${SCRIPTDIR}/tinyupsd.sh

uninstall:
		rm -f ${BINDIR}/tinyupsd
		rm -f ${SCRIPTDIR}/tinyupsd.sh

clean:
	/bin/rm -f *.o *.core core tinyupsd
