CC=gcc
DIR_BUILD=.

CFLAGS=-Wall -g
LDFLAGS=

SOURCES=main.c

OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=dxtr

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) $(CFLAGS) -o $@
    
clean:
	rm -f $(DIR_BUILD)/*.o *~
