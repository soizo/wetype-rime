#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <execinfo.h>
#include <fcntl.h>
#include "abi_macos_3.h"
#include "candidate_probe.h"

static void local_logger(int level, const char *message) {
    if (message) fprintf(stderr, "[engine:%d] %s\n", level, message);
}
static int crash_fd = -1;
static void crash_handler(int sig, siginfo_t *info, void *context) {
    (void)context;
    void *stack[64];
    int n = backtrace(stack, 64);
    dprintf(crash_fd, "signal=%d address=%p\n", sig, info->si_addr);
    backtrace_symbols_fd(stack, n, crash_fd);
    _exit(128 + sig);
}

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3 && argc != 5) { fprintf(stderr, "usage: probe ENGINE [WORK_DIR [DICT_TABLE INPUT]]\n"); return 2; }
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 3; }
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(9), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    int r = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    int saved_errno = errno;
    close(sock);
    if (r != -1 || saved_errno != EPERM) { fprintf(stderr, "network isolation missing; refusing engine load (errno=%d)\n", saved_errno); return 3; }
    printf("network connect blocked: errno=%d\n", saved_errno);
    fflush(stdout);
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    const char *crash_path = getenv("WETYPE_CRASH_LOG");
    if (crash_path) {
        crash_fd = open(crash_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (crash_fd < 0) { perror("crash.log"); return 9; }
        struct sigaction action = {0};
        action.sa_sigaction = crash_handler;
        action.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &action, NULL); sigaction(SIGABRT, &action, NULL);
        sigaction(SIGBUS, &action, NULL); sigaction(SIGILL, &action, NULL);
    }
    const char *names[] = {"wxime_initialize", "wxime_config_dict", "wxime_create_session", "wxime_process_input", "wxime_candidate_next", "wxime_select_candidate", "wxime_destroy_session", "wxime_get_version"};
    for (unsigned i = 0; i < sizeof(names)/sizeof(names[0]); ++i) {
        void *p = dlsym(h, names[i]);
        if (!p) { fprintf(stderr, "missing %s\n", names[i]); return 4; }
        printf("%s: %p\n", names[i], p);
    }
    const char *(*get_version)(void) = (const char *(*)(void))dlsym(h, "wxime_get_version");
    printf("engine version: %s\n", get_version());
    fflush(stdout);
    if (argc >= 3) {
        WetypeConfig cfg = {0};
        cfg.logger = (void *)local_logger;
        cfg.logger_config_json = "{\"logger\":{\"appender\":{\"type\":\"capi\"}},\"channels\":[]}";
        cfg.work_dir = argv[2];
        cfg.syllable_prefix_file_name = "";
        cfg.uin = "10000001";
        cfg.uin_len = (uint32_t)strlen(cfg.uin);
        cfg.log_level = 2;
        cfg.platform = 5;
        cfg.device_performance = -5;
        cfg.app_ver = "3.0.1"; cfg.app_ver_len = 5;
        cfg.device_name = "WetypeRime"; cfg.device_name_len = 10;
        cfg.system_version = "macOS"; cfg.system_version_len = 5;
        cfg.cpu_score = cfg.gpu_score = 1.0f;
        cfg.package_type = 3;
        void (*initialize)(const WetypeConfig *) = dlsym(h, "wxime_initialize");
        puts("initializing native engine..."); fflush(stdout);
        initialize(&cfg);
        puts("native initialization returned"); fflush(stdout);
        int result = 0;
        if (argc == 5) result = run_candidates(h, argv[2], argv[3], argv[4]);
        void (*finalize)(void) = dlsym(h, "wxime_finalize");
        finalize();
        puts("native finalization returned"); fflush(NULL);
        /* Vendor process-wide destructors assume the original app lifetime.
         * Finalize explicitly, then terminate without running unrelated ones. */
        _exit(result);
    }
    return 0;
}
