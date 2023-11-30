#ifndef __IJEL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <jel/jel.h>


int ijel_copy_markers(jel_config *cfg);
int ijel_image_capacity(jel_config *cfg, int component);
int ijel_capacity_iter(jel_config *cfg, int component);
int ijel_stuff_message(jel_config *cfg, int component);
int ijel_unstuff_message(jel_config *cfg, int component);
int ijel_get_freq_indices(JQUANT_TBL *q, int *i, int nfreq, int nlevels);
void ijel_log_qtables(jel_config *c);
int ijel_print_energies(jel_config *cfg);
int ac_energy(jel_config *cfg, JCOEF *mcu );


  
#ifdef __cplusplus
}
#endif

#define __IJEL_H__
#endif
