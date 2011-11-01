CFLAGS=`pkg-config --cflags gstreamer-0.10` `pkg-config --cflags gio-2.0` `pkg-config --cflags libavformat` -Wall -D_FORTIFY_SOURCE=2 -g -D_GNU_SOURCE --std=c99 `pkg-config --cflags libnotify` -DUSE_LIBNOTIFY
LIBS=`pkg-config --libs gstreamer-0.10` `pkg-config --libs gio-2.0` `pkg-config --libs libavformat` -lsqlite3 -ltermcap `pkg-config --libs libnotify`
OBJS=minstrel.o util.o index.o queue.o conn.o

all: minstrel

clean:
	rm $(OBJS) *.d *~ minstrel
	
minstrel: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)
	
-include $(OBJS:.o=.d)

%.o: %.c
	gcc -c $(CFLAGS) $*.c -o $*.o
	gcc -MM $(CFLAGS) $*.c > $*.d
	
