// Offline preparation only. This decoder is never linked into the game runtime.
import { unpackDx8Archive } from './vendor/wolftools/archive/dx8.mjs';
const [source, output] = process.argv.slice(2);
if (!source || !output) throw new Error('Usage: node tools/unpack_archive.mjs archive.wolf empty-output-directory');
console.log(JSON.stringify(await unpackDx8Archive(source, output)));
