all:
	gcc -Wall -g -O0 -o xmpp-shell `pkg-config --libs --cflags gthread-2.0 gtk+-2.0 strophe` main.c
