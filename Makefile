bin	:= xmpp-shell
src	:= main.c
objs	:= $(src:%.c=%.o)
pkgs	:= gthread-2.0 gtk+-2.0 libstrophe gtksourceview-2.0
ldflags	:= $(shell pkg-config --libs $(pkgs))
cflags	:= $(shell pkg-config --cflags $(pkgs)) -Wall

%.o: %.c
	gcc -c $(cflags) -o $@ $<
$(bin): $(objs)
	gcc -o $@ $(objs) $(ldflags)
clean:
	rm $(bin) $(objs)
all: $(bin)
