#ifndef WETYPE_CANDIDATE_PROBE_H
#define WETYPE_CANDIDATE_PROBE_H
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "session_options.h"

typedef struct {
    void *fd;
    const char *path;
    uint32_t id, version;
} DictItem;
typedef struct {
    DictItem *items;
    uint32_t count;
    const char *user_dict;
    uint8_t reserved;
} DictConfig;
typedef struct {
    const char *text;
    uint32_t text_len;
    const char *comment;
    uint32_t comment_len;
    const void *id;
    uint32_t id_len;
    unsigned char tail[68];
} Candidate;
_Static_assert(sizeof(DictItem) == 24, "dict item ABI");
_Static_assert(sizeof(DictConfig) == 32, "dict config ABI");
_Static_assert(sizeof(Candidate) == 112, "candidate ABI");
static bool (*next_candidate)(uint64_t, Candidate *);
static void (*delete_iterator)(uint64_t);
static pthread_mutex_t result_mu = PTHREAD_MUTEX_INITIALIZER;
static unsigned candidate_batches;
typedef struct { char text[4096]; unsigned char id[4096]; uint32_t id_len, cover; } CandidateCopy;
static CandidateCopy latest[10];
static unsigned latest_count;

/* Mac 3.0.1 batches event types and payloads. Type 0 payload starts with the
 * candidate iterator, as used by the app's own C ABI consumer. */
static void on_events(uint64_t context, const uint32_t *types, void *const *events,
                      uint32_t count, uint32_t flags) {
    (void)context; (void)flags;
    if (!types || !events || count > 128) return;
    for (uint32_t i = 0; i < count; ++i) {
        fprintf(stderr, "[event] type=%u\n", types[i]);
        if (types[i] != 0 || !events[i]) continue;
        uint64_t iterator;
        memcpy(&iterator, events[i], sizeof(iterator));
        if (!iterator) continue;
        Candidate c = {0};
        unsigned got = 0;
        CandidateCopy copied[10] = {0};
        while (got < 10 && next_candidate(iterator, &c)) {
            if (c.text && c.text_len < 4096) {
                printf("candidate %u: %.*s\n", got + 1, (int)c.text_len, c.text);
                memcpy(copied[got].text, c.text, c.text_len);
                copied[got].text[c.text_len] = 0;
                if (c.id && c.id_len <= sizeof(copied[got].id)) {
                    memcpy(copied[got].id, c.id, c.id_len);
                    copied[got].id_len = c.id_len;
                }
                memcpy(&copied[got].cover, (const char *)&c + 100, 4);
                ++got;
            } else break;
        }
        delete_iterator(iterator);
        if (got) {
            pthread_mutex_lock(&result_mu);
            memcpy(latest, copied, sizeof(latest));
            latest_count = got;
            ++candidate_batches;
            pthread_mutex_unlock(&result_mu);
        }
        fflush(stdout);
    }
}

static int run_candidates(void *h, const char *work_dir, const char *dict_table, const char *input) {
    FILE *f = fopen(dict_table, "r");
    if (!f) { perror("dict table"); return 5; }
    static DictItem items[64];
    static char paths[64][2048], user_dict[2048];
    unsigned count = 0;
    while (count < 64 && fscanf(f, "%2047[^\t]\t%u\t%u\n", paths[count], &items[count].id, &items[count].version) == 3) {
        items[count].path = paths[count]; ++count;
    }
    fclose(f);
    if (!count) { fprintf(stderr, "no dictionaries\n"); return 5; }
    snprintf(user_dict, sizeof(user_dict), "%s/userdict", work_dir);
    DictConfig dict = {.items = items, .count = count, .user_dict = user_dict};
    bool (*config_dict)(const DictConfig *) = dlsym(h, "wxime_config_dict");
    printf("loading %u dictionaries...\n", count); fflush(stdout);
    bool loaded = config_dict(&dict);
    printf("config_dict: %s\n", loaded ? "ok" : "failed"); fflush(stdout);
    if (!loaded) return 6;

    /* Opaque session buffer: byte offsets come from the shipped Swift field
     * metadata, not guessed from the Android version. Cloud options remain 0. */
    _Alignas(8) unsigned char config[312] = {0};
    config[0] = 1; config[1] = 1;
    config[9] = config[10] = config[11] = config[12] = 1;
    config[47] = 1; /* local normal text */
    config[58] = 1; config[60] = 1; config[174] = 1;
    config[248] = 1; /* group info unknown; no sync setup */
    config[308] = 1; /* disable cloud merge */
    wetype_session_emoji(config, getenv("WETYPE_EMOJI") != NULL);
    const char *app_name = "WetypeRime";
    memcpy(config + 192, &app_name, sizeof(app_name));
    uint32_t app_len = 10; memcpy(config + 220, &app_len, sizeof(app_len));
    uint64_t (*create_session)(const void *) = dlsym(h, "wxime_create_session");
    void (*add_listener)(uint64_t, void *, uint64_t) = dlsym(h, "wxime_add_session_listener");
    int (*process_input)(uint64_t, const char *, uint32_t, const void *, uint32_t) = dlsym(h, "wxime_process_input");
    void (*destroy_session)(uint64_t) = dlsym(h, "wxime_destroy_session");
    next_candidate = dlsym(h, "wxime_candidate_next");
    delete_iterator = dlsym(h, "wxime_delete_candidate_iterator");
    uint64_t sid = create_session(config);
    printf("session: %llu\n", (unsigned long long)sid); fflush(stdout);
    if (!sid) return 7;
    add_listener(sid, (void *)on_events, sid);
    for (const char *p = input; *p; ++p) {
        char event[4] = {'d', ' ', *p, 0};
        int r = process_input(sid, event, 3, NULL, 0);
        fprintf(stderr, "[input] key=%c status=%d\n", *p, r);
        usleep(150000);
        event[0] = 'u'; process_input(sid, event, 3, NULL, 0);
    }
    usleep(800000);
    const char *chosen = getenv("WETYPE_SELECT");
    if (chosen && *chosen) {
        CandidateCopy selected = {0};
        pthread_mutex_lock(&result_mu);
        for (unsigned i = 0; i < latest_count; ++i)
            if (!strcmp(latest[i].text, chosen)) selected = latest[i];
        pthread_mutex_unlock(&result_mu);
        if (!selected.id_len) { fprintf(stderr, "selection not present: %s\n", chosen); destroy_session(sid); return 10; }
        void (*select_candidate)(uint64_t, const char *, uint32_t, const void *, uint32_t,
            const char *, uint32_t, const char *, uint32_t, void *, uint32_t, const char *const *) = dlsym(h, "wxime_select_candidate");
        select_candidate(sid, selected.text, (uint32_t)strlen(selected.text), selected.id, selected.id_len,
                         NULL, 0, NULL, 0, NULL, 0, NULL);
        printf("selected: %s; input coverage: %u\n", selected.text, selected.cover);
        fflush(stdout);
        usleep(500000);
    }
    destroy_session(sid);
    pthread_mutex_lock(&result_mu);
    unsigned got = candidate_batches;
    pthread_mutex_unlock(&result_mu);
    printf("candidate batches: %u\n", got); fflush(stdout);
    return got ? 0 : 8;
}
#endif
