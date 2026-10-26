// Exact supported replay/engine identities. A new label alone is insufficient.
import crypto from 'node:crypto';
import fs from 'node:fs';
import {fileURLToPath} from 'node:url';

export const PROFILE_PATH = fileURLToPath(new URL('./replay-profiles.json', import.meta.url));
export const PROFILE_MODULE_PATH = fileURLToPath(import.meta.url);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const bytes = fs.readFileSync(PROFILE_PATH);
const registry = JSON.parse(bytes.toString('utf8'));
if (registry.schemaVersion !== 1 || !Array.isArray(registry.profiles))
  throw Error('Unsupported replay profile registry');
export const PROFILE_SHA256 = sha(bytes);
export const PROFILE_MODULE_SHA256 = sha(fs.readFileSync(PROFILE_MODULE_PATH));
export function verifyProfileInputs() {
  if (sha(fs.readFileSync(PROFILE_PATH)) !== PROFILE_SHA256 ||
      sha(fs.readFileSync(PROFILE_MODULE_PATH)) !== PROFILE_MODULE_SHA256)
    throw Error('Replay profile inputs changed; restart inspection');
}
export const REPLAY_PROFILES = registry.profiles.map(profile => Object.freeze(profile));
export function profileById(id) {
  const matches = REPLAY_PROFILES.filter(profile => profile.id === id);
  if (matches.length !== 1) throw Error('Unsupported official decoder profile');
  return matches[0];
}
export function profileForEngine(version, buildHash) {
  const matches = REPLAY_PROFILES.filter(profile => profile.engineVersion === version &&
    /^[0-9a-f]{64}$/.test(profile.engineBuildHash ?? '') && profile.engineBuildHash === buildHash);
  if (matches.length !== 1) throw Error('Pinned native engine identity differs');
  return matches[0];
}
export function profileForDecoder(sha256) {
  const matches = REPLAY_PROFILES.filter(profile => profile.decoderSha256 === sha256);
  if (matches.length !== 1) throw Error('Unsupported official decoder identity');
  return matches[0];
}
