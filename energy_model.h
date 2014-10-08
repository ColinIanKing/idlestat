#ifndef ENERGY_MODEL_H
#define ENERGY_MODEL_H

struct program_options; /* Defined elsewhere */

int parse_energy_model(struct program_options *);
void calculate_energy_consumption(struct program_options *);

#endif
