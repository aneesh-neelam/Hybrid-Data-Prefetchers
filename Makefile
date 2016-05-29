all: 
	gcc -Wall -o bin/vldp_ampm vldp_ampm_hybrid.c lib/dpc2sim.a
	gcc -Wall -o bin/dcpt_vldp dcpt_vldp_hybrid.c lib/dpc2sim.a

clean: 
	rm bin/vldp_ampm
	rm bin/dcpt_vldp

run:
	bin/runtraces.sh
