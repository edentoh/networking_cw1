// main/security.c
#include "tasks.h"

#include "esp_err.h"
#include "mbedtls/cmac.h"

#include <string.h>

// 16-byte pre-shared key (example key from coursework)
// static const uint8_t s_aes_key[16] = {
//     0x2B, 0x7E, 0x15, 0x16,
//     0x28, 0xAE, 0xD2, 0xA6,
//     0xAB, 0xF7, 0x15, 0x88,
//     0x09, 0xCF, 0x4F, 0x3C
// };

static const uint8_t s_aes_key[16] = {
    0x2B, 0x7E, 0x15, 0x16,
    0x22, 0xA0, 0xD2, 0xA6,
    0xAC, 0xF7, 0x19, 0x88,
    0x09, 0xCF, 0x4F, 0x3C
};

static void compute_mac(const uint8_t *buf, size_t len, uint8_t out[4])
{
    uint8_t full_mac[16];

    const mbedtls_cipher_info_t *info =
        mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);

    int ret = mbedtls_cipher_cmac(info,
                                  s_aes_key, 128,
                                  buf, len,
                                  full_mac);
    if (ret != 0) {
        fast_log("SECURITY (E): CMAC failed (%d)", ret);
        memset(out, 0, 4);
        return;
    }

    // IMPORTANT: match the course skeleton = use the LAST 4 bytes
    memcpy(out, full_mac + 12, 4);
}

void sign_packet(NeighbourState *state)
{
    uint8_t mac[4];
    compute_mac((const uint8_t*)state,
                offsetof(NeighbourState, mac_tag),
                mac);
    memcpy(state->mac_tag, mac, 4);
}

bool verify_packet(NeighbourState *state)
{
    uint8_t expected[4];
    compute_mac((const uint8_t*)state,
                offsetof(NeighbourState, mac_tag),
                expected);

    uint8_t diff = 0;
    for (int i = 0; i < 4; ++i) {
        diff |= (uint8_t)(expected[i] ^ state->mac_tag[i]);
    }
    return diff == 0;
}
