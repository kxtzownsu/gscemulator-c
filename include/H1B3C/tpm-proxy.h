#ifndef H1B3C_TPM_PROXY_H
#define H1B3C_TPM_PROXY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool TPM_ProxyStart(void);
void TPM_ProxyStop(void);

bool TPM_ProxyTransact(const uint8_t *cmd, size_t cmd_len, uint8_t *resp, size_t resp_cap,
                       size_t *resp_len);

#endif /* H1B3C_TPM_PROXY_H */
