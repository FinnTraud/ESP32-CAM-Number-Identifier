// Eingebettete Web-Oberflaeche. Liegt als String im Flash, damit kein
// separater Dateisystem-Upload noetig ist.
#pragma once

#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(
<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Wasserzähler</title>
<style>
:root{--bg:#12151a;--card:#1b2029;--line:#2c3542;--fg:#e7ecf3;--mut:#93a1b5;--acc:#4c9aff;--ok:#3ecf8e;--warn:#ffb84d;--bad:#ff6b6b}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{padding:14px 16px;border-bottom:1px solid var(--line);display:flex;align-items:center;gap:12px;flex-wrap:wrap}
h1{font-size:16px;margin:0;font-weight:600}
.pill{font-size:12px;padding:2px 8px;border-radius:99px;background:var(--card);color:var(--mut);border:1px solid var(--line)}
.pill.ok{color:var(--ok);border-color:#245c45}.pill.bad{color:var(--bad);border-color:#5c2424}
nav{display:flex;gap:4px;padding:10px 16px 0;flex-wrap:wrap}
nav button{background:transparent;border:1px solid transparent;color:var(--mut);padding:7px 12px;border-radius:8px 8px 0 0;cursor:pointer;font:inherit}
nav button.on{background:var(--card);color:var(--fg);border-color:var(--line);border-bottom-color:var(--card)}
main{padding:16px;max-width:900px;margin:0 auto}
section{display:none}section.on{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:14px}
.card h2{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--mut);margin:0 0 10px}
.value{font-size:40px;font-weight:650;letter-spacing:.02em;font-variant-numeric:tabular-nums}
.value small{font-size:16px;color:var(--mut);font-weight:400;margin-left:6px}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:10px}
label{display:block;font-size:12px;color:var(--mut);margin-bottom:3px}
input,select{width:100%;background:#10141a;border:1px solid var(--line);color:var(--fg);border-radius:8px;padding:7px 9px;font:inherit}
input[type=checkbox]{width:auto}
input[type=range]{padding:0}
button.btn{background:var(--acc);border:0;color:#06111f;font-weight:600;padding:9px 14px;border-radius:9px;cursor:pointer;font:inherit;font-weight:600}
button.btn.sec{background:var(--card);color:var(--fg);border:1px solid var(--line)}
button.btn.danger{background:#3a1f22;color:var(--bad);border:1px solid #5c2424}
button.btn:disabled{opacity:.5;cursor:default}
.wrap{position:relative;line-height:0;touch-action:none}
.wrap img{width:100%;border-radius:8px;display:block;background:#000}
.wrap canvas{position:absolute;inset:0;width:100%;height:100%}
.strip{display:flex;gap:8px;flex-wrap:wrap}
.dg{text-align:center}
.dg img{width:52px;image-rendering:pixelated;border:1px solid var(--line);border-radius:6px;background:#000}
.dg b{display:block;font-size:20px;font-variant-numeric:tabular-nums}
.dg span{font-size:11px;color:var(--mut)}
.tpl{display:grid;grid-template-columns:repeat(10,1fr);gap:6px;max-width:420px}
.tpl figure{margin:0;text-align:center}
.tpl img{width:100%;image-rendering:pixelated;border-radius:4px;border:1px solid var(--line)}
.tpl figcaption{font-size:10px;color:var(--mut)}
.tpl .new{border-color:var(--ok)}
.muted{color:var(--mut);font-size:12px}
.bar{height:6px;background:#10141a;border-radius:99px;overflow:hidden}
.bar i{display:block;height:100%;background:var(--ok)}
table{width:100%;border-collapse:collapse;font-size:13px}
td{padding:4px 0;border-bottom:1px solid var(--line)}
td:first-child{color:var(--mut);width:45%}
#toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:#0b0e13;border:1px solid var(--line);padding:9px 16px;border-radius:10px;opacity:0;transition:.2s;pointer-events:none;z-index:9}
#toast.on{opacity:1}
</style>

<header>
  <h1>Wasserzähler</h1>
  <span class="pill" id=pWifi>WLAN …</span>
  <span class="pill" id=pMqtt>MQTT …</span>
  <span class="pill" id=pUp>–</span>
</header>

<nav>
  <button data-t=view class=on>Ansicht</button>
  <button data-t=setup>Einrichtung</button>
  <button data-t=cfg>Einstellungen</button>
  <button data-t=sys>System</button>
</nav>

<main>

<section id=view class=on>
  <div class=card>
    <h2>Zählerstand</h2>
    <div class=value><span id=vVal>–</span><small id=vUnit></small></div>
    <div class=muted id=vSub>noch keine Messung</div>
    <div style="margin-top:10px" class=bar><i id=vConf style="width:0"></i></div>
    <div class=muted id=vConfT style="margin-top:4px"></div>
  </div>

  <div class=card>
    <h2>Erkannte Stellen</h2>
    <div class=strip id=strip><span class=muted>noch nichts erkannt</span></div>
    <div class=row style="margin-top:12px">
      <button class=btn onclick="recognize()">Jetzt erkennen</button>
      <button class="btn sec" onclick="refreshImg()">Bild neu laden</button>
    </div>
  </div>

  <div class=card>
    <h2>Livebild</h2>
    <div class=wrap><img id=img0 src="/snapshot.jpg"></div>
  </div>
</section>

<section id=setup>
  <div class=card>
    <h2>Ausschnitte festlegen</h2>
    <p class=muted>Rechtecke ziehen zum Verschieben, rechte untere Ecke zum Skalieren.
       Ein Rechteck soll <b>genau eine Ziffer</b> umfassen – oben und unten ein
       schmaler Streifen der Trommel, damit das Weiterdrehen sichtbar bleibt.</p>
    <div class=wrap>
      <img id=img1 src="/snapshot.jpg">
      <canvas id=ov width=640 height=480></canvas>
    </div>
    <div class=row style="margin-top:10px">
      <button class="btn sec" onclick="refreshImg()">Bild neu</button>
      <button class="btn sec" onclick="spread()">Rollen gleichmäßig verteilen</button>
      <button class=btn onclick="saveGeom()">Geometrie speichern</button>
    </div>
  </div>

  <div class=card>
    <h2>Aufbau des Zählwerks</h2>
    <div class=grid>
      <div><label>Schwarze Rollen</label><input type=number id=gNd min=0 max=8></div>
      <div><label>davon hinter dem Komma</label><input type=number id=gDec min=0 max=8></div>
      <div><label>Rote Zeiger</label><input type=number id=gNdl min=0 max=4></div>
      <div><label>Bilddrehung <span id=gRotL></span></label><input type=range id=gRot min=-15 max=15 step=.5></div>
    </div>
    <div id=dialCfg style="margin-top:10px"></div>
  </div>

  <div class=card>
    <h2>Anlernen</h2>
    <p class=muted>Den am Zähler abgelesenen Stand eintippen und speichern. Die
      Firmware schneidet dann die passenden Ausschnitte heraus und legt sie als
      Vorlagen ab. Nach ein paar Durchgängen mit unterschiedlichen Endziffern ist
      die Sammlung vollständig.</p>
    <div class=row>
      <input id=lVal placeholder="z. B. 01234.567" style="max-width:220px">
      <button class=btn onclick="learn()">Anlernen</button>
    </div>
    <div class=muted id=lMsg style="margin-top:8px"></div>
  </div>

  <div class=card>
    <h2>Vorlagen</h2>
    <div class=row style="margin-bottom:8px">
      <label style="margin:0">Bank</label>
      <select id=tBank style="max-width:180px" onchange="drawTpl()"></select>
      <span class=muted id=tInfo></span>
    </div>
    <div class=tpl id=tpl></div>
  </div>
</section>

<section id=cfg>
  <div class=card>
    <h2>Kamera</h2>
    <div class=grid>
      <div><label>LED-Helligkeit <span id=cFlashL></span></label><input type=range id=cFlash min=0 max=255></div>
      <div><label>Helligkeit</label><input type=number id=cBri min=-2 max=2></div>
      <div><label>Kontrast</label><input type=number id=cCon min=-2 max=2></div>
      <div><label>Verworfene Frames</label><input type=number id=cWarm min=0 max=10></div>
      <div><label><input type=checkbox id=cVf> vertikal spiegeln</label></div>
      <div><label><input type=checkbox id=cHm> horizontal spiegeln</label></div>
      <div><label><input type=checkbox id=cAe> Automatikbelichtung</label></div>
      <div><label>Belichtung (manuell)</label><input type=number id=cExp min=0 max=1200></div>
      <div><label>Verstärkung (manuell)</label><input type=number id=cGain min=0 max=30></div>
    </div>
  </div>

  <div class=card>
    <h2>Erkennung</h2>
    <div class=grid>
      <div><label>Polarität</label><select id=rPol><option value=0>automatisch</option><option value=1>dunkle Ziffer auf hell</option><option value=2>helle Ziffer auf dunkel</option></select></div>
      <div><label>Drehrichtung</label><select id=rDir><option value=1>Ziffer wandert nach oben</option><option value=-1>Ziffer wandert nach unten</option></select></div>
      <div><label>Suchbereich (Zeilen)</label><input type=number id=rShift min=1 max=12></div>
      <div><label>Mindestkonfidenz</label><input type=number id=rConf min=0 max=100></div>
      <div><label><input type=checkbox id=rShare> gemeinsame Vorlagen für alle Stellen</label></div>
      <div><label><input type=checkbox id=rAuto> Vorlagen automatisch nachführen</label></div>
    </div>
  </div>

  <div class=card>
    <h2>Messung &amp; Plausibilität</h2>
    <div class=grid>
      <div><label>Messintervall (s)</label><input type=number id=pInt min=10 max=86400></div>
      <div><label>max. Zuwachs je Minute</label><input type=number id=pRate step=0.001></div>
      <div><label><input type=checkbox id=pDec> Rückwärtslaufen erlauben</label></div>
      <div><label>Einheit</label><input id=pUnit maxlength=7></div>
    </div>
  </div>

  <div class=card>
    <h2>MQTT</h2>
    <div class=grid>
      <div><label><input type=checkbox id=mEn> aktiv</label></div>
      <div><label>Broker</label><input id=mHost></div>
      <div><label>Port</label><input type=number id=mPort></div>
      <div><label>Benutzer</label><input id=mUser></div>
      <div><label>Passwort</label><input type=password id=mPass placeholder="unverändert"></div>
      <div><label>Basis-Topic</label><input id=mTopic></div>
      <div><label><input type=checkbox id=mDisc> Home-Assistant-Discovery</label></div>
    </div>
  </div>

  <div class=card>
    <h2>WLAN</h2>
    <div class=grid>
      <div><label>SSID</label><input id=wSsid></div>
      <div><label>Passwort</label><input type=password id=wPass placeholder="unverändert"></div>
      <div><label>Hostname</label><input id=wHost></div>
    </div>
    <p class=muted>Änderungen am WLAN werden erst nach einem Neustart wirksam.</p>
  </div>

  <div class=row>
    <button class=btn onclick="saveCfg()">Alles speichern</button>
  </div>
</section>

<section id=sys>
  <div class=card>
    <h2>Gerät</h2>
    <table id=sysT></table>
  </div>
  <div class=card>
    <h2>Zählerstand korrigieren</h2>
    <p class=muted>Setzt den gespeicherten Stand direkt – nützlich bei der
      Inbetriebnahme oder nach einem Zählertausch.</p>
    <div class=row>
      <input id=sVal placeholder="z. B. 01234.567" style="max-width:220px">
      <button class=btn onclick="setValue()">Setzen</button>
      <button class="btn sec" onclick="clearValue()">Verlauf vergessen</button>
    </div>
  </div>
  <div class=card>
    <h2>Firmware aktualisieren</h2>
    <p class=muted>Datei <code>WaterMeterCam.ino.bin</code> aus dem Arduino-Export wählen.</p>
    <form method=POST action="/api/ota" enctype="multipart/form-data">
      <input type=file name=fw accept=".bin" style="margin-bottom:8px">
      <button class=btn type=submit>Hochladen</button>
    </form>
  </div>
  <div class=card>
    <h2>Zurücksetzen</h2>
    <div class=row>
      <button class="btn danger" onclick="resetTpl()">Vorlagen zurücksetzen</button>
      <button class="btn danger" onclick="reboot()">Neustart</button>
    </div>
  </div>
</section>

</main>
<div id=toast></div>

<script>
const $=s=>document.querySelector(s), $$=s=>[...document.querySelectorAll(s)];
let G={rot:0,nd:0,ndl:0,dec:0,digits:[],dials:[]}, C=null, sel=-1, drag=null, lastRec=null;

function toast(m){const t=$('#toast');t.textContent=m;t.classList.add('on');clearTimeout(t._h);t._h=setTimeout(()=>t.classList.remove('on'),2200)}
function post(u,d){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(d||{})}).then(r=>r.json())}

$$('nav button').forEach(b=>b.onclick=()=>{
  $$('nav button').forEach(x=>x.classList.remove('on'));b.classList.add('on');
  $$('section').forEach(s=>s.classList.remove('on'));$('#'+b.dataset.t).classList.add('on');
  if(b.dataset.t=='setup'){draw();drawTpl()}
});

function refreshImg(){const t=Date.now();$('#img0').src='/snapshot.jpg?t='+t;$('#img1').src='/snapshot.jpg?t='+t}

// ---- Geometrie-Editor ----------------------------------------------------
function draw(){
  const cv=$('#ov'), x=cv.getContext('2d');
  x.clearRect(0,0,640,480);
  x.lineWidth=2; x.font='bold 13px system-ui'; x.textBaseline='top';
  G.digits.forEach((r,i)=>{
    x.strokeStyle=i==sel?'#ffb84d':'#4c9aff';
    x.strokeRect(r.x,r.y,r.w,r.h);
    x.fillStyle=x.strokeStyle; x.fillRect(r.x+r.w-9,r.y+r.h-9,9,9);
    x.fillText(i+1,r.x+2,r.y-16<0?r.y+2:r.y-16);
  });
  G.dials.forEach((r,i)=>{
    const j=100+i;
    x.strokeStyle=sel==j?'#ffb84d':'#3ecf8e';
    x.beginPath();x.ellipse(r.x+r.w/2,r.y+r.h/2,r.w/2,r.h/2,0,0,7);x.stroke();
    x.strokeRect(r.x,r.y,r.w,r.h);
    x.fillStyle=x.strokeStyle;x.fillRect(r.x+r.w-9,r.y+r.h-9,9,9);
    x.fillText('Z'+(i+1),r.x+2,r.y-16<0?r.y+2:r.y-16);
  });
}
function all(){return G.digits.map((r,i)=>[i,r]).concat(G.dials.map((r,i)=>[100+i,r]))}
function pt(e){const r=$('#ov').getBoundingClientRect();return{x:(e.clientX-r.left)/r.width*640,y:(e.clientY-r.top)/r.height*480}}
$('#ov').addEventListener('pointerdown',e=>{
  const p=pt(e); const list=all().reverse();
  for(const [id,r] of list){
    const near=Math.abs(p.x-(r.x+r.w))<14&&Math.abs(p.y-(r.y+r.h))<14;
    const inside=p.x>=r.x&&p.x<=r.x+r.w&&p.y>=r.y&&p.y<=r.y+r.h;
    if(near||inside){sel=id;drag={id,r,mode:near?'size':'move',ox:p.x-r.x,oy:p.y-r.y};
      $('#ov').setPointerCapture(e.pointerId);draw();return}
  }
  sel=-1;draw();
});
$('#ov').addEventListener('pointermove',e=>{
  if(!drag)return; const p=pt(e), r=drag.r;
  if(drag.mode=='move'){r.x=Math.round(Math.max(0,Math.min(640-r.w,p.x-drag.ox)));r.y=Math.round(Math.max(0,Math.min(480-r.h,p.y-drag.oy)))}
  else{r.w=Math.round(Math.max(8,Math.min(640-r.x,p.x-r.x)));r.h=Math.round(Math.max(8,Math.min(480-r.y,p.y-r.y)))}
  draw();
});
addEventListener('pointerup',()=>drag=null);

function spread(){
  if(G.digits.length<2){toast('mindestens zwei Rollen nötig');return}
  const f=G.digits[0], l=G.digits[G.digits.length-1], n=G.digits.length;
  const step=(l.x-f.x)/(n-1);
  G.digits.forEach((r,i)=>{r.x=Math.round(f.x+step*i);r.y=f.y;r.w=f.w;r.h=f.h});
  draw();toast('verteilt – nicht vergessen zu speichern');
}

function syncCounts(){
  const nd=+$('#gNd').value, ndl=+$('#gNdl').value;
  while(G.digits.length<nd)G.digits.push({x:200+G.digits.length*40,y:210,w:32,h:48});
  G.digits.length=nd;
  while(G.dials.length<ndl)G.dials.push({x:200+G.dials.length*70,y:320,w:64,h:64,off:0,cw:1});
  G.dials.length=ndl;
  dialFields();draw();
}
function dialFields(){
  $('#dialCfg').innerHTML=G.dials.map((d,i)=>
    `<div class=row style="margin-bottom:6px"><span class=muted style="width:60px">Zeiger ${i+1}</span>
     <label style="margin:0">Null bei °</label><input type=number style="max-width:90px" value="${d.off}" oninput="G.dials[${i}].off=+this.value">
     <select style="max-width:150px" onchange="G.dials[${i}].cw=+this.value">
       <option value=1 ${d.cw==1?'selected':''}>im Uhrzeigersinn</option>
       <option value=-1 ${d.cw==-1?'selected':''}>gegen den Uhrzeigersinn</option></select></div>`).join('');
}
$('#gNd').oninput=$('#gNdl').oninput=syncCounts;
$('#gRot').oninput=e=>{G.rot=+e.target.value;$('#gRotL').textContent=G.rot+'°'};

function saveGeom(){
  const d={rot:G.rot,nd:G.digits.length,ndl:G.dials.length,dec:+$('#gDec').value};
  G.digits.forEach((r,i)=>{d['d'+i+'x']=r.x;d['d'+i+'y']=r.y;d['d'+i+'w']=r.w;d['d'+i+'h']=r.h});
  G.dials.forEach((r,i)=>{d['a'+i+'x']=r.x;d['a'+i+'y']=r.y;d['a'+i+'w']=r.w;d['a'+i+'h']=r.h;d['a'+i+'o']=r.off;d['a'+i+'c']=r.cw});
  post('/api/config',d).then(()=>{toast('gespeichert');refreshImg()});
}

// ---- Erkennung -----------------------------------------------------------
function recognize(){
  toast('erkenne …');
  fetch('/api/recognize').then(r=>r.json()).then(j=>{lastRec=j;showRec(j);load();refreshImg()});
}
function showRec(j){
  const s=$('#strip');
  if(!j.digits.length&&!j.dials.length){s.innerHTML='<span class=muted>nichts konfiguriert</span>';return}
  const t=Date.now();
  s.innerHTML=j.digits.map((d,i)=>
    `<div class=dg><img src="/digit.jpg?i=${i}&t=${t}"><b>${d.d<0?'?':d.d}</b>
     <span>${d.conf}% · ${d.score.toFixed(2)}</span></div>`).join('')
   +j.dials.map((d,i)=>
    `<div class=dg><img src="/dial.jpg?i=${i}&t=${t}"><b>${d.d}</b>
     <span>${d.conf}% · ${d.angle.toFixed(0)}°</span></div>`).join('');
  $('#vConfT').textContent='Konfidenz '+j.confidence+'% · Schärfe '+j.sharpness.toFixed(0)+
    ' · '+j.ms+' ms'+(j.error?' · '+j.error:'');
}

function learn(){
  const v=$('#lVal').value.replace(',','.');
  if(!v){toast('Zählerstand eingeben');return}
  post('/api/learn',{value:v}).then(j=>{$('#lMsg').textContent=j.message;drawTpl();toast(j.ok?'angelernt':'nicht angelernt')});
}
function drawTpl(){
  const b=$('#tBank');
  if(!b.options.length){
    b.innerHTML='<option value=8>gemeinsam</option>'+[...Array(8)].map((_,i)=>`<option value=${i}>Stelle ${i+1}</option>`).join('');
  }
  const bank=+b.value, t=Date.now();
  fetch('/api/templates?b='+bank).then(r=>r.json()).then(j=>{
    $('#tInfo').textContent=j.learned+' von 10 aus eigenen Bildern';
    $('#tpl').innerHTML=j.weights.map((w,c)=>
      `<figure><img class="${w>0?'new':''}" src="/template.jpg?b=${bank}&c=${c}&t=${t}"><figcaption>${c}${w>0?'':' *'}</figcaption></figure>`).join('');
  });
}

// ---- Status --------------------------------------------------------------
function load(){
  fetch('/api/status').then(r=>r.json()).then(j=>{
    $('#vVal').textContent=j.hasValue?j.value:'–';
    $('#vUnit').textContent=j.unit;
    $('#vSub').textContent=j.hasValue
      ? `letzte Erkennung ${j.raw} · ${j.readings} Messungen, ${j.rejected} verworfen · ${j.reject}`
      : 'noch keine Messung';
    $('#vConf').style.width=j.confidence+'%';
    $('#vConf').style.background=j.confidence>=j.minConfidence?'var(--ok)':'var(--warn)';
    $('#pWifi').textContent='WLAN '+(j.wifi?j.rssi+' dBm':'aus');
    $('#pWifi').className='pill '+(j.wifi?'ok':'bad');
    $('#pMqtt').textContent='MQTT '+(j.mqttEnabled?(j.mqtt?'verbunden':'getrennt'):'aus');
    $('#pMqtt').className='pill '+(!j.mqttEnabled?'':(j.mqtt?'ok':'bad'));
    $('#pUp').textContent=Math.floor(j.uptime/3600)+' h '+Math.floor(j.uptime%3600/60)+' min';
    $('#sysT').innerHTML=[
      ['Firmware',j.fw],['IP',j.ip],['Hostname',j.host],['MAC',j.mac],
      ['Freier Heap',(j.heap/1024).toFixed(0)+' kB'],['Freier PSRAM',(j.psram/1024).toFixed(0)+' kB'],
      ['Bildschärfe',j.sharpness.toFixed(0)],['MQTT-Fehler',j.mqttError||'–'],
      ['Letztes Problem',j.reject]
    ].map(r=>`<tr><td>${r[0]}</td><td>${r[1]}</td></tr>`).join('');
  });
}
function fill(j){
  G={rot:j.rot,digits:j.digits,dials:j.dials};
  $('#gNd').value=j.digits.length;$('#gNdl').value=j.dials.length;$('#gDec').value=j.dec;
  $('#gRot').value=j.rot;$('#gRotL').textContent=j.rot+'°';
  $('#cFlash').value=j.flash;$('#cFlashL').textContent=j.flash;
  $('#cBri').value=j.bri;$('#cCon').value=j.con;$('#cWarm').value=j.warm;
  $('#cVf').checked=j.vflip;$('#cHm').checked=j.hmirror;
  $('#cAe').checked=j.ae;$('#cExp').value=j.exp;$('#cGain').value=j.gain;
  $('#rPol').value=j.pol;$('#rDir').value=j.dir;$('#rShift').value=j.shift;$('#rConf').value=j.minConf;
  $('#rShare').checked=j.share;$('#rAuto').checked=j.autoLearn;
  $('#pInt').value=j.interval;$('#pRate').value=j.maxRate;$('#pDec').checked=j.allowDec;$('#pUnit').value=j.unit;
  $('#mEn').checked=j.mqttEnabled;$('#mHost').value=j.mqttHost;$('#mPort').value=j.mqttPort;
  $('#mUser').value=j.mqttUser;$('#mTopic').value=j.mqttTopic;$('#mDisc').checked=j.mqttDisc;
  $('#wSsid').value=j.ssid;$('#wHost').value=j.host;
  dialFields();draw();
}
$('#cFlash').oninput=e=>$('#cFlashL').textContent=e.target.value;

function saveCfg(){
  const d={
    flash:$('#cFlash').value,bri:$('#cBri').value,con:$('#cCon').value,warm:$('#cWarm').value,
    vflip:$('#cVf').checked?1:0,hmirror:$('#cHm').checked?1:0,
    ae:$('#cAe').checked?1:0,exp:$('#cExp').value,gain:$('#cGain').value,
    pol:$('#rPol').value,dir:$('#rDir').value,shift:$('#rShift').value,minConf:$('#rConf').value,
    share:$('#rShare').checked?1:0,autoLearn:$('#rAuto').checked?1:0,
    interval:$('#pInt').value,maxRate:$('#pRate').value,allowDec:$('#pDec').checked?1:0,unit:$('#pUnit').value,
    mqttEnabled:$('#mEn').checked?1:0,mqttHost:$('#mHost').value,mqttPort:$('#mPort').value,
    mqttUser:$('#mUser').value,mqttTopic:$('#mTopic').value,mqttDisc:$('#mDisc').checked?1:0,
    ssid:$('#wSsid').value,host:$('#wHost').value,dec:$('#gDec').value
  };
  if($('#mPass').value)d.mqttPass=$('#mPass').value;
  if($('#wPass').value)d.wifiPass=$('#wPass').value;
  post('/api/config',d).then(()=>{toast('gespeichert');boot()});
}
function setValue(){
  const v=$('#sVal').value.replace(',','.');
  if(!v)return; post('/api/setvalue',{value:v}).then(()=>{toast('gesetzt');load()});
}
function clearValue(){if(confirm('Gespeicherten Stand verwerfen?'))post('/api/clearvalue').then(()=>{toast('zurückgesetzt');load()})}
function resetTpl(){if(confirm('Alle angelernten Vorlagen löschen?'))post('/api/reset-templates').then(()=>{toast('zurückgesetzt');drawTpl()})}
function reboot(){if(confirm('Neu starten?'))post('/api/reboot').then(()=>toast('startet neu …'))}

function boot(){fetch('/api/config').then(r=>r.json()).then(fill);load()}
boot();
setInterval(load,10000);
</script>
)HTMLPAGE";
