"""Installer checks that do not need a Docker daemon or a running deployment."""
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
from install_support import Installation, ensure_credentials, read_env, release_images
from install import parse_arguments


class InstallTests(unittest.TestCase):
    def test_native_cli_flags_are_forwarded_before_and_after_subcommand(self):
        for arguments in (['--json', 'runs', 'list'], ['capacity', '--json'], ['--help']):
            self.assertEqual(parse_arguments(['cli', *arguments]).arguments, arguments)

    def test_setup_preserves_existing_credentials_and_configuration(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / '.env'
            path.write_text('RUNYARD_OWNER_TOKEN=existing-owner-token\nRUNYARD_LOCAL_UI_PORT=3005')
            first = ensure_credentials(path)
            second = ensure_credentials(path)
            self.assertEqual(first, second)
            self.assertEqual(second['RUNYARD_OWNER_TOKEN'], 'existing-owner-token')
            self.assertEqual(second['RUNYARD_LOCAL_UI_PORT'], '3005')
            self.assertEqual(read_env(path), first)
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    def test_empty_existing_secret_requires_repair_not_rotation(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / '.env'
            path.write_text('RUNYARD_SIGNING_KEY=\n')
            with self.assertRaisesRegex(RuntimeError, 'Empty credential'):
                ensure_credentials(path)
            self.assertEqual(path.read_text(), 'RUNYARD_SIGNING_KEY=\n')

    def test_release_images_require_explicit_version(self):
        images = release_images('ghcr.io/owner/runyard/', 'v0.1.0')
        self.assertEqual(images['ui'], 'ghcr.io/owner/runyard/ui:v0.1.0')
        self.assertEqual(len(images), 6)
        for root, version in [('ghcr.io/owner/runyard', 'latest'), ('bad root', 'v0.1.0')]:
            with self.assertRaises(RuntimeError):
                release_images(root, version)

    def test_prebuilt_pull_failure_never_falls_back_to_source_build(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {
                'RUNYARD_INSTALL_STATE': tmp, 'RUNYARD_ENV_FILE': tmp + '/env'}):
            installation = Installation()
            calls = []
            def failed(*args, **kwargs):
                calls.append(args)
                return subprocess.CompletedProcess(args, 1, '', 'not found')
            with patch.object(installation, 'run', side_effect=failed):
                with self.assertRaisesRegex(RuntimeError, 'Cannot pull'):
                    installation.configure_images('ghcr.io/owner/runyard', 'v0.1.0')
            self.assertFalse(any('build' in call for call in calls))
            self.assertFalse(installation.settings_file.exists())

    def test_stopping_scopes_runners_and_preserves_volumes(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {
                'RUNYARD_INSTALL_STATE': tmp, 'RUNYARD_ENV_FILE': tmp + '/env',
                'RUNYARD_PROJECT_NAME': 'isolated-install'}):
            installation = Installation()
            with patch.object(installation, 'compose') as compose, patch.object(installation, 'run') as run:
                run.return_value = subprocess.CompletedProcess([], 0, 'owned-runner\n', '')
                installation.stop()
                self.assertIn('label=runyard.attempt', run.call_args_list[0].args)
                self.assertIn('network=isolated-install', run.call_args_list[0].args)
                self.assertEqual(run.call_args_list[1].args[-1], 'owned-runner')
                self.assertTrue(all(call.args[0] == 'stop' for call in compose.call_args_list))

    def test_docker_daemon_failure_is_actionable(self):
        with patch.object(Installation, 'run', return_value=subprocess.CompletedProcess([], 1, '', '')):
            with patch('shutil.which', return_value='/usr/bin/docker'):
                with self.assertRaisesRegex(RuntimeError, 'Start Docker Desktop'):
                    Installation().check_prerequisites()


if __name__ == '__main__':
    unittest.main()
