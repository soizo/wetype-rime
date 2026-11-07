#ifndef WETYPE_SESSION_OPTIONS_H
#define WETYPE_SESSION_OPTIONS_H
#include <stdbool.h>
/* Mac 3.0.1 (680) _wxime_session_config Swift field metadata:
 * local_most_likely_config at 44, local_process_input_config at 51.
 * Normal Unicode emoji only; cloud and WeChat-internal emoticons stay off. */
static inline void wetype_session_emoji(unsigned char *config, bool enabled) {
    config[45] = enabled; /* local_most_likely.enable_normal_emoji */
    config[52] = enabled; /* local_process_input.enable_normal_emoji */
}
#endif
