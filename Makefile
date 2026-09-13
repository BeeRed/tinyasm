# TinyASM - Makefile
#
# by Heinz Blaettner
#
# Creation date: 2026-09-14
#
############################
SHELL 	= bash

CC 	= 	gcc
CFLAGS 	=	-Wall
CFLAGS 	+=	-std=gnu18
CFLAGS 	+=	-pedantic
CFLAGS 	+=	-Wold-style-definition
CFLAGS 	+=	-Wmissing-prototypes
CFLAGS 	+=	-Wstrict-prototypes
CFLAGS 	+=	-Wmissing-declarations
CFLAGS 	+=	-Wshadow
CFLAGS 	+=	-O3
CFLAGS 	+=	-g

SRCS	= 	tinyasm.c
SRCS	+= 	ins.c
OBJS	= 	$(SRCS:.c=.o)
TASM	= 	tinyasm

TESTS 	=	test01
TESTS 	+=	test02
TESTS 	+=	test03
TESTS 	+=	test04
TESTS 	+=	test05
TESTS 	+=	test06
TESTS 	+=	test07
TESTS 	+=	test08
TESTS 	+=	test09
TESTS 	+=	test10
TESTS 	+=	test11
TESTS 	+=	test12
TESTS 	+=	test13
############################
all:	$(TASM)

clean:
	-rm -f $(TASM) *.o
	-rm -rf test/OUT/*.{img,com,lst}
	-rm -rf test/OUT/*.{img_hex,com_hex}
	-rm -rf test/OUT/*.{IMG_HEX,COM_HEX}

tests: 	$(TASM) $(TESTS)

############################
$(OBJS):	Makefile

tinyasm.o:	tinyasm.c

tinyasm:	$(OBJS)
AFLAGS 	=
AFLAGS 	+= 	-f bin
#AFLAGS 	+= 	-v
TDIR 	= 	test
TODIR 	= 	$(TDIR)/OUT

############################
test01:	$(TASM) test/input0.asm
	$(MAKE) test_img Tnum=01 Ttype=img Tlo=input0 	Tup=INPUT0 	Targs=""

test02:	$(TASM) test/input1.asm
	$(MAKE) test_img Tnum=02 Ttype=img Tlo=input1 	Tup=INPUT1 	Targs=""

test03:	$(TASM) test/input2.asm
	$(MAKE) test_img Tnum=03 Ttype=img Tlo=input2 	Tup=INPUT2 	Targs=""

test04:	$(TASM) test/fbird.asm
	$(MAKE) test_img Tnum=04 Ttype=img Tlo=fbird 	Tup=FBIRD 	Targs=""

test05:	$(TASM) test/invaders.asm
	$(MAKE) test_img Tnum=05 Ttype=img Tlo=invaders Tup=INVADERS 	Targs=""

test06:	$(TASM) test/pillman.asm
	$(MAKE) test_img Tnum=06 Ttype=img Tlo=pillman 	Tup=PILLMAN 	Targs=""

test07:	$(TASM) test/rogue.asm
	$(MAKE) test_img Tnum=07 Ttype=img Tlo=rogue 	Tup=ROGUE 	Targs=""

test08:	$(TASM) test/rogue.asm
	$(MAKE) test_img Tnum=08 Ttype=com Tlo=rogue 	Tup=ROGUE 	Targs="-dCOM_FILE=1"

test09:	$(TASM) test/os.asm
	$(MAKE) test_img Tnum=09 Ttype=img Tlo=os 	Tup=OS 		Targs=""

test10:	$(TASM) test/basic.asm
	$(MAKE) test_img Tnum=10 Ttype=img Tlo=basic 	Tup=BASIC 	Targs=""

test11:	$(TASM) test/include.asm
	$(MAKE) test_img Tnum=11 Ttype=com Tlo=include 	Tup=INCLUDE 	Targs=""

test12:	$(TASM) test/bricks.asm
	$(MAKE) test_img Tnum=12 Ttype=img Tlo=bricks 	Tup=BRICKS 	Targs=""

test13:	$(TASM) test/doom.asm
	$(MAKE) test_img Tnum=13 Ttype=img Tlo=doom 	Tup=DOOM 	Targs=""




############################
# will be called from makefile itself
test_img:	$(TASM) $(TDIR)/$(Tlo).asm
	@echo
	@echo "########################################"
	@echo "### test$(Tnum): $(Tlo)/$(Tup)"
	-./$(TASM) $(AFLAGS) $(TDIR)/$(Tlo).asm -o $(TODIR)/$(Tlo).$(Ttype) -l $(TODIR)/$(Tlo).lst $(Targs)
	xxd -g 1 $(TDIR)/$(Tup).$(shell echo $(Ttype)|tr '[:lower:]' '[:upper:]') 		$(TODIR)/$(Tup).$(shell echo $(Ttype)|tr '[:lower:]' '[:upper:]')_HEX
	xxd -g 1 $(TODIR)/$(Tlo).$(Ttype) 		$(TODIR)/$(Tlo).$(Ttype)_hex
	-diff -bwui $(TODIR)/$(Tup).$(shell echo $(Ttype)|tr '[:lower:]' '[:upper:]')_HEX 	$(TODIR)/$(Tlo).$(Ttype)_hex
	@-if [ -s $(TDIR)/$(Tup).LST ]; then 		\
		diff -bwui $(TDIR)/$(Tup).LST $(TODIR)/$(Tlo).lst ; 	\
	else 	\
	echo "#LSTskip <$(TDIR)/$(Tup).LST> NOT exist." ;		\
	fi

