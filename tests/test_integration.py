from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import subprocess
from wetype.integration import register_scheme, disable, sync_configs, migrate_runtime, runtime_directory
from wetype.modes import SCHEMAS, SAVE_OPTIONS_PATCH


class IsolatedBackups(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root=patch('wetype.integration.ROOT',Path(temporary.name))
        root.start();self.addCleanup(root.stop)


class ConfigSyncTests(IsolatedBackups):
    def test_copies_editable_files_and_preserves_runtime_and_personal_patches(self):
        from wetype.integration import ROOT
        source = ROOT / 'data'
        source.mkdir()
        for schema in SCHEMAS:
            (source / (schema + '.schema.yaml')).write_text('menu:\n  page_size: 6\n')
        (source / 'my.dict.yaml').write_text('name: my\nversion: "1"\n')
        (source / 'my.txt').write_text('短语\tcode\t100\n')
        (source / 'lua').mkdir()
        (source / 'wetype_common.yaml').write_text('base: {}\nsettings: {}\n')
        (source / 'lua/wetype_bootstrap.lua').write_text('return {}\n')
        (source / 'lua/wetype_candidate_rules.lua').write_text('return {}\n')
        (source / 'lua/custom.lua').write_text('return {}\n')
        target = ROOT / 'installed'
        target.mkdir()
        (target / 'wetype.schema.yaml').write_text('old configuration\n')
        (target / 'wetype.runtime.yaml').write_text('private runtime paths\n')
        (target / 'wetype.custom.yaml').write_text('personal patch\n')
        backup = sync_configs(target)
        self.assertEqual((backup / 'wetype.schema.yaml').read_text(), 'old configuration\n')
        self.assertEqual((target / 'wetype.runtime.yaml').read_text(), 'private runtime paths\n')
        self.assertEqual((target / 'wetype.custom.yaml').read_text(), 'personal patch\n')
        for path in source.rglob('*'):
            if path.is_file():
                self.assertEqual((target / path.relative_to(source)).read_bytes(), path.read_bytes())
        self.assertIsNone(sync_configs(target))
        (target / 'my.dict.yaml').write_text('personal vocabulary\n')
        (target / 'my.txt').write_text('personal phrases\n')
        (source / 'wetype.custom.yaml').write_text('checkout patch\n')
        self.assertIsNone(sync_configs(target))
        self.assertEqual((target / 'my.dict.yaml').read_text(), 'personal vocabulary\n')
        self.assertEqual((target / 'my.txt').read_text(), 'personal phrases\n')
        self.assertEqual((target / 'wetype.custom.yaml').read_text(), 'personal patch\n')

    def test_missing_schema_does_not_partially_update_destination(self):
        from wetype.integration import ROOT
        (ROOT / 'data').mkdir()
        target = ROOT / 'installed'
        with self.assertRaisesRegex(ValueError, 'missing scheme'):
            sync_configs(target)
        self.assertFalse(target.exists())


class RegisterSchemeTests(IsolatedBackups):
    def test_preserves_other_schemes_comments_and_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'default.custom.yaml'
            old='patch:\n  # personal comment\n  schema_list:\n    - schema: rime_ice_gov\n    # keep this\n    - schema: vn\n  menu:\n    page_size: 8\n'
            path.write_text(old)
            backup=register_scheme(root, SCHEMAS)
            self.assertEqual(backup.read_text(),old)
            changed=path.read_text().replace(SAVE_OPTIONS_PATCH,'')
            for schema in SCHEMAS:changed=changed.replace('    - schema: '+schema+'\n','')
            self.assertEqual(changed,old)
            self.assertIsNone(register_scheme(root, SCHEMAS))
            for schema in SCHEMAS:self.assertEqual(path.read_text().splitlines().count('    - schema: '+schema),1)

    def test_upgrades_existing_wetype_without_duplicates(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'default.custom.yaml'
            path.write_text('patch:\n  "schema_list/+":\n    - schema: wetype\n')
            register_scheme(root, SCHEMAS)
            self.assertIsNone(register_scheme(root, SCHEMAS))
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
            self.assertEqual((root/'default.custom.yaml').read_text().count('    - schema:'), 1)

    def test_default_registration_keeps_existing_standalone_choices(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'default.custom.yaml').write_text('patch:\n  schema_list:\n    - schema: wetype_flypy\n')
            register_scheme(root)
            text=(root/'default.custom.yaml').read_text()
            self.assertIn('schema: wetype_flypy\n',text)
            self.assertIn('schema: wetype\n',text)
            self.assertNotIn('schema: wetype_wubi86',text)


class RuntimeMigrationTests(unittest.TestCase):
    def test_directory_follows_the_selected_rime_profile(self):
        self.assertEqual(runtime_directory(Path('/other/Rime')),Path('/other/Rime/wetype'))
        self.assertEqual(runtime_directory(Path('/other/Rime'),Path('/engine')),Path('/engine'))

    def test_moves_learning_without_copying_and_retains_legacy_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);rime=root/'Rime';legacy=root/'old';runtime=rime/'wetype'
            legacy.mkdir();(legacy/'learning').write_bytes(b'personal learning')
            inode=(legacy/'learning').stat().st_ino
            with patch('wetype.integration.DEFAULT_RIME',rime),patch('wetype.integration.LEGACY_RUNTIME',legacy),patch('wetype.integration.verify'):
                self.assertTrue(migrate_runtime(rime,runtime))
                self.assertTrue(legacy.is_symlink())
                self.assertEqual((runtime/'learning').stat().st_ino,inode)
                self.assertEqual((legacy/'learning').read_bytes(),b'personal learning')
                self.assertFalse(migrate_runtime(rime,runtime))

    def test_existing_destination_and_other_profiles_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);rime=root/'Rime';legacy=root/'old';legacy.mkdir()
            with patch('wetype.integration.DEFAULT_RIME',rime),patch('wetype.integration.LEGACY_RUNTIME',legacy):
                self.assertFalse(migrate_runtime(root/'other',root/'other/wetype'))
                (rime/'wetype').mkdir(parents=True)
                self.assertFalse(migrate_runtime(rime,rime/'wetype'))
            self.assertFalse(legacy.is_symlink())

class DisableSchemeTests(IsolatedBackups):
    def test_removes_all_managed_schemes_but_preserves_user_named_scheme(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/'default.custom.yaml'
            original='patch:\n  schema_list:\n    - schema: original\n    - schema: wetype_custom\n'
            path.write_text(original+''.join('    - schema: '+schema+'\n' for schema in SCHEMAS)+'    - schema: wetype_flypy_gov\n')
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
