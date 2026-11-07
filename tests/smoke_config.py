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
    parser.add_argument('--feature', choices=('all', 'phrases', 'force', 'traditional', 'mainland', 'native-emoji'), default='all')
    args = parser.parse_args()
    base = Path(tempfile.mkdtemp(prefix='config-extensions-', dir=ROOT / '.local'))
    user, logs = base / 'user', base / 'logs'
    user.mkdir(); logs.mkdir()
    stage(user, base / 'runtime', copy_engine=False)
    examples = ROOT / 'examples/rime-extensions'
    patches = []
    features = (('phrases', 'force', 'traditional') if args.feature == 'all'
                else (() if args.feature in ('mainland', 'native-emoji') else (args.feature,)))
    for feature in features:
        text = (examples / feature / 'wetype.custom.yaml').read_text()
        patches.append(text.split('patch:\n', 1)[1])
    if patches:
        (user / 'wetype.custom.yaml').write_text('patch:\n' + ''.join(patches))
    if args.feature == 'native-emoji':
        (user / 'wetype.custom.yaml').write_text('patch:\n  wetype/emoji_option: emoji\n  "switches/@next":\n    name: emoji\n    reset: 1\n    states: [关, 开]\n')
    shutil.copy2(examples / 'phrases/wetype_phrases.txt', user / 'wetype_phrases.txt')
    shutil.copy2(examples / 'force/wetype_force.lua', user / 'lua/wetype_force.lua')
    schema = 'wetype_flypy_gov' if args.feature == 'mainland' else 'wetype'
    if args.feature == 'mainland':
        from wetype.integration import DEFAULT_RIME
        shutil.copytree(DEFAULT_RIME / 'opencc', user / 'opencc')
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
        def expect(code, expected):
            for letter in code:
                assert key(session, ord(letter), 0), f'key rejected: {letter}'
            assert key(session, ord(' '), 0), 'space rejected'
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
        code = 'vsgo' if args.feature == 'mainland' else 'zhongguo'
        expect(code, '中国')
        if args.feature == 'mainland':
            expect('nihc', '你好')
            # 左 Shift 切换到大陆繁体；右 Shift 标点与全角空格。
            assert key(session, 65505, 0), 'left Shift toggle rejected'
            expect(code, '中國')
            assert key(session, 65505, 0), 'left Shift toggle rejected'
            expect(code, '中国')
            def expect_keys(keys, expected):
                for codepoint, modifiers in keys:
                    key(session, codepoint, modifiers)
                value = Commit(); value.data_size = C.sizeof(value) - C.sizeof(C.c_int)
                assert commit(session, C.byref(value)), 'no punctuation commit'
                try:
                    assert value.text.decode() == expected, (value.text, expected)
                finally:
                    free(C.byref(value))
                print(f'PASS: key sequence -> {expected}', flush=True)
            expect_keys([(65506, 1), (123, 1), (65506, 1 << 30)], '『')
            expect_keys([(32, 1)], '　')
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
        if 'traditional' in features:
            option(session, b'wetype_traditional', 1)
            expect('zhongguo', '中國')
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
