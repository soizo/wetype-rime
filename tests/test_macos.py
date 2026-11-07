import json
from pathlib import Path
import struct
import tempfile
import unittest
from wetype.macos import SOURCE_SHA256, convert_image, sha256, verify


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


if __name__ == '__main__':
    unittest.main()
