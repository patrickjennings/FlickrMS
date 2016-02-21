CC:=gcc

export PKG_CONFIG_PATH:=$PKG_CONFIG_PATH:/usr/local/lib/pkgconfig

LXML:=libxml-2.0
FUSE:=fuse
GLIB:=glib-2.0
FLKC:=flickcurl
CURL:=libcurl
IMGM:=MagickWand
INCLUDES:=`pkg-config --libs $(FUSE) $(GLIB) $(FLKC) $(CURL) $(LXML) $(IMGM)`

OPTS:=-mtune=native -march=native -O2 -pipe
CFLAGS:=$(OPTS) -Wall -W -Werror -Wextra -Wconversion -Wsign-conversion -fstack-protector-strong
LDFLAGS:=-Wl,-O1,--as-needed,-z,relro

OBJS:=flickrms.o cache.o wget.o conf.o

PROJ:=flickrms

all: $(PROJ)

$(PROJ): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(INCLUDES)

flickrms.o: flickrms.c cache.c wget.c
	$(CC) $(CFLAGS) `pkg-config --cflags $(FUSE) $(IMGM)` -c $<

cache.o: cache.c conf.c
	$(CC) $(CFLAGS) `pkg-config --cflags $(GLIB) $(FLKC) $(LXML)` -c $<

wget.o: wget.c
	$(CC) $(CFLAGS) `pkg-config --cflags $(CURL)` -c $<

conf.o: conf.c
	$(CC) $(CFLAGS) `pkg-config --cflags $(FLKC) $(LXML)` -c $<

install:
	cp flickrms /usr/local/bin/

uninstall:
	rm /usr/local/bin/flickrms

clean:
	rm -rf $(OBJS) $(PROJ)
