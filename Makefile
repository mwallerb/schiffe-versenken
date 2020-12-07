% : %.c
	gcc -Wall -pedantic -lm -g -O -o $@ $<

% : %.cpp
	g++ -Wall -pedantic -lm -g -std=c++11 -O -o $@ $< 

%.o : %.c
	gcc -Wall -pedantic -g -O -o $@ $<

clean :
	rm */*.log */*.out */*.vrb */*.snm */*.toc */*.nav */*.synctex.gz _region_.* */*~ */*.aux *.log *.out *.vrb *.snm *.toc *.nav *.synctex.gz _region_.* *~ *.aux

cloud:
	cp -pu *.tex ~/ownCloud/EDV1_devel/
	cp -pu *.pdf ~/ownCloud/EDV1_devel/
	cp -pu Beispiele/*.* ~/ownCloud/EDV1_devel/Beispiele/
	cp -pu figures/* ~/ownCloud/EDV1_devel/figures/
	cp -pu exercises/* ~/ownCloud/EDV1_devel/exercises/
	cp -pu Makefile ~/ownCloud/EDV1_devel/

