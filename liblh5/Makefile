CC		= gcc
CFLAGS		= -g -Wall
OBJS		= decode.o encode.o huf.o io.o maketbl.o maketree.o lh5wrapper.o
OBJLIBS		= liblh5.a
AR		= ar
ARFLAGS		= 

all: liblh5.a

liblh5.a: $(OBJS)
	$(AR) $(ARFLAGS) rcs liblh5.a $(OBJS)

clean:
	rm -f $(OBJS) $(OBJLIBS)

