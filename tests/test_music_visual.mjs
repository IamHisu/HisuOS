import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {createRequire} from 'node:module';
import {mkdir} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
const require=createRequire(import.meta.url);
const {chromium}=require('playwright');
const executable=fileURLToPath(new URL('../build/ui-tests/ui-face-test.exe',import.meta.url));
const output=fileURLToPath(new URL('../build/music-visual/',import.meta.url));
await mkdir(output,{recursive:true});
const browser=await chromium.launch({headless:true,executablePath:process.env.HISU_BROWSER_PATH});
try {
  const page=await browser.newPage({viewport:{width:240,height:240},deviceScaleFactor:2});
  await page.setContent('<body style="margin:0;background:black"><canvas width="240" height="240"></canvas></body>');
  const images=[];
  const frames=[];
  const cases=[['quiet',1500,0],['playing',1500,200],['moving',2300,200],['volume',2300,200],['burst',5150,200],['falling',5650,200]];
  if(process.argv.includes('--animation')) for(let ms=4000;ms<=7000;ms+=50) cases.push([`anim-${ms}`,ms,200]);
  for (const [name,ms,energy] of cases) {
    const frame=JSON.parse(execFileSync(executable,['--music-json',String(ms),String(energy)],{encoding:'utf8'}));
    frames.push(frame);
    assert.equal(frame.view_y+frame.height,240);
    assert(frame.notes.filter(n=>n[5]!==0).length>=3 && frame.notes.filter(n=>n[5]!==0).length<=5);
    assert(frame.notes.filter(n=>n[5]===3).length<=4 && frame.sparks.length<=128);
    if(name==='burst' || name==='falling') assert(frame.sparks.length,'Preview must include real native firework particles');
    const pixels=await page.evaluate(({frame,energy,name})=>{
      energy=frame.energy;
      const c=document.querySelector('canvas').getContext('2d');
      c.globalAlpha=1;c.fillStyle='black';c.fillRect(0,0,240,240);
      function rect(x,y,w,h,color,opacity,r=0) {
        c.globalAlpha=opacity/255;c.fillStyle='#'+color.toString(16).padStart(6,'0');
        c.beginPath();c.roundRect(x,y+frame.view_y,w,h,Math.min(r,w/2,h/2));c.fill();
      }
      for(let i=0;i<frame.background.length;++i) {
        const color='#'+frame.background[i].toString(16).padStart(6,'0');
        const height=32+Math.floor(energy/4)+Math.floor(frame.bars[Math.floor(i*12*frame.bars.length/240)][1]/2);
        const y=240-height;
        const g=c.createLinearGradient(0,y,0,240);
        g.addColorStop(0,color+'00');
        g.addColorStop(1,color+(16+Math.floor(energy/3)).toString(16).padStart(2,'0'));
        c.globalAlpha=1;c.fillStyle=g;c.fillRect(i*12,y,12,height);
      }
      frame.bars.forEach(([x,h,color])=>{
        if(!h) return;
        const top=frame.baseline-h+1;
        rect(x-1,top-2,5,h+2,color,35+Math.floor(energy/4),2);
        rect(x,top,3,h,color,225,1);rect(x,top,3,2,0xffffff,130,1);
      });
      for(const [x,y,size,color,kind,phase,alpha] of frame.notes) {
        if(!alpha) continue;
        for(const [s,opacity] of [[3,Math.floor(alpha/5)],[1,Math.floor(alpha/2)],[0,alpha]]) {
          rect(x-s,y-s,size+s*2,size-2+s*2,color,opacity,4+s);
          rect(x+size-1-s,y-17-s,2+s*2,19+s*2,color,opacity,s);
          if(kind===1) {
            rect(x+11-s,y-2-s,size+s*2,size-2+s*2,color,opacity,4+s);
            rect(x+size+10-s,y-19-s,2+s*2,19+s*2,color,opacity,s);
            rect(x+size-s,y-18-s,11+s*2,2+s*2,color,opacity,s);
          } else if(kind===0) {
            rect(x+size-s,y-17-s,4+s*2,2+s*2,color,opacity,1+s);
            rect(x+size+3-s,y-15-s,2+s*2,5+s*2,color,opacity,1+s);
          }
        }
      }
      for(const [x,y,tx,ty,color,alpha] of frame.sparks) {
        for(const [width,opacity] of [[4,Math.floor(alpha/3)],[2,alpha]]) {
          c.globalAlpha=opacity/255;c.strokeStyle='#'+color.toString(16).padStart(6,'0');
          c.lineWidth=width;c.lineCap='round';c.beginPath();
          c.moveTo(tx,ty+frame.view_y);c.lineTo(x,y+frame.view_y);c.stroke();
        }
        rect(x,y,1,1,0xffffff,Math.floor(alpha*3/4));
      }
      c.globalAlpha=1;c.textAlign='center';c.fillStyle='#a1a9a5';c.font='14px Arial';c.fillText('Đang phát',120,16);
      c.fillStyle='#eeeeee';c.font='16px Arial';c.fillText('Sóng Gió',120,38);c.fillText('Jack, K-ICM',120,58);
      const caption=c.getImageData(0,22,240,44).data;
      if(!caption.some((value,i)=>i%4!==3 && value>100)) throw Error('Track caption is missing');
      if(name==='volume') {
        c.globalAlpha=220/255;c.fillStyle='#111719';c.beginPath();c.roundRect(208,59,30,128,8);c.fill();
        c.globalAlpha=1;c.fillStyle='#354348';c.fillRect(219,75,8,82);
        c.fillStyle='#5de3de';c.fillRect(219,112,8,45);
        c.fillStyle='#eaffff';c.beginPath();c.roundRect(217,109,12,6,3);c.fill();
        c.font='16px Arial';c.fillStyle='#eeeeee';c.fillText('55',223,180);
      }
      const bottom=c.getImageData(0,239,240,1).data;
      const lit=Array.from(bottom).filter((v,i)=>i%4!==3 && v>70).length;
      if(!energy && lit!==0) throw Error('Silent floor must not retain a fixed bright line');
      if(energy && lit<80) throw Error('Bars must reach the physical bottom edge');
      return Array.from(c.getImageData(0,frame.view_y,240,frame.height).data);
    },{frame,energy,name});
    assert(pixels.some((value,i)=>i%4!==3 && value>100),'Visual must not be blank');
    images.push(pixels);
    await page.evaluate(()=>new Promise(requestAnimationFrame));
    await page.screenshot({path:`${output}/${name}.png`});
  }
  assert.notDeepEqual(images[0],images[1],'Amplitude must affect visualization');
  assert.notDeepEqual(images[1],images[2],'Notes must move over time');
  assert.notDeepEqual(frames[1].notes.map(n=>n.slice(0,2)),frames[2].notes.map(n=>n.slice(0,2)),'Note motion must change geometry, not only colors');
  assert(frames[5].sparks.every(s=>s[1]>=s[3] && s[5]<60),'Late sparks must fall and fade, not form stationary petals');
  assert(frames[4].sparks.length>36,'Burst must have visibly more rays than the earlier eighteen-per-note version');
  assert(frames.every(f=>!('clef' in f)),'Treble clef must be removed');
  console.log('Music visual model: softly backed bottom bars, 3-5 randomized flights, onset-triggered fireworks, falling/fading streaks and volume overlay passed; no treble clef. Canvas mirrors LVGL primitives; not physical TFT verification.');
} finally {await browser.close();}
