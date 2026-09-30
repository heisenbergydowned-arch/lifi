#include "ProtocolConfig.h"
#include "MorseDecoder.h"
#include <stdio.h>
#include <string.h>

void ProtocolConfig_Default(ProtocolConfig *out)
{
    if (out == NULL) return;
    out->t_ms = MORSE_DECODER_DEFAULT_DOT_MS;
    out->dash_t = MORSE_DECODER_DEFAULT_DASH_T;
    out->word_gap_t = MORSE_DECODER_DEFAULT_WORD_GAP_T;
    out->msg_gap_t = MORSE_DECODER_DEFAULT_MSG_GAP_T;
}

static bool ParseU32(const char *text, const char **end, uint32_t *value)
{
    uint32_t result = 0U;
    if (text == NULL || end == NULL || value == NULL) return false;
    *end = text;
    while (**end >= '0' && **end <= '9')
    {
        uint32_t d = (uint32_t)(**end - '0');
        /* Pre-multiply overflow check: max u32 is 4294967295 = 429496729 * 10 + 5. */
        if (result > 0xFFFFFFFFU / 10U || (result == 0xFFFFFFFFU / 10U && d > 5U))
        {
            return false; /* overflow */
        }
        result = (result * 10U) + d;
        ++*end;
    }
    if (text == *end) return false; /* no digits */
    *value = result;
    return true;
}

bool ProtocolConfig_Parse(const char *text, ProtocolConfig *out)
{
    ProtocolConfig next;
    const char *p;
    uint32_t fields[4];
    unsigned i;
    if (text == NULL || out == NULL) return false;
    p = text;
    for (i = 0U; i < 4U; ++i)
    {
        if (!ParseU32(p, &p, &fields[i])) return false;
        if (i != 3U)
        {
            if (*p != '|') return false;
            ++p;
        }
    }
    if (*p != '\0') return false;
    next.t_ms = fields[0U];
    next.dash_t = fields[1U];
    next.word_gap_t = fields[2U];
    next.msg_gap_t = fields[3U];
    if (next.t_ms < MORSE_DECODER_T_MS_MIN || next.t_ms > MORSE_DECODER_T_MS_MAX) return false;
    if (next.dash_t < MORSE_DECODER_DASH_T_MIN || next.dash_t > MORSE_DECODER_DASH_T_MAX) return false;
    if (next.word_gap_t < MORSE_DECODER_WORD_GAP_T_MIN || next.word_gap_t > MORSE_DECODER_WORD_GAP_T_MAX) return false;
    if (next.msg_gap_t < MORSE_DECODER_MSG_GAP_T_MIN || next.msg_gap_t > MORSE_DECODER_MSG_GAP_T_MAX) return false;
    *out = next;
    return true;
}

bool ProtocolConfig_ToText(const ProtocolConfig *cfg, char *text, unsigned size)
{
    if (cfg == NULL || text == NULL || size < 12U) return false; /* max "2000|5|8|20" + NUL */
    return snprintf(text, size, "%lu|%lu|%lu|%lu", (unsigned long)cfg->t_ms,
                    (unsigned long)cfg->dash_t, (unsigned long)cfg->word_gap_t,
                    (unsigned long)cfg->msg_gap_t) >= 0;
}

/* Self-contained SHA-256 (public-domain style). Only stdint.h/string.h. */
static uint32_t RotR(uint32_t x, unsigned n) { return (x >> n) | (x << (32U - n)); }
static uint32_t Be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) |
           ((uint32_t)p[2] << 8U) | (uint32_t)p[3];
}

/* Single-block variant: callers only hash short config strings (<56 bytes).
 * Returns false instead of writing when the message would not fit one
 * 512-bit block with padding, which keeps the allocation-free design safe. */
static bool Sha256(const uint8_t *message, size_t length, uint8_t digest[32])
{
    static const uint32_t K[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
    };
    uint32_t state[8];
    uint32_t w[64];
    size_t i, j;
    uint8_t buffer[64];
    uint64_t bit_length;

    if (message == NULL || digest == NULL || length > 55U) return false;
    state[0] = 0x6a09e667U; state[1] = 0xbb67ae85U; state[2] = 0x3c6ef372U; state[3] = 0xa54ff53aU;
    state[4] = 0x510e527fU; state[5] = 0x9b05688cU; state[6] = 0x1f83d9abU; state[7] = 0x5be0cd19U;

    bit_length = (uint64_t)length * 8U;
    memset(buffer, 0, sizeof(buffer));
    for (i = 0U; i < length; ++i) buffer[i] = message[i];
    buffer[length] = 0x80U;
    for (j = 0U; j < 8U; ++j) buffer[63U - j] = (uint8_t)(bit_length >> (8U * j));

    for (i = 0U; i < 64U; ++i) w[i] = Be32(buffer + 4U * i);
    for (i = 16U; i < 64U; ++i)
    {
        uint32_t s0 = RotR(w[i - 15U], 7U) ^ RotR(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
        uint32_t s1 = RotR(w[i - 2U], 17U) ^ RotR(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
        w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }
    {
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (i = 0U; i < 64U; ++i)
        {
            uint32_t S1 = RotR(e, 6U) ^ RotR(e, 11U) ^ RotR(e, 25U);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t temp1 = h + S1 + ch + K[i] + w[i];
            uint32_t S0 = RotR(a, 2U) ^ RotR(a, 13U) ^ RotR(a, 22U);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;
            h = g; g = f; f = e;
            e = d + temp1;
            d = c; c = b; b = a;
            a = temp1 + temp2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
    for (i = 0U; i < 8U; ++i)
    {
        digest[4U * i] = (uint8_t)(state[i] >> 24U);
        digest[4U * i + 1U] = (uint8_t)(state[i] >> 16U);
        digest[4U * i + 2U] = (uint8_t)(state[i] >> 8U);
        digest[4U * i + 3U] = (uint8_t)state[i];
    }
    return true;
}

bool ProtocolConfig_ComputeToken(const ProtocolConfig *cfg, char *hex16)
{
    char text[32];
    uint8_t digest[32];
    static const char digits[] = "0123456789abcdef";
    int n;
    unsigned i;
    if (cfg == NULL || hex16 == NULL) return false;
    n = snprintf(text, sizeof(text), "%lu:%lu:%lu:%lu", (unsigned long)cfg->t_ms,
                 (unsigned long)cfg->dash_t, (unsigned long)cfg->word_gap_t,
                 (unsigned long)cfg->msg_gap_t);
    if (n < 0) return false;
    if (!Sha256((const uint8_t *)text, (size_t)n, digest)) return false;
    for (i = 0U; i < 8U; ++i)
    {
        hex16[2U * i] = digits[digest[i] >> 4U];
        hex16[2U * i + 1U] = digits[digest[i] & 0x0FU];
    }
    hex16[16U] = '\0';
    return true;
}
