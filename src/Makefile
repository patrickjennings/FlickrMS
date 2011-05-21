CC:=gcc

export PKG_CONFIG_PATH:=/usr/local/lib/pkgconfig

FUSEINC:=`pkg-config --cflags --libs fuse`
GLIBINC:=`pkg-config --cflags --libs glib-2.0` `pkg-config --libs gthread-2.0`
FLKCINC:=`pkg-config --cflags --libs flickcurl`
CURLINC:=`pkg-config --cflags --libs libcurl`
INCLUDES:=$(FUSEINC) $(GLIBINC) $(FLKCINC) $(CURLINC)

OPTS:=-march=native -O2 -pipe
CFLAGS:=-Wall -W -Werror -Wextra -g

OBJS:=flickrfs.o cache.o wget.o

PROJ:=flickrfs

all: $(PROJ)

$(PROJ): $(OBJS)
	$(CC) -o $@ $(CFLAGS) $(INCLUDES) $^

flickrfs.o: flickrfs.c
	$(CC) $(CFLAGS) $(FUSEINC) -c $<

cache.o: cache.c
	$(CC) $(CFLAGS) $(GLIBINC) $(FLKCINC) -c $<

wget.o: wget.c
	$(CC) $(CFLAGS) $(CURLINC) -c $<

install:
	cp flickrfs /usr/local/bin/

uninstall:
	rm /usr/local/bin/flickrfs

clean:
	rm -rf $(OBJS) $(PROJ)
