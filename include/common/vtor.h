#ifndef COMMON_VTOR_H
#define COMMON_VTOR_H

#include <stdint.h>

#include <unicorn/unicorn.h>

void set_sp_pc(uc_engine *uc, uint32_t vtor_base);

#endif /* COMMON_VTOR_H */
