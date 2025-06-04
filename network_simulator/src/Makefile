
CXXFLAGS = -O3 -Wall -I ./

.PHONY: clean

simulator : simdjson.o main.o
	g++ -o $@ -I ./ $^

simdjson.o : simdjson.cpp

main.o : main.cpp

clean:
	rm *.o simulator
