FREESWITCH_MOD_PATH=/usr/local/freeswitch-1.10.2/lib/freeswitch/mod
FREESWITCH_INCLUDE=/oncon/jiekou/freeswitch-1.10.2/include/freeswitch
ROCKET_REDIS_INCLUDE=/usr/local/include/sw/redis++/
ROCKET_REDIS_LIB_PATH=/usr/local/lib/

MODNAME = mod_redis_sentinel.so
MODOBJ = mod_redis_sentinel.o
MODCFLAGS = -Wall -Werror -I$(FREESWITCH_INCLUDE) -I$(ROCKET_REDIS_INCLUDE)
MODLDFLAGS = -L$(ROCKET_REDIS_LIB_PATH) -lfreeswitch -lhiredis -lredis++
CC = g++
CFLAGS = -fPIC -g -ggdb  $(MODCFLAGS)
CPPFLAGS = -fPIC -std=c++11 -g -ggdb  $(MODCFLAGS)
LDFLAGS = $(MODLDFLAGS)

.PHONY: all
all: $(MODNAME)

$(MODNAME): $(MODOBJ)
	@$(CC) -shared $(CPPFLAGS) -o $@ $(MODOBJ) $(LDFLAGS)

.c.o: $<
	@$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	rm -f $(MODNAME) $(MODOBJ)

install: $(MODNAME)
	install -d $(FREESWITCH_MOD_PATH)
	install $(MODNAME) $(FREESWITCH_MOD_PATH)