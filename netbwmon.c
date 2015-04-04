/*
    Copyright (c) 2015
    vurtun <polygone@gmx.net>
    MIT licence
*/
#ifndef linux
    #error "your platform is not supported"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <assert.h>
#include <linux/if_link.h>
#include <sys/ioctl.h>
#include <sys/unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <termios.h>

#include "arg.h"

/* macros */
#define TERM_ERR        (-1)
#define TERM_FRGRND     3
#define TERM_BKGRND     4

#define TERM_END        "K"
#define TERM_START      "1K"
#define TERM_LINE       "2K"
#define TERM_UP         "1J"
#define TERM_DOWN       "J"
#define TERM_SCREEN     "2J"

#define MAX_BUF         256
#define COLOR_DEFAULT   (-1)
#define COLOR_RX        TERM_GREEN
#define COLOR_TX        TERM_BLUE
#define APP_VERSION     "2"

#define term_color(d,c)     printf("\e[1;%d%dm", (d), (c))
#define term_color_reset(d) printf("\e[%d9m", (d))
#define term_erase(c)       printf("\e[%s", (c))
#define term_move_to(x,y)   printf("\e[%u;%u;f", (y), (x))
#define term_reset()        printf("\e[0m");
#define term_cursor_hide()  printf("\e[?25l")
#define term_cursor_show()  printf("\e[?25h")
#define term_clear()        term_erase(TERM_SCREEN)

/* types */
typedef unsigned long long ullong;
typedef unsigned long ulong;
typedef struct Terminal Terminal;
typedef struct Interface Interface;
typedef struct Graph Graph;

enum {
    TERM_BLACK,
    TERM_RED,
    TERM_GREEN,
    TERM_YELLOW,
    TERM_BLUE,
    TERM_MAGENTA,
    TERM_CYAN,
    TERM_WHITE
};

struct Terminal {
    size_t width;
    size_t height;
};

struct Interface {
    char name[IFNAMSIZ];
    ullong rx;
    ullong tx;

    ulong *rxs;
    ulong *txs;
    size_t size;

    ulong rx_avg;
    ulong tx_avg;

    ulong tx_max;
    ulong rx_max;
};

struct Graph {
    size_t nlines;
    size_t ncols;
    size_t begin_x;
    size_t begin_y;
    int color;
    const char *title;
    ulong *data;
    ulong max;
};

static const Terminal TERM_NULL;
static const Interface IFA_NULL;
static const Graph GRAPH_NULL;
static struct termios term_conf;
static volatile sig_atomic_t term_resized;

/* functions */
static void term_size(size_t*,size_t*);
static void term_printf(size_t, size_t, const char*, ...);
static int term_kbhit(void);
static int term_getch(void);

static ulong array_avg(const ulong*, size_t);
static ulong array_max(const ulong*, size_t);
static ulong *array_dup(const ulong*, size_t, size_t);

static void die(const char*, ...);
static void *ecalloc(size_t, size_t);
static double estrtod(const char *);
static size_t strlcpy(char*, const char*, size_t);
static void sleep_for(long long);
static const char *btos(ulong, char*, size_t, bool);

static int ifa_find(const char *name);
static int ifa_detect(char *buf);
static int ifa_read(const char*, ullong*, ullong*);
static int ifa_poll(Interface*, double);

static void usage(const char*);
static void sighandler(int);
static void print_graph(const Graph*, size_t, size_t, bool);
static void print_stats(const Interface*, size_t, size_t, bool);

static void
die(const char *fmt, ...)
{
    va_list args;
    fflush(stdout);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    exit(EXIT_FAILURE);
}

static void*
ecalloc(size_t nmemb, size_t size)
{
    void *p = calloc(nmemb, size);
    if (!p)
        die("out of memory\n");
    return p;
}

static double
estrtod(const char *str)
{
    assert(str);
    char *ep = NULL;
    double d = 0;;
    d = strtod(str, &ep);
    if (!d || *ep != '\0' || ep == str)
        die("invalid number: %s\n", str);
    return d;
}

static size_t
strlcpy(char *dest, const char *src, size_t size)
{
    const size_t len = strlen(src);
    if (size) {
        size = (len >= size) ? (size - 1): len;
        strncpy(dest, src, size);
        dest[size] = '\0';
    }
    return size;
}

static ulong
array_avg(const ulong *a, size_t n)
{
    ulong sum = 0; {
        size_t i = 0;
        for (i = 0; i < n; i++)
            sum += a[i];
    }
    return (sum != 0) ? (sum / n) : sum;
}

static ulong
array_max(const ulong *a, size_t n)
{
    ulong max = 0; {
        size_t i = 0;
        for (i = 0; i < n; i++) {
            if (a[i] > max)
                max = a[i];
        }
    }
    return max;
}

static ulong*
array_dup(const ulong *array, size_t n, size_t o)
{
    assert(array);
    ulong *tmp = ecalloc(n, sizeof(ulong));
    if (n > o)
        memcpy(tmp + (n - o), array, sizeof(ulong) * o);
    else
        memcpy(tmp, array + (o - n), sizeof(ulong) * n);
    return tmp;
}

static void
sleep_for(long long ms)
{
    const time_t sec = (int)(ms / 1000);
    const long long t = ms - (sec * 1000);
    struct timespec req = {
        .tv_sec = sec,
        .tv_nsec = t * 1000000L
    };
    while (nanosleep(&req, &req) == -1);
}

static void
term_printf(size_t x, size_t y, const char *fmt, ...)
{
    term_move_to((unsigned)x, (unsigned)y);
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
}

static void
term_size(size_t *width, size_t *height)
{
    assert(width && height);
    struct winsize ws;
    ioctl(1, TIOCGWINSZ, &ws);
    *width = ws.ws_col;
    *height = ws.ws_row;
}

static int
term_kbhit(void)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = { 0L, 0L };
    return select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
}

static int
term_getch(void)
{
    unsigned char c = 0;
    ssize_t r = read(0, &c, sizeof(c));
    return (int)((r < 0) ? r : c);
}

static int
ifa_find(const char *name)
{
    assert(name);
    struct ifaddrs *addr = NULL;
    if (getifaddrs(&addr) < 0)
        return 0;

    const struct ifaddrs *iter = addr;
    while (iter) {
        if (!strncmp(name, iter->ifa_name, IFNAMSIZ))
            break;
        iter = iter->ifa_next;
    }
    freeifaddrs(addr);
    return (iter != NULL) ? 1 : 0;
}

static int
ifa_detect(char *buf)
{
    assert(buf);
    struct ifaddrs *addr = NULL;
    if (getifaddrs(&addr) < 0)
        return 0;

    const struct ifaddrs *ifa = NULL;
    for (ifa = addr; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;
        if (!(ifa->ifa_flags & IFF_RUNNING))
            continue;
        if (!(ifa->ifa_flags & IFF_UP))
            continue;
        break;
    }
    if (ifa)
        strlcpy(buf, ifa->ifa_name, IFNAMSIZ);
    freeifaddrs(addr);
    return (ifa != NULL) ? 1 : 0;
}

static int
ifa_read(const char *nam, ullong *rx, ullong *tx)
{
    assert(nam && rx && tx);
    struct ifaddrs *addr = NULL;
    if (getifaddrs(&addr) < 0)
        return 0;

    const struct ifaddrs *ifa = NULL;
    const struct rtnl_link_stats *stats = NULL;
    for (ifa = addr; ifa; ifa = ifa->ifa_next) {
        if (strncmp(nam, ifa->ifa_name, IFNAMSIZ))
            continue;
        if (ifa->ifa_addr->sa_family != AF_PACKET)
            continue;
        if (!ifa->ifa_data)
            continue;

        stats = ifa->ifa_data;
        *rx = stats->rx_bytes;
        *tx = stats->tx_bytes;
        break;
    }

    freeifaddrs(addr);
    return (!stats) ? 0 : 1;
}

static int
ifa_poll(Interface *ifa, double delay)
{
    assert(ifa);
    static ullong rx;
    static ullong tx;
    if (rx && tx && !term_resized) {
        if (!ifa_read(ifa->name, &ifa->rx, &ifa->tx))
            return 0;

        memmove(ifa->rxs, ifa->rxs + 1, sizeof(ulong) * (ifa->size-1));
        memmove(ifa->txs, ifa->txs + 1, sizeof(ulong) * (ifa->size-1));

        ifa->rxs[ifa->size - 1] = (ulong)((double)(ifa->rx - rx) / delay);
        ifa->txs[ifa->size - 1] = (ulong)((double)(ifa->tx - tx) / delay);

        ifa->rx_avg = array_avg(ifa->rxs, ifa->size);
        ifa->tx_avg = array_avg(ifa->txs, ifa->size);

        ifa->rx_max = array_max(ifa->rxs, ifa->size);
        ifa->tx_max = array_max(ifa->txs, ifa->size);
    }
    return ifa_read(ifa->name, &rx, &tx);
}

static const char*
btos(ulong len, char *buf, size_t siz, bool siunits)
{
    assert(buf);
    int i = 0;
    double bytes = (double)len;
    const double prefix = siunits ? 1000.0 : 1024.0;
    for (i = 0; bytes >= prefix && i < 4; i++)
        bytes /= prefix;

    static const char *bs[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    static const char *si[] = {"B", "kB", "MB", "GB", "TB"};
    const char *unit = siunits ? si[i] : bs[i];
    const char *fmt = i ? "%.2f %s" : "%.0f %s";
    snprintf(buf, siz, fmt, bytes, unit);
    return buf;
}

static void
print_graph(const Graph *graph, size_t lines, size_t cols, bool si)
{
    assert(graph);
    if (lines < 3 || cols < 2)
        return;

    size_t i = 0, j = 0;
    const size_t x = graph->begin_x;
    const size_t y = graph->begin_y;

    if (graph->color != COLOR_DEFAULT)
        term_color(TERM_FRGRND, graph->color);

    for (i = 0; i < lines- 1; i++) {
        term_move_to((unsigned)x, (unsigned)(y + i));
        term_erase(TERM_LINE);
        fputc('_', stdout);
        term_move_to((unsigned)x+cols+3, (unsigned)(y + i));
        fputc('_', stdout);
    }

    char buf[MAX_BUF];
    char buf1[MAX_BUF];
    const char *max = btos(graph->max, buf, MAX_BUF, si);
    const char *curr = btos(graph->data[cols-1], buf1, MAX_BUF, si);
    term_printf(x + 1, y, "[ %s/s ][ %s/s ]", curr, max);
    //term_printf(x+15, y, "[ %s/s ]", btos(graph->data[cols-1], buf, MAX_BUF, si));
    term_printf(x + 1, y + lines-1, "[ %s/s ]", btos(0.0, buf, MAX_BUF, si));

    lines = lines - 1;
    for (j = lines-1; j > 0; --j) {
        for (i = cols-1; i > 0; --i) {
            if (!graph->data[cols-i] || !graph->max)
                continue;

            const size_t bar_height = lines-((graph->data[cols-i]*lines)/graph->max);
            if (bar_height < j) {
                term_move_to((unsigned)(i + x + 2), (unsigned)(j + y));
                fputc('*', stdout);
            }
        }
    }
    term_color_reset(TERM_FRGRND);
}

static void
print_stats(const Interface *ifa, size_t line, size_t width, bool si)
{
    assert(ifa && ifa->size > 0);
    if (!ifa || ifa->size <= 0)
        return;

    size_t i = 0;
    for (i = line; i < line + 5; i++) {
        term_move_to(0, (int)i);
        term_erase(TERM_LINE);
    }

    char buf[MAX_BUF] = {0};
    const size_t cols = ifa->size;
    const ulong col_rx = ifa->size / 4 - 8;
    const ulong col_tx = col_rx + ifa->size / 2 + 1;
    const char fmt[] = "%6s %s/s";
    const char fmt1[] = "%6s %s";

    term_printf((width/2)-8, line++, "[ Interface: %s ]\n", ifa->name);
    //term_printf(col_rx, line, fmt,"RX:",btos(ifa->rxs[cols-1],buf,MAX_BUF,si));
    //term_printf(col_tx, line++,fmt,"TX:",btos(ifa->txs[cols-1],buf,MAX_BUF,si));
    term_printf(col_rx, line, fmt, "avg:", btos(ifa->rx_avg,buf,MAX_BUF,si));
    term_printf(col_tx, line++, fmt, "avg:", btos(ifa->tx_avg,buf,MAX_BUF,si));
    //term_printf(col_rx, line, fmt, "max:", btos(ifa->rx_max,buf,MAX_BUF,si));
    //term_printf(col_tx, line++, fmt, "max:", btos(ifa->tx_max,buf,MAX_BUF,si));
    term_printf(col_rx, line, fmt1, "total:", btos(ifa->rx, buf, MAX_BUF, si));
    term_printf(col_tx, line, fmt1, "total:", btos(ifa->tx, buf, MAX_BUF, si));
}

static void
sighandler(int sig)
{
    if (sig == SIGWINCH) {
        term_resized = 1;
        return;
    }
    static char const clear[] = "\e[2J";
    static char const creset[] = "\e[0;0;f";
    static char const cshow[] = "\e[?25h";
    tcsetattr(0, TCSANOW, &term_conf);
    write(STDOUT_FILENO, clear, sizeof clear);
    write(STDOUT_FILENO, creset, sizeof creset);
    write(STDOUT_FILENO, cshow, sizeof cshow);
    exit(sig);
}

static void
usage(const char *app)
{
    die(
        "\n"
        "usage: %s [options]\n"
        "\n"
        "   options:\n"
        "\n"
        "   -h,         help\n"
        "   -v,         version\n"
        "   -c,         no colors\n"
        "   -s,         use SI units\n"
        "   -d <sec>,   redraw delay\n"
        "   -i <i>,     network interface\n"
        "\n",
        app
    );
}

int
main(int argc, char *argv[])
{
    /* Globals */
    Terminal term = TERM_NULL;
    Interface ifa = IFA_NULL;
    Graph graph_rx = GRAPH_NULL;
    Graph graph_tx = GRAPH_NULL;

    /* Arguments */
    bool colors = true;
    double delay = 1.0f;
    bool siunits = false;
    size_t graph_lines = 0;
    size_t stats_lines = 0;

    ARG_BEGIN {
    case 'v':
        die("%s-%s\n", argv0, APP_VERSION);
        break;
    case 'c':
        colors = false;
        break;
    case 's':
        siunits = true;
        break;
    case 'd':
        delay = estrtod(ARG_EF(usage(argv0)));
        break;
    case 'i':
        strlcpy(ifa.name, ARG_EF(usage(argv0)), IFNAMSIZ);
        break;
    case 'h':
    default:
        usage(argv0);
    } ARG_END;

    if (!ifa_find(ifa.name) && !ifa_detect(ifa.name))
        die("failed to detect network interface\n");

    /* Terminal */
    struct termios term_new;
    tcgetattr(0, &term_conf);
    term_new = term_conf;
    term_new.c_lflag &= ~(unsigned)ICANON;
    term_new.c_lflag &= ~(unsigned)ECHO;
    tcsetattr(0, TCSANOW, &term_new);

    term_clear();
    term_move_to(0,0);
    term_size(&term.width, &term.height);
    if (term.width < 3 || term.height < 9)
        die("terminal is to small!\n");
    term_cursor_hide();

    /* Interface */
    size_t cols = term.width - 3;
    ifa.rxs = ecalloc(cols, sizeof(*ifa.rxs));
    ifa.txs = ecalloc(cols, sizeof(*ifa.txs));
    ifa.size = cols;

    /* Graphs */
    graph_lines = (term.height - 5) / 2;
    stats_lines = graph_lines * 2 + 1;

    graph_rx.title = "[ RX ]";
    graph_rx.nlines = graph_lines;
    graph_rx.ncols = term.width;
    graph_rx.begin_y = 1;
    graph_rx.begin_x = 0;
    graph_rx.color = colors ? COLOR_RX : COLOR_DEFAULT;

    graph_tx.title = "[ TX ]";
    graph_tx.nlines = graph_lines;
    graph_tx.ncols = term.width;
    graph_tx.begin_y = graph_lines + 1;
    graph_tx.begin_x = 0;
    graph_tx.color = colors ? COLOR_TX : COLOR_DEFAULT;

    /* Signals */
    signal(SIGINT, sighandler);
    signal(SIGWINCH, sighandler);
    unsigned int aa = 0;

    while (1) {
        /* Input */
        while (term_kbhit()) {
            const int c = term_getch();
            if (c == TERM_ERR || c == 'q')
                sighandler(EXIT_SUCCESS);
        }

        if (term_resized) {
            size_t width = 0, height = 0;
            term_size(&width, &height);
            if (term.width < 3 || term.height < 9)
                die("terminal is to small!\n");

            ifa.size = width-3;
            ulong *rxs = array_dup(ifa.rxs, width-3, term.width-3);
            ulong *txs = array_dup(ifa.txs, width-3, term.width-3);
            free(ifa.rxs);
            free(ifa.txs);
            ifa.rxs = rxs;
            ifa.txs = txs;

            term.width = width;
            term.height = height;

            graph_lines = (term.height - 5) / 2;
            stats_lines = graph_lines * 2 + 1;
            graph_rx.nlines = graph_lines;
            graph_tx.nlines = graph_lines;
            graph_rx.ncols = term.width;
            graph_tx.ncols = term.width;
            graph_tx.begin_y = graph_lines + 1;

            term_resized = 0;
            term_clear();
        }

        /* Update */
        if (!ifa_poll(&ifa, delay))
            die("failed to read rx and tx bytes from %s\n", ifa.name);
        graph_rx.data = ifa.rxs;
        graph_tx.data = ifa.txs;
        graph_rx.max = ifa.rx_max;
        graph_tx.max = ifa.tx_max;

        /* Draw */
        term_move_to(0,0);
        print_graph(&graph_rx, graph_lines, ifa.size, siunits);
        print_graph(&graph_tx, graph_lines, ifa.size, siunits);
        if(aa == 10) aa = 0;
        if(aa == 1) print_stats(&ifa, stats_lines, term.width, siunits);
        ++aa;
        fflush
        (stdout);
        sleep_for((long long)(delay * 1000.0));
    }
}

