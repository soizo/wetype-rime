import json
from pathlib import Path
import plistlib
import struct
import tempfile
import unittest
from wetype.macos import SOURCE_SHA256, convert_image, sha256, verify, discover, dictionary_rows


def executable():
    pagezero = struct.pack('<II16sQQQQiiII', 0x19, 72, b'__PAGEZERO', 0, 1 << 32, 0, 0, 0, 0, 0, 0)
    text = struct.pack('<II16sQQQQiiII', 0x19, 72, b'__TEXT', 1 << 32, 4096, 0, 4096, 5, 5, 0, 0)
    entry = struct.pack('<IIQQ', 0x80000028, 24, 256, 0)
    commands = pagezero + text + entry
    header = struct.pack('<8I', 0xFEEDFACF, 0x100000C, 0, 2, 3, len(commands), 0x200004, 0)
    return header + commands + bytes(range(128))


class MachOTests(unittest.TestCase):
    def test_conversion_preserves_code_and_segment_order(self):
        source = executable()
        output = convert_image(source)
        self.assertEqual(output[200:], source[200:])
        self.assertEqual(output[40:56].rstrip(b'\0'), b'__PAGEZERO')
        self.assertEqual(output[112:128].rstrip(b'\0'), b'__TEXT')
        self.assertEqual(struct.unpack_from('<I', output, 12)[0], 8)
        self.assertEqual(struct.unpack_from('<Q', output, 64)[0], 0)
        self.assertEqual(struct.unpack_from('<II', output, 16), (2, 144))

    def test_refuses_unknown_architecture_and_truncation(self):
        source = bytearray(executable())
        struct.pack_into('<I', source, 4, 0x1000007)
        for data in (bytes(source), executable()[:40], b'', executable()[:199]):
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                convert_image(data)

    def test_refuses_malformed_load_commands(self):
        source = bytearray(executable())
        struct.pack_into('<I', source, 36, 0)
        with self.assertRaises(ValueError):
            convert_image(source)


class ManifestTests(unittest.TestCase):
    def test_detects_missing_changed_and_escaping_resources(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'engine.bundle').write_bytes(b'test engine')
            (root / 'imeData.bundle').mkdir()
            (root / 'imeData.bundle/index.json').write_text('[]')
            files = {name: {'sha256': sha256(root / name)} for name in ('engine.bundle', 'imeData.bundle/index.json')}
            manifest = {'format': 1, 'source_sha256': SOURCE_SHA256, 'files': files}
            (root / 'manifest.json').write_text(json.dumps(manifest))
            verify(root)
            (root / 'engine.bundle').write_bytes(b'changed engine')
            with self.assertRaisesRegex(ValueError, 'changed'):
                verify(root)
            files['../outside'] = {'sha256': 'x'}
            files['engine.bundle']['sha256'] = sha256(root / 'engine.bundle')
            (root / 'manifest.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, 'unsafe'):
                verify(root)

    def test_rejects_framework_link_escaping_private_copy(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'engine.bundle').write_bytes(b'e')
            (root / 'imeData.bundle').mkdir()
            (root / 'imeData.bundle/index.json').write_text('[]')
            (root / 'Frameworks').symlink_to('/tmp', target_is_directory=True)
            manifest = {'format': 1, 'source_sha256': SOURCE_SHA256, 'files': {
                'engine.bundle': {'sha256': sha256(root / 'engine.bundle')},
                'imeData.bundle/index.json': {'sha256': sha256(root / 'imeData.bundle/index.json')},
                'Frameworks': {'symlink': '/tmp'}}}
            (root / 'manifest.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, 'escapes'):
                verify(root)


class DiscoveryTests(unittest.TestCase):
    def fixture(self, root):
        contents = root / 'Future.app/Contents'
        (contents / 'MacOS').mkdir(parents=True)
        (contents / 'MacOS/RenamedEngine').write_bytes(executable())
        (contents / 'Info.plist').write_bytes(plistlib.dumps({
            'CFBundleExecutable': 'RenamedEngine',
            'CFBundleShortVersionString': '99.2', 'CFBundleVersion': '9000'}))
        resources = contents / 'Resources/new-layout/models'
        resources.mkdir(parents=True)
        (resources / 'nested').mkdir()
        (resources / 'nested/pinyin.dat').write_bytes(b'dictionary')
        rows = [{'name': 'nested/pinyin.dat', 'type': 262144, 'version': 12}]
        (resources / 'index.json').write_text(json.dumps(rows))
        return contents.parent, resources, rows

    def test_future_version_renamed_binary_and_nested_dictionary(self):
        with tempfile.TemporaryDirectory() as temp:
            app, resources, _ = self.fixture(Path(temp))
            result = discover(app)
            self.assertEqual(result['app_version'], '99.2')
            self.assertEqual(result['source_resources'], str(resources.resolve()))
            self.assertEqual(result['compatibility'], 'unvalidated')
            self.assertEqual(result['dictionary_count'], 1)

    def test_ambiguous_indexes_require_explicit_choice(self):
        with tempfile.TemporaryDirectory() as temp:
            app, resources, _ = self.fixture(Path(temp))
            import shutil
            shutil.copytree(resources, resources.parent / 'other')
            with self.assertRaisesRegex(ValueError, '2 dictionary indexes'):
                discover(app)
            self.assertEqual(discover(app, resources)['dictionary_count'], 1)

    def test_rejects_bad_checksum_paths_and_loader_fields(self):
        with tempfile.TemporaryDirectory() as temp:
            _, resources, rows = self.fixture(Path(temp))
            for changes in ({'md5': '0' * 32}, {'name': '../outside'},
                            {'name': 'bad\tname'}, {'type': -1}, {'version': '12'}):
                (resources / 'index.json').write_text(json.dumps([dict(rows[0], **changes)]))
                with self.subTest(changes=changes), self.assertRaises(ValueError):
                    dictionary_rows(resources)

    def test_new_manifest_accepts_other_source_hash(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'engine.bundle').write_bytes(b'future')
            (root / 'imeData.bundle').mkdir()
            (root / 'imeData.bundle/index.json').write_text('[]')
            files = {name: {'sha256': sha256(root / name)} for name in
                     ('engine.bundle', 'imeData.bundle/index.json')}
            (root / 'manifest.json').write_text(json.dumps({
                'format': 2, 'source_sha256': 'a' * 64, 'files': files}))
            self.assertEqual(verify(root)['format'], 2)


if __name__ == '__main__':
    unittest.main()
