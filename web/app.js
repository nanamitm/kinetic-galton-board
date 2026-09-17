const $ = id => document.getElementById(id);
const board = $('board'), chart = $('chart');
let physics, geometry, state, paused = false, slider = true, angled = false, last = 0;
const status = text => { $('status').textContent = text; };
function configure() {
  physics.configure(+$('rpm').value, +$('count').value, +$('restitution').value / 100, $('motor').checked, slider);
}
function updateLabels() {
  $('rpm-value').value = `${$('rpm').value} rpm`;
  $('count-value').value = $('count').value;
  $('restitution-value').value = (+$('restitution').value / 100).toFixed(2);
  $('speed-value').value = `${+$('speed').value / 4}×`;
  $('drop').textContent = slider ? 'Release balls' : 'Reinsert slider';
  $('pause').textContent = paused ? 'Resume' : 'Pause';
  $('pause').setAttribute('aria-pressed', String(paused));
  status(paused ? 'Paused' : slider ? 'Ready to release' : 'Running');
}
function readState() {
  state = physics.snapshot();
  state.balls = state.balls.slice();
  $('time').innerHTML = `${state.time.toFixed(2)} <small>s</small>`;
  $('chamber').textContent = state.chamber;
  $('escaped').textContent = state.escaped;
  $('rms').innerHTML = `${state.rms.toFixed(2)} <small>m/s</small>`;
  const loaded = state.balls.length/4;
  $('count-note').textContent = loaded < +$('count').value
    ? `Loaded ${loaded} balls to fit the chamber. Changing the count resets the experiment.`
    : 'Changing the ball count resets the experiment.';
  const total = state.bins.reduce((a,b) => a+b,0);
  $('distribution-note').textContent = state.fitted
    ? `${total} balls in bins · Fitted ideal peak speed: ${(state.sigma/1000).toFixed(2)} m/s`
    : `${total} balls in bins · The ideal curve appears after at least 8 balls have landed.`;
}
function surface(canvas) {
  const {width,height} = canvas.getBoundingClientRect();
  const ratio = Math.min(devicePixelRatio || 1, 2);
  if (canvas.width !== Math.round(width*ratio) || canvas.height !== Math.round(height*ratio)) {
    canvas.width = Math.round(width*ratio); canvas.height = Math.round(height*ratio);
  }
  const ctx = canvas.getContext('2d');
  ctx.setTransform(ratio,0,0,ratio,0,0); ctx.clearRect(0,0,width,height);
  return {ctx,width,height};
}
function drawBoard() {
  const {ctx:c,width:w,height:h} = surface(board);
  const scale = Math.min((w-32)/250,(h-40)/135);
  const project = (x,y,z=0) => [w/2 + (x+(angled ? z*.65 : 0))*scale, h/2 + (-y+(angled ? z*.40 : 0))*scale];
  const polygon = (points,z,fill,stroke) => {
    c.beginPath(); points.forEach(([x,y],i) => {const p=project(x,y,z); i ? c.lineTo(...p) : c.moveTo(...p);}); c.closePath();
    if(fill){c.fillStyle=fill;c.fill();} if(stroke){c.strokeStyle=stroke;c.lineWidth=1;c.stroke();}
  };
  // Low contrast measurement grid, then the exact extracted cross-section.
  c.strokeStyle='#1c2a3a'; c.lineWidth=1;
  for(let x=-100;x<=100;x+=20){c.beginPath();c.moveTo(...project(x,-56));c.lineTo(...project(x,56));c.stroke();}
  for(let y=-40;y<=40;y+=20){c.beginPath();c.moveTo(...project(-112,y));c.lineTo(...project(112,y));c.stroke();}
  if(angled) for(const ring of geometry.body.rings) polygon(ring,-12,'#263444','#46627b');
  for(const ring of geometry.body.rings) polygon(ring,0,'#607d95','#9ab0c4');
  const a=geometry.agitator, co=Math.cos(state.angle), si=Math.sin(state.angle);
  polygon(a.profile.map(([x,y]) => [a.center[0]+x*co-y*si,a.center[1]+x*si+y*co]),0,'#58cce8','#a8effc');
  const center=project(...a.center);c.beginPath();c.arc(...center,Math.max(2,a.hub_radius*scale),0,Math.PI*2);c.fillStyle='#203e52';c.fill();
  if(slider){const f=geometry.features;polygon([[f.left_wall_x,f.slider_slot_y0],[f.divider_x0,f.slider_slot_y0],[f.divider_x0,f.slider_slot_y1],[f.left_wall_x,f.slider_slot_y1]],0,'#f3b969');}
  const balls=[];
  for(let i=0;i<state.balls.length;i+=4) balls.push(state.balls.subarray(i,i+4));
  balls.sort((a,b)=>a[2]-b[2]);
  const radius=geometry.ball_diameter/2*scale;
  for(const [x,y,z,escaped] of balls){
    const p=project(x,y,z);c.beginPath();c.arc(...p,radius,0,Math.PI*2);
    c.fillStyle=escaped?'#78d8ec':'#ffd166';c.fill();
    if(radius>2){c.beginPath();c.arc(p[0]-radius*.28,p[1]-radius*.3,radius*.28,0,Math.PI*2);c.fillStyle='#ffffff9c';c.fill();}
  }
  c.fillStyle='#91a2b8';c.font='10px system-ui';c.textAlign='center';
  geometry.features.bins.forEach((b,i)=>c.fillText(String(i+1),...project((b.x0+b.x1)/2,-61)));
}
function drawChart() {
  const {ctx:c,width:w,height:h}=surface(chart), f=geometry.features;
  const left=42, right=w-20, top=20, bottom=h-38;
  const vmin=(f.bins[0].x0-f.divider_x1)/state.fallTime;
  const vmax=(f.bins.at(-1).x1-f.divider_x1)/state.fallTime;
  const total=state.bins.reduce((a,b)=>a+b,0);
  const binWidth=(f.bins[0].x1-f.bins[0].x0)/state.fallTime;
  const density=v=>total*binWidth*v/(state.sigma**2)*Math.exp(-.5*(v/state.sigma)**2);
  const max=Math.max(4,Math.ceil(Math.max(...state.bins,state.fitted?density(state.sigma):0)*1.2));
  const sx=v=>left+(v-vmin)/(vmax-vmin)*(right-left), sy=n=>bottom-n/max*(bottom-top);
  c.font='10px system-ui';c.textAlign='right';
  for(let i=0;i<=4;i++){const n=max*i/4,y=sy(n);c.strokeStyle='#293647';c.beginPath();c.moveTo(left,y);c.lineTo(right,y);c.stroke();c.fillStyle='#91a2b8';c.fillText(n.toFixed(0),left-8,y+3);}
  c.fillStyle='#5ac7e5';state.bins.forEach((n,i)=>{const b=f.bins[i],x=sx((b.x0-f.divider_x1)/state.fallTime),end=sx((b.x1-f.divider_x1)/state.fallTime);c.fillRect(x+1,sy(n),Math.max(1,end-x-2),bottom-sy(n));});
  if(state.fitted && $('theory').checked){c.save();c.beginPath();c.rect(left,top,right-left,bottom-top);c.clip();c.beginPath();for(let i=0;i<=160;i++){const v=vmin+(vmax-vmin)*i/160;i?c.lineTo(sx(v),sy(density(v))):c.moveTo(sx(v),sy(density(v)));}c.strokeStyle='#ffd166';c.lineWidth=2;c.stroke();c.restore();}
  c.fillStyle='#91a2b8';c.textAlign='center';for(let i=0;i<=5;i++){const v=vmin+(vmax-vmin)*i/5;c.fillText((v/1000).toFixed(1),sx(v),bottom+16);}c.fillText('Escape speed [m/s] = bin position / fall time',(left+right)/2,h-5);
}
function draw(){if(state){drawBoard();drawChart();}}
function frame(now) {
  if(!paused && !document.hidden){physics.advance(Math.min((now-last)/1000,.02)*(+$('speed').value/4));readState();}
  last=now;draw();requestAnimationFrame(frame);
}
async function start() {
  try {
    const [{default:createPhysics}, response] = await Promise.all([import('./physics.js'),fetch('./geometry.json')]);
    if(!response.ok) throw new Error(`Could not load geometry (${response.status})`);
    geometry=await response.json();physics=await createPhysics();physics.initialize(geometry);
    $('controls').disabled=false;$('view').disabled=false;
    $('drop').addEventListener('click',()=>{slider=!slider;configure();updateLabels();});
    $('pause').addEventListener('click',()=>{paused=!paused;last=performance.now();updateLabels();});
    const reset=()=>{slider=true;paused=false;configure();physics.reset();readState();updateLabels();draw();};
    $('reset').addEventListener('click',reset);
    $('count').addEventListener('input',reset);
    for(const id of ['rpm','restitution','motor']) $(id).addEventListener('input',()=>{configure();updateLabels();});
    $('speed').addEventListener('input',updateLabels);
    $('theory').addEventListener('change',draw);
    $('view').addEventListener('click',()=>{angled=!angled;$('view').textContent=angled?'Front view':'Oblique view';draw();});
    document.addEventListener('visibilitychange',()=>{last=performance.now();});
    new ResizeObserver(draw).observe(board);
    updateLabels();readState();last=performance.now();requestAnimationFrame(frame);
  } catch(error) {
    console.error(error);$('status').classList.add('error');
    status(`Could not start the simulator. Please reload the page. ${error.message}`);
  }
}
start();
