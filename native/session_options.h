#ifndef WETYPE_SESSION_OPTIONS_H
#define WETYPE_SESSION_OPTIONS_H
#include <stdbool.h>
/* Mac 3.0.1 (680) _wxime_session_config Swift field metadata:
 * local_most_likely_config at 44, local_process_input_config at 51.
 * Independent local Unicode Emoji, kaomoji and symbol flags.
 * Emoji Plus, cloud, WeChat-internal emoticons and Ask AI remain disabled. */
enum WetypeExpression {
    WETYPE_UNICODE_EMOJI=1, WETYPE_KAOMOJI=2,
    WETYPE_SYMBOL_EMOJI=4
};
static inline void wetype_session_expressions(unsigned char *config, unsigned flags) {
    config[45] = config[52] = (flags & WETYPE_UNICODE_EMOJI) != 0;
    config[46] = config[53] = (flags & WETYPE_KAOMOJI) != 0;
    config[54] = 0; // Emoji Plus is prohibited, including with stale flags.
    config[57] = (flags & WETYPE_SYMBOL_EMOJI) != 0;
}
static inline void wetype_session_emoji(unsigned char *config, bool enabled) {
    wetype_session_expressions(config,enabled ? WETYPE_UNICODE_EMOJI : 0);
}
#endif
