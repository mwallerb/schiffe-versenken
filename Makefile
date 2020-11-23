CXX=g++
CXXFLAGS=-std=c++11 -Wall

EXECS:=schiffe_versenken test_ki

all: $(EXECS)

$(EXECS): %: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	-rm -f $(EXECS)

.PHONY: all clean
