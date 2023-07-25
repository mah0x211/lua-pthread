CLIB=thread.$(LIB_EXTENSION)
SRCS=$(wildcard src/*.c)
OBJS=$(SRCS:.c=.o)
GCDAS=$(OBJS:.o=.gcda)
LUALIB=$(wildcard lib/*.lua)
INSTALL?=install
DEFINE=

ifdef PTHREAD_COVERAGE
COVFLAGS=--coverage
else
DEFINE=-DNDEBUG
endif

.PHONY: all install

all: $(CLIB)

%.o: %.c
	$(CC) $(CFLAGS) $(WARNINGS) $(COVFLAGS) $(CPPFLAGS) $(DEFINE) -o $@ -c $<

$(CLIB): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS) $(PLATFORM_LDFLAGS) $(COVFLAGS)

install:
	$(INSTALL) -d $(INST_LIBDIR)/pthread/
	$(INSTALL) $(CLIB) $(INST_LIBDIR)/pthread/
	$(INSTALL) -d $(INST_LUADIR)/pthread/
	$(INSTALL) $(LUALIB) $(INST_LUADIR)/pthread/
	$(INSTALL) pthread.lua $(INST_LUADIR)
	rm -f $(OBJS) $(CLIB) $(GCDAS)
