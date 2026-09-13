# TinyASM - Makefile
#
# by Heinz Blaettner
#
# Creation date: 2026-09-14
#
############################

CC 	= 	gcc
CFLAGS 	=	-Wall
CFLAGS 	+=	-std=gnu18
CFLAGS 	+=	-pedantic
CFLAGS 	+=	-Wold-style-definition
CFLAGS 	+=	-O3
CFLAGS 	+=	-g

SRCS	= tinyasm.c
SRCS	+= ins.c
OBJS	= $(SRCS:.c=.o)
TARGETS	= tinyasm

############################
all:	$(TARGETS)

clean:
	-rm $(TARGETS) *.o

############################
$(OBJS):	Makefile

tinyasm.o:	tinyasm.c

tinyasm:	$(OBJS)
