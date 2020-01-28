OBJS = client.o

CFLAGS += -I /usr/include/libnl3
LDLIBS += -lnl-genl-3 -lnl-3

all: xen-domstate-notify-client

xen-domstate-notify-client: $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS)

clean:
	-rm xen-domstate-notify-client $(OBJS)
