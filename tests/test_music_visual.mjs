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
  for (const [name,ms,energy] of [['quiet',1500,0],['playing',1500,200],['moving',2300,200]]) {
    const frame=JSON.parse(execFileSync(executable,['--music-json',String(ms),String(energy)],{encoding:'utf8'}));
    const pixels=await page.evaluate(({frame,energy})=>{
      const c=document.querySelector('canvas').getContext('2d');
      c.globalAlpha=1;c.fillStyle='black';c.fillRect(0,0,240,240);
      function rect(x,y,w,h,color,opacity,r=0) {
        c.globalAlpha=opacity/255;c.fillStyle='#'+color.toString(16).padStart(6,'0');
        c.beginPath();c.roundRect(x,y+22,w,h,r);c.fill();
      }
      for(let i=0;i<frame.background.length;++i) {
        const color='#'+frame.background[i].toString(16).padStart(6,'0');
        for(let half=0;half<2;++half) {
          const y=half ? 86 : 0,h=half ? 78 : 86;
          const g=c.createLinearGradient(0,y+22,0,y+22+h);
          const opacity=(44+Math.floor(energy/2))/255;
          g.addColorStop(0,color+Math.round((half ? opacity : 0)*255).toString(16).padStart(2,'0'));
          g.addColorStop(1,color+Math.round((half ? 0 : opacity)*255).toString(16).padStart(2,'0'));
          c.globalAlpha=1;c.fillStyle=g;c.fillRect(i*12,y+22,12,h);
        }
      }
      rect(0,82,240,9,0x83cfff,25+Math.floor(energy/5));rect(0,86,240,1,0xeaffff,205);
      frame.bars.forEach(([x,h,color],i)=>{
        if(i%3===0 && x>2 && x<237) rect(x-2,84-h,5,h*2+5,color,70,2);
        rect(x,86-h,1,h*2+1,0xffffff,235);
      });
      for(const [x,y,size,color,kind] of frame.notes) {
        rect(x-2,y-2,size+4,size+2,color,35+Math.floor(energy/5),7);
        rect(x,y,size,size-2,0xffffff,240,4);rect(x+size-1,y-17,1,19,0xffffff,240);
        if(kind===1) {
          rect(x+11,y-2,size,size-2,0xffffff,240,4);rect(x+size+10,y-19,1,19,0xffffff,240);
          rect(x+size,y-18,11,2,0xffffff,240);
        } else if(kind===0) {
          rect(x+size,y-17,4,2,0xffffff,240,1);rect(x+size+3,y-15,2,5,0xffffff,240,1);
        }
      }
      if(frame.clef.length!==8) throw Error('Treble clef curves missing');
      c.globalAlpha=245/255;c.strokeStyle='white';c.lineWidth=2;c.lineCap='round';c.lineJoin='round';
      for(const curve of frame.clef) {
        c.beginPath();curve.forEach(([x,y],i)=> i ? c.lineTo(x,y+22) : c.moveTo(x,y+22));c.stroke();
      }
      c.globalAlpha=1;c.textAlign='center';c.fillStyle='#a1a9a5';c.font='14px Arial';c.fillText('Đang phát',120,16);
      c.fillStyle='#eeeeee';c.font='16px Arial';c.fillText('Sóng Gió',120,205);c.fillText('Jack, K-ICM',120,225);
      const caption=c.getImageData(0,188,240,46).data;
      if(!caption.some((value,i)=>i%4!==3 && value>100)) throw Error('Track caption is missing');
      return Array.from(c.getImageData(0,22,240,164).data);
    },{frame,energy});
    assert(pixels.some((value,i)=>i%4!==3 && value>100),'Visual must not be blank');
    images.push(pixels);
    await page.evaluate(()=>new Promise(requestAnimationFrame));
    await page.screenshot({path:`${output}/${name}.png`});
  }
  assert.notDeepEqual(images[0],images[1],'Amplitude must affect visualization');
  assert.notDeepEqual(images[1],images[2],'Notes must move over time');
  console.log('Music visual model: native geometry, amplitude response, motion and 240x240 screenshots passed. Canvas preview mirrors LVGL primitives; not physical TFT verification.');
} finally {await browser.close();}
