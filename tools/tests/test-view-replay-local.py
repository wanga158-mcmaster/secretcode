"""Local-only HTTP fixtures for the pinned replay viewer. No browser, no CLI, no network.

    python tools/tests/test-view-replay-local.py [-v]

Fixture assets live in a temporary directory under .cache/tools-tests/; the real-asset checks
read the CLI's cached visualiser (skipped when it is not cached).
"""
import contextlib
import http.client
import importlib.util
import io
import json
import shutil
import socket
from pathlib import Path
import tempfile
import threading
import unittest
from unittest import mock
import zipfile

PROJECT = Path(__file__).resolve().parents[2]
FIXTURE_REPLAY = PROJECT / 'tools/tests/fixtures/00collision-fundamentals-vs-starter.repl26a'
spec = importlib.util.spec_from_file_location('local_replay_viewer', PROJECT / 'tools/view-replay-local.py')
v = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)
CURRENT = v.PROFILES['cli-3.0.4']
REAL_ASSETS = v.ASSETS / CURRENT['cacheRelease'] / 'visualiser'
LATEST = v.PROFILES['cli-3.0.5']
LATEST_ASSETS = v.ASSETS / LATEST['cacheRelease'] / 'visualiser'
HEADER = (125779999602273).to_bytes(8, 'little')


def write_replay(path, engine=(1, 0, 2), format_version=(0, 0, 3), payload=b'opaque fixture'):
    with zipfile.ZipFile(path, 'w') as archive:
        archive.writestr('replay.repl26a', HEADER + bytes(engine + format_version) + payload)


class ViewerFixtures(unittest.TestCase):
    def setUp(self):
        scratch = PROJECT / '.cache/tools-tests'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='viewer-fixture-', dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name).resolve()
        self.assets = self.base / 'visualiser'
        (self.assets / 'assets').mkdir(parents=True)
        self.engine = 'v1_0_2-fixture.wasm'
        (self.assets / 'assets' / self.engine).write_bytes(b'\x00asm\x01\x00\x00\x00')
        (self.assets / 'index.html').write_text('<html>official fixture</html>', encoding='utf-8')
        (self.assets / 'assets/app.js').write_text('export const value=1;', encoding='utf-8')
        self.metadata = dict(schema=1, nodeEnv='production', minified=True, sourcemaps=False, files={
            p.relative_to(self.assets).as_posix(): dict(sha256=v.sha(p), bytes=p.stat().st_size)
            for p in self.assets.rglob('*') if p.is_file()})
        self.release = self.assets / '.browser-release.json'
        self.release.write_text(json.dumps(self.metadata), encoding='utf-8')
        self.profile = dict(CURRENT, engineFile=self.engine, decoderSha256=v.sha(self.assets / 'assets' / self.engine),
                            browserReleaseSha256=v.sha(self.release))
        self.allowed = v.validate_assets(self.assets, self.engine, self.profile)[1]
        self.replay = self.base / 'fixture.repl26a'
        write_replay(self.replay)

    def start(self, assets=None, allowed=None, replay=None):
        server = v.make_server(assets or self.assets, allowed or self.allowed, replay or self.replay, 0)
        self.assertEqual(server.server_address[0], '127.0.0.1')
        self.assertTrue(server.daemon_threads)
        thread = threading.Thread(target=lambda: server.serve_forever(poll_interval=0.01), daemon=True)
        thread.start()

        def close():
            server.shutdown(); server.server_close(); thread.join(timeout=2)
            self.assertFalse(thread.is_alive())
        self.addCleanup(close)
        return server.server_port

    def request(self, port, path, method='GET', headers=None):
        connection = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
        try:
            connection.request(method, path, headers=headers or {})
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def test_wasm_javascript_and_selected_replay_mime_and_length(self):
        port = self.start()
        for path, mime, p in [('/assets/' + self.engine, 'application/wasm', self.assets / 'assets' / self.engine),
                              ('/assets/app.js', 'text/javascript', self.assets / 'assets/app.js'),
                              ('/replay.zip', 'application/zip', self.replay)]:
            status, headers, body = self.request(port, path)
            self.assertEqual(status, 200); self.assertEqual(headers['Content-Type'], mime)
            self.assertEqual(int(headers['Content-Length']), len(body)); self.assertEqual(body, p.read_bytes())
            self.assertEqual(headers['Cache-Control'], 'no-store')
        status, headers, body = self.request(port, '/replay.zip', method='HEAD')
        self.assertEqual(status, 200); self.assertEqual(body, b'')
        self.assertEqual(int(headers['Content-Length']), self.replay.stat().st_size)
        self.assertEqual(self.request(port, '/?replay=replay.zip&cambc-cli=1')[0], 200)

    def test_latest_engine_header_selects_its_unique_profile(self):
        write_replay(self.replay, engine=(1, 0, 3))
        self.assertEqual(v.profile_for_replay(self.replay)['id'], LATEST['id'])
        self.assertEqual(v.validate_replay(self.replay, LATEST),
                         {'engineVersion': '1.0.3', 'formatVersion': '0.0.3'})
        with self.assertRaisesRegex(ValueError, 'differs from selected profile'):
            v.validate_replay(self.replay, CURRENT)

    @unittest.skipUnless((LATEST_ASSETS / '.browser-release.json').is_file(),
                         'official 3.0.5 visualiser not cached')
    def test_latest_official_visualiser_assets_match_verified_profile(self):
        assets, allowed = v.assets_from_project(LATEST)
        self.assertIn('assets/' + LATEST['engineFile'], allowed)
        self.assertEqual(v.sha(assets / 'assets' / LATEST['engineFile']), LATEST['decoderSha256'])

    def test_traversal_absolute_target_and_nonloopback_host_rejected(self):
        port = self.start()
        (self.base / 'secret.txt').write_text('PRIVATE OUTSIDE ASSETS', encoding='utf-8')
        for path in ['/../secret.txt', '/%2e%2e/secret.txt', '/assets/%2e%2e/%2e%2e/secret.txt',
                     '/assets%5c..%5csecret.txt', '/C:/secret.txt', 'http://external.invalid/index.html']:
            status, headers, body = self.request(port, path)
            self.assertEqual(status, 403, path); self.assertNotIn(b'PRIVATE OUTSIDE', body)
        self.assertEqual(self.request(port, '/', headers={'Host': 'external.invalid'})[0], 403)
        self.assertEqual(self.request(port, '/assets/')[0], 404)
        self.assertEqual(self.request(port, '/.browser-release.json')[0], 404)

    def test_idle_browser_preconnect_does_not_block_actual_asset_request(self):
        accepted = threading.Event()
        original_setup = v.ReplayHandler.setup

        def observe_setup(handler):
            original_setup(handler)
            accepted.set()
        with mock.patch.object(v.ReplayHandler, 'setup', new=observe_setup):
            port = self.start()
            idle = socket.create_connection(('127.0.0.1', port), timeout=3)
            try:
                # Prove the idle socket was accepted first. A single-threaded server would now
                # block reading its request line and the GET would time out.
                self.assertTrue(accepted.wait(timeout=2))
                status, headers, body = self.request(port, '/assets/' + self.engine)
                self.assertEqual(status, 200)
                self.assertEqual(body, (self.assets / 'assets' / self.engine).read_bytes())
                self.assertNotEqual(idle.fileno(), -1)
            finally:
                idle.close()

    def test_modified_asset_wrong_version_and_changed_manifest_rejected(self):
        with self.assertRaisesRegex(ValueError, 'version'):
            v.validate_assets(self.assets, 'v1_0_1-fixture.wasm', self.profile)
        (self.assets / 'assets' / self.engine).write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'asset differs'):
            v.validate_assets(self.assets, self.engine, self.profile)
        self.release.write_text('{}', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'manifest hash differs'):
            v.validate_assets(self.assets, self.engine, self.profile)
        with self.assertRaisesRegex(ValueError, 'not cached'):
            v.validate_assets(self.base / 'missing', self.engine, self.profile)

    def test_release_manifest_cannot_authorize_path_outside_assets(self):
        outside = self.base / 'secret.txt'; outside.write_text('private', encoding='utf-8')
        self.metadata['files']['../secret.txt'] = dict(bytes=outside.stat().st_size, sha256=v.sha(outside))
        self.release.write_text(json.dumps(self.metadata), encoding='utf-8')
        profile = dict(self.profile, browserReleaseSha256=v.sha(self.release))
        with self.assertRaisesRegex(ValueError, 'Invalid asset path'):
            v.validate_assets(self.assets, self.engine, profile)

    def test_redirected_manifest_parent_uses_same_directory_identity(self):
        # Windows package redirection can return a logical directory path and
        # physical file paths even though both directories are the same object.
        physical = self.base / 'physical-visualiser'
        shutil.copytree(self.assets, physical)
        original_resolve, original_samefile = Path.resolve, Path.samefile

        def resolve(path, *args, **kwargs):
            if path != self.assets and path.is_relative_to(self.assets):
                path = physical / path.relative_to(self.assets)
            return original_resolve(path, *args, **kwargs)

        def samefile(path, other):
            if path == physical and other == self.assets:
                return True
            return original_samefile(path, other)

        with mock.patch.object(Path, 'resolve', new=resolve), \
             mock.patch.object(Path, 'samefile', new=samefile):
            assets, allowed = v.validate_assets(self.assets, self.engine, self.profile)
            self.assertEqual(assets, physical)
            self.assertIn('assets/' + self.engine, allowed)
            port = self.start(assets, allowed)
            self.assertEqual(self.request(port, '/assets/' + self.engine)[0], 200)
            self.assertEqual(self.request(port, '/%2e%2e/secret.txt')[0], 403)

    def test_redirected_manifest_cannot_select_another_directory(self):
        physical = self.base / 'another-visualiser'
        shutil.copytree(self.assets, physical)
        original_resolve = Path.resolve

        def resolve(path, *args, **kwargs):
            if path == self.release:
                path = physical / '.browser-release.json'
            return original_resolve(path, *args, **kwargs)

        # Even an identical pinned manifest cannot change the trusted directory.
        with mock.patch.object(Path, 'resolve', new=resolve):
            with self.assertRaisesRegex(ValueError, 'manifest escapes visualiser root'):
                v.validate_assets(self.assets, self.engine, self.profile)

    def test_each_profile_checks_its_own_release_and_engine(self):
        profile = dict(v.PROFILES['cli-3.0.2'])
        engine = 'v1_0_1-fixture.wasm'
        engine_path = self.assets / 'assets' / engine
        engine_path.write_bytes(b'\x00asm\x01\x00\x00\x00')
        self.metadata['files']['assets/' + engine] = dict(bytes=engine_path.stat().st_size, sha256=v.sha(engine_path))
        self.release.write_text(json.dumps(self.metadata), encoding='utf-8')
        profile.update(engineFile=engine, decoderSha256=v.sha(engine_path), browserReleaseSha256=v.sha(self.release))
        self.assertIn('assets/' + engine, v.validate_assets(self.assets, engine, profile)[1])
        with self.assertRaisesRegex(ValueError, 'version'):
            v.validate_assets(self.assets, self.engine, profile)
        profile['browserReleaseSha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'manifest hash differs'):
            v.validate_assets(self.assets, engine, profile)

    def test_replay_header_profiles_reject_unknown_formats_and_wrong_engines(self):
        self.assertEqual(sorted(v.PROFILES), ['cli-3.0.2', 'cli-3.0.4', 'cli-3.0.5'])
        self.assertEqual(v.validate_replay(self.replay)['engineVersion'], '1.0.2')
        self.assertEqual(v.profile_for_replay(self.replay)['id'], 'cli-3.0.4')
        with self.assertRaisesRegex(ValueError, 'differs from selected profile'):
            v.validate_replay(self.replay, v.PROFILES['cli-3.0.2'])
        write_replay(self.replay, (1, 0, 1))
        self.assertEqual(v.validate_replay(self.replay, v.PROFILES['cli-3.0.2'])['engineVersion'], '1.0.1')
        self.assertEqual(v.profile_for_replay(self.replay)['id'], 'cli-3.0.2')
        for engine, fmt in [((0, 0, 3), (0, 0, 3)), ((1, 0, 1), (9, 9, 9)), ((9, 9, 9), (0, 0, 3))]:
            write_replay(self.replay, engine, fmt)  # 0.0.3 was CLI 2.2.5, whose assets are gone
            with self.assertRaisesRegex(ValueError, 'engine/format'):
                v.validate_replay(self.replay)
        with zipfile.ZipFile(self.replay, 'w') as archive:
            archive.writestr('replay.repl26a', b'unknown header')
        with self.assertRaisesRegex(ValueError, 'Unknown or truncated'):
            v.validate_replay(self.replay)
        self.replay.write_bytes(b'not a zip')
        with self.assertRaisesRegex(ValueError, 'retained .repl26a'):
            v.validate_replay(self.replay)

    def test_cli_exact_arguments_print_url_and_never_autoopen(self):
        server = mock.Mock(server_port=18765)
        server.serve_forever.side_effect = KeyboardInterrupt
        text = io.StringIO()
        with mock.patch.object(v, 'assets_from_project', return_value=(self.assets, self.allowed)), \
             mock.patch.object(v, 'make_server', return_value=server) as factory, \
             mock.patch('webbrowser.open') as browser, mock.patch('subprocess.Popen') as process, \
             contextlib.redirect_stdout(text):
            v.main(['--replay', str(self.replay), '--port', '18765'])
        self.assertEqual(text.getvalue().strip(), 'http://127.0.0.1:18765/?replay=replay.zip&cambc-cli=1')
        factory.assert_called_once_with(self.assets, self.allowed, self.replay, 18765)
        server.server_close.assert_called_once(); browser.assert_not_called(); process.assert_not_called()

    def test_cli_auto_selects_the_header_profile_and_a_free_port(self):
        server = mock.Mock(server_port=50123)
        server.serve_forever.side_effect = KeyboardInterrupt
        with mock.patch.object(v, 'assets_from_project', return_value=(self.assets, self.allowed)) as assets, \
             mock.patch.object(v, 'make_server', return_value=server) as factory, \
             mock.patch('webbrowser.open') as browser, mock.patch('subprocess.Popen') as process, \
             contextlib.redirect_stdout(io.StringIO()):
            v.main(['--replay', str(self.replay)])
        assets.assert_called_once_with(CURRENT)
        factory.assert_called_once_with(self.assets, self.allowed, self.replay, 0)
        server.server_close.assert_called_once()
        browser.assert_not_called(); process.assert_not_called()
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            v.main(['--replay', str(self.replay), '--profile', 'cli-2.2.5'])

    @unittest.skipUnless((REAL_ASSETS / '.browser-release.json').is_file() and FIXTURE_REPLAY.is_file(),
                         'official 3.0.4 visualiser not cached')
    def test_real_cached_visualiser_serves_the_fixture_replay(self):
        profile = v.profile_for_replay(FIXTURE_REPLAY)
        self.assertEqual(profile['id'], 'cli-3.0.4')
        assets, allowed = v.assets_from_project(profile)
        self.assertIn('assets/' + profile['engineFile'], allowed)
        port = self.start(assets, allowed, FIXTURE_REPLAY)
        status, headers, body = self.request(port, '/')
        self.assertEqual((status, headers['Content-Type']), (200, 'text/html; charset=utf-8'))
        status, headers, body = self.request(port, '/assets/' + profile['engineFile'])
        self.assertEqual((status, v.hashlib.sha256(body).hexdigest()), (200, profile['decoderSha256']))
        self.assertEqual(self.request(port, '/replay.zip')[2], FIXTURE_REPLAY.read_bytes())


if __name__ == '__main__':
    unittest.main()
