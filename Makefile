CFLAGS=`pkg-config --cflags gstreamer-0.10` `pkg-config --cflags taglib_c` -Wall -D_FORTIFY_SOURCE=2 -g -D_GNU_SOURCE --std=c99
LIBS=`pkg-config --libs gstreamer-0.10` `pkg-config --libs taglib_c`
OBJS=minstrel.o

all: minstrel

clean:
	rm $(OBJS) *.d *~ minstrel
	
minstrel: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)
	
-include $(OBJS:.o=.d)

%.o: %.c
	gcc -c $(CFLAGS) $*.c -o $*.o
	gcc -MM $(CFLAGS) $*.c > $*.d
	
