#ifndef H1B3C_PROXIES_H
#define H1B3C_PROXIES_H

#include <stdbool.h>

void H1B3C_StartProxies(bool disable_tpm, bool disable_usb);
void H1B3C_StopProxies(void);
void H1B3C_RestartUSBProxy(void);
void H1B3C_SuspendUSBProxy(void);
void H1B3C_ResumeUSBProxy(void);

#endif /* H1B3C_PROXIES_H */
