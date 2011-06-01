CC:=gcc

export PKG_CONFIG_PATH:=/usr/local/lib/pkgconfig

FUSEINC:=`pkg-config --cflags --libs fuse`
GLIBINC:=`pkg-config --cflags --libs glib-2.0`
FLKCINC:=`pkg-config --cflags --libs flickcurl`
CURLINC:=`pkg-config --cflags --libs libcurl`
INCLUDES:=$(FUSEINC) $(GLIBINC) $(FLKCINC) $(CURLINC)

OPTS:=-march=native -O2 -pipe
CFLAGS:=-Wall -W -Werror -Wextra -g

OBJS:=flickrfs.o cache.o wget.o conf.o file_manager.o

PROJ:=flickrfs

all: $(PROJ)

$(PROJ): $(OBJS)
	$(CC) -o $@ $(CFLAGS) $(INCLUDES) $^

flickrfs.o: flickrfs.c cache.c wget.c file_manager.o
	$(CC) $(CFLAGS) $(FUSEINC) -c $<

cache.o: cache.c conf.c
	$(CC) $(CFLAGS) $(GLIBINC) $(FLKCINC) -c $<

wget.o: wget.c
	$(CC) $(CFLAGS) $(CURLINC) -c $<

conf.o: conf.c
	$(CC) $(CFLAGS) -c $<

file_manager.o: file_manager.c
	$(CC) $(CFLAGS) $(GLIBINC) -pthread -c $<

install:
	cp flickrfs /usr/local/bin/

uninstall:
	rm /usr/local/bin/flickrfs

clean:
	rm -rf $(OBJS) $(PROJ)
