#ifndef H1B3C_ENDORSEMENT_CERT_H
#define H1B3C_ENDORSEMENT_CERT_H

#include <stdint.h>
#include <unicorn/unicorn.h>

#define ENDORSEMENT_SEED_OFFSET 0x600
#define ENDORSEMENT_SEED_SIZE 32
#define ENDORSEMENT_CERT_OFFSET 0x3800
#define ENDORSEMENT_CERT_SIZE 2048

extern const uint8_t PROD_ENDORSEMENT_SEED[ENDORSEMENT_SEED_SIZE];
extern const uint32_t TEST_ENDORSEMENT_CA_RSA_N[64];
extern const uint32_t PROD_ENDORSEMENT_CA_RSA_N[64];
extern const uint8_t PROD_RO_CERT_REGION[ENDORSEMENT_CERT_SIZE];

void endorsement_patch_ca(uc_engine *uc);

#endif /* H1B3C_ENDORSEMENT_CERT_H */
