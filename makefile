LINT-sun4	=	alint -bxu
LINT-solaris	=	alint -bxu
LINT-i386	=	lint
LINT		=	$(LINT-$(ARCH))

#CPPFLAGS-sun4	=	-vc -Xa -DWILDCARDS
CPPFLAGS-solaris=	-DWILDCARDS
CPPFLAGS-linux	=	
CPPFLAGS	=	$(CPPFLAGS-$(ARCH))

DEBUG		=	-g $(CPPFLAGS)

#CC-linux	=	gcc -traditional-cpp
CC-linux	=	gcc
CC-sun4		=	cc
CC-solaris	=	/prod_opt/SUNWspro_6.1/SUNWspro/bin/cc
#CC-solaris	=	insure
CC-i386		=	cc -arch i386 -arch m68k
CC		=	$(CC-$(ARCH))

REUTERS-sun4 	=	...
REUTERS-solaris	=	/usr/local/ReutersDevKits
REUTERS-linux	=	/tp/ReutersDevKits
REUTERS		=	$(REUTERS-$(ARCH))

SSL_3-sun4	=	$(REUTERS)
SSL_3-solaris	=	$(REUTERS)/ssl3
SSL_3		=	$(SSL_3-$(ARCH))
#SSL_3		=	/usr/ssl3.2.2.L3/ssl

SSL_4-sun4	=	$(REUTERS)/ssl4.0.1.L5.sun4
SSL_4-solaris	=	$(REUTERS)/ssl4.0.5.L2.devkit.solaris.rri
SSL_4-i386	=	$(REUTERS)/ssl4.0.2.L1.next.devkit
#SSL_4-linux	=	$(REUTERS)/ssl4.0.10.L1.devkit.linux.rri
SSL_4-linux	=	$(REUTERS)/ssl4.0.11.L1.devkit.linux.as30.eap
SSL_4		=	$(SSL_4-$(ARCH))

SSL_ARCH-sun4	=	$(ARCH)
SSL_ARCH-solaris=	sun4_5.5
SSL_ARCH-i386	=	MAB
#SSL_ARCH-linux	=	lnx86_24
SSL_ARCH-linux	=	lnx86_32
SSL_ARCH	=	$(SSL_ARCH-$(ARCH))

INCLUDE_3	=	-I$(SSL_3)/include
INCLUDE_4	=	-I$(SSL_4)/include
LIBS_3		=	-L$(SSL_3)/lib -lssl
LIBS_4		=	-L$(SSL_4)/lib/$(SSL_ARCH) -lssl $(OS_LIBS)

#INSTALL_DIR	=	$(HOME)/Unix/$(ARCH)/bin
INSTALL_DIR-linux	=	$(HOME)/lbin
INSTALL_DIR-solaris	=	$(HOME)/bin
INSTALL_DIR	=	$(INSTALL_DIR-$(ARCH))
#PURIFY = /local/pure/purify-3.2-sunos4/purify 
#PURIFY	= /usr/local/release5.x/thirdParty/purify/4.0.1/purify
PURIFY	= /usr/local/release5.x/thirdParty/purify/4.2/purify-4.2-solaris2/purify
PURIFY_OPTIONS	=	-cache-dir=/home/chubine/tmp -always-use-cache-dir -inuse-at_exit=yes

#LINK.c-sun4	=       $(PURIFY) $(PURIFY_OPTIONS) $(CC)
LINK.c-sun4	=       $(CC)
#LINK.c-solaris	=       $(PURIFY) $(PURIFY_OPTIONS) $(CC)
LINK.c-solaris	=       $(CC)
LINK.c-linux	=       $(CC)
#LINK.c-solaris	=       insure
LINK.c-i386	=       $(CC)
LINK.c		=       $(LINK.c-$(ARCH))

OS_LIBS-sun4	=
OS_LIBS-solaris	=	-lsocket -lnsl
OS_LIBS		=	$(OS_LIBS-$(ARCH))

MALLOC_DEBUG_OBJ=	/usr/lib/debug/malloc.o /usr/lib/debug/mallocmap.o

FRE_SRC		=	sslfre.c fidutils.c fiddefs.c\
			freread.c freparse.c freutils.c\
			frac2dec.c str2dbl.c hash.c queue.c dump.c

FRE_SRC4	=	$(FRE_SRC) sslsubs4.c
FRE_SRC3	=	$(FRE_SRC) sslsubs3.c

FRE_OBJ_3	=	$(FRE_SRC3:%.c=$(ARCH)/%.o)
FRE_OBJ_4	=	$(FRE_SRC4:%.c=$(ARCH)/%.o)

HEADERS		=	sslfre.h
PROD_3		=	sslfre.v3
PROD_4		=	sslfre

PAGE_PROD_3	=	$(ARCH)/page $(ARCH)/record $(ARCH)/n2k $(ARCH)/ts
PAGE_PROD_4	=	$(ARCH)/page4 $(ARCH)/record4 $(ARCH)/n2k4 $(ARCH)/ts4
PAGE_SRC	=	page.c triprice.c dump.c queue.c fidutils.c\
			str2dbl.c fiddefs.c
PAGE_SRC_3	=	$(PAGE_SRC) sslsubs3.c
PAGE_SRC_4	=	$(PAGE_SRC) sslsubs4.c
PAGE_OBJ_3	=	$(PAGE_SRC_3:%.c=$(ARCH)/%.o)
PAGE_OBJ_4	=	$(PAGE_SRC_4:%.c=$(ARCH)/%.o)

prod:	$(ARCH) $(ARCH)/$(PROD_4)

all:	prod $(PAGE_PROD_4)

allprod:	$(ARCH) $(ARCH)/$(PROD_3) $(ARCH)/$(PROD_4)

everything:	allprod $(PAGE_PROD_3) $(PAGE_PROD_4)

$(ARCH)/sslsubs3.o:	sslsubs3.c
	$(CC) $(DEBUG) -DIMPLEMENT_QUEUE -c -o $@ sslsubs3.c $(INCLUDE_3)

$(ARCH)/sslsubs4.o:	sslsubs4.c
	$(CC) $(DEBUG) -DIMPLEMENT_QUEUE -c -o $@ sslsubs4.c $(INCLUDE_4)

$(ARCH):
	@if [ \! -d $(ARCH) ]; then \
		mkdir $@; \
	fi

$(ARCH)/$(PROD_4): $(FRE_OBJ_4)
	$(LINK.c) -o $@ $(FRE_OBJ_4) $(LIBS_4)

$(ARCH)/$(PROD_3): $(FRE_OBJ_3)
	$(LINK.c) -o $@ $(FRE_OBJ_3) $(LIBS_3)

$(ARCH)/%.o: %.c
	$(CC) $(DEBUG) -c -o $@ $*.c

$(ARCH)/page:	$(PAGE_OBJ_3)
	$(LINK.c) -o $@ $(PAGE_OBJ_3) $(LIBS_3)

$(ARCH)/record:	$(ARCH)/page
	cd $(ARCH); rm -f record; ln -s page record

$(ARCH)/n2k:	$(ARCH)/page
	cd $(ARCH); rm -f n2k; ln -s page n2k

$(ARCH)/ts:	$(ARCH)/page
	cd $(ARCH); rm -f ts; ln -s page ts

#$(ARCH)/record:	$(PAGE_OBJ_3)
#	$(LINK.c) -o $@ $(PAGE_OBJ_3) $(LIBS_3)
#$(ARCH)/n2k:	$(PAGE_OBJ_3)
#	$(LINK.c) -o $@ $(PAGE_OBJ_3) $(LIBS_3)
#$(ARCH)/ts:	$(PAGE_OBJ_3)
#	$(LINK.c) -o $@ $(PAGE_OBJ_3) $(LIBS_3)
#

$(ARCH)/page4:	$(PAGE_OBJ_4)
	$(LINK.c) -o $@ $(PAGE_OBJ_4) $(LIBS_4)

$(ARCH)/record4: $(ARCH)/page4
	cd $(ARCH); rm -f record4; ln -s page4 record4

$(ARCH)/n2k4:	$(ARCH)/page4
	cd $(ARCH); rm -f n2k4; ln -s page4 n2k4

$(ARCH)/ts4:	$(ARCH)/page4
	cd $(ARCH); rm -f ts4; ln -s page4 ts4

#$(ARCH)/record4: $(PAGE_OBJ_4)
#	$(LINK.c) -o $@ $(PAGE_OBJ_4) $(LIBS_4)
#$(ARCH)/n2k4:	$(PAGE_OBJ_4)
#	$(LINK.c) -o $@ $(PAGE_OBJ_4) $(LIBS_4)
#$(ARCH)/ts4:	$(PAGE_OBJ_4)
#	$(LINK.c) -o $@ $(PAGE_OBJ_4) $(LIBS_4)

clean_obj:
	rm -rf	$(FRE_OBJ_3) $(FRE_OBJ_4) $(PAGE_OBJ_4)

clean:
	rm -rf	$(FRE_OBJ_3) $(FRE_OBJ_4) \
		$(ARCH)/$(PROD_4) $(ARCH)/$(PROD_3) \
		$(PAGE_OBJ_4) $(PAGE_PROD_3) $(PAGE_PROD_4)

list:		list.c
		$(LINK.c) -DUNIT_TEST -g -o list list.c $(MALLOC_DEBUG_OBJ)

INSTALL_PROD2 = $(INSTALL_DIR)/$(PROD_4)\
		$(INSTALL_DIR)/page4\
		$(INSTALL_DIR)/record4\
		$(INSTALL_DIR)/n2k4\
		$(INSTALL_DIR)/ts4

INSTALL_PRODS = $(INSTALL_DIR)/$(PROD_3)\
		$(INSTALL_DIR)/$(PROD_4)\
		$(INSTALL_DIR)/page\
		$(INSTALL_DIR)/record\
		$(INSTALL_DIR)/n2k\
		$(INSTALL_DIR)/ts\
		$(INSTALL_DIR)/page4\
		$(INSTALL_DIR)/record4\
		$(INSTALL_DIR)/n2k4\
		$(INSTALL_DIR)/ts4

install:	$(INSTALL_PRODS)

install2:	$(INSTALL_PROD2)

$(INSTALL_DIR)/$(PROD_4): $(ARCH)/$(PROD_4)
	cp $(ARCH)/$(PROD_4) $(INSTALL_DIR)/$(PROD_4)
	@echo $(PROD_4) installed

$(INSTALL_DIR)/$(PROD_3): $(ARCH)/$(PROD_3)
	cp $(ARCH)/$(PROD_3) $(INSTALL_DIR)/$(PROD_3)
	@echo $(PROD_3) installed

$(INSTALL_DIR)/page: $(ARCH)/page
	cp -p $(ARCH)/page $(INSTALL_DIR)/page
	@echo page installed

#$(INSTALL_DIR)/record: $(ARCH)/record
#	cp $(ARCH)/record $(INSTALL_DIR)/record
$(INSTALL_DIR)/record:	$(INSTALL_DIR)/page
	cd $(INSTALL_DIR); rm -f record; ln -s page record
	@echo record installed

#$(INSTALL_DIR)/n2k: $(ARCH)/n2k
#	cp $(ARCH)/n2k $(INSTALL_DIR)/n2k
$(INSTALL_DIR)/n2k:	$(INSTALL_DIR)/page
	cd $(INSTALL_DIR); rm -f n2k; ln -s page n2k
	@echo n2k installed

#$(INSTALL_DIR)/ts: $(ARCH)/ts
#	cp $(ARCH)/ts $(INSTALL_DIR)/ts
$(INSTALL_DIR)/ts:	$(INSTALL_DIR)/page
	cd $(INSTALL_DIR); rm -f ts; ln -s page ts
	@echo ts installed

$(INSTALL_DIR)/page4: $(ARCH)/page4
	cp -p $(ARCH)/page4 $(INSTALL_DIR)/page4
	@echo page4 installed

#$(INSTALL_DIR)/record4: $(ARCH)/record4
#	cp $(ARCH)/record4 $(INSTALL_DIR)/record4
$(INSTALL_DIR)/record4:	$(INSTALL_DIR)/page4
	cd $(INSTALL_DIR); rm -f record4; ln -s page4 record4
	@echo record4 installed

#$(INSTALL_DIR)/n2k4: $(ARCH)/n2k4
#	cp $(ARCH)/n2k4 $(INSTALL_DIR)/n2k4
$(INSTALL_DIR)/n2k4:	$(INSTALL_DIR)/page4
	cd $(INSTALL_DIR); rm -f n2k4; ln -s page4 n2k4
	@echo n2k4 installed

#$(INSTALL_DIR)/ts4: $(ARCH)/ts4
#	cp $(ARCH)/ts4 $(INSTALL_DIR)/ts4
$(INSTALL_DIR)/ts4:	$(INSTALL_DIR)/page4
	cd $(INSTALL_DIR); rm -f ts4; ln -s page4 ts4
	@echo ts4 installed

lint1:
	$(LINT) $(INCLUDE_3) $(PAGE_SRC_3)
lint2:
	$(LINT) $(INCLUDE_4) $(PAGE_SRC_4)
lint3:
	$(LINT) $(INCLUDE_3) $(FRE_SRC3)
lint4:
	$(LINT) $(INCLUDE_4) $(FRE_SRC4)


tags:	$(FRE_SRC4)
	ctags $(FRE_SRC4)
