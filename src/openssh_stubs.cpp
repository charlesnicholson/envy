extern "C" {
#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>

#include "ssh-sk.h"
#include "ssherr.h"
}

int sshsk_enroll(int /*type*/, const char * /*provider_path*/, const char * /*device*/,
    const char * /*application*/, const char * /*userid*/, uint8_t /*flags*/,
    struct sshbuf * /*challenge*/, struct sshkey ** /*keyp*/,
    uint8_t * /*attestation_data*/, size_t * /*attestation_len*/,
    struct sshbuf ** /*signaturep*/, uint32_t * /*counterp*/)
{
    return SSH_ERR_FEATURE_UNSUPPORTED;
}

int sshsk_sign(const char * /*provider_path*/, struct sshkey * /*key*/,
    u_char **sigp, size_t *lenp, const u_char * /*data*/, size_t /*datalen*/,
    u_int /*compat*/, const char * /*pin*/)
{
    if (sigp != nullptr) {
        *sigp = nullptr;
    }
    if (lenp != nullptr) {
        *lenp = 0;
    }
    return SSH_ERR_FEATURE_UNSUPPORTED;
}

int sshsk_load_resident(const char * /*provider_path*/, const char * /*device*/,
    const char * /*pin*/, u_int /*flags*/, struct sshsk_resident_key ***srksp,
    size_t *nsrksp)
{
    if (srksp != nullptr) {
        *srksp = nullptr;
    }
    if (nsrksp != nullptr) {
        *nsrksp = 0;
    }
    return SSH_ERR_FEATURE_UNSUPPORTED;
}

void sshsk_free_resident_keys(struct sshsk_resident_key ** /*srks*/, size_t /*nsrks*/)
{
    /* No-op stub */
}
