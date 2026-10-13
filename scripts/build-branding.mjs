// Optional artwork regeneration. Normal C builds use the committed icon.
// node scripts/build-branding.mjs [--sharp path/to/sharp]
import { createRequire } from 'node:module';
import { readFile, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const require = createRequire(import.meta.url);
const args = process.argv.slice(2);
if (args.length && (args.length !== 2 || args[0] !== '--sharp')) {
  throw new Error('Usage: node scripts/build-branding.mjs [--sharp path/to/sharp]');
}
const sharp = require(args.length ? path.resolve(args[1]) : 'sharp');
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const assetRoot = path.join(root, 'src', 'assets');
const svg = await readFile(path.join(assetRoot, 'windroid-capture.svg'), 'utf8');
const sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256];
const images = [];

for (const size of sizes) {
  // Render each size from vector geometry at 4x, then filter once; never
  // enlarge a smaller raster icon to make a larger Windows resource.
  const oversampled = svg.replace('width="256" height="256"',
    `width="${size * 4}" height="${size * 4}"`);
  const image = await sharp(Buffer.from(oversampled))
    .resize(size, size, { kernel: 'lanczos3' })
    .png().toBuffer();
  images.push(image);
  await writeFile(path.join(assetRoot, `windroid-capture-${size}.png`), image);
}

// PNG-compressed 32-bit icon entries are supported by the Windows 11 target.
const directory = Buffer.alloc(6 + sizes.length * 16);
directory.writeUInt16LE(1, 2);
directory.writeUInt16LE(sizes.length, 4);
let offset = directory.length;
for (let i = 0; i < sizes.length; i++) {
  const entry = 6 + i * 16;
  directory[entry] = directory[entry + 1] = sizes[i] === 256 ? 0 : sizes[i];
  directory.writeUInt16LE(1, entry + 4);
  directory.writeUInt16LE(32, entry + 6);
  directory.writeUInt32LE(images[i].length, entry + 8);
  directory.writeUInt32LE(offset, entry + 12);
  offset += images[i].length;
}
await writeFile(path.join(assetRoot, 'windroid-capture.ico'),
  Buffer.concat([directory, ...images]));
console.log(`Phone Lens Windows icon generated: ${sizes.join(', ')} px`);
