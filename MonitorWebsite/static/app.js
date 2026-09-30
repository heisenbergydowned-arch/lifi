'use strict';
const $ = id => document.getElementById(id);
const state = {csrf:'', view:'overview', kind:'all', page:1, items:[], total:0, dashboard:null, busy:false, formLoaded:false, protocolLoaded:false, sound:false, seenAlarm:0, rendered:'', testKey:null, testComplete:false};
const mailLabels = {pending:'排队中', sending:'发送中', sent:'服务已接受', failed:'发送失败', uncertain:'结果待确认', waiting_config:'待配置发信', paused:'通知已暂停'};
let audioContext, toastTimer, searchTimer;
function esc(value){return String(value ?? '').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function date(value, timeOnly=false){if(!value)return 'RTC 未校时';return new Intl.DateTimeFormat('zh-CN',{timeZone:'Asia/Shanghai',...(timeOnly?{}:{year:'numeric',month:'2-digit',day:'2-digit'}),hour:'2-digit',minute:'2-digit',second:'2-digit',hour12:false}).format(new Date(value));}
function toast(message){$('toast').textContent=message;$('toast').hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').hidden=true,4500);}
async function api(path,{method='GET',data}={}){
  const response=await fetch(path,{method,credentials:'same-origin',headers:{...(data!==undefined?{'Content-Type':'application/json'}:{}),...(method!=='GET'&&state.csrf?{'X-CSRF-Token':state.csrf}:{})},...(data!==undefined?{body:JSON.stringify(data)}:{})});
  const result=await response.json().catch(()=>({detail:'服务器响应异常'}));
  if(!response.ok){if(response.status===401&&path!=='/api/auth/login'){state.csrf='';showLogin();}throw new Error(typeof result.detail==='string'?result.detail:'请求未完成，请稍后重试');}return result;
}
function showLogin(){$('login').hidden=false;$('workspace').hidden=true;$('detail').close();}
function showWorkspace(){$('login').hidden=true;$('workspace').hidden=false;$('login-password').value='';state.formLoaded=false;refresh();}
$('login-form').addEventListener('submit',async e=>{e.preventDefault();const button=e.submitter;button.disabled=true;$('login-error').textContent='';try{const r=await api('/api/auth/login',{method:'POST',data:{password:$('login-password').value}});state.csrf=r.csrf;showWorkspace();}catch(error){$('login-error').textContent=error.message;}finally{button.disabled=false;}});
async function logout(){try{await api('/api/auth/logout',{method:'POST'});state.csrf='';showLogin();}catch(e){toast(e.message);}}
$('logout').addEventListener('click',logout);
function view(name){
  state.view=name;state.page=1;
  const names={overview:'监测概览',messages:'消息记录',alarms:'求救告警',settings:'系统设置'};
  $('page-title').textContent=names[name];
  $('feed-title').textContent=name==='overview'?'最近消息':name==='alarms'?'求救记录':'消息记录';
  document.querySelectorAll('[data-view]').forEach(b=>{
    const active=b.dataset.view===name;b.classList.toggle('active',active);
    if(active)b.setAttribute('aria-current','page');else b.removeAttribute('aria-current');
  });
  $('overview-panel').hidden=name!=='overview';
  if($('overview-aside'))$('overview-aside').hidden=name!=='overview';
  if($('overview-layout')){
    $('overview-layout').hidden=name==='settings';
    $('overview-layout').classList.toggle('overview-active',name==='overview');
  }
  $('messages-panel').hidden=name==='settings';
  if($('view-messages'))$('view-messages').hidden=name!=='overview';
  $('settings-panel').hidden=name!=='settings';
  if(name==='alarms')state.kind='alarm';else if(name!=='settings')state.kind='all';
  if(name==='settings'&&!state.protocolLoaded)loadProtocol();
  syncFilters();refresh();
}
async function loadProtocol(){
  try{const p=await api('/app/api/protocol');$('proto-t-ms').value=p.t_ms;$('proto-dash-t').value=p.dash_t;$('proto-word-gap-t').value=p.word_gap_t;$('proto-msg-gap-t').value=p.msg_gap_t;$('protocol-token').textContent=p.token;state.protocolLoaded=true;}catch(e){toast(e.message);}
}
$('protocol-form').addEventListener('submit',async e=>{e.preventDefault();e.submitter.disabled=true;
  const data={t_ms:Number($('proto-t-ms').value),dash_t:Number($('proto-dash-t').value),word_gap_t:Number($('proto-word-gap-t').value),msg_gap_t:Number($('proto-msg-gap-t').value)};
  try{if(!Object.values(data).every(Number.isInteger)){toast('四项参数都必须是整数');return;}
  const r=await api('/app/api/protocol',{method:'PUT',data});$('protocol-token').textContent=r.token;toast('协议参数已保存，新令牌 '+r.token);state.protocolLoaded=true;}catch(err){toast(err.message);}finally{e.submitter.disabled=false;}});
document.querySelectorAll('[data-view]').forEach(b=>b.addEventListener('click',()=>view(b.dataset.view)));
$('show-alarms').addEventListener('click',()=>view('alarms'));
if($('view-notices'))$('view-notices').addEventListener('click',()=>view('settings'));
if($('view-messages'))$('view-messages').addEventListener('click',()=>view('messages'));
function syncFilters(){$('feed-title').textContent=state.kind==='alarm'?'求救记录':state.view==='overview'?'最近消息':'消息记录';document.querySelectorAll('[data-kind]').forEach(b=>{const selected=b.dataset.kind===state.kind;b.classList.toggle('selected',selected);b.setAttribute('aria-pressed',String(selected));});}
document.querySelectorAll('[data-kind]').forEach(b=>b.addEventListener('click',()=>{state.kind=b.dataset.kind;state.page=1;syncFilters();refresh();}));
$('search').addEventListener('input',()=>{clearTimeout(searchTimer);searchTimer=setTimeout(()=>{state.page=1;refresh();},250);});
$('refresh').addEventListener('click',()=>refresh(true));
$('prev').addEventListener('click',()=>{if(state.page>1){state.page--;refresh();}});
$('next').addEventListener('click',()=>{if(state.page*25<state.total){state.page++;refresh();}});
function beep(){if(!audioContext)return;const start=audioContext.currentTime;[0,.25,.5].forEach(t=>{const osc=audioContext.createOscillator(),gain=audioContext.createGain();osc.type='sine';osc.frequency.value=880;gain.gain.setValueAtTime(.07,start+t);gain.gain.exponentialRampToValueAtTime(.001,start+t+.15);osc.connect(gain);gain.connect(audioContext.destination);osc.start(start+t);osc.stop(start+t+.17);});}
$('sound').addEventListener('click',async()=>{try{state.sound=!state.sound;if(state.sound){audioContext=audioContext||new(window.AudioContext||window.webkitAudioContext)();await audioContext.resume();beep();}($('sound-label')||$('sound')).textContent='声音提醒：'+(state.sound?'开':'关');$('sound').setAttribute('aria-pressed',String(state.sound));}catch{state.sound=false;($('sound-label')||$('sound')).textContent='声音提醒：关';$('sound').setAttribute('aria-pressed','false');toast('浏览器暂不支持声音提醒');}});
function renderDashboard(d){
  state.dashboard=d;$('stat-total').textContent=d.stats.total;$('stat-alarms').textContent=d.stats.pending_alarms;$('stat-devices').textContent=d.devices.length;$('stat-mail').textContent=d.stats.mail_sent;
  $('nav-alarm-count').textContent=d.stats.pending_alarms;$('nav-alarm-count').hidden=!d.stats.pending_alarms;$('alarm-banner').hidden=!d.stats.pending_alarms;$('alarm-summary').textContent=`有 ${d.stats.pending_alarms} 条求救消息等待处理`;
  if(state.sound&&d.stats.pending_alarms>state.seenAlarm)beep();state.seenAlarm=d.stats.pending_alarms;
  $('devices').innerHTML=d.devices.length?d.devices.map(x=>`<div class="device"><div><strong>接收端 ${String(x.address).padStart(3,'0')}</strong><span class="pill">${x.messages?'有上报记录':'待首次上报'}</span></div><p>${x.last_seen?'最近上报 '+esc(date(x.last_seen)):'尚未收到该设备的数据'}</p></div>`).join(''):'<p class="muted">暂无已配置的接收端</p>';
  $('recipient').textContent=d.mail.recipient;$('mail-config-status').textContent=d.mail.configured?(d.mail.enabled?'已配置':'已暂停'):'待配置';
  if(!state.formLoaded){$('provider').value=d.mail.provider;$('sender').value=d.mail.sender;$('mail-enabled').checked=d.mail.enabled;state.formLoaded=true;}
  $('smtp-password').placeholder=d.mail.configured?'已保存，留空可保持原授权码':'输入邮箱授权码';$('password-hint').textContent=d.mail.configured?'授权码已保存到服务器，不会回显。':'使用邮箱生成的授权码，不是 QQ 登录密码。';
  $('send-test').disabled=!d.mail.configured||!d.mail.enabled;
  $('mail-history').innerHTML=d.outbox.length?d.outbox.map(x=>`<div><div><span class="badge ${['failed','uncertain'].includes(x.status)?'red':x.status==='sent'?'':'gray'}">${esc(mailLabels[x.status]||x.status)}</span><small>${esc(date(x.sent_at||x.created_at))}</small></div>${x.last_error?`<p>${esc(x.last_error)}</p>`:''}</div>`).join(''):'<p class="muted">暂时没有通知记录</p>';
  if($('recent-notices'))$('recent-notices').innerHTML=d.outbox.length?d.outbox.slice(0,3).map(x=>`<div class="recent-notice"><div class="notice-line"><span class="badge ${['failed','uncertain'].includes(x.status)?'red':x.status==='sent'?'':'gray'}">${esc(mailLabels[x.status]||x.status)}</span><time datetime="${esc(x.sent_at||x.created_at)}">${esc(date(x.sent_at||x.created_at))}</time></div>${x.last_error?`<p>${esc(x.last_error)}</p>`:''}</div>`).join(''):'<p class="muted">暂无邮件通知记录</p>';
  $('version').textContent=d.version;$('sync-time').textContent='最近同步 '+date(d.server_time,true)+' · 北京时间';
  if(state.testKey){const out=d.outbox.find(x=>x.event_id==='test-'+state.testKey);if(out){state.testComplete=['sent','failed'].includes(out.status);$('send-test').textContent=state.testComplete?'再次发送测试邮件':'发送测试邮件';$('test-result').textContent='测试邮件：'+(mailLabels[out.status]||out.status)+(out.last_error?' · '+out.last_error:'');}}
}
function renderMessages(data){
  state.items=data.items;state.total=data.total;
  const filtered=Boolean($('search').value.trim())||state.kind!=='all';
  $('empty').querySelector('h3').textContent=filtered?'没有匹配的消息':'暂无消息';
  $('empty').querySelector('p').textContent=filtered?'试试其他关键词或消息类型。':'接收端上报后将在这里显示。';
  const signature=JSON.stringify([state.view,data]);if(signature===state.rendered)return;state.rendered=signature;
  const preview=state.view==='overview';
  const items=preview?data.items.slice(0,5):data.items;
  const pagination=$('messages-panel').querySelector('.pagination');
  if(pagination)pagination.hidden=preview;
  $('record-count').textContent=preview&&data.total>5?`最近 5 条 · 共 ${data.total} 条`:`${data.total} 条记录`;$('empty').hidden=!!items.length;
  $('message-list').innerHTML=items.map(x=>{
    const label=x.is_test?'系统测试':x.alarm?(x.acknowledged_at?'求救 · 已确认':'求救 · 待处理'):(!x.decode_valid||x.truncated?'译码异常':'普通消息');
    return `<tr><td>${x.received_at?esc(date(x.received_at,true)):'未校时'}<small>${esc(date(x.ingested_at))} 上报</small></td><td>${String(x.address).padStart(3,'0')} <small>#${x.message_id} · T=${x.t_ms} ms</small></td><td class="text-cell">${esc(x.text)}</td><td><span class="badge ${x.is_test?'gray':x.alarm?(x.acknowledged_at?'gray':'red'):!x.decode_valid||x.truncated?'gray':''}">${label}</span></td><td><span class="muted">${esc(x.mail_status?mailLabels[x.mail_status]:'—')}</span></td><td><button class="text-button" data-detail="${x.id}">详情</button></td></tr>`;
  }).join('');$('page-info').textContent=`第 ${data.page} / ${Math.max(1,Math.ceil(data.total/25))} 页`;$('prev').disabled=state.page<=1;$('next').disabled=state.page*25>=data.total;
}
let refreshAgain=false;
async function refresh(manual=false){
  if(!state.csrf)return;if(state.busy){refreshAgain=true;return;}state.busy=true;$('refresh').disabled=true;
  try{const query=new URLSearchParams({kind:state.kind,q:$('search').value,page:String(state.page)});const[d,m]=await Promise.all([api('/api/dashboard'),api('/api/messages?'+query)]);renderDashboard(d);renderMessages(m);$('connection').innerHTML='<span class="status-dot"></span>同步正常';$('connection-error').hidden=true;if(manual)toast('已更新到最新数据');}
  catch(e){$('connection-error').hidden=false;$('connection').textContent='连接中断';if(manual)toast(e.message);}
  finally{state.busy=false;$('refresh').disabled=false;if(refreshAgain){refreshAgain=false;refresh();}}
}
$('message-list').addEventListener('click',e=>{const b=e.target.closest('[data-detail]');if(b)detail(Number(b.dataset.detail));});
function detail(id){const x=state.items.find(x=>x.id===id);if(!x)return;$('detail-body').innerHTML=`<pre>${esc(x.text)}</pre><dl><dt>记录类型</dt><dd>${x.is_test?'系统测试 · 非硬件采集':x.alarm?'求救消息':'接收端消息'}</dd><dt>地址 / 编号</dt><dd>${x.address} / #${x.message_id} · 启动计数 ${x.boot}</dd><dt>RTC 接收时间</dt><dd>${esc(date(x.received_at))}</dd><dt>服务器入库</dt><dd>${esc(date(x.ingested_at))}</dd><dt>最小时间 T</dt><dd>${x.t_ms} ms</dd><dt>消息完整性</dt><dd>${x.decode_valid?'译码有效':'译码含异常'} / ${x.truncated?'已截断':'未截断'}</dd><dt>邮件状态</dt><dd>${esc(x.mail_status?mailLabels[x.mail_status]:'无通知任务')}${x.mail_error?'<br>'+esc(x.mail_error):''}</dd><dt>告警确认</dt><dd>${x.acknowledged_at?esc(date(x.acknowledged_at)):x.alarm?'等待处理':'无需处理'}</dd><dt>事件编号</dt><dd>${esc(x.event_id)}</dd></dl>`;$('detail-actions').innerHTML=x.alarm&&!x.acknowledged_at?`<button class="primary" id="ack-alarm">确认已处理此告警</button>`:'';if($('ack-alarm'))$('ack-alarm').onclick=async()=>{try{await api(`/api/messages/${id}/acknowledge`,{method:'POST'});$('detail').close();toast('已确认，记录继续保留');refresh();}catch(e){toast(e.message);}};$('detail').showModal();}
$('close-detail').addEventListener('click',()=>$('detail').close());
$('mail-form').addEventListener('submit',async e=>{e.preventDefault();e.submitter.disabled=true;try{await api('/api/settings/mail',{method:'POST',data:{provider:$('provider').value,sender:$('sender').value,password:$('smtp-password').value.trim(),enabled:$('mail-enabled').checked}});$('smtp-password').value='';state.formLoaded=false;toast('通知设置已保存');refresh();}catch(e){toast(e.message);}finally{e.submitter.disabled=false;}});
$('send-test').addEventListener('click',async()=>{const b=$('send-test');b.disabled=true;if(state.testComplete){state.testKey=null;state.testComplete=false;}state.testKey=state.testKey||crypto.randomUUID();try{const r=await api('/api/test-email',{method:'POST',data:{request_id:state.testKey}});$('test-result').textContent=r.duplicate?'这次测试已创建，不会重复发信。':'测试数据已保存，正在等待发信服务。';refresh();}catch(e){$('test-result').textContent=e.message;}finally{b.disabled=false;}});
setInterval(()=>{if(!document.hidden)refresh();},5000);
document.addEventListener('visibilitychange',()=>{if(!document.hidden)refresh();});
api('/api/auth/session').then(r=>{state.csrf=r.csrf;showWorkspace();}).catch(()=>showLogin());
