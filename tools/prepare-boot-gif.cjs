const path = require('node:path');
const sharp = require('sharp');

async function main() {
  const assets = path.resolve(__dirname, '../components/hisu_ui/assets');
  const source = path.join(assets, 'boot_windows_xp_source.gif');
  const output = path.join(assets, 'boot_windows_xp.gif');
  const before = await sharp(source, {animated:true}).metadata();
  await sharp(source, {animated:true})
    .resize(240,240,{fit:'contain',background:'#000000'})
    .gif({loop:0,delay:before.delay,colours:256,effort:7})
    .toFile(output);
  const after = await sharp(output, {animated:true}).metadata();
  if (after.width !== 240 || after.pageHeight !== 240 || after.pages !== before.pages ||
      JSON.stringify(after.delay) !== JSON.stringify(before.delay))
    throw new Error('Boot GIF dimensions, frame count or timing changed unexpectedly.');
  console.log(`Boot GIF prepared: 240x240, ${after.pages} frames, original timing, black letterbox.`);
}

main().catch(error => { console.error(error); process.exitCode = 1; });
