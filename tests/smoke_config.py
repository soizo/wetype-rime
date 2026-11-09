"""Opt-in macOS check of editable YAML and optional examples with real Rime.

Requires previously built native binaries and the default prepared engine.
Uses a fresh profile; never changes the user's Squirrel configuration.
"""
import ctypes as C
import argparse
from pathlib import Path
import shutil
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from wetype.integration import ROOT, SQUIRREL, stage
from fixture_cleanup import compact


class Traits(C.Structure):
    _fields_ = [('data_size', C.c_int)] + [
        (key, C.c_char_p) for key in ('shared_data_dir', 'user_data_dir',
        'distribution_name', 'distribution_code_name', 'distribution_version', 'app_name')
    ] + [('modules', C.POINTER(C.c_char_p)), ('min_log_level', C.c_int),
         ('log_dir', C.c_char_p)]


class Commit(C.Structure):
    _fields_ = [('data_size', C.c_int), ('text', C.c_char_p)]


class Composition(C.Structure):
    _fields_ = [(name, C.c_int) for name in ('length', 'cursor_pos', 'sel_start', 'sel_end')] + [('preedit', C.c_char_p)]


class Candidate(C.Structure):
    _fields_ = [('text', C.c_char_p), ('comment', C.c_char_p), ('reserved', C.c_void_p)]


class Menu(C.Structure):
    _fields_ = [(name, C.c_int) for name in ('page_size', 'page_no', 'is_last_page', 'highlighted_candidate_index', 'num_candidates')] + [('candidates', C.POINTER(Candidate)), ('select_keys', C.c_char_p)]


class Context(C.Structure):
    _fields_ = [('data_size', C.c_int), ('composition', Composition), ('menu', Menu),
                ('commit_text_preview', C.c_char_p), ('select_labels', C.POINTER(C.c_char_p))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--feature', choices=('all', 'phrases', 'force', 'traditional', 'mainland', 'native-emoji', 'fuzzy', 'fuzzy-flypy'), default='all')
    parser.add_argument('--mainland-dir', type=Path, help='Generated OpenCCGovForRime package for mainland checks')
    args = parser.parse_args()
    if args.feature == 'mainland' and args.mainland_dir is None:
        parser.error('--mainland requires --mainland-dir from OpenCCGovForRime')
    base = Path(tempfile.mkdtemp(prefix='config-extensions-', dir=ROOT / '.local'))
    user, logs = base / 'user', base / 'logs'
    user.mkdir(); logs.mkdir()
    stage(user, base / 'runtime', copy_engine=False)
    examples = ROOT / 'examples/rime-extensions'
    patches = []
    features = (('phrases', 'force', 'traditional') if args.feature == 'all'
                else (() if args.feature in ('mainland', 'native-emoji', 'fuzzy', 'fuzzy-flypy') else (args.feature,)))
    for feature in features:
        text = (examples / feature / 'wetype.custom.yaml').read_text()
        patches.append(text.split('patch:\n', 1)[1])
    if patches:
        (user / 'wetype.custom.yaml').write_text('patch:\n' + ''.join(patches))
    if args.feature == 'native-emoji':
        (user / 'wetype.custom.yaml').write_text('patch:\n  wetype/emoji_option: emoji\n  "switches/@next":\n    name: emoji\n    reset: 1\n    states: [关, 开]\n')
    shutil.copy2(examples / 'phrases/wetype_phrases.txt', user / 'wetype_phrases.txt')
    shutil.copy2(examples / 'force/wetype_force.lua', user / 'lua/wetype_force.lua')
    schema = ('wetype_flypy_gov' if args.feature == 'mainland'
              else ('wetype_flypy' if args.feature == 'fuzzy-flypy' else 'wetype'))
    if args.feature in ('fuzzy', 'fuzzy-flypy'):
        shutil.copy2(examples / 'fuzzy/wetype.custom.yaml', user / (schema + '.custom.yaml'))
    if args.feature == 'mainland':
        shutil.copy2(args.mainland_dir / 'wetype_flypy_gov.schema.yaml', user / 'wetype_flypy_gov.schema.yaml')
        shutil.copytree(args.mainland_dir / 'opencc', user / 'opencc')
        (user / 'custom_phrase.txt').write_text('简体原文\tcpraw\t50\n兌換原文\tcptrad\t50\n')
    (user / 'default.custom.yaml').write_text('patch:\n  schema_list:\n    - schema: ' + schema + '\n')
    print('Isolated configuration fixture:', base, flush=True)

    lib = C.CDLL(str(SQUIRREL / 'Frameworks/librime.1.dylib'), mode=C.RTLD_GLOBAL)
    C.CDLL(str(SQUIRREL / 'Frameworks/rime-plugins/librime-lua.dylib'), mode=C.RTLD_GLOBAL)
    # Deprecated C++ exports on the validated Squirrel build, using the public
    # structures from rime_api.h. This is test code, not the production adapter.
    def api(symbol, result, *args):
        function = getattr(lib, symbol)
        function.restype, function.argtypes = result, list(args)
        return function
    setup = api('_Z9RimeSetupP13rime_traits_t', None, C.POINTER(Traits))
    initialize = api('_Z14RimeInitializeP13rime_traits_t', None, C.POINTER(Traits))
    maintain = api('_Z20RimeStartMaintenancei', C.c_int, C.c_int)
    join = api('_Z25RimeJoinMaintenanceThreadv', None)
    create = api('_Z17RimeCreateSessionv', C.c_size_t)
    select = api('_Z16RimeSelectSchemamPKc', C.c_int, C.c_size_t, C.c_char_p)
    option = api('_Z13RimeSetOptionmPKci', None, C.c_size_t, C.c_char_p, C.c_int)
    key = api('_Z14RimeProcessKeymii', C.c_int, C.c_size_t, C.c_int, C.c_int)
    commit = api('_Z13RimeGetCommitmP13rime_commit_t', C.c_int, C.c_size_t, C.POINTER(Commit))
    free = api('_Z14RimeFreeCommitP13rime_commit_t', C.c_int, C.POINTER(Commit))
    context = api('_Z14RimeGetContextmP14rime_context_t', C.c_int, C.c_size_t, C.POINTER(Context))
    free_context = api('_Z15RimeFreeContextP14rime_context_t', C.c_int, C.POINTER(Context))
    clear = api('_Z20RimeClearCompositionm', None, C.c_size_t)
    get_property = api('_Z15RimeGetPropertymPKcPcm', C.c_int, C.c_size_t, C.c_char_p, C.c_void_p, C.c_size_t)
    def property_value(session, name):
        buffer = C.create_string_buffer(4096)
        get_property(session, name, buffer, len(buffer))
        return buffer.value
    destroy = api('_Z18RimeDestroySessionm', C.c_int, C.c_size_t)
    finalize = api('_Z12RimeFinalizev', None)
    modules = (C.c_char_p * 3)(b'default', b'lua', None)
    traits = Traits()
    traits.data_size = C.sizeof(traits) - C.sizeof(C.c_int)
    traits.shared_data_dir = str(SQUIRREL / 'SharedSupport').encode()
    traits.user_data_dir = str(user).encode()
    traits.app_name = b'rime.wetype-config-test'
    traits.modules, traits.min_log_level, traits.log_dir = modules, 2, str(logs).encode()
    setup(C.byref(traits)); initialize(C.byref(traits))
    session = 0
    try:
        maintain(1); join()
        session = create()
        assert session and select(session, schema.encode()), 'cannot select configured scheme'
        option(session, b'ascii_mode', 0)
        def expect(code, expected, final_key=' '):
            for letter in code:
                assert key(session, ord(letter), 0), f'key rejected: {letter}'
            assert key(session, ord(final_key), 0), f'commit key rejected: {final_key}'
            value = Commit(); value.data_size = C.sizeof(value) - C.sizeof(C.c_int)
            assert commit(session, C.byref(value)), f'no commit for {code}'
            try:
                actual = value.text.decode()
                assert actual == expected, (code, actual, expected)
            finally:
                free(C.byref(value))
            print(f'PASS: {code} -> {expected}', flush=True)
        if 'phrases' in features:
            expect('myphrase', '我的自定义短语')
        if 'force' in features:
            expect('myname', '自定义姓名')
        code = 'vsgo' if schema.startswith('wetype_flypy') else 'zhongguo'
        expect(code, '中國' if args.feature == 'mainland' else '中国')
        if args.feature == 'mainland':
            expect('go', '過；', final_key=';')
            assert not property_value(session, b'_wetype_error'), 'semicolon was sent to the engine as spelling'
            expect('nihc', '你好')
            # Generated variants default to mainland traditional output.
            assert key(session, 65505, 0), 'left Shift toggle rejected'
            expect(code, '中国')
            expect('cpraw', '简体原文')
            expect('cptrad', '兌換原文')
            assert key(session, 65505, 0), 'left Shift toggle rejected'
            expect(code, '中國')
            option(session, b'traditionalization', 0)
        if args.feature in ('mainland', 'native-emoji'):
            emoji_code = 'xnlm' if args.feature == 'mainland' else 'xiaolian'
            def candidates(code):
                for letter in code:
                    assert key(session, ord(letter), 0)
                value = Context(); value.data_size = C.sizeof(value) - C.sizeof(C.c_int)
                assert context(session, C.byref(value))
                try:
                    return [value.menu.candidates[i].text.decode() for i in range(value.menu.num_candidates)]
                finally:
                    free_context(C.byref(value))
            option(session, b'emoji', 1)
            rows = candidates(emoji_code)
            assert rows[:2] == ['笑脸', '😄'], rows
            assert key(session, ord('2'), 0), 'native emoji selection rejected'
            value = Commit(); value.data_size = C.sizeof(value) - C.sizeof(C.c_int)
            assert commit(session, C.byref(value))
            try:
                assert value.text.decode() == '😄', value.text
            finally:
                free(C.byref(value))
            expect(code, '中国')
            # Toggle while composing: pending candidates must be invalidated.
            candidates(emoji_code)
            option(session, b'emoji', 0)
            rows = candidates(emoji_code)
            assert '😄' not in rows and rows[0] == '笑脸', rows
            clear(session)
            option(session, b'emoji', 1)
            rows = candidates(emoji_code)
            assert '😄' in rows[:3], rows
            clear(session)
            print('PASS: native emoji ranking, numeric selection, feedback, on/off/on and composition invalidation', flush=True)
        if args.feature in ('fuzzy', 'fuzzy-flypy', 'mainland'):
            fuzzy_code = 'qb' if schema.startswith('wetype_flypy') else 'qin'
            def fuzzy_menu():
                for letter in fuzzy_code:
                    assert key(session, ord(letter), 0)
                value = Context(); value.data_size = C.sizeof(value) - C.sizeof(C.c_int)
                assert context(session, C.byref(value))
                try:
                    assert value.composition.length == len(fuzzy_code)
                    return [(value.menu.candidates[i].text.decode(),
                             (value.menu.candidates[i].comment or b'').decode())
                            for i in range(value.menu.num_candidates)]
                finally:
                    free_context(C.byref(value))
            option(session, b'wetype_fuzzy', 0)
            rows = fuzzy_menu()
            assert not any(comment == '模糊' for _, comment in rows), rows
            clear(session)
            option(session, b'wetype_fuzzy', 1)
            rows = fuzzy_menu()
            print('Fuzzy menu:', rows, flush=True)
            fuzzy_indices = [i for i, (_, comment) in enumerate(rows) if comment == '模糊']
            assert fuzzy_indices, rows
            index = fuzzy_indices[0]
            assert index < 9
            assert key(session, ord('1') + index, 0)
            value = Commit(); value.data_size = C.sizeof(value) - C.sizeof(C.c_int)
            assert commit(session, C.byref(value))
            try:
                assert value.text.decode() == rows[index][0], (value.text, rows)
            finally:
                free(C.byref(value))
            assert not property_value(session, b'_wetype_error'), 'native fuzzy selection feedback failed'
            expect(code, '中国')
            if args.feature == 'mainland':
                option(session, b'traditionalization', 1)
                rows = fuzzy_menu()
                index = next(i for i, (text, comment) in enumerate(rows) if text == '請' and comment == '模糊')
                assert key(session, ord('1') + index, 0)
                value = Commit(); value.data_size = C.sizeof(value) - C.sizeof(C.c_int)
                assert commit(session, C.byref(value))
                try:
                    assert value.text.decode() == '請'
                finally:
                    free(C.byref(value))
                option(session, b'traditionalization', 0)
            assert not property_value(session, b'_wetype_error'), 'native selection feedback failed'
            assert not property_value(session, b'_wetype_fuzzy_error'), 'Rime fuzzy branch failed'
            fuzzy_menu()
            option(session, b'wetype_fuzzy', 0)
            rows = fuzzy_menu()
            assert not any(comment == '模糊' for _, comment in rows), rows
            clear(session)
            print('PASS: Rime fuzzy spelling, original input coverage, native branch selection and live toggle', flush=True)
        if 'traditional' in features:
            option(session, b'wetype_traditional', 1)
            expect('zhongguo', '中國')
            if 'phrases' in features:
                expect('myphrase', '我的自定义短语')
            if 'force' in features:
                expect('myname', '自定義姓名')
            option(session, b'wetype_traditional', 0)
            expect('zhongguo', '中国')
    finally:
        if session:
            destroy(session)
        finalize()
    compact(base, False)


if __name__ == '__main__':
    main()
