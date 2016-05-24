all: 
	gcc -Wall -o bin/dpc2sim hybrid_prefetcher.c lib/dpc2sim.a

clean: 
	rm bin/dpc2sim

run:
	bin/runtraces.sh

