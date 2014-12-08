#ifndef ENERGY_MODEL_H
#define ENERGY_MODEL_H

struct program_options; /* Defined elsewhere */
struct cpu_topology;

int parse_energy_model(struct program_options *);
void calculate_energy_consumption(struct cpu_topology *cpu_topo, struct program_options *);

#endif
