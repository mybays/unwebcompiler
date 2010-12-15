DIRS		= liblh5
CXX		= g++
CXXFLAGS	= -g -Wall
OBJS		= main.o
LIBS		= liblh5/liblh5.a
EXE		= unweb

all: $(EXE)

$(EXE): $(OBJS) liblh5.a
	g++ -g -o unweb main.cpp liblh5/liblh5.a

liblh5.a:
	cd liblh5; $(MAKE)

clean:
	rm -f *.o unweb
	-for d in $(DIRS); do (cd $$d; $(MAKE) clean ); done

