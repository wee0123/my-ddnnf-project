# Copyright 2015: Jan Burchard

# compiler
CC=g++

# compiler flags
CFLAGS= -O3 -g -std=c++0x -W -Wall -Wextra -Weffc++ -Wswitch -Wsign-compare -Iinclude 


# linker flags
# dynamic linking:
LDFLAGS=
LDADD=-lgmpxx -lgmp -lboost_system -lboost_thread -lpthread

# static linking:
#LDFLAGS=-static
#LDADD=-Wl,-Bstatic -lgmpxx -lgmp -lboost_system -lboost_thread -lpthread


SOURCES=source/countAntom.cpp  source/main.cpp
EXECUTABLE=source/countAntom



# no parameter changes below this line
OBJECTS=$(SOURCES:.cpp=.o)

DEPS=$(OBJECTS:.o=.d)


all: $(SOURCES) $(EXECUTABLE)
	
clean: 
	$(RM) source/*.o
	$(RM) source/*.d
	$(RM) $(EXECUTABLE)
	
print-%  : ; @echo $* = $($*)
	
# executable depends on the object files (build below)
# this is the linker step
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@ $(LDADD)
	
# builds a .o file for every source file


-include $(DEPS)
%.o: %.cpp
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
