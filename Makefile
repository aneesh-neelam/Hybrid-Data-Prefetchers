all: 
	gcc -Wall -o $(CLION_EXE_DIR)/vldp_ampm_dpc2sim vldp_ampm_hybrid_prefetcher.c lib/dpc2sim.a

clean: 
	rm bin/dpc2sim

run:
	bin/runtraces.sh
