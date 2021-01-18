CC:=gcc
CXX:=g++
LD:=g++
CPPFLAGS:=
CFLAGS:=-Wall -pedantic -O3
CXXFLAGS:=-Wall -pedantic -O3 -std=c++11
LDFLAGS:=-lm

EXECS:=schiffe_versenken test_ki

all: $(EXECS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ -c $<

$(EXECS): %: %.o
	$(LD) $(LDFLAGS) -o $@ $<

clean:
	rm -f $(EXECS) *.o

.PHONY: all clean

