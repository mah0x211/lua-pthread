TARGET=pthread.$(LIB_EXTENSION)
SRCS=$(wildcard src/*.c)
OBJS=$(SRCS:.c=.o)
GCDAS=$(OBJS:.o=.gcda)
INSTALL?=install

ifdef PTHREAD_COVERAGE
COVFLAGS=--coverage
endif

.PHONY: all install

all: $(TARGET)

preprocess:
	lua ./configure.lua

%.o: %.c
	$(CC) $(CFLAGS) $(WARNINGS) $(COVFLAGS) $(CPPFLAGS) -o $@ -c $<

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS) $(PLATFORM_LDFLAGS) $(COVFLAGS)

install:
	$(INSTALL) -d $(INST_LIBDIR)
	$(INSTALL) $(TARGET) $(INST_LIBDIR)
	rm -f $(OBJS) $(TARGET) $(GCDAS)
