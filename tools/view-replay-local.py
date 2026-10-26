"""Serve one retained replay with pinned official assets on loopback; never open a browser.

    python tools/view-replay-local.py --replay PATH.repl26a [--port N] [--profile auto|cli-3.0.4|...]

The decoder profile (tools/replay-profiles.json) is chosen from the replay header; the
visualiser comes from the CLI's asset cache (%LOCALAPPDATA%/cambc/assets/<release>/visualiser)
and is verified file by file against its release manifest. Prints the URL to open; Ctrl+C stops.
"""
import argparse
import functools
import hashlib
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
import json
import os
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit
import zipfile

PROJECT = Path(__file__).resolve().parents[1]
PROFILE_PATH = PROJECT / 'tools/replay-profiles.json'
PROFILE_REGISTRY = json.loads(PROFILE_PATH.read_text(encoding='utf-8'))
if PROFILE_REGISTRY.get('schemaVersion') != 1:
    raise ValueError('Unsupported replay profile registry')
PROFILES = {profile['id']: profile for profile in PROFILE_REGISTRY['profiles']}
ASSETS = Path(os.environ.get('LOCALAPPDATA') or Path.home() / 'AppData/Local') / 'cambc/assets'


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def contained(root, name):
    if not name or '\\' in name or ':' in name or '\x00' in name or any(part in ('.', '..') for part in name.split('/')):
        raise ValueError('Invalid asset path')
    path = (root / name).resolve()
    if not path.is_relative_to(root):
        raise ValueError('Asset path escapes visualiser root')
    return path


def validate_assets(root, engine_name, profile):
    """Check the visualiser release manifest, the pinned engine and every listed asset."""
    root = Path(root).resolve()
    release = root / '.browser-release.json'
    if not release.is_file():
        raise ValueError(f'Official visualiser for {profile["id"]} is not cached at {root}')
    # Packaged Windows apps can resolve the directory through its logical path
    # while individual files resolve into LocalCache. Anchor containment to the
    # manifest's physical parent, but only if it is the same directory object.
    release = release.resolve()
    if not release.parent.samefile(root):
        raise ValueError('Official browser release manifest escapes visualiser root')
    root = release.parent
    if sha(release) != profile['browserReleaseSha256']:
        raise ValueError('Official browser release manifest hash differs')
    metadata = json.loads(release.read_text(encoding='utf-8'))
    if (metadata.get('schema'), metadata.get('nodeEnv'), metadata.get('minified'), metadata.get('sourcemaps')) != (1, 'production', True, False):
        raise ValueError('Unsupported browser release metadata')
    if not re.fullmatch('v' + profile['engineVersion'].replace('.', '_') + r'-[A-Za-z0-9_-]+\.wasm', engine_name):
        raise ValueError('Wrong pinned engine version')
    files = metadata['files']
    if 'index.html' not in files or files.get('assets/' + engine_name, {}).get('sha256') != profile['decoderSha256']:
        raise ValueError('Pinned visualiser engine or index missing')
    for name, record in files.items():
        path = contained(root, name)
        if not path.is_file() or path.stat().st_size != record['bytes'] or sha(path) != record['sha256']:
            raise ValueError('Official visualiser asset differs: ' + name)
    return root, frozenset(files)


def assets_from_project(profile):
    return validate_assets(ASSETS / profile['cacheRelease'] / 'visualiser', profile['engineFile'], profile)


def validate_replay(replay, profile=None):
    if not replay.is_file() or replay.suffix != '.repl26a' or not zipfile.is_zipfile(replay):
        raise ValueError('Select an existing retained .repl26a replay bundle')
    with zipfile.ZipFile(replay) as archive:
        entries = [item for item in archive.infolist() if item.filename == 'replay.repl26a']
        if len(entries) != 1 or entries[0].file_size > 256 * 1024 * 1024 or entries[0].flag_bits & 1:
            raise ValueError('Replay bundle has missing, ambiguous or unsupported payload')
        with archive.open(entries[0]) as stream:
            header = stream.read(14)
    if len(header) != 14 or int.from_bytes(header[:8], 'little') != 125779999602273:
        raise ValueError('Unknown or truncated replay header')
    engine = '.'.join(str(value) for value in header[8:11])
    format_version = '.'.join(str(value) for value in header[11:14])
    supported = [profile] if profile else list(PROFILES.values())
    if not any(item['engineVersion'] == engine and format_version in item['replayFormatVersions']
               for item in supported):
        raise ValueError('Replay engine/format version differs from selected profile')
    return {'engineVersion': engine, 'formatVersion': format_version}


def profile_for_replay(replay):
    header = validate_replay(replay)
    matching = [item for item in PROFILES.values() if item['engineVersion'] == header['engineVersion']
                and header['formatVersion'] in item['replayFormatVersions']]
    if len(matching) != 1:
        raise ValueError('Replay profile is ambiguous; select an exact --profile')
    return matching[0]


class ReplayHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, assets, allowed, replay, **kwargs):
        self.assets, self.allowed, self.replay = assets, allowed, replay
        super().__init__(*args, directory=str(assets), **kwargs)

    def setup(self):
        # Browsers preconnect before sending a request; bound idle sockets while
        # independent handler threads keep module/preload requests moving.
        self.request.settimeout(10)
        super().setup()

    def log_message(self, *_args):
        pass

    def send_head(self):
        if self.headers.get('Host') != f'127.0.0.1:{self.server.server_port}':
            self.send_error(403, 'Loopback host required')
            return None
        try:
            url = urlsplit(self.path)
            if url.scheme or url.netloc:
                raise ValueError('Absolute request target forbidden')
            name = unquote(url.path, errors='strict').removeprefix('/') or 'index.html'
            path = contained(self.assets, name)
            if name == 'replay.zip':
                path = self.replay
            elif name not in self.allowed:
                self.send_error(404)
                return None
        except (ValueError, UnicodeError):
            self.send_error(403)
            return None
        try:
            stream = path.open('rb')
        except OSError:
            self.send_error(404)
            return None
        mime = {'.wasm': 'application/wasm', '.zip': 'application/zip', '.js': 'text/javascript',
                '.mjs': 'text/javascript', '.html': 'text/html; charset=utf-8'}.get(path.suffix, self.guess_type(str(path)))
        if name == 'replay.zip':
            mime = 'application/zip'
        self.send_response(200)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(path.stat().st_size))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.end_headers()
        return stream


def make_server(assets, allowed, replay, port):
    handler = functools.partial(ReplayHandler, assets=assets, allowed=allowed, replay=replay)
    server = ThreadingHTTPServer(('127.0.0.1', port), handler)
    server.daemon_threads = True
    return server


def port_number(value):
    number = int(value)
    if not 0 <= number <= 65535:
        raise argparse.ArgumentTypeError('port must be 0..65535 (0 picks a free port)')
    return number


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--replay', required=True, type=Path)
    parser.add_argument('--port', type=port_number, default=0, help='loopback port (default: a free one)')
    parser.add_argument('--profile', choices=['auto', *sorted(PROFILES)], default='auto',
                        help='Exact browser/decoder profile; auto requires one known replay header match')
    args = parser.parse_args(argv)
    try:
        replay = args.replay.resolve()
        if args.profile == 'auto':
            profile = profile_for_replay(replay)
        else:
            profile = PROFILES[args.profile]
            validate_replay(replay, profile)
        assets, allowed = assets_from_project(profile)
        server = make_server(assets, allowed, replay, args.port)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        parser.error(str(error))
    print(f'http://127.0.0.1:{server.server_port}/?replay=replay.zip&cambc-cli=1', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
