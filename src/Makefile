CC:=gcc
export PKG_CONFIG_PATH:=/usr/local/lib/pkgconfig
FUSEINC:=`pkg-config --cflags --libs fuse`
GLIBINC:=`pkg-config --cflags --libs glib-2.0` `pkg-config --libs gthread-2.0`
FLKCINC:=`pkg-config --cflags --libs flickcurl`

OPTS:=-march=native -O2 -pipe
CFLAGS:=-Wall -W -Werror -Wextra -g

PROJ:=flickrfs

all: $(PROJ)

$(PROJ): flickrfs.o cache.o
	$(CC) -o $@ $(CFLAGS) $(FUSEINC) $(GLIBINC) $(FLKCINC) $^

flickrfs.o: flickrfs.c
	$(CC) $(CFLAGS) $(FUSEINC) -c $<

cache.o: cache.c
	$(CC) $(CFLAGS) $(GLIBINC) $(FLKCINC) -c $<

clean:
	rm -rf *.o $(PROJ)
