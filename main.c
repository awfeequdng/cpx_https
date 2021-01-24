#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/random.h>
#include <time.h>

#define DEFAULT_CONFIG_FILE "/var/conf/cpx_https.conf"
// #define PID_FILE_NAME       "/var/run/cpx_https.pid"
#define PID_FILE_NAME       "./cpx_https.pid"
#define VERSION             "0.0.1"

const char *APP_NAME = "cpx_https";
// �����ļ�
char *cpx_config_file = NULL;

// 
// �ڷ���ngx_spawn_process()�����½���ʱ�����˽��½������ԡ�ͨ�׵�˵���ǽ��̹����費��Ҫ������
// ��Դ���У�nginx_process.h�У������¼������Ա�ʶ��
// 
// NGX_PROCESS_NORESPAWN    ���ӽ����˳�ʱ,�����̲����ٴ�����
// NGX_PROCESS_JUST_SPAWN   ��--
// NGX_PROCESS_RESPAWN      ���ӽ����쳣�˳�ʱ,��������Ҫ����
// NGX_PROCESS_JUST_RESPAWN ��--
// NGX_PROCESS_DETACHED     ���ȴ����滻����ʱ�����������ڲ�����Nginx������½����������
// 
// NGX_PROCESS_JUST_RESPAWN��ʶ���ջ���ngx_spawn_process()����worker����ʱ����ngx_processes[s].just_spawn = 1���Դ���Ϊ����ɵ�worker���̵ı�ǡ�
// 
#define MAX_PROCESSES           128
#define PROCESS_NO_RESPAWN      -1
#define PROCESS_JUST_SPAWN      -2
#define PROCESS_RESPAWN         -3
#define PROCESS_JUST_RESPAWN    -4
#define PROCESS_DETACHED        -5

typedef void (*spawn_process_fn) (void *data);
typedef struct {
    pid_t               pid;        // ���̵�pid
    int                 status;     // ����״̬����sig_child��ͨ��waitpid��ȡ

    spawn_process_fn    proc_func;  // �������̵Ļص��������������أ�Ҫôֱ���˳�
    void                *data;      // �ص������Ĳ���
    char                *name;      // ������

    unsigned            respawn:1;      // ���̹ҵ��Ժ���master������������
    unsigned            just_spawn:1;   // 
    unsigned            detached:1;     // 
    unsigned            exiting:1;      // 
    unsigned            exited:1;       // 
} process_t;

process_t cpx_processes[MAX_PROCESSES];
int cpx_process_slot;
int cpx_last_process;

// getopt args
int cpx_opt_no_daemon = 0;      // Ĭ��Ϊ�ػ�����
int cpx_opt_debug_stderr = -1;
int cpx_opt_parse_cfg_only = 0;
// SIGHUP: reload
// SIGTERM: shutdown
// SIGKILL: kill
// SIGUSR1: rotate   : rotate��ʲô��
int cpx_opt_send_signal = -1;

sig_atomic_t cpx_reap;
sig_atomic_t cpx_terminate;
sig_atomic_t cpx_quit;

char **os_argv;
char  *os_argv_last;

void cpx_init_set_process_title(void) {
    int i;

    os_argv_last = os_argv[0];
    for (i = 0; os_argv[i]; i++) {
        if (os_argv_last == os_argv[i]) {
            fprintf(stderr, "os_argv_last = %s\n", os_argv_last);
            os_argv_last = os_argv[i] + strlen(os_argv[i]) + 1;
        }
    }
    fprintf(stderr, "os_argv_last = %s\n", os_argv_last);
    // ��ʱos_argv_lastָ��'SHELL=/bin/bash'
    os_argv_last += strlen(os_argv_last);
    fprintf(stderr, "os_argv_last = %s\n", os_argv_last);
}

void cpx_save_argv(int argc, char *const *argv) {
    os_argv = (char **)argv;
}

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
                // SIGUSR1�� rotate
                // todo: rotate��ʲô��˼��
                cpx_opt_send_signal = SIGUSR1;
            } else if (strncmp(optarg, "shutdown", strlen(optarg)) == 0) {
                // SIGTERM: shutdown
                cpx_opt_send_signal = SIGTERM;
            } else if (strncmp(optarg, "kill", strlen(optarg)) == 0) {
                // SIGKILL: kill
                cpx_opt_send_signal = SIGKILL;
            } else if (strncmp(optarg, "parse", strlen(optarg)) == 0) {
                // ֻ���������ļ�
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
// int prctl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5);
// 
// PR_SET_DUMPABLE (since Linux 2.3.20)
//               Set  the  state of the "dumpable" flag, which determines whether
//               core dumps are produced for the calling process upon delivery of
//               a signal whose default behavior is to produce a core dump.

//               In  kernels  up  to  and including 2.6.12, arg2 must be either 0
//               (SUID_DUMP_DISABLE,   process   is   not    dumpable)    or    1   
//               (SUID_DUMP_USER,  process  is dumpable).  Between kernels 2.6.13
//               and 2.6.17, the value 2 was also permitted, which caused any binary
//               which normally would not be dumped to be dumped readable by
//               root only; for security reasons, this feature has been  removed.
//               (See  also  the  description  of  /proc/sys/fs/suid_dumpable  in  
//               proc(5).)

//               Normally, this flag is set to 1.  However, it is  reset  to  the 
//               current  value  contained in the file /proc/sys/fs/suid_dumpable
//               (which by default has the value 0),  in  the  following  circumstances:

//               *  The process's effective user or group ID is changed.

//               *  The  process's  filesystem  user  or group ID is changed (see
//                  credentials(7)).

//               *  The process executes (execve(2)) a set-user-ID or  set-group-ID
//                  program,  resulting  in  a change of either the effective
//                  user ID or the effective group ID. 

//               *  The process executes (execve(2)) a program that has file  capabilities
//                  (see  capabilities(7)), but only if the permitted
//                  capabilities gained exceed those already  permitted  for  the 
//                  process.

//               Processes  that  are  not  dumpable  can  not  be  attached  via 
//               ptrace(2) PTRACE_ATTACH; see ptrace(2) for further details.

//               If a process is not dumpable, the  ownership  of  files  in  the 
//               process's  /proc/[pid]  directory  is  affected  as described in
//               proc(5).
// 
    if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) != 0) {
        fprintf(stderr, "prctl: %s\n", strerror(errno));
    }

    struct rlimit rlim;

    if (getrlimit(RLIMIT_CORE, &rlim) != 0) {
        fprintf(stderr, "getrlimit: %s\n", strerror(errno));
        return;
    }
    rlim.rlim_cur = rlim.rlim_max;
    if (setrlimit(RLIMIT_CORE, &rlim) != 0) {
        fprintf(stderr, "setrlimit: %s\n", strerror(errno));
        return;
    }
    fprintf(stdout, "Enable Core Dumps Ok!\n");
}

static void cpx_write_pid_file() {
    FILE *fp = NULL;

    // w+     Open  for  reading  and writing.  The file is created if it does
    //        not exist, otherwise it is truncated.  The stream is  positioned
    //        at the beginning of the file.
    fp = fopen(PID_FILE_NAME, "w+");
    if (!fp) {
        fprintf(stderr, "could not write pid file '%s': %s\n", PID_FILE_NAME, strerror(errno));
        return;
    }

    fprintf(fp, "%d\n", (int)getpid());
    fclose(fp);
}

static pid_t cpx_read_pid_file(void) {
    FILE *fp = NULL;
    const char *file = PID_FILE_NAME;
    pid_t pid = -1;
    int i;

    if (file == NULL) {
        fprintf(stderr, "%s: error: no pid file name defined.\n", APP_NAME);
        exit(1);
    }

    fp = fopen(file, "r");
    if (fp == NULL) {
        if (errno != ENOENT) {
            fprintf(stderr, "%s: error: could not read pid file\n", APP_NAME);
            fprintf(stderr, "\t%s: %s\n", file, strerror(errno));
            exit(1);
        }
    } else {
        // pid Ϊ 0 Ҳ����Чֵ
        pid = 0;
        if (fscanf(fp, "%d", &i) == 1) {
            pid = (pid_t)i;
        }
        fclose(fp);
    }
    return pid;
}

// return value: 
//              0: process not running
//              1: process is running
static int cpx_check_running_pid(void) {
    pid_t pid;
    pid = cpx_read_pid_file();
    if (pid < 2) {
        return 0;
    }
    // kill �����ź�0��̽�����Ƿ񻹻���
    if (kill(pid, 0) < 0) {
        return 0;
    }
    fprintf(stderr, "cpx master is already running! process id %ld\n", (long)pid);
    return 1;
}

typedef void sig_hander_fn(int sig);

void sig_child(int sig) {
    // ֪ͨ���������´���һ��worker����
    cpx_reap = 1;

    int status;
    pid_t pid;
    int i;
    do {
        pid = waitpid(-1, &status, WNOHANG);
        for (i = 0; i < cpx_last_process; i++) {
            if (cpx_processes[i].pid == pid) {
                cpx_processes[i].status = status;
                cpx_processes[i].exited = 1;
                break;
            }
        }
    } while (pid > 0);

    // todo: Ϊʲô������signal���Ǵ���һ�κ��ظ�default handler��
    signal(sig, sig_child);
}

void cpx_signal_set(int sig, sig_hander_fn *func, int flags) {
    // struct sigaction {
    //            void     (*sa_handler)(int);
    //            void     (*sa_sigaction)(int, siginfo_t *, void *);
    //            sigset_t   sa_mask;
    //            int        sa_flags;
    //            void     (*sa_restorer)(void);
    //        };
    struct sigaction sa;
    sa.sa_handler = func;
    sa.sa_flags = flags;
    // �������κ��ź�
    sigemptyset(&sa.sa_mask);
    if (sigaction(sig, &sa, NULL) < 0) {
        fprintf(stderr, "sigaction: sig=%d func=%p: %s\n", sig, func, strerror(errno));
    }
}

void cpx_init_signals(void) {
    cpx_signal_set(SIGCHLD, sig_child, SA_NODEFER | SA_RESTART);
}

pid_t spawn_process(spawn_process_fn proc_func, void *data, char *name, int respawn) {
    pid_t pid;
    int slot;
    if (respawn >= 0) {
        slot = respawn;
    } else {
        for (slot = 0; slot < cpx_last_process; slot++) {
            if (cpx_processes[slot].pid == -1) {
                break;
            }
        }
        if (slot == MAX_PROCESSES) {
            fprintf(stderr, "no more than %d process can be spawned\n", MAX_PROCESSES);
            return -1;
        }
    }

    cpx_process_slot = slot;
    pid = fork();
    switch (pid)
    {
    case -1:
        fprintf(stderr, "fork() failed while spawning \"%s\" :%s\n", name, strerror(errno));
        return -1;
    case 0:
        pid = getpid();
        proc_func(data);
        break;
    default:
        break;
    }
    // fprintf(stderr, "start %s %d\n", name, pid);
    cpx_processes[slot].pid = pid;
    cpx_processes[slot].exited = 0;
    if (respawn >= 0) {
        return pid;
    }

    cpx_processes[slot].proc_func = proc_func;
    cpx_processes[slot].data = data;
    cpx_processes[slot].name = name;
    cpx_processes[slot].exiting = 0;

    switch (respawn)
    {
    case PROCESS_NO_RESPAWN:
        cpx_processes[slot].respawn = 0;
        cpx_processes[slot].just_spawn = 0;
        cpx_processes[slot].detached = 0;
        break;
    case PROCESS_JUST_SPAWN:
        cpx_processes[slot].respawn = 0;
        cpx_processes[slot].just_spawn = 1;
        cpx_processes[slot].detached = 0;
        break;
    case PROCESS_RESPAWN:
        cpx_processes[slot].respawn = 1;
        cpx_processes[slot].just_spawn = 0;
        cpx_processes[slot].detached = 0;
        break;
    case PROCESS_JUST_RESPAWN:
        cpx_processes[slot].respawn = 1;
        cpx_processes[slot].just_spawn = 1;
        cpx_processes[slot].detached = 0;
        break;
    case PROCESS_DETACHED:
        cpx_processes[slot].respawn = 0;
        cpx_processes[slot].just_spawn = 0;
        cpx_processes[slot].detached = 1;
        break;
    
    default:
        fprintf(stderr, "error respawn type: %d\n", respawn);
        break;
    }
    if (slot == cpx_last_process) {
        cpx_last_process++;
    }
    return pid;
}

int cpx_reap_children(void) {
    int i, n;
    int live = 0;
    for (i = 0; i < cpx_last_process; i++) {
        // fprintf(stderr,"child[%d] %d exiting:%d exited:%d detached:%d respawn:%d just_spawn:%d\n",
        //                i,
        //                cpx_processes[i].pid,
        //                cpx_processes[i].exiting,
        //                cpx_processes[i].exited,
        //                cpx_processes[i].detached,
        //                cpx_processes[i].respawn,
        //                cpx_processes[i].just_spawn);
        if (cpx_processes[i].pid == -1) {
            continue;
        }
        if (cpx_processes[i].exited) {
            if (!cpx_processes[i].detached) {
                for (n = 0; n < cpx_last_process; n++) {
                    if (cpx_processes[n].exited) {
                        continue;
                    }

                    // fprintf(stderr, "detached: %d\n", cpx_processes[n].pid);
                }
            }

            if (cpx_processes[i].respawn &&
                !cpx_processes[i].exiting &&
                !cpx_terminate &&
                !cpx_quit) {
                if (spawn_process(cpx_processes[i].proc_func,
                        cpx_processes[i].data,
                        cpx_processes[i].name, i) == -1) {
                    fprintf(stderr, "could not respawn %s\n", cpx_processes[i].name);
                    continue;
                }
                live = 1;
                continue;
            }
            if (i == cpx_last_process - 1) {
                cpx_last_process--;
            } else {
                // ��i������respawn����
                cpx_processes[i].pid = -1;
            }
        } else if (cpx_processes[i].exiting || !cpx_processes[i].detached) {
            live = 1;
        }
    }
    return live;
}

static int cpx_get_npus(void) {
    int ncpu = 0;
    ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    return ncpu;
}

static void cpx_worker_process_cycle(void *data) {
    fprintf(stderr, "start worker process: %d, pid = %d\n", (int)(intptr_t)data, getpid());

    for (;;) {
        srandom(time(NULL) + (int)(intptr_t)data);
        int sec = random() % 30;
        printf("sec = %d\n", sec);
        sleep(40 - sec);
        exit(1);
    }
}

static void start_worker_processes(int type) {
    int i;

    int ncpu = cpx_get_npus(); 

    for (i = 0; i < ncpu; i++) {
        spawn_process(cpx_worker_process_cycle, (void *)(intptr_t)i, "worker process", type);
    }
}

int main(int argc, char **argv) {
    // int fd;
    sigset_t set;
    cpx_parse_options(argc, argv);
    if (-1 == cpx_opt_send_signal) {
        // �������ź�ʱ���������Ƿ���ţ�������ţ����˳�����
        if (cpx_check_running_pid()) {
            exit(1);
        }
    }

    cpx_enable_core_dumps();
    cpx_write_pid_file();
    cpx_save_argv(argc, argv);
    cpx_init_set_process_title();
    cpx_init_signals();
    sigemptyset(&set);

    printf("master pid = %d\n", getpid());
    start_worker_processes(PROCESS_RESPAWN);

    for (;;) {
        printf("father before suspend\n");
        // �ȴ������źţ����źŴ�����������ɺ�Ż᷵��
        // ������̽����˾Ͳ��᷵��
        sigsuspend(&set);
        printf("father after suspend\n");
        if (cpx_reap) {
            // ��Ҫ��������
            cpx_reap = 0;
            fprintf(stderr, "reap children\n");
            cpx_reap_children();
        }
    }
    return 0;
}