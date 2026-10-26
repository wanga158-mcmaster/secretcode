// Build lab maps for the mechanics probes with the official editor engine's MapBuilder.
//   node mapgen.mjs build SPEC.json OUT.map26a   (spec format: engine.mjs buildMap)
//   node mapgen.mjs all                          build every spec in ../../data/mechanics/maps/*.json
//   node mapgen.mjs show FILE.map26a             ASCII view (north up)
import fs from 'node:fs';
import path from 'node:path';
import {buildMap, renderMap, ROOT} from './engine.mjs';

const [cmd, a, b] = process.argv.slice(2);
const SPECS = path.join(ROOT, 'bots/contenders/foundation/data/mechanics/maps');
if (cmd === 'build') {
  const bytes = await buildMap(JSON.parse(fs.readFileSync(a, 'utf8')));
  fs.writeFileSync(b, bytes);
  console.log(`${b}: ${bytes.length} bytes`);
} else if (cmd === 'all') {
  for (const f of fs.readdirSync(SPECS).filter(n => n.endsWith('.json')).sort()) {
    const spec = JSON.parse(fs.readFileSync(path.join(SPECS, f), 'utf8'));
    const out = path.join(SPECS, f.replace(/\.json$/, '.map26a'));
    fs.writeFileSync(out, await buildMap(spec));
    console.log(`built ${path.relative(ROOT, out)}`);
  }
} else if (cmd === 'show') {
  console.log(await renderMap(fs.readFileSync(a)));
} else {
  console.error('usage: mapgen.mjs build SPEC OUT | all | show FILE');
  process.exit(2);
}
