#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/client/input.h"

#include "../src/client/input.c"

static int tests_run = 0;

static void setup_stdin_bytes(const uint8_t* bytes, size_t len, int* saved_stdin_fd) {
    int pipefd[2];
    int rc = pipe(pipefd);
    assert(rc == 0);

    if (len > 0) {
        ssize_t written = write(pipefd[1], bytes, len);
        assert(written == (ssize_t)len);
    }
    close(pipefd[1]);

    *saved_stdin_fd = dup(STDIN_FILENO);
    assert(*saved_stdin_fd >= 0);

    rc = dup2(pipefd[0], STDIN_FILENO);
    assert(rc >= 0);
    close(pipefd[0]);

    initialized = true;
}

static void restore_stdin(int saved_stdin_fd) {
    int rc = dup2(saved_stdin_fd, STDIN_FILENO);
    assert(rc >= 0);
    close(saved_stdin_fd);
    initialized = false;
}

static void test_input_poll_char_requires_init(void) {
    tests_run++;
    initialized = false;
    assert(input_poll_char() == -1);
}

static void test_input_poll_esc_alone_deselects(void) {
    tests_run++;
    int saved_stdin_fd = -1;
    uint8_t bytes[] = {27};
    setup_stdin_bytes(bytes, sizeof(bytes), &saved_stdin_fd);

    assert(input_poll() == INPUT_DESELECT);

    restore_stdin(saved_stdin_fd);
}

static void test_input_poll_arrow_sequences(void) {
    tests_run++;
    struct {
        uint8_t seq[3];
        InputAction expected;
    } cases[] = {
        {{27, '[', 'A'}, INPUT_SCROLL_UP},
        {{27, '[', 'B'}, INPUT_SCROLL_DOWN},
        {{27, '[', 'C'}, INPUT_SCROLL_RIGHT},
        {{27, '[', 'D'}, INPUT_SCROLL_LEFT},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int saved_stdin_fd = -1;
        setup_stdin_bytes(cases[i].seq, sizeof(cases[i].seq), &saved_stdin_fd);
        assert(input_poll() == cases[i].expected);
        restore_stdin(saved_stdin_fd);
    }
}

static void test_input_poll_malformed_escape_returns_none(void) {
    tests_run++;
    int saved_stdin_fd = -1;
    uint8_t bytes[] = {27, 'x'};
    setup_stdin_bytes(bytes, sizeof(bytes), &saved_stdin_fd);

    assert(input_poll() == INPUT_NONE);

    restore_stdin(saved_stdin_fd);
}

static void test_input_poll_unknown_arrow_suffix_returns_none(void) {
    tests_run++;
    int saved_stdin_fd = -1;
    uint8_t bytes[] = {27, '[', 'Z'};
    setup_stdin_bytes(bytes, sizeof(bytes), &saved_stdin_fd);

    assert(input_poll() == INPUT_NONE);

    restore_stdin(saved_stdin_fd);
}

static void test_input_poll_regular_key_aliases(void) {
    tests_run++;
    struct {
        uint8_t key;
        InputAction expected;
    } cases[] = {
        {'q', INPUT_QUIT},
        {'Q', INPUT_QUIT},
        {'p', INPUT_PAUSE},
        {' ', INPUT_PAUSE},
        {'+', INPUT_SPEED_UP},
        {'=', INPUT_SPEED_UP},
        {'-', INPUT_SLOW_DOWN},
        {'_', INPUT_SLOW_DOWN},
        {'w', INPUT_SCROLL_UP},
        {'K', INPUT_SCROLL_UP},
        {'s', INPUT_SCROLL_DOWN},
        {'J', INPUT_SCROLL_DOWN},
        {'a', INPUT_SCROLL_LEFT},
        {'H', INPUT_SCROLL_LEFT},
        {'d', INPUT_SCROLL_RIGHT},
        {'L', INPUT_SCROLL_RIGHT},
        {'\t', INPUT_SELECT},
        {'r', INPUT_RESET},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int saved_stdin_fd = -1;
        setup_stdin_bytes(&cases[i].key, 1, &saved_stdin_fd);
        assert(input_poll() == cases[i].expected);
        restore_stdin(saved_stdin_fd);
    }
}

static void test_input_poll_unknown_key_returns_none(void) {
    tests_run++;
    int saved_stdin_fd = -1;
    uint8_t key = 'x';
    setup_stdin_bytes(&key, 1, &saved_stdin_fd);

    assert(input_poll() == INPUT_NONE);

    restore_stdin(saved_stdin_fd);
}

int main(void) {
    test_input_poll_char_requires_init();
    test_input_poll_esc_alone_deselects();
    test_input_poll_arrow_sequences();
    test_input_poll_malformed_escape_returns_none();
    test_input_poll_unknown_arrow_suffix_returns_none();
    test_input_poll_regular_key_aliases();
    test_input_poll_unknown_key_returns_none();

    printf("test_client_input_surface: %d tests passed\n", tests_run);
    return 0;
}
