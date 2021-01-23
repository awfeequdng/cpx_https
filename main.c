#include <error.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

// #include <sys/prctl.h>

#define DEFAULT_CONFIG_FILE "/var/conf/cpx_https.conf"
#define PID_FILE_NAME       "/var/run/cpx_https.pid"
#define VERSION             "0.0.1"

const char *APP_NAME = "cpx_https";
// 配置文件
char *cpx_config_file = NULL;

// getopt args
int cpx_opt_no_daemon = 0;      // 默认为守护进程
int cpx_opt_debug_stderr = -1;
int cpx_opt_parse_cfg_only = 0;
// SIGHUP: reload
// SIGTERM: shutdown
// SIGKILL: kill
// SIGUSR1: rotate   : rotate是什么？
int cpx_opt_send_signal = -1;


void cpx_usage(void) {
    fprintf(stderr,
            "Usage: %s [-?hvN]\n"
            "       -h          Print help message.\n"
            "       -v          Show version and exit.\n"
            "       -N          No daemno mode.\n"
            "       -c file     Use given config-file instead of\n"
            "                   %s\n"
            "       -k reload|rotate|kill|parse\n"
            "                   kill is fast shutdown\n"
            "                   Parse configuration file, then send signal to \n"
            "                   running copy (except -k parse) and exit.\n",
            APP_NAME, DEFAULT_CONFIG_FILE);
    exit(1);
}

static void cpx_show_version(void) {
    fprintf(stderr, "%s version: %s\n", APP_NAME, VERSION);
    exit(1);
}

void cpx_parse_options(int argc, char **argv) {
    extern char *optarg;
    int c;
    // RETURN VALUE
    //        If an option was successfully found, then getopt() returns  the  option
    //        character.  If all command-line options have been parsed, then getopt()
    //        returns -1.  If getopt() encounters an option character that was not in
    //        optstring, then '?' is returned.  If getopt() encounters an option with
    //        a missing argument, then the return value depends on the first character
    //        in optstring: if it is ':', then ':' is returned; otherwise '?' is
    //        returned.
    while ((c = getopt(argc, argv, "hvNc")) != -1) {
        switch (c)
        {
        case 'h':
            cpx_usage();
            break;
        case 'v':
            cpx_show_version();
            break;
        case 'N':
            cpx_opt_no_daemon = 1;
            break;
        case 'c':
            cpx_config_file = strdup(optarg);
            break;
        case 'k':
            if ((int) strlen(optarg) < 1) {
                cpx_usage();
            } else if (strncmp(optarg, "reload", strlen(optarg)) == 0) {
                // SIGHUP: reload
                cpx_opt_send_signal = SIGHUP;
            } else if (strncmp(optarg, "rotate", strlen(optarg)) == 0) {
                // SIGUSR1： rotate
                // todo: rotate是什么意思？
                cpx_opt_send_signal = SIGUSR1;
            } else if (strncmp(optarg, "shutdown", strlen(optarg)) == 0) {
                // SIGTERM: shutdown
                cpx_opt_send_signal = SIGTERM;
            } else if (strncmp(optarg, "kill", strlen(optarg)) == 0) {
                // SIGKILL: kill
                cpx_opt_send_signal = SIGKILL;
            } else if (strncmp(optarg, "parse", strlen(optarg)) == 0) {
                // 只解析配置文件
                cpx_opt_parse_cfg_only = 1;
            } else {
                cpx_usage();
            }
            break;
        case '?':
        default:
            cpx_usage();
            break;
        }
    }
}

static void cpx_enable_core_dumps(void) {
    // todo:
}

static void cpx_write_pid_file() {
    // todo:
}

int main(int argc, char **argv) {
    // int fd;
    
    cpx_parse_options(argc, argv);

    cpx_enable_core_dumps();
    cpx_write_pid_file();
}