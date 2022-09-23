CFLAGS = -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include
LFLAGS = -lglib-2.0 -ljit

avl-libjit: avl-libjit.c
	gcc -Wall -g ${CFLAGS} avl-libjit.c -o avl-libjit ${LFLAGS}

clean:
	rm -rf *.o
	rm -rf avl-libjit