from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import subprocess
from wetype.integration import register_scheme, disable
from wetype.modes import SCHEMAS, SAVE_OPTIONS_PATCH


class IsolatedBackups(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root=patch('wetype.integration.ROOT',Path(temporary.name))
        root.start();self.addCleanup(root.stop)


class RegisterSchemeTests(IsolatedBackups):
    def test_preserves_other_schemes_comments_and_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'default.custom.yaml'
            old='patch:\n  # personal comment\n  schema_list:\n    - schema: rime_ice_gov\n    # keep this\n    - schema: vn\n  menu:\n    page_size: 8\n'
            path.write_text(old)
            backup=register_scheme(root)
            self.assertEqual(backup.read_text(),old)
            changed=path.read_text().replace(SAVE_OPTIONS_PATCH,'')
            for schema in SCHEMAS:changed=changed.replace('    - schema: '+schema+'\n','')
            self.assertEqual(changed,old)
            self.assertIsNone(register_scheme(root))
            for schema in SCHEMAS:self.assertEqual(path.read_text().splitlines().count('    - schema: '+schema),1)

    def test_upgrades_existing_wetype_without_duplicates(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'default.custom.yaml'
            path.write_text('patch:\n  "schema_list/+":\n    - schema: wetype\n')
            register_scheme(root)
            self.assertIsNone(register_scheme(root))
            for schema in SCHEMAS:self.assertEqual(path.read_text().splitlines().count('    - schema: '+schema),1)

    def test_unsupported_format_is_not_rewritten(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'default.custom.yaml';old='patch:\n  schema_list: [{schema: existing}]\n'
            path.write_text(old)
            with self.assertRaises(ValueError):register_scheme(root)
            self.assertEqual(path.read_text(),old)

    def test_empty_user_directory_gets_append_patch(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            self.assertIsNone(register_scheme(root))
            self.assertIn('"schema_list/+"',(root/'default.custom.yaml').read_text())

class DisableSchemeTests(IsolatedBackups):
    def test_removes_all_managed_schemes_but_preserves_user_named_scheme(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'default.custom.yaml'
            original='patch:\n  schema_list:\n    - schema: original\n    - schema: wetype_custom\n'
            path.write_text(original+''.join('    - schema: '+schema+'\n' for schema in SCHEMAS))
            with patch('wetype.integration.deploy'),patch('wetype.integration.subprocess.run'):
                disable(root,root/'runtime')
            self.assertEqual(path.read_text(),original)

    def test_removes_only_wetype_and_keeps_data(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'default.custom.yaml'
            before='patch:\n  schema_list:\n    - schema: original\n    # - schema: wetype\n    - schema: "wetype" # added\n  menu:\n    page_size: 8\n'
            path.write_text(before)
            data=root/'learning.bin';data.write_bytes(b'keep')
            with patch('wetype.integration.deploy') as deploy,patch('wetype.integration.subprocess.run') as run:
                backup=disable(root,root/'runtime')
            self.assertEqual(backup.read_text(),before)
            self.assertEqual(path.read_text(),before.replace('    - schema: "wetype" # added\n',''))
            self.assertEqual(data.read_bytes(),b'keep')
            deploy.assert_called_once_with(root.resolve(),root/'runtime',default_only=True)
            self.assertEqual(run.call_args.args[0][-2:],['--set-active-schema','original'])
            self.assertEqual(run.call_args.kwargs['cwd'],root.resolve())
            self.assertIsNone(disable(root,root/'runtime'))

    def test_refuses_to_remove_the_only_scheme(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'default.custom.yaml'
            before='patch:\n  schema_list:\n    - schema: wetype\n';path.write_text(before)
            with self.assertRaises(ValueError):disable(root,root/'runtime')
            self.assertEqual(path.read_text(),before)

    def test_failed_deploy_restores_configuration_and_user_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'default.custom.yaml';user=root/'user.yaml'
            before='patch:\n  schema_list:\n    - schema: original\n    - schema: wetype\n';path.write_text(before)
            old_user=b'var:\n  previously_selected_schema: wetype\n';user.write_bytes(old_user)
            failure=subprocess.CalledProcessError(1,['deploy'])
            with patch('wetype.integration.deploy',side_effect=[failure,None]) as deploy:
                with self.assertRaises(subprocess.CalledProcessError):disable(root,root/'runtime')
            self.assertEqual(path.read_text(),before)
            self.assertEqual(user.read_bytes(),old_user)
            self.assertEqual(deploy.call_count,2)

if __name__=='__main__':unittest.main()
