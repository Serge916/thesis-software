#define _GNU_SOURCE
#include <stdio.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "filter-constants.h"

#define DECAY_INCREMENT 10000u
#define SPIKE_INCREMENT 100u

void print_unicode_divider(const char *ch)
{
    struct winsize w;
    int cols = 80;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
        cols = w.ws_col;

    for (int i = 0; i < cols; i++)
        printf("%s", ch);

    printf("\n");
}

/* ---- Ctrl+C handling ---- */
static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int signo)
{
    (void)signo;
    g_stop = 1;
}

/* ---- Cleanup state ---- */
static volatile uint8_t *g_reg_map = NULL;
static int g_fd_uio = -1;
static struct termios g_term_old;
static int g_term_saved = 0;

static void restore_terminal(void)
{
    if (g_term_saved)
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &g_term_old);
}

static void cleanup(void)
{
    restore_terminal();

    if (g_reg_map && g_reg_map != (void *)MAP_FAILED)
        (void)munmap((void *)g_reg_map, REG_MAP_SIZE);

    if (g_fd_uio >= 0)
        (void)close(g_fd_uio);

    g_reg_map = NULL;
    g_fd_uio = -1;
}

static int set_terminal_raw(void)
{
    struct termios t;

    if (tcgetattr(STDIN_FILENO, &g_term_old) != 0)
        return -1;
    g_term_saved = 1;

    t = g_term_old;
    /* Non-canonical, no echo: get keypresses immediately */
    t.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0)
        return -1;

    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const char *uio_dev = "/dev/uio5";

    /* Ensure we always close/mmap-cleanup even on early exit */
    atexit(cleanup);

    /* Catch Ctrl+C so we can cleanup properly */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) != 0)
    {
        perror("sigaction(SIGINT)");
        return 1;
    }

    g_fd_uio = open(uio_dev, O_RDWR | O_SYNC | O_CLOEXEC);
    if (g_fd_uio < 0)
    {
        perror("open(/dev/uio5)");
        return 1;
    }

    g_reg_map = (volatile uint8_t *)mmap(NULL, REG_MAP_SIZE,
                                         PROT_READ | PROT_WRITE,
                                         MAP_SHARED, g_fd_uio, 0);
    if (g_reg_map == (void *)MAP_FAILED)
    {
        perror("mmap(regs)");
        g_reg_map = NULL;
        return 1;
    }

    if (set_terminal_raw() != 0)
    {
        perror("set_terminal_raw");
        return 1;
    }
    print_unicode_divider("-");
    printf("This script controls the parameters of the Neural Filter.\n"
           "Spike Counter Limit: (+) increase, (-) decrease.\n"
           "Decay Counter Limit: (p) increase, (l) decrease.\n"
           "Press Ctrl+C (or q) to exit cleanly.\n");
    print_unicode_divider("-");

    /* Register pointer to the 32-bit register at SPIKE_COUNTER_LIMIT_OFFSET */
    volatile uint32_t *spike_limit_reg =
        (volatile uint32_t *)(g_reg_map + SPIKE_COUNTER_LIMIT_OFFSET);

    volatile uint32_t *decay_limit_reg =
        (volatile uint32_t *)(g_reg_map + DECAY_COUNTER_LIMIT_OFFSET);

    printf("Current SPIKE_COUNTER_LIMIT = %u\n", (unsigned)*spike_limit_reg);
    printf("Current DECAY_COUNTER_LIMIT = %u\n", (unsigned)*decay_limit_reg);

    while (!g_stop)
    {
        /* Wait for a keypress with a small timeout (so Ctrl+C is responsive) */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200 * 1000; /* 200 ms */

        int r = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
        if (r < 0)
        {
            if (errno == EINTR)
                continue; /* interrupted by signal, re-check g_stop */
            perror("select(stdin)");
            break;
        }
        if (r == 0)
            continue; /* timeout */

        if (FD_ISSET(STDIN_FILENO, &rfds))
        {
            unsigned char ch;
            ssize_t n = read(STDIN_FILENO, &ch, 1);
            if (n <= 0)
                continue;

            /* read offset SPIKE_COUNTER_LIMIT_OFFSET */
            uint32_t curSpike = *spike_limit_reg;
            uint32_t nextSpike = curSpike;
            uint32_t curDecay = *decay_limit_reg;
            uint32_t nextDecay = curDecay;

            /* if +, increase by 100 (decimal) */
            if (ch == '+')
            {
                if (curSpike > UINT32_MAX - SPIKE_INCREMENT)
                    nextSpike = UINT32_MAX;
                else
                    nextSpike = curSpike + SPIKE_INCREMENT;
            }
            /* if -, decrease by 100 (decimal) */
            else if (ch == '-')
            {
                if (curSpike < SPIKE_INCREMENT)
                    nextSpike = 0u;
                else
                    nextSpike = curSpike - SPIKE_INCREMENT;
            }
            else if (ch == 'p' || ch == 'P')
            {
                /* pressing p increments by 100 offset DECAY_COUNTER_LIMIT_OFFSET */
                if (curDecay > UINT32_MAX - DECAY_INCREMENT)
                    nextDecay = UINT32_MAX;
                else
                    nextDecay = curDecay + DECAY_INCREMENT;
            }
            else if (ch == 'l' || ch == 'L')
            {
                /* pressing l decrements it by 100 */
                if (curDecay < DECAY_INCREMENT)
                    nextDecay = 0u;
                else
                    nextDecay = curDecay - DECAY_INCREMENT;
            }
            else if (ch == 'q' || ch == 'Q')
            {
                g_stop = 1;
                continue;
            }
            else
            {
                continue;
            }

            /* guard the value between 0 and UINT32_MAX (done above) */
            if (nextSpike != curSpike || nextDecay != curDecay)
            {
                *spike_limit_reg = nextSpike;
                *decay_limit_reg = nextDecay;
                /* read back once (helps with posted writes on some buses) */
                uint32_t verifySpike = *spike_limit_reg;
                uint32_t verifyDecay = *decay_limit_reg;
                printf("\rSPIKE_COUNTER_LIMIT=%-10u  DECAY_COUNTER_LIMIT=%-10u", (unsigned)verifySpike, (unsigned)verifyDecay);
                fflush(stdout);
            }
        }
    }

    printf("\nExiting...\n");
    return 0; /* cleanup() runs via atexit */
}
