#ifndef H1B3C_RMA_H
#define H1B3C_RMA_H

#include <stdbool.h>
#include <stddef.h>

bool RMA_ChallengeSeen(const char *challenge, size_t length);
void RMA_Poll(void);

#endif /* H1B3C_RMA_H */
