"""Cold build boundaries and process evidence. No compiler, game or real shared slot."""
from contextlib import ExitStack
import copy
import hashlib
import json
from pathlib import Path
import shutil
import sys
from types import SimpleNamespace
import unittest
import uuid
from unittest.mock import MagicMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import core
import compiled_runner as native
HERE = core.ROOT / '.cache/cold-build-tests'


class ColdBuilds(unittest.TestCase):
    def setUp(self):
        self.root = HERE / 'cases' / uuid.uuid4().hex
        self.root.mkdir(parents=True)
        def cleanup():
            self.assertEqual(self.root.resolve(), self.root)
            self.assertTrue(self.root.is_relative_to(HERE / 'cases'))
            shutil.rmtree(self.root)
        self.addCleanup(cleanup)
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        for name, value in (('ROOT', self.root), ('BOTS', self.root / 'bots'), ('CACHE', self.root / '.cache/arena'),
                            ('CAMBC', self.root / 'cli.exe')):
            self.stack.enter_context(patch.object(core, name, value))
        self.stack.enter_context(patch.object(native, 'ROOT', self.root))
        self.stack.enter_context(patch.object(native, '_PENDING_CLEANUP', []))
        (self.root / 'sdk').mkdir()
        self.bot = core.Bot('main', 'main')
        self.contents = {self.root / 'bots/main/main/src/main.cpp': b'int main(){return 0;}\n'}
        self.key = f'main/main@{core.version_of(self.contents)}'
        self.roots = tuple(self.root / '.cache/main' / name for name in ('cold-one', 'cold-two'))
        self.events = []
        self.runtime = {'host': {'LOCALAPPDATA': str(self.root / 'host/local'), 'USERPROFILE': str(self.root / 'host/profile'),
                                 'SystemRoot': str(self.root / 'host/windows')},
                        'files': {name: {'path': str(self.root / 'host' / name / filename)} for name, filename in
                                  (('compiler', 'clang++.exe'), ('cmake', 'cmake.exe'), ('ninja', 'ninja.exe'),
                                   ('toolchain', 'wasi-sdk-p1.cmake'))}, 'pin': 'stable-runtime'}
        self.runtime_mock = self.stack.enter_context(patch.object(core, 'cold_build_runtime', side_effect=lambda: copy.deepcopy(self.runtime)))
        self.stack.enter_context(patch.object(core, 'prepare', side_effect=AssertionError('no prepare/cache reuse')))
        self.stack.enter_context(patch.object(core, 'read_closure', side_effect=AssertionError('no current source reads')))
        events = self.events
        class Slots:
            def __init__(self, want, **kwargs):
                self.count = 0
            def __enter__(self):
                events.append('slots-enter')
                self.count = 2
                return self
            def __exit__(self, *args):
                events.append('slots-release')
                self.count = 0
        class Lock:
            def __init__(self, path):
                self.path = path
            def __enter__(self):
                events.append('lock-enter')
                return self
            def __exit__(self, *args):
                events.append('lock-release')
        self.stack.enter_context(patch.object(core, 'Slots', Slots))
        self.stack.enter_context(patch.object(core, 'FileLock', Lock))
        self.command = self.stack.enter_context(patch.object(native, '_command', side_effect=self.fake_command))

    def write(self, path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value), encoding='utf-8')

    def native_descriptor(self, path):
        desc = core._cold_file(path)
        desc['path'] = path.relative_to(self.root).as_posix()
        return desc

    def fake_command(self, argv, cwd, directory, timeout, *, env):
        self.events.append('command')
        self.assertEqual(self.events[:2], ['slots-enter', 'lock-enter'])
        self.assertEqual(argv[0], str(core.CAMBC))
        self.assertEqual(argv[-1], '--release')
        target = Path(argv[2])
        build = target / '.cambc/build-release'
        build.mkdir(parents=True)
        cache = {'BOT_DIR': str(target), 'BC_SDK': str(self.root / 'sdk'), 'CMAKE_HOME_DIRECTORY': str(self.root / 'cmake'),
                 'CMAKE_COMMAND': self.runtime['files']['cmake']['path'], 'CMAKE_MAKE_PROGRAM': self.runtime['files']['ninja']['path'],
                 'CMAKE_TOOLCHAIN_FILE': self.runtime['files']['toolchain']['path'], 'CMAKE_BUILD_TYPE': 'Release'}
        (build / 'CMakeCache.txt').write_text('\n'.join(f'{key}:STRING={value}' for key, value in cache.items()), encoding='utf-8')
        compiler = build / 'CMakeFiles/3.31.8/CMakeCXXCompiler.cmake'
        compiler.parent.mkdir(parents=True)
        compiler.write_text(f'set(CMAKE_CXX_COMPILER "{self.runtime["files"]["compiler"]["path"]}")\n', encoding='utf-8')
        for name in ('compile_commands.json', 'build.ninja', 'CMakeFiles/rules.ninja'):
            (build / name).write_bytes(b'fixed generated input')
        for name, suffix in (('bot.wasm', b'RAW'), ('bot-injected.wasm', b'SDK')):
            (build / name).write_bytes(b'\0asm\x01\0\0\0' + suffix)
        for name in ('stdout.bin', 'stderr.bin'):
            (directory / name).write_bytes(b'build fixture\n')
        self.write(directory / 'dispatch.json', {'argv': argv, 'cwd': str(cwd), 'timeout': timeout, 'environment': env})
        record = {'pid': 19, 'returncode': 0, 'timed_out': False, 'tree_exit_confirmed': True, 'forced_termination': False,
                  'containment': {'kind': 'windows-private-job', 'empty_confirmed': True, 'assigned_before_resume': True,
                                  'resumed': True, 'kill_on_close': True, 'forced_termination': False},
                  'stdout': self.native_descriptor(directory / 'stdout.bin'), 'stderr': self.native_descriptor(directory / 'stderr.bin')}
        self.write(directory / 'process-end.json', record)
        return record

    def build(self, **kwargs):
        return core.cold_build_pair(self.bot, self.contents, owner='main', roots=self.roots, expected_key=self.key, **kwargs)

    def validate(self, pair):
        return core.validate_cold_build_pair(pair, self.bot, self.contents, owner='main', expected_key=self.key)

    def test_two_distinct_original_identity_builds_and_independent_validation(self):
        pair = self.build()
        self.assertEqual(self.command.call_count, 2)
        self.assertEqual(self.events, ['slots-enter', 'lock-enter', 'command', 'command', 'lock-release', 'slots-release'])
        self.assertEqual(pair['key'], self.key)
        for root, build in zip(self.roots, pair['builds']):
            self.assertEqual(Path(build['target']), root / self.key.rsplit('@', 1)[1] / 'bots/main/main')
            self.assertEqual(build['environment']['TEMP'], str(root / 'tmp'))
            self.assertNotEqual(build['raw']['sha256'], build['sdk']['sha256'])
        self.assertIs(self.validate(pair), pair)
        self.assertFalse((core.CACHE / 'snap').exists())
        self.assertFalse((self.root / 'bots').exists())

    def test_reuse_wrong_key_or_nonowner_roots_fail_before_admission(self):
        self.roots[0].mkdir(parents=True)
        with self.assertRaisesRegex(core.ArenaError, 'existing roots'):
            self.build()
        self.assertEqual(self.events, [])
        with self.assertRaisesRegex(core.ArenaError, 'source identity'):
            core.cold_build_pair(self.bot, self.contents, owner='main', roots=self.roots, expected_key='main/main@000000000000')
        with self.assertRaisesRegex(core.ArenaError, 'two distinct'):
            core.cold_build_pair(self.bot, self.contents, owner='main', roots=[self.roots[1]] * 2, expected_key=self.key)
        with self.assertRaises(core.ArenaError):
            core.cold_build_pair(self.bot, self.contents, owner='main', roots=[self.roots[1], self.root / '.cache/other/fresh'], expected_key=self.key)
        self.command.assert_not_called()

    def test_exact_qualification_subtree_is_admitted_but_generic_nesting_is_not(self):
        self.roots = tuple(self.root / '.cache/main/qualification-tooling' / name for name in ('cold-a', 'cold-b'))
        self.validate(self.build())
        for parent in ('snap', 'exp', 'arbitrary/nested'):
            roots = tuple(self.root / '.cache/main' / parent / name for name in ('cold-x', 'cold-y'))
            with self.assertRaisesRegex(core.ArenaError, 'owner child'):
                core.cold_build_pair(self.bot, self.contents, owner='main', roots=roots, expected_key=self.key)

    def test_runtime_compiler_alias_is_resolved_but_output_root_redirect_is_rejected(self):
        import _winapi
        compiler = Path(self.runtime['files']['compiler']['path'])
        compiler.parent.mkdir(parents=True)
        compiler.write_bytes(b'compiler fixture')
        alias = compiler.parent.with_name('compiler-alias')
        _winapi.CreateJunction(str(compiler.parent), str(alias))
        def run(*args, **kwargs):
            record = self.fake_command(*args, **kwargs)
            selected = Path(args[0][2]) / '.cambc/build-release/CMakeFiles/3.31.8/CMakeCXXCompiler.cmake'
            selected.write_text(f'set(CMAKE_CXX_COMPILER "{alias / compiler.name}")\n', encoding='utf-8')
            return record
        self.command.side_effect = run
        self.validate(self.build())
        redirected = self.root / '.cache/main/cold-redirect'
        _winapi.CreateJunction(str(self.roots[0]), str(redirected))
        with self.assertRaisesRegex(core.ArenaError, 'unredirected'):
            core.cold_build_pair(self.bot, self.contents, owner='main', roots=[redirected, self.root / '.cache/main/unused'], expected_key=self.key)

    def test_no_second_build_after_runtime_source_or_compiler_drift(self):
        for drift in ('runtime', 'source', 'compiler'):
            with self.subTest(drift=drift):
                self.roots = tuple(self.root / '.cache/main' / f'{drift}-{n}' for n in (1, 2))
                self.events.clear()
                self.command.reset_mock()
                def run(*args, **kwargs):
                    record = self.fake_command(*args, **kwargs)
                    if drift == 'runtime':
                        self.runtime['pin'] = 'changed-runtime'
                    elif drift == 'source':
                        (Path(args[0][2]) / 'src/main.cpp').write_bytes(b'changed')
                    else:
                        compiler = Path(args[0][2]) / '.cambc/build-release/CMakeFiles/3.31.8/CMakeCXXCompiler.cmake'
                        compiler.write_text('set(CMAKE_CXX_COMPILER "unexpected.exe")\n', encoding='utf-8')
                    return record
                self.command.side_effect = run
                with self.assertRaises(core.ArenaError):
                    self.build()
                self.assertEqual(self.command.call_count, 1)
                self.assertEqual(self.events[-2:], ['lock-release', 'slots-release'])
                self.runtime['pin'] = 'stable-runtime'

    def test_validator_rejects_changed_roles_dispatch_logs_runtime_and_source(self):
        pair = self.build()
        cases = [(Path(pair['builds'][0]['raw']['path']), b'\0asm\x01\0\0\0OTHER'),
                 (self.roots[0] / 'process/dispatch.json', b'{}'),
                 (self.roots[0] / 'process/stdout.bin', b'changed logs'),
                 (Path(pair['builds'][0]['target']) / 'src/main.cpp', b'changed source')]
        for path, changed in cases:
            with self.subTest(path=path.name):
                previous = path.read_bytes()
                path.write_bytes(changed)
                with self.assertRaises(core.ArenaError):
                    self.validate(pair)
                path.write_bytes(previous)
        self.runtime['pin'] = 'runtime drift'
        with self.assertRaisesRegex(core.ArenaError, 'runtime'):
            self.validate(pair)

    def test_first_build_artifact_drift_during_second_build_blocks_pair(self):
        calls = []
        def run(*args, **kwargs):
            record = self.fake_command(*args, **kwargs)
            calls.append(Path(args[0][2]))
            if len(calls) == 2:
                (calls[0] / '.cambc/build-release/bot.wasm').write_bytes(b'\0asm\x01\0\0\0CHANGED')
            return record
        self.command.side_effect = run
        with self.assertRaisesRegex(core.ArenaError, 'earlier artifacts'):
            self.build()
        self.assertEqual(self.command.call_count, 2)

    def test_timeout_nonzero_or_forced_exit_never_produces_pair(self):
        for changed in ({'timed_out': True}, {'returncode': 1}, {'forced_termination': True}):
            with self.subTest(changed=changed):
                self.roots = tuple(self.root / '.cache/main' / f'failed-{len(self.events)}-{n}' for n in (1, 2))
                self.command.reset_mock()
                def run(*args, **kwargs):
                    record = self.fake_command(*args, **kwargs)
                    record.update(changed)
                    self.write(args[2] / 'process-end.json', record)
                    return record
                self.command.side_effect = run
                with self.assertRaisesRegex(core.ArenaError, 'finish cleanly'):
                    self.build()
                self.assertEqual(self.command.call_count, 1)

    def test_unconfirmed_tree_retains_actual_admission_and_locks_until_cleanup(self):
        events = self.events
        class Process:
            empty_confirmed = False
            assigned = True
            def terminate_tree(self):
                events.append('tree-empty')
                self.empty_confirmed = True
            def close(self):
                events.append('process-close')
            def record(self):
                return {'empty_confirmed': self.empty_confirmed}
        error = KeyboardInterrupt('cancel')
        error.compiled_process = Process()
        self.command.side_effect = error
        with self.assertRaises(KeyboardInterrupt):
            self.build()
        self.assertEqual(self.events, ['slots-enter', 'lock-enter'])
        self.assertEqual(len(native._PENDING_CLEANUP), 1)
        self.assertTrue(native.confirm_pending_cleanup())
        self.assertEqual(self.events[-4:], ['tree-empty', 'process-close', 'lock-release', 'slots-release'])

    def test_confirmed_cancellation_releases_resources_without_second_dispatch(self):
        error = KeyboardInterrupt('cancel')
        error.compiled_process = SimpleNamespace(empty_confirmed=True)
        self.command.side_effect = error
        with self.assertRaises(KeyboardInterrupt):
            self.build()
        self.assertEqual(self.command.call_count, 1)
        self.assertEqual(self.events, ['slots-enter', 'lock-enter', 'lock-release', 'slots-release'])
        self.assertEqual(native._PENDING_CLEANUP, [])

    def test_source_escape_and_mutable_bytes_fail_before_admission(self):
        bad = {self.root / 'bots/main/main/src/main.cpp': bytearray(b'mutable')}
        with self.assertRaisesRegex(core.ArenaError, 'immutable'):
            core.cold_build_pair(self.bot, bad, owner='main', roots=self.roots, expected_key=self.key)
        bad = {self.root / 'bots/main/main/src/main.cpp': b'#include "../../other/private.h"\n'}
        key = 'main/main@' + core.version_of(bad)
        with self.assertRaisesRegex(core.ArenaError, 'outside'):
            core.cold_build_pair(self.bot, bad, owner='main', roots=self.roots, expected_key=key)
        self.assertEqual(self.events, [])

    def test_runtime_tree_pins_detect_bytes_and_inventory_changes_and_reject_aliases(self):
        runtime = self.root / 'small-runtime'
        runtime.mkdir()
        (runtime / 'one').write_bytes(b'first')
        first = core._cold_tree(runtime)
        (runtime / 'one').write_bytes(b'second')
        second = core._cold_tree(runtime)
        self.assertNotEqual(first['sha256'], second['sha256'])
        (runtime / 'two').write_bytes(b'second')
        third = core._cold_tree(runtime)
        self.assertNotEqual(second['sha256'], third['sha256'])
        self.assertEqual(third['files'], 2)
        import _winapi
        alias = self.root / 'small-runtime-alias'
        _winapi.CreateJunction(str(runtime), str(alias))
        with self.assertRaisesRegex(core.ArenaError, 'redirected'):
            core._cold_tree(alias)

    def test_explicit_environment_never_copies_ambient_settings(self):
        class NamedOnly:
            def __iter__(self):
                raise AssertionError('must not enumerate the ambient environment')
            def get(self, name, default=None):
                raise AssertionError('cold build environment must use already pinned named values')
        with patch.object(core.os, 'environ', NamedOnly()):
            actual = core._cold_environment(self.runtime, self.roots[0], 2)
        self.assertEqual(set(actual), {'LOCALAPPDATA', 'USERPROFILE', 'SystemRoot', 'WINDIR', 'PATH', 'TEMP', 'TMP',
                                       'CMAKE_BUILD_PARALLEL_LEVEL', 'LANG', 'LC_ALL', 'TZ'})
        self.assertEqual(actual['CMAKE_BUILD_PARALLEL_LEVEL'], '2')


class RuntimeInstallationAliases(unittest.TestCase):
    def setUp(self):
        self.root = HERE / 'cases' / uuid.uuid4().hex
        self.root.mkdir(parents=True)
        def cleanup():
            self.assertEqual(self.root.resolve(), self.root)
            self.assertTrue(self.root.is_relative_to(HERE / 'cases'))
            shutil.rmtree(self.root)
        self.addCleanup(cleanup)
        self.configured = self.root / 'logical-sdk'
        self.physical = self.root / 'packaged-sdk'
        for base in (self.configured, self.physical):
            for name, data in (('VERSION', b'wasi-sdk-33'), ('bin/clang++.exe', b'fake-compiler'),
                               ('share/cmake/wasi-sdk-p1.cmake', b'fake-toolchain')):
                path = base / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
        self.resolve = Path.resolve

    def virtualized(self, path, *args, **kwargs):
        if path.is_relative_to(self.configured) and path.is_file():
            return self.physical / path.relative_to(self.configured)
        return self.resolve(path, *args, **kwargs)

    def test_file_virtualization_pins_physical_version_without_relaxing_direct_paths(self):
        test = self
        def resolve(path, *args, **kwargs):
            return test.virtualized(path, *args, **kwargs)
        with patch.object(Path, 'resolve', resolve):
            self.assertEqual(self.configured.resolve(), self.configured)
            with self.assertRaisesRegex(core.ArenaError, 'redirected'):
                core._cold_file(self.configured / 'VERSION')
            physical, tree = core._cold_runtime_installation(self.configured, Path('bin/clang++.exe'))
            self.assertEqual(physical, self.physical)
            self.assertEqual(tree['files'], 3)
            self.assertEqual(tree['configured_path'], str(self.configured))
            self.assertEqual(tree['sha256'], core._cold_tree(self.physical)['sha256'])
            with self.assertRaisesRegex(core.ArenaError, 'redirected'):
                core._cold_direct(self.configured / 'VERSION')

    def test_mixed_file_identity_or_inventory_does_not_get_runtime_alias_credit(self):
        test = self
        def mixed(path, *args, **kwargs):
            if path == test.configured / 'VERSION':
                return test.root / 'unrelated/VERSION'
            return test.virtualized(path, *args, **kwargs)
        with patch.object(Path, 'resolve', mixed):
            with self.assertRaisesRegex(core.ArenaError, 'installation identity'):
                core._cold_runtime_installation(self.configured, Path('bin/clang++.exe'))
        (self.physical / 'EXTRA').write_bytes(b'physical-only')
        def resolve(path, *args, **kwargs):
            return test.virtualized(path, *args, **kwargs)
        with patch.object(Path, 'resolve', resolve):
            with self.assertRaisesRegex(core.ArenaError, 'inventories differ'):
                core._cold_runtime_installation(self.configured, Path('bin/clang++.exe'))

    def test_optimized_physical_walk_has_identical_full_fingerprint_to_strict_old_walk(self):
        files = []
        for path in self.physical.rglob('*'):
            if path.is_file():
                descriptor = core._cold_file(path)
                files.append([path.relative_to(self.physical).as_posix(), descriptor['size'], descriptor['sha256']])
        expected = {'path': str(self.physical), 'files': len(files), 'bytes': sum(row[1] for row in files),
                    'sha256': core._cold_hash(sorted(files))}
        self.assertEqual(core._cold_tree(self.physical), expected)
        # Strictly validate directories, without repeated ancestor resolution at each leaf.
        direct = core._cold_direct
        visited = []
        def checked(path):
            visited.append(path)
            return direct(path)
        with patch.object(core, '_cold_direct', side_effect=checked):
            self.assertEqual(core._cold_tree(self.physical), expected)
        self.assertEqual(len(visited), len(set(visited)))
        self.assertTrue(all(path.is_dir() for path in visited))

    def test_optimized_walk_refuses_reparse_leaf_and_redirected_ancestor(self):
        leaf = self.physical / 'VERSION'
        real_lstat = Path.lstat
        def flagged(path):
            info = real_lstat(path)
            if path == leaf:
                return SimpleNamespace(st_mode=info.st_mode, st_file_attributes=0x400)
            return info
        with patch.object(Path, 'lstat', flagged):
            with self.assertRaisesRegex(core.ArenaError, 'redirected'):
                core._cold_tree(self.physical)
        import _winapi
        alias = self.physical / 'external-alias'
        _winapi.CreateJunction(str(self.configured), str(alias))
        with self.assertRaises(core.ArenaError):
            core._cold_tree(self.physical)


class ExplicitEnvironment(unittest.TestCase):
    def setUp(self):
        self.root = HERE / 'cases' / uuid.uuid4().hex
        self.root.mkdir(parents=True)
        def cleanup():
            self.assertEqual(self.root.resolve(), self.root)
            self.assertTrue(self.root.is_relative_to(HERE / 'cases'))
            shutil.rmtree(self.root)
        self.addCleanup(cleanup)

    def test_environment_dictionary_is_copied_with_unicode_and_empty_values(self):
        supplied = {'PATH': 'C:\\unicode-\u00e9', 'EMPTY': ''}
        result = native._explicit_environment(supplied)
        self.assertEqual(result, supplied)
        self.assertIsNot(result, supplied)
        self.assertIsNone(native._explicit_environment(None))

    def test_invalid_or_case_colliding_environment_fails_before_process_creation(self):
        for env in ({'Path': 'one', 'PATH': 'two'}, {'NUL\0': 'v'}, {'KEY': 'bad\0value'}, {'': 'v'}, {'=C:': 'v'}, {'KEY': 3}, []):
            with self.subTest(env=env):
                with self.assertRaises(core.ArenaError):
                    native._explicit_environment(env)

    def test_command_preserves_old_constructor_calls_and_records_only_explicit_environment(self):
        captured = []
        class Process:
            pid, returncode, empty_confirmed = 42, 0, True
            def __init__(self, argv, cwd, stdout, stderr):
                captured.append({})
                stdout.write(b'fixture output')
            def wait(self, timeout):
                return 0
            def record(self):
                return {'empty_confirmed': True, 'forced_termination': False}
            def close(self):
                pass
        for explicit in (False, True):
            directory = self.root / ('explicit' if explicit else 'default')
            directory.mkdir()
            kwargs = {'env': {'KEY': '\u00e9'}} if explicit else {}
            def constructor(*args, **kwargs):
                process = Process(*args)
                captured[-1] = kwargs
                return process
            with patch.object(native, 'ROOT', self.root), patch.object(native, '_WinJobProcess', side_effect=constructor):
                native._command(['fixed.exe'], self.root, directory, 9, **kwargs)
            self.assertEqual(captured[-1], kwargs)
            dispatch = json.loads((directory / 'dispatch.json').read_bytes())
            self.assertEqual('environment' in dispatch, explicit)
            if explicit:
                self.assertEqual(dispatch['environment'], kwargs['env'])

    def test_job_creation_receives_explicit_unicode_mapping_and_flag_without_launching(self):
        functions = ('CreateJobObjectW', 'SetInformationJobObject', 'AssignProcessToJobObject',
                     'QueryInformationJobObject', 'TerminateJobObject', 'TerminateProcess', 'ResumeThread',
                     'WaitForSingleObject', 'GetExitCodeProcess', 'CloseHandle')
        kernel = SimpleNamespace(**{name: MagicMock(return_value=1) for name in functions})
        import _winapi
        create = MagicMock(return_value=(11, 12, 13, 14))
        with (self.root / 'stdout.bin').open('wb') as out, (self.root / 'stderr.bin').open('wb') as err, \
                patch.object(native.ctypes, 'WinDLL', return_value=kernel), patch.object(_winapi, 'CreateProcess', create), \
                patch.object(native._WinJobProcess, '_assign'), patch.object(native._WinJobProcess, '_resume'):
            process = native._WinJobProcess(['fixed.exe'], self.root, out, err, env={'KEY': '\u00e9'})
            self.assertEqual(create.call_args.args[6], {'KEY': '\u00e9'})
            self.assertTrue(create.call_args.args[5] & 0x400)
            process.close()


if __name__ == '__main__':
    unittest.main()
