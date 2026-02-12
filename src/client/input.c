#include "input.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/select.h>

static struct termios orig_termios;
static bool initialized = false;

void input_init(void) {
    if (initialized) return;
    
    // Get current terminal attributes
    if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) {
        return;
    }
    
    struct termios raw = orig_termios;
    
    // Disable canonical mode and echo
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    
    // Disable input processing
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    
    // Set read to return immediately
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) {
        return;
    }
    
    initialized = true;
}

void input_cleanup(void) {
    if (!initialized) return;
    
    // Restore original terminal settings
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    initialized = false;
}

bool input_is_initialized(void) {
    return initialized;
}

int input_poll_char(void) {
    if (!initialized) return -1;
    
    // Use select to check if input is available
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    
    struct timeval tv = {0, 0};  // Non-blocking
    
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
        unsigned char c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            return c;
        }
    }
    
    return -1;
}

InputAction input_poll(void) {
    int c = input_poll_char();
    if (c < 0) return INPUT_NONE;
    
    // Handle escape sequences (arrow keys)
    if (c == 27) {  // ESC
        int c2 = input_poll_char();
        if (c2 < 0) {
            // Just ESC pressed
            return INPUT_DESELECT;
        }
        
        if (c2 == '[') {
            int c3 = input_poll_char();
            switch (c3) {
                case 'A': return INPUT_SCROLL_UP;
                case 'B': return INPUT_SCROLL_DOWN;
                case 'C': return INPUT_SCROLL_RIGHT;
                case 'D': return INPUT_SCROLL_LEFT;
            }
        }
        return INPUT_NONE;
    }
    
    // Handle regular keys
    switch (c) {
        case 'q':
        case 'Q':
            return INPUT_QUIT;
            
        case 'p':
        case 'P':
        case ' ':
            return INPUT_PAUSE;
            
        case '+':
        case '=':
            return INPUT_SPEED_UP;
            
        case '-':
        case '_':
            return INPUT_SLOW_DOWN;
            
        case 'w':
        case 'W':
        case 'k':
        case 'K':
            return INPUT_SCROLL_UP;
            
        case 's':
        case 'S':
        case 'j':
        case 'J':
            return INPUT_SCROLL_DOWN;
            
        case 'a':
        case 'A':
        case 'h':
        case 'H':
            return INPUT_SCROLL_LEFT;
            
        case 'd':
        case 'D':
        case 'l':
        case 'L':
            return INPUT_SCROLL_RIGHT;
            
        case '\t':  // TAB
            return INPUT_SELECT;
            
        case 'r':
        case 'R':
            return INPUT_RESET;
    }
    
    return INPUT_NONE;
}
