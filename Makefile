FLAGS = -g -O0 -std=c++14 -pedantic-errors -Wall

1730sh: 1730sh.o
	g++ $(FLAGS) -o 1730sh 1730sh.o

1730sh.o: 1730sh.cpp
	g++ $(FLAGS) -c 1730sh.cpp

clean:
	rm -rf 1730sh *.o *~ \#*
