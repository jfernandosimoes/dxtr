CC = gcc
DIR_BUILD = .
EXECUTABLE = dxtr

CFLAGS  = -Wall -std=c99
CFLAGS += -g

LDFLAGS=

SOURCES = main.c

OBJECTS = $(SOURCES:.c=.o)

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) $(CFLAGS) -o $@
    
clean:
	rm -f $(DIR_BUILD)/*.o *~
