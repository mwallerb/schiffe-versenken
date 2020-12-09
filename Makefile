CC:=gcc
CXX:=g++
LD:=g++
CPPFLAGS:=
CFLAGS:=-Wall -pedantic -g -O0
CXXFLAGS:=-Wall -pedantic -g -O0 -std=c++11
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
	rm -f */*.log */*.out */*.vrb */*.snm */*.toc */*.nav */*.synctex.gz _region_.* */*~ */*.aux *.log *.out *.vrb *.snm *.toc *.nav *.synctex.gz _region_.* *~ *.aux

cloud:
	cp -pu *.tex ~/ownCloud/EDV1_devel/
	cp -pu *.pdf ~/ownCloud/EDV1_devel/
	cp -pu Beispiele/*.* ~/ownCloud/EDV1_devel/Beispiele/
	cp -pu figures/* ~/ownCloud/EDV1_devel/figures/
	cp -pu exercises/* ~/ownCloud/EDV1_devel/exercises/
	cp -pu Makefile ~/ownCloud/EDV1_devel/

.PHONY: all clean cloud

