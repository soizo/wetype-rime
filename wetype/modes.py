"""One catalog for native engine settings, Rime options and standalone schemas."""
import json
from pathlib import Path

MODES = json.loads(Path(__file__).with_name('modes.json').read_text(encoding='utf-8'))
SCHEMAS = ['wetype'] + ['wetype_' + mode['id'] for mode in MODES]
WUBI_DICTS = {'wubi86': 'swbd86.bin', 'wubi98': 'swbd98.bin', 'wubi_new': 'swbdnc.bin'}
SAVE_OPTIONS_PATCH = '\n  # Remember the unified wetype input mode.\n  "switcher/save_options/+": ' + json.dumps(['wetype_mode_' + m['id'] for m in MODES]) + '\n'


def native_header() -> str:
    text = '#pragma once\n#include <stdexcept>\n#include <string>\nnamespace wetype {\n'
    text += 'struct InputMode {const char *id,*label;int keyboard,double_pin;const char *group;bool semicolon;};\n'
    text += 'inline constexpr InputMode input_modes[]={\n'
    for mode in MODES:
        text += '{' + ','.join([json.dumps(mode['id']), json.dumps(mode['label'],ensure_ascii=False),
                                str(mode['keyboard']), str(mode['double_pin']), json.dumps(mode['group']),
                                str(mode['semicolon']).lower()]) + '},\n'
    text += '};\ninline const InputMode &input_mode(const std::string &id){\n'
    text += 'for(const auto &mode:input_modes)if(id==mode.id)return mode;\n'
    text += 'throw std::runtime_error("unknown input mode: "+id);}\n}\n'
    return text
