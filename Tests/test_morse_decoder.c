#include "MorseDecoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    MorseDecoder decoder;
    uint32_t now;
    uint32_t unit;
    char output[512];
    unsigned int output_length;
    unsigned int errors[5];
    unsigned int events;
    char last_code[MORSE_DECODER_MAX_SYMBOLS + 1U];
} Fixture;

static unsigned int checks;

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void Drain(Fixture *f)
{
    MorseDecoderEvent event;
    while (MorseDecoder_PopEvent(&f->decoder, &event))
    {
        CHECK(f->output_length < (sizeof(f->output) - 1U));
        f->output[f->output_length++] = event.character;
        f->output[f->output_length] = '\0';
        f->events++;
        if (event.type == MORSE_EVENT_INVALID)
        {
            CHECK((unsigned int)event.error < 5U);
            f->errors[event.error]++;
        }
        if ((event.type == MORSE_EVENT_CHARACTER) ||
            (event.type == MORSE_EVENT_INVALID))
        {
            memcpy(f->last_code, event.code, sizeof(f->last_code));
        }
    }
}

static void Hold(Fixture *f, bool light, uint32_t milliseconds)
{
    uint32_t i;
    for (i = 0U; i < milliseconds; i++)
    {
        MorseDecoder_Feed(&f->decoder, light, f->now);
        f->now++;
        Drain(f);
    }
}

static void InitAt(Fixture *f, uint32_t unit, uint32_t now)
{
    memset(f, 0, sizeof(*f));
    f->unit = unit;
    f->now = now;
    MorseDecoder_Init(&f->decoder, unit, MORSE_DECODER_DEFAULT_DASH_T,
                      MORSE_DECODER_DEFAULT_WORD_GAP_T,
                      MORSE_DECODER_DEFAULT_MSG_GAP_T);
    Hold(f, false, 3U * unit);
}

static void Code(Fixture *f, const char *code, uint32_t trailingGap)
{
    while (*code != '\0')
    {
        Hold(f, true, (*code == '.') ? f->unit : (3U * f->unit));
        code++;
        Hold(f, false, (*code != '\0') ? f->unit : trailingGap);
    }
}

static void Expect(Fixture *f, const char *expected)
{
    if (strcmp(f->output, expected) != 0)
    {
        fprintf(stderr, "Expected [%s], got [%s]\n", expected, f->output);
    }
    CHECK(strcmp(f->output, expected) == 0);
}

static void TestMessages(uint32_t unit)
{
    Fixture f;
    InitAt(&f, unit, 0U);
    Code(&f, "....", 3U * unit);
    Code(&f, ".", 3U * unit);
    Code(&f, ".-..", 3U * unit);
    Code(&f, ".-..", 3U * unit);
    Code(&f, "---", 5U * unit); /* Morsify word gap. */
    Code(&f, "...", 3U * unit);
    Code(&f, "---", 3U * unit);
    Code(&f, "...", 12U * unit);
    Expect(&f, "HELLO SOS\n");
    Hold(&f, false, 100U * unit);
    Expect(&f, "HELLO SOS\n"); /* No repeated last character/newline. */

    Code(&f, ".", 7U * unit); /* Standard Morse word gap. */
    Code(&f, "-", 12U * unit);
    Expect(&f, "HELLO SOS\nE T\n");
}

static void TestAlphabet(uint32_t unit)
{
    /* Independent transmitted test vectors in standard alphabetic order. */
    static const char *vectors[] =
    {
        ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..",
        ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.",
        "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..",
        "-----", ".----", "..---", "...--", "....-", ".....", "-....",
        "--...", "---..", "----.", ".-.-.", "-...-", ".-.-.-", "--..--",
        "..--..", "-..-.", "-....-"
    };
    Fixture f;
    unsigned int i;
    InitAt(&f, unit, 123U);
    for (i = 0U; i < (sizeof(vectors) / sizeof(vectors[0])); i++)
    {
        Code(&f, vectors[i], 3U * unit);
    }
    Hold(&f, false, 12U * unit);
    Expect(&f, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+=.,?/-\n");
}

static void TestLastCharacterAndBoundary(void)
{
    Fixture f;
    InitAt(&f, 60U, 0U);
    Hold(&f, true, 60U);
    Hold(&f, false, 120U); /* Samples elapsed 0..119 ms. */
    Expect(&f, "");
    Hold(&f, false, 1U); /* Exactly 2T: flush without a new light edge. */
    Expect(&f, "E");
    CHECK(strcmp(f.last_code, ".") == 0);
    Hold(&f, false, 479U);
    Expect(&f, "E");
    Hold(&f, false, 1U); /* Exactly 10T: newline, no trailing space. */
    Expect(&f, "E\n");
    Hold(&f, false, 1000U);
    Expect(&f, "E\n");

    /* Pulse lengths on either side of the 2T decision boundary. */
    Hold(&f, true, 119U);
    Hold(&f, false, 180U);
    Hold(&f, true, 120U);
    Hold(&f, false, 720U);
    Expect(&f, "E\nET\n");
}

static void TestNoiseAndRecovery(void)
{
    Fixture f;
    InitAt(&f, 60U, 0U);
    Hold(&f, true, 5U); /* An isolated short glint is not a letter. */
    Hold(&f, false, 1000U);
    Expect(&f, "");

    /* Noise during a dark word gap must not emit a premature space. */
    Code(&f, ".", 5U * f.unit);
    Hold(&f, true, 5U);
    Hold(&f, false, 2U * f.unit);
    Expect(&f, "E");
    Code(&f, "-", 12U * f.unit);
    Expect(&f, "E T\n");

    Code(&f, "......", 3U * f.unit);
    CHECK(f.errors[MORSE_ERROR_UNKNOWN_CODE] == 1U);
    CHECK(strcmp(f.last_code, "......") == 0);
    Code(&f, ".", 12U * f.unit);
    Expect(&f, "E T\n?E\n");

    Hold(&f, true, 20U * f.unit);
    CHECK(f.errors[MORSE_ERROR_PULSE_TOO_LONG] == 1U);
    Hold(&f, true, 20U * f.unit);
    CHECK(f.errors[MORSE_ERROR_PULSE_TOO_LONG] == 1U);
    Hold(&f, false, 3U * f.unit);
    Code(&f, "...", 12U * f.unit);
    Expect(&f, "E T\n?E\n?S\n");

    Code(&f, ".........", 3U * f.unit);
    CHECK(f.errors[MORSE_ERROR_TOO_MANY_SYMBOLS] == 1U);
    CHECK(strlen(f.last_code) == MORSE_DECODER_MAX_SYMBOLS);
    Code(&f, ".-", 12U * f.unit);
    Expect(&f, "E T\n?E\n?S\n?A\n");
}

static void TestStartupResetAndWrap(void)
{
    Fixture f;
    memset(&f, 0, sizeof(f));
    f.unit = 60U;
    MorseDecoder_Init(&f.decoder, f.unit, MORSE_DECODER_DEFAULT_DASH_T,
                      MORSE_DECODER_DEFAULT_WORD_GAP_T,
                      MORSE_DECODER_DEFAULT_MSG_GAP_T);
    Hold(&f, true, 3000U); /* Already illuminated at startup: ignore. */
    Hold(&f, false, 3U * f.unit);
    Expect(&f, "");
    Code(&f, ".", 12U * f.unit);
    Expect(&f, "E\n");

    Code(&f, ".", f.unit); /* Pending partial character. */
    MorseDecoder_Reset(&f.decoder, f.now);
    Hold(&f, false, 3U * f.unit);
    Code(&f, "-", 12U * f.unit);
    Expect(&f, "E\nT\n");

    InitAt(&f, 60U, UINT32_MAX - 240U);
    Code(&f, "...", 3U * f.unit);
    Code(&f, "---", 3U * f.unit);
    Code(&f, "...", 12U * f.unit);
    Expect(&f, "SOS\n");
    CHECK(f.now < 10000U);
}

static void TestTimingTolerance(void)
{
    Fixture f;
    unsigned int i;
    InitAt(&f, 60U, 0U);
    for (i = 0U; i < 4U; i++)
    {
        Hold(&f, true, (i & 1U) ? 75U : 45U);
        Hold(&f, false, (i == 3U) ? 180U : 75U);
    }
    for (i = 0U; i < 3U; i++)
    {
        Hold(&f, true, (i & 1U) ? 195U : 165U);
        Hold(&f, false, (i == 2U) ? 720U : 45U);
    }
    Expect(&f, "HO\n");
}

static void TestOverflowAndReset(void)
{
    MorseDecoder decoder;
    MorseDecoderEvent event;
    uint32_t now = 0U;
    unsigned int i;
    unsigned int j;
    unsigned int received = 0U;
    MorseDecoder_Init(&decoder, 60U, MORSE_DECODER_DEFAULT_DASH_T,
                      MORSE_DECODER_DEFAULT_WORD_GAP_T,
                      MORSE_DECODER_DEFAULT_MSG_GAP_T);
    for (i = 0U; i < 180U; i++)
    {
        MorseDecoder_Feed(&decoder, false, now++);
    }
    for (i = 0U; i < 18U; i++)
    {
        for (j = 0U; j < 60U; j++)
        {
            MorseDecoder_Feed(&decoder, true, now++);
        }
        for (j = 0U; j < 180U; j++)
        {
            MorseDecoder_Feed(&decoder, false, now++);
        }
    }
    while (MorseDecoder_PopEvent(&decoder, &event))
    {
        if (received < MORSE_DECODER_QUEUE_LENGTH)
        {
            CHECK(event.type == MORSE_EVENT_CHARACTER);
            CHECK(event.character == 'E');
        }
        else
        {
            CHECK(event.type == MORSE_EVENT_INVALID);
            CHECK(event.error == MORSE_ERROR_EVENT_OVERFLOW);
        }
        received++;
    }
    CHECK(received == (MORSE_DECODER_QUEUE_LENGTH + 1U));
    MorseDecoder_Reset(&decoder, now);
    CHECK(!MorseDecoder_PopEvent(&decoder, &event));
}

static void NewFixture(Fixture *f, uint32_t dotMs, uint32_t dashT,
                       uint32_t wordGapT, uint32_t msgGapT)
{
    memset(f, 0, sizeof(*f));
    f->unit = dotMs;
    f->now = 0U;
    MorseDecoder_Init(&f->decoder, dotMs, dashT, wordGapT, msgGapT);
    Hold(f, false, 3U * dotMs);
}

/* Pulse on both sides of the (1+dash_t)/2*T boundary: below -> dot,
 * at/above -> dash. dotSide/dashSide are exact half-T boundary pairs. */
static void TestDashTBoundary(uint32_t dashT, uint32_t dotSide, uint32_t dashSide)
{
    Fixture f;
    NewFixture(&f, 60U, dashT, 4U, 10U);
    Hold(&f, true, dotSide);   /* '.' */
    Hold(&f, false, 1U * f.unit);
    Hold(&f, true, dashSide);  /* '-' */
    Hold(&f, false, 12U * f.unit); /* Hold() accumulates hold-1 ms max. */
    Expect(&f, "A\n");
}

/* 2*dash_t*T is the overlong limit: okMs (== limit-1) still decodes as a
 * dash, badMs (== limit+1) invalidates the character exactly once. */
static void TestOverlongLimit(uint32_t dashT, uint32_t okMs, uint32_t badMs)
{
    Fixture f;
    NewFixture(&f, 60U, dashT, 4U, 10U);
    Hold(&f, true, okMs);
    Hold(&f, false, 2U * f.unit + 1U); /* flush at exactly 2T */
    CHECK(f.errors[MORSE_ERROR_PULSE_TOO_LONG] == 0U);
    Expect(&f, "T");
    Hold(&f, true, badMs);
    Hold(&f, false, 2U * f.unit);
    CHECK(f.errors[MORSE_ERROR_PULSE_TOO_LONG] == 1U);
    Code(&f, ".", 12U * f.unit);
    Expect(&f, "T?E\n");
}

/* Non-default word_gap_t=5 / msg_gap_t=12: 3T letter gaps stay letters,
 * a 5T gap inserts one space, and only >=12T ends the message. */
static void TestCustomGaps(void)
{
    Fixture f;
    NewFixture(&f, 60U, 3U, 5U, 12U);
    Code(&f, ".", 3U * f.unit);
    Code(&f, ".", 3U * f.unit);
    Expect(&f, "EE");
    Hold(&f, false, 2U * f.unit); /* letter gap now totals 5T */
    Code(&f, "-", 14U * f.unit); /* 14T hold crosses the 12T idle gap */
    Code(&f, ".", 14U * f.unit);
    Expect(&f, "EE T\nE\n");
    Hold(&f, false, 100U * f.unit);
    Expect(&f, "EE T\nE\n");
}

static void TestInitClampingAndReset(void)
{
    MorseDecoder decoder;
    MorseDecoder_Init(&decoder, 0U, 0U, 0U, 0U); /* 0 -> defaults */
    CHECK(decoder.dot_time_ms == MORSE_DECODER_DEFAULT_DOT_MS);
    CHECK(decoder.dash_t == MORSE_DECODER_DEFAULT_DASH_T);
    CHECK(decoder.word_gap_t == MORSE_DECODER_DEFAULT_WORD_GAP_T);
    CHECK(decoder.msg_gap_t == MORSE_DECODER_DEFAULT_MSG_GAP_T);
    MorseDecoder_Init(&decoder, 10U, 1U, 1U, 2U); /* below min -> clamp up */
    CHECK(decoder.dot_time_ms == MORSE_DECODER_T_MS_MIN);
    CHECK(decoder.dash_t == MORSE_DECODER_DASH_T_MIN);
    CHECK(decoder.word_gap_t == MORSE_DECODER_WORD_GAP_T_MIN);
    CHECK(decoder.msg_gap_t == MORSE_DECODER_MSG_GAP_T_MIN);
    MorseDecoder_Init(&decoder, 9999U, 9U, 9U, 99U); /* above max -> clamp down */
    CHECK(decoder.dot_time_ms == MORSE_DECODER_T_MS_MAX);
    CHECK(decoder.dash_t == MORSE_DECODER_DASH_T_MAX);
    CHECK(decoder.word_gap_t == MORSE_DECODER_WORD_GAP_T_MAX);
    CHECK(decoder.msg_gap_t == MORSE_DECODER_MSG_GAP_T_MAX);
    MorseDecoder_Init(&decoder, 240U, 4U, 6U, 15U);
    MorseDecoder_Reset(&decoder, 1234U);
    CHECK(decoder.dot_time_ms == 240U);
    CHECK(decoder.dash_t == 4U);
    CHECK(decoder.word_gap_t == 6U);
    CHECK(decoder.msg_gap_t == 15U);
}

int main(void)
{
    TestMessages(60U);
    TestMessages(120U);
    TestMessages(240U);
    TestAlphabet(60U);
    TestAlphabet(120U);
    TestAlphabet(240U);
    TestLastCharacterAndBoundary();
    TestNoiseAndRecovery();
    TestStartupResetAndWrap();
    TestTimingTolerance();
    TestOverflowAndReset();
    TestDashTBoundary(2U, 89U, 90U);   /* boundary 1.5T */
    TestDashTBoundary(4U, 149U, 150U); /* boundary 2.5T */
    TestDashTBoundary(5U, 179U, 180U); /* boundary 3T */
    TestOverlongLimit(2U, 239U, 241U); /* limit 4T */
    TestOverlongLimit(4U, 479U, 481U); /* limit 8T */
    TestOverlongLimit(5U, 599U, 601U); /* limit 10T */
    TestCustomGaps();
    TestInitClampingAndReset();
    printf("PASS: production MorseDecoder.c, %u checks\n", checks);
    return 0;
}
