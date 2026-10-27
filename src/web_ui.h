// Web GUI (single page, no external resources). Served from flash at "/".
#pragma once
#include <pgmspace.h>

static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Switch Reset</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1b1f24;--mut:#667085;--bd:#d9dde3;--acc:#2563eb;--accfg:#fff;--ok:#15803d;--warn:#b45309;--err:#b91c1c;--term:#0b0f14;--termfg:#d1d5db}
@media(prefers-color-scheme:dark){:root{--bg:#0f1318;--card:#171c23;--fg:#e5e7eb;--mut:#98a2b3;--bd:#2a313b;--acc:#60a5fa;--accfg:#0b1220;--ok:#4ade80;--warn:#fbbf24;--err:#f87171;--term:#05070a}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
header{display:flex;flex-wrap:wrap;align-items:center;gap:.75rem;padding:.75rem 1rem;border-bottom:1px solid var(--bd);background:var(--card)}
h1{font-size:1.1rem;margin:0;flex:1}
.pill{font-size:.8rem;padding:.15rem .6rem;border-radius:99px;border:1px solid var(--bd);color:var(--mut)}
.pill.on{color:var(--ok);border-color:var(--ok)}.pill.busy{color:var(--warn);border-color:var(--warn)}
main{display:grid;gap:1rem;padding:1rem;grid-template-columns:1fr;max-width:1300px;margin:0 auto}
@media(min-width:900px){main{grid-template-columns:3fr 2fr}}
.card{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:1rem;min-width:0}
.card h2{font-size:.95rem;margin:0 0 .6rem}
.col{display:flex;flex-direction:column;gap:1rem;min-width:0}
button,select,input{font:inherit;color:inherit}
button{background:var(--card);border:1px solid var(--bd);border-radius:7px;padding:.35rem .75rem;cursor:pointer}
button:hover:not(:disabled){border-color:var(--acc)}
button:disabled{opacity:.45;cursor:not-allowed}
button.primary{background:var(--acc);color:var(--accfg);border-color:var(--acc)}
button.danger{background:var(--err);color:#fff;border-color:var(--err)}
select,input[type=text],input[type=password]{background:var(--bg);border:1px solid var(--bd);border-radius:7px;padding:.35rem .5rem;min-width:0}
.row{display:flex;flex-wrap:wrap;gap:.5rem;align-items:center}
.row>input[type=text]{flex:1}
pre{margin:0;white-space:pre-wrap;word-break:break-word;font:13px/1.35 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
#term{background:var(--term);color:var(--termfg);border-radius:7px;padding:.6rem;height:48vh;min-height:260px;overflow:auto;outline:none}
#term:focus{box-shadow:0 0 0 2px var(--acc)}
#log{height:26vh;min-height:160px;overflow:auto;border:1px solid var(--bd);border-radius:7px;padding:.5rem;background:var(--bg)}
.i{color:var(--fg)}.o{color:var(--ok)}.w{color:var(--warn)}.e{color:var(--err);font-weight:600}.d{color:var(--mut)}
.hint{color:var(--mut);font-size:.85rem;margin:.4rem 0}
ol{margin:.3rem 0 .6rem 1.2rem;padding:0;font-size:.85rem}
code{font:12px ui-monospace,Menlo,Consolas,monospace;background:var(--bg);padding:.05rem .3rem;border-radius:4px}
.warnbox{border:1px solid var(--err);color:var(--err);border-radius:7px;padding:.5rem .7rem;font-size:.85rem;margin:.5rem 0}
.banner{background:var(--warn);color:#fff;padding:.4rem 1rem;font-size:.85rem;display:none}
#overlay,#askbox{position:fixed;inset:0;background:rgba(0,0,0,.55);display:none;align-items:center;justify-content:center;padding:1rem;z-index:10}
.modal{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:1.2rem;width:min(92vw,360px);display:flex;flex-direction:column;gap:.7rem}
.modal input{width:100%}
.err{color:var(--err);font-size:.85rem;min-height:1.2em}
</style></head><body>
<div class="banner" id="defpw">Default web password in use. Change it: hold BOOT 3 s at power-up, then set a new one in the WiFi setup portal.</div>
<header>
  <h1>Switch Reset <span class="pill" id="pillConn">offline</span></h1>
  <span class="pill" id="pillBusy">idle</span>
  <label class="hint">Baud <select id="baud"><option>9600</option><option>19200</option><option>38400</option><option>57600</option><option>115200</option></select></label>
</header>
<main>
  <div class="col">
    <section class="card">
      <h2>Terminal <span class="hint">(click into it to type directly, or use the line below)</span></h2>
      <pre id="term" tabindex="0" spellcheck="false"></pre>
      <div class="row" style="margin-top:.6rem">
        <input type="text" id="line" placeholder="Type a command and press Enter" autocomplete="off" autocapitalize="off" spellcheck="false">
        <button id="sendLine">Send</button>
        <button id="kEnter">Enter</button>
        <button id="kSpace">Space</button>
        <button id="kCtrlC">Ctrl-C</button>
        <button id="clearTerm">Clear</button>
      </div>
    </section>
  </div>
  <div class="col">
    <section class="card">
      <h2>Factory reset</h2>
      <select id="profile" style="width:100%" aria-label="Switch type"></select>
      <select id="mode" style="width:100%;margin-top:.5rem" aria-label="Reset mode"></select>
      <div class="hint" id="mdesc"></div>
      <div class="hint" id="phint"></div>
      <ol id="psteps"></ol>
      <div class="warnbox">The configuration of the switch is erased (depending on the mode the switch also reboots). This cannot be undone.</div>
      <div class="row">
        <input type="text" id="yes" placeholder="Type YES to confirm" autocomplete="off" autocapitalize="characters" style="width:11rem;flex:none">
        <button class="danger" id="start" disabled>Start reset</button>
        <button id="abort" disabled>Abort</button>
      </div>
    </section>
    <section class="card">
      <h2>Progress <button id="clearLog" style="float:right;font-size:.75rem;padding:.1rem .5rem">Clear</button></h2>
      <div id="log"></div>
    </section>
  </div>
</main>

<div id="overlay"><div class="modal">
  <strong>Switch Reset</strong>
  <span class="hint">Enter the web UI password.</span>
  <input type="password" id="pw" autocomplete="current-password">
  <div class="err" id="pwerr"></div>
  <button class="primary" id="pwok">Log in</button>
</div></div>

<div id="askbox"><div class="modal">
  <strong>The switch asks for input</strong>
  <label id="asklabel"></label>
  <input type="password" id="askinput" autocomplete="off">
  <div class="row"><button class="primary" id="askok">OK</button><button id="askcancel">Cancel reset</button></div>
</div></div>

<script>
(function(){
var $=function(i){return document.getElementById(i)};
var ws=null,authed=false,busy=false,profiles=[],termText="",pending=false;

function setConn(on){var p=$("pillConn");p.textContent=on?"online":"offline";p.className="pill"+(on?" on":"")}
function setBusy(b){busy=b;var p=$("pillBusy");p.textContent=b?"reset running":"idle";p.className="pill"+(b?" busy":"");updateButtons()}
function updateButtons(){
  var ok=authed&&!busy&&profiles.length>0&&$("yes").value.trim().toUpperCase()==="YES";
  $("start").disabled=!ok;$("abort").disabled=!(authed&&busy);
  ["baud","sendLine","kEnter","kSpace","kCtrlC"].forEach(function(i){$(i).disabled=!authed||(busy&&i==="baud")});
}
function send(s){if(ws&&ws.readyState===1)ws.send(s)}
function raw(s){if(!busy)send("r"+s)}

function connect(){
  ws=new WebSocket("ws://"+location.hostname+":81/");
  ws.onopen=function(){setConn(true);var pw=sessionStorage.getItem("pw");if(pw)send("P"+pw);else showLogin("")};
  ws.onclose=function(){setConn(false);authed=false;updateButtons();setTimeout(connect,2000)};
  ws.onmessage=function(e){onMsg(e.data)};
}
function showLogin(msg){$("overlay").style.display="flex";$("pwerr").textContent=msg;$("pw").value="";$("pw").focus()}
$("pwok").onclick=function(){var v=$("pw").value;sessionStorage.setItem("pw",v);send("P"+v)};
$("pw").onkeydown=function(e){if(e.key==="Enter")$("pwok").click()};

function onMsg(m){
  var t=m.charAt(0),p=m.slice(1);
  if(t==="A"){
    if(p==="1"){authed=true;$("overlay").style.display="none";updateButtons()}
    else{authed=false;sessionStorage.removeItem("pw");showLogin("Wrong password.")}
  }else if(t==="T")term(p);
  else if(t==="L")logLine(p.charAt(0),p.slice(1));
  else if(t==="Q")ask(p.charAt(0)==="1",p.slice(1));
  else if(t==="S"){var a=p.split(":");setBusy(a[0]==="1");if(a[1])$("baud").value=a[1]}
  else if(t==="D"){logLine(p==="1"?"o":"e",p==="1"?"Done.":"Reset did not complete.");$("askbox").style.display="none"}
}

// ---- terminal ----
var ESC=/\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][A-Za-z0-9]|\x1b[=>]/g;
function term(s){
  s=s.replace(ESC,"").replace(/\r/g,"");
  for(var i=0;i<s.length;i++){
    var c=s.charAt(i);
    if(c==="\b"){if(termText&&termText.slice(-1)!=="\n")termText=termText.slice(0,-1)}
    else termText+=c;
  }
  if(termText.length>60000)termText=termText.slice(-40000);
  if(!pending){pending=true;requestAnimationFrame(function(){
    pending=false;var el=$("term");var stick=el.scrollTop+el.clientHeight>=el.scrollHeight-30;
    el.textContent=termText;if(stick)el.scrollTop=el.scrollHeight})}
}
$("clearTerm").onclick=function(){termText="";$("term").textContent=""};
function sendLine(){var v=$("line").value;raw(v+"\r");$("line").value=""}
$("sendLine").onclick=sendLine;
$("line").onkeydown=function(e){if(e.key==="Enter")sendLine()};
$("kEnter").onclick=function(){raw("\r")};
$("kSpace").onclick=function(){raw(" ")};
$("kCtrlC").onclick=function(){raw("\x03")};
$("baud").onchange=function(){send("b"+$("baud").value)};
$("term").onkeydown=function(e){
  if(e.metaKey||(e.ctrlKey&&e.key.length>1))return;
  var k=e.key,s=null;
  if(e.ctrlKey&&k.length===1&&/[a-z]/i.test(k))s=String.fromCharCode(k.toUpperCase().charCodeAt(0)-64);
  else if(k.length===1&&!e.ctrlKey)s=k;
  else if(k==="Enter")s="\r";else if(k==="Backspace")s="\x08";else if(k==="Tab")s="\t";
  else if(k==="Escape")s="\x1b";else if(k==="ArrowUp")s="\x1b[A";else if(k==="ArrowDown")s="\x1b[B";
  else if(k==="ArrowRight")s="\x1b[C";else if(k==="ArrowLeft")s="\x1b[D";
  if(s!==null){e.preventDefault();raw(s)}
};

// ---- log ----
function logLine(l,text){
  var d=document.createElement("pre");d.className=l;d.textContent=text;
  var el=$("log"),stick=el.scrollTop+el.clientHeight>=el.scrollHeight-30;
  el.appendChild(d);while(el.childNodes.length>400)el.removeChild(el.firstChild);
  if(stick)el.scrollTop=el.scrollHeight;
}
$("clearLog").onclick=function(){$("log").textContent=""};

// ---- ask (username / password) ----
function ask(secret,label){
  $("asklabel").textContent=label+":";$("askinput").type=secret?"password":"text";$("askinput").value="";
  $("askbox").style.display="flex";$("askinput").focus();
}
function answer(){send("a"+$("askinput").value);$("askbox").style.display="none"}
$("askok").onclick=answer;
$("askinput").onkeydown=function(e){if(e.key==="Enter")answer()};
$("askcancel").onclick=function(){send("x");$("askbox").style.display="none"};

// ---- reset panel ----
function showProfile(){
  var p=profiles[$("profile").value];if(!p)return;
  $("phint").textContent=p.hint+" (baud "+p.baud+")";
  var sel=$("mode");sel.textContent="";
  p.modes.forEach(function(m,i){var o=document.createElement("option");o.value=i;o.textContent=m.name;sel.appendChild(o)});
  showMode();
}
function showMode(){
  var p=profiles[$("profile").value];if(!p)return;
  var m=p.modes[$("mode").value||0];if(!m)return;
  $("mdesc").textContent=m.desc;
  var ol=$("psteps");ol.textContent="";
  m.steps.forEach(function(s){var li=document.createElement("li");li.textContent=s.d+" ";var c=document.createElement("code");c.textContent=s.c;li.appendChild(c);ol.appendChild(li)});
}
$("profile").onchange=showProfile;
$("mode").onchange=showMode;
$("yes").oninput=updateButtons;
$("start").onclick=function(){
  if($("yes").value.trim().toUpperCase()!=="YES")return;
  $("yes").value="";updateButtons();send("s"+$("profile").value+","+($("mode").value||0));
};
$("abort").onclick=function(){send("x")};

fetch("/api/profiles").then(function(r){return r.json()}).then(function(j){
  profiles=j;var sel=$("profile");
  j.forEach(function(p,i){var o=document.createElement("option");o.value=i;o.textContent=p.name;sel.appendChild(o)});
  showProfile();updateButtons();
}).catch(function(){logLine("e","Could not load the switch list.")});
fetch("/api/info").then(function(r){return r.json()}).then(function(j){if(j.defaultPassword)$("defpw").style.display="block"}).catch(function(){});

updateButtons();connect();
})();
</script></body></html>)rawliteral";
