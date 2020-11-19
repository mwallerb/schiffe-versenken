CXX=g++
CXXFLAGS=-std=c++11 -Wall

all: schiffe_versenken test_ki

schiffe_versenken: schiffe_versenken.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

test_ki: test_ki.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<
