CXXFLAGS = -O0 -g  -Wall -I ./base -pthread
LDFLAGS = -lpthread

BASE_SRC = ./base/thread/Thread.cc 
LIB_SRC = ./reactor/EventLoop.cc ./reactor/Channel.cc ./reactor/Poller.cc
LIBS =
TESTS = $(shell find ./test -name "*.cc")
TESTS_OBJ = $(patsubst %.cc, %, $(TESTS)) 

all: test

test : $(TESTS_OBJ)

$(TESTS_OBJ): $(TESTS)
	g++ $(CXXFLAGS) -o $@ $(LIB_SRC) $(BASE_SRC) $(patsubst %, %.cc, $@) $(LDFLAGS)

clean:
	rm -f $(BINARIES) $(TESTS_OBJ) core
