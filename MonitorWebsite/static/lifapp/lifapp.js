(() => {
  'use strict';
  // 与 STM32 MorseDecoder 的 morse_table 完全一致（39 字符，无 "!"）。
  const MORSE = {
    A: '.-', B: '-...', C: '-.-.', D: '-..', E: '.', F: '..-.', G: '--.', H: '....',
    I: '..', J: '.---', K: '-.-', L: '.-..', M: '--', N: '-.', O: '---', P: '.--.',
    Q: '--.-', R: '.-.', S: '...', T: '-', U: '..-', V: '...-', W: '.--', X: '-..-',
    Y: '-.--', Z: '--..',
    '0': '-----', '1': '.----', '2': '..---', '3': '...--', '4': '....-',
    '5': '.....', '6': '-....', '7': '--...', '8': '---..', '9': '----.',
    '+': '.-.-.', '=': '-...-', '.': '.-.-.-', ',': '--..--', '?': '..--..', '/': '-..-.', '-': '-....-'
  };
  const BOUNDS = { t_ms: [60, 2000], dash_t: [2, 5], word_gap_t: [2, 8], msg_gap_t: [4, 20] };
  // ITU 标准摩斯比例：点=1T（100ms）、划=3T、字内隙=1T、字间/词间=7T、消息间=10T。
  const STANDARD = { t_ms: 100, dash_t: 3, word_gap_t: 7, msg_gap_t: 10 };
  const $ = (id) => document.getElementById(id);
  const state = { cfg: null, sending: false };

  const el = {
    protocolState: $('protocol-state'), protocolParams: $('protocol-params'),
    protocolToken: $('protocol-token'), refreshBtn: $('refresh-btn'),
    input: $('msg-input'), error: $('msg-error'), preview: $('msg-preview'),
    brightness: $('brightness'), brightnessLabel: $('brightness-label'),
    transmit: $('transmit-btn'), stop: $('stop-btn'),
    txStatus: $('tx-status'), timingHint: $('timing-hint'), deviceMode: $('device-mode'),
    set: { t: $('set-t'), d: $('set-d'), wg: $('set-wg'), mg: $('set-mg') },
    setError: $('set-error'), setSuccess: $('set-success'), save: $('save-btn'), standard: $('standard-btn'),
    overlay: $('flash-overlay')
  };

  // ---------- 闪光灯（torch）管理 ----------
  // 通过 getUserMedia 获取后置摄像头轨道，用 torch 约束驱动手机真实闪光灯精确开/关
  // （Chrome for Android 支持）。不支持 torch 的设备（如 iOS Safari）或用户拒绝
  // 摄像头权限时，自动回退到全屏白闪覆盖层。
  // 摄像头轨道在会话内保持打开（仅用于 torch 控制，不取景、不上送任何画面），
  // 使下一次发送无需重新授权与初始化。
  const torch = { stream: null, track: null, supported: null, acquiring: null, dead: false };

  function torchUsable() {
    return torch.supported === true && !torch.dead && !!torch.track && torch.track.readyState === 'live';
  }

  function updateDeviceMode() {
    if (torch.supported === null) {
      el.deviceMode.textContent = '输出设备：首次发送时自动检测（torch 需摄像头权限）';
      return;
    }
    if (torchUsable()) {
      el.deviceMode.textContent = '输出设备：手机闪光灯（torch 精确开关）';
      el.brightness.disabled = true;
      el.brightnessLabel.textContent = '闪光亮度（仅屏幕白闪模式生效）';
    } else {
      el.deviceMode.textContent = '输出设备：屏幕白闪（此设备不可用 torch，已回退）';
      el.brightness.disabled = false;
      el.brightnessLabel.textContent = '闪光亮度';
    }
  }

  function ensureStream() {
    if (torch.supported !== null) return Promise.resolve();
    if (!torch.acquiring) {
      torch.acquiring = (async () => {
        try {
          if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) throw new Error('浏览器不支持摄像头 API');
          torch.stream = await navigator.mediaDevices.getUserMedia({
            video: { facingMode: 'environment' }, audio: false
          });
          torch.track = torch.stream.getVideoTracks()[0];
          torch.supported = !!(torch.track.getCapabilities && torch.track.getCapabilities().torch);
        } catch (e) {
          torch.stream = null;
          torch.track = null;
          torch.supported = false;
        }
        updateDeviceMode();
      })();
    }
    return torch.acquiring;
  }

  function overlayFlash(on) {
    // Web Animations API：单次两帧动画把覆盖层亮度精确打到目标值（回退方案）。
    el.overlay.animate([{ opacity: 0 }, { opacity: on ? 1 : 0 }], {
      duration: 1, easing: 'steps(1, end)', fill: 'forwards'
    });
  }

  function flash(on) {
    if (torchUsable()) {
      // fire-and-forget：不等 applyConstraints 完成，避免抖动 rAF 计时循环。
      // 某一条边失败则标记 torch 死亡，整体切到屏幕白闪（保证后续边一致）。
      torch.track.applyConstraints({ advanced: [{ torch: on }] }).catch(() => {
        torch.dead = true;
        updateDeviceMode();
        overlayFlash(on);
      });
    } else {
      overlayFlash(on);
    }
  }

  async function setTorchOff() {
    if (!torchUsable()) return;
    try {
      await torch.track.applyConstraints({ advanced: [{ torch: false }] });
    } catch (e) {
      torch.dead = true;
      updateDeviceMode();
    }
  }

  // ---------- 协议 ----------
  async function tokenOf(t, d, wg, mg) {
    const bytes = new TextEncoder().encode(`${t}:${d}:${wg}:${mg}`);
    const hex = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)))
      .map((b) => b.toString(16).padStart(2, '0')).join('');
    return hex.slice(0, 16);
  }

  function renderProtocolCard() {
    const { t_ms, dash_t, word_gap_t, msg_gap_t } = state.cfg;
    el.protocolParams.textContent = `t=${t_ms} ms · 划=${dash_t}·T · 字隙=${word_gap_t}·T · 消息隙=${msg_gap_t}·T`;
    el.protocolToken.textContent = el.protocolToken.textContent || '—';
  }

  async function refreshProtocol() {
    el.protocolState.className = 'badge';
    el.protocolState.textContent = '读取中';
    try {
      const response = await fetch('/app/api/protocol');
      const data = await response.json();
      if (!response.ok || !data || typeof data.t_ms !== 'number') throw new Error((data && data.detail) || '协议读取失败');
      state.cfg = { t_ms: data.t_ms, dash_t: data.dash_t, word_gap_t: data.word_gap_t, msg_gap_t: data.msg_gap_t };
      el.protocolToken.textContent = data.token || '—';
      renderProtocolCard();
      const local = await tokenOf(state.cfg.t_ms, state.cfg.dash_t, state.cfg.word_gap_t, state.cfg.msg_gap_t);
      const ok = local === data.token;
      el.protocolState.className = 'badge ' + (ok ? 'ok' : 'err');
      el.protocolState.textContent = ok ? '已同步' : '配置漂移';
      // 同步后回填设置表单，避免表单与服务器值脱节。
      el.set.t.value = state.cfg.t_ms;
      el.set.d.value = state.cfg.dash_t;
      el.set.wg.value = state.cfg.word_gap_t;
      el.set.mg.value = state.cfg.msg_gap_t;
      renderTimingHint();
    } catch (e) {
      state.cfg = null;
      el.protocolState.className = 'badge err';
      el.protocolState.textContent = '未同步';
      el.protocolParams.textContent = e.message || '协议读取失败';
      el.protocolToken.textContent = '—';
      renderTimingHint();
    }
  }

  // ---------- 编码与时序 ----------
  function unsupported(text) {
    // 空格是合法的分词符（词间空格），不属于"不支持的字符"。
    // 旧版把空格也当成不支持字符拦截，导致任何多词句子都发不出去，
    // 且报错信息里的空格渲染为空白，用户看不到具体是哪个字符。
    return [...text].filter((ch) => ch !== ' ' && !(ch in MORSE)).filter((ch, i, arr) => arr.indexOf(ch) === i);
  }

  function encodeText(text) {
    return text.split(' ').filter(Boolean).map((word) => [...word].map((ch) => MORSE[ch]));
  }

  // 生成 [{on: 0|1, ms: n}] 时序事件。
  // 契约（MorseDecoder.c）：
  //  - 亮段：点=T 恒判点、划=dash_t·T 恒判划（边界 (1+dash_t)/2·T）；
  //  - 暗段 ≥ 2T：当前字母完成（flush）；
  //  - rising edge 时暗段 ≥ word_gap_t·T：词间空格（SPACE）；
  //  - 暗段 ≥ msg_gap_t·T：IDLE，本条消息结束。
  // 因此词内字母间隙必须落在开区间 (2T, word_gap_t·T)，词间隙必须落在
  // (word_gap_t·T, msg_gap_t·T)。两者均取开区间中点，给手机 rAF/torch
  // 计时抖动留出两侧余量。旧版把词内字母间隙也写成 word_gap_t·T，解码端
  // 会把它判成词间空格，导致句子每个字母之间都被插入空格。
  // 另：解码器 Init/Reset 后处于未同步态，须经历 ≥2T 暗段才同步并开始
  // 记录脉冲；发送前必须给 ≥2T 引导暗段，否则首字符脉冲被同步过程吃掉。
  function gapPlan() {
    const { t_ms: t, dash_t: d, word_gap_t: wg, msg_gap_t: mg } = state.cfg;
    return {
      lead: Math.round(2.5 * t),                   // 引导暗段：> 2T 保证解码器在首脉冲前完成同步
      dot: t,
      dash: d * t,
      symbol: t,                                   // 字符内点/划之间：1T（< 2T 不触发 flush）
      letter: Math.round(2.5 * t),                 // 词内字母间隙：严格位于 (2T, wg·T)，需 wg ≥ 3
      word: Math.round(((wg + mg) / 2) * t),       // 词间隙：严格位于 (wg·T, mg·T)，需 mg > wg
      end: mg * t                                  // 结尾暗段：≥ mg·T 触发 IDLE（暗段阈值，逐 ms 判定，边界可靠）
    };
  }

  function buildEvents(text) {
    const g = gapPlan();
    const events = [];
    // 引导暗段（torch 本就熄灭，此段用于计时对齐）：让解码器先完成同步。
    events.push({ on: 0, ms: g.lead });
    const words = encodeText(text);
    for (let w = 0; w < words.length; w++) {
      for (let c = 0; c < words[w].length; c++) {
        const chars = words[w][c];
        for (let i = 0; i < chars.length; i++) {
          events.push({ on: 1, ms: chars[i] === '-' ? g.dash : g.dot });
          if (i < chars.length - 1) events.push({ on: 0, ms: g.symbol });
        }
        if (c < words[w].length - 1) events.push({ on: 0, ms: g.letter });
      }
      if (w < words.length - 1) events.push({ on: 0, ms: g.word });
    }
    events.push({ on: 0, ms: g.end });
    return events;
  }

  function renderTimingHint() {
    if (!state.cfg) {
      el.timingHint.textContent = '点击发送后保持闪光灯正对探测器；点时长与划长随当前协议实时生成。';
      return;
    }
    const g = gapPlan();
    el.timingHint.textContent =
      `点击发送后保持闪光灯正对探测器。当前时序：引导暗段 ${g.lead}ms · 点 ${g.dot}ms · 划 ${g.dash}ms · 字母间隙 ${g.letter}ms · 词间隙 ${g.word}ms · 结束暗段 ${g.end}ms。`;
  }

  function showPreview(text) {
    const words = text.split(' ').filter(Boolean);
    const lines = words.map((w) => `${w}  →  ${encodeText(w).join(' ')}`);
    el.preview.hidden = false;
    el.preview.textContent = lines.join('\n');
  }

  function clearPreview() {
    el.preview.hidden = true;
    el.preview.textContent = '';
  }

  function showMsgError(message) {
    el.error.hidden = false;
    el.error.textContent = message;
  }

  function clearMsgError() {
    el.error.hidden = true;
    el.error.textContent = '';
  }

  let stopSignal = null;
  let txRaf = 0;

  function setSendingUI(on) {
    state.sending = on;
    el.transmit.disabled = on || !state.cfg;
    el.stop.hidden = !on;
    el.txStatus.hidden = !on;
  }

  async function transmit() {
    const text = el.input.value.toUpperCase().replace(/[^A-Z0-9=.,?/-]/g, ' ');
    if (!text) { showMsgError('请输入要发送的内容'); return; }
    if (!state.cfg) { showMsgError('请先同步协议参数'); return; }
    // 参数可行性校验：解码器空格阈值为 word_gap_t·T、字母结束阈值为 2T，
    // word_gap_t=2 时两个阈值重合，字母间隙无论取多少都会被判成空格；
    // msg_gap_t ≤ word_gap_t 时词间隙会触发 IDLE，句子在词处被截断。
    const { word_gap_t: wg, msg_gap_t: mg } = state.cfg;
    if (wg < 3) {
      showMsgError('当前 word_gap_t=2：空格阈值（2T）与字母结束阈值重合，多字母单词/句子必然错乱。请把 word_gap_t 调到 ≥3（或直接点「一键标准格式」），并同步 STM32/网关端后重新同步。');
      return;
    }
    if (mg <= wg) {
      showMsgError('msg_gap_t 必须大于 word_gap_t，否则词间隙会触发 IDLE，消息会在词处被截断。');
      return;
    }
    const bad = unsupported(text);
    if (bad.length) { showMsgError('不支持的字符：' + bad.map((ch) => (ch === ' ' ? '␣(空格)' : ch.replace('<', '‹'))).join(' ')); return; }
    clearMsgError();
    showPreview(text);
    setSendingUI(true);
    stopSignal = { stopped: false };
    // 首次发送时触发摄像头授权与 torch 能力检测（需 HTTPS）。
    await ensureStream();
    // 发送前确保闪光灯处于熄灭状态，建立干净的起始电平。
    await setTorchOff();
    flash(0);
    try {
      const events = buildEvents(text);
      const start = performance.now();
      await new Promise((resolve) => {
        const tick = (now) => {
          if (stopSignal.stopped) { resolve(); return; }
          let elapsed = now - start;
          for (let i = 0; i < events.length; i++) {
            const e = events[i];
            if (elapsed < e.ms) {
              flash(e.on);
              txRaf = requestAnimationFrame(tick);
              return;
            }
            elapsed -= e.ms;
          }
          resolve();
        };
        txRaf = requestAnimationFrame(tick);
      });
    } finally {
      cancelAnimationFrame(txRaf);
      // 收尾必须显式熄灭 torch，避免闪光灯长亮。
      await setTorchOff();
      flash(0);
      setSendingUI(false);
      el.txStatus.textContent = stopSignal.stopped ? '已手动停止' : '发送完成，可再次点击发送';
    }
  }

  function stopTransmit() {
    if (stopSignal) stopSignal.stopped = true;
  }

  // ---------- 协议参数设置 ----------
  function readSettings() {
    const out = {};
    for (const key of Object.keys(BOUNDS)) {
      const input = el.set[key === 't_ms' ? 't' : key === 'dash_t' ? 'd' : key === 'word_gap_t' ? 'wg' : 'mg'];
      if (input.value === '') return { value: null, problem: `请填写${key}` };
      if (!/^-?\d+$/.test(input.value)) return { value: null, problem: `${key} 必须是整数` };
      out[key] = parseInt(input.value, 10);
    }
    for (const [key, [low, high]] of Object.entries(BOUNDS)) {
      if (out[key] < low || out[key] > high) return { value: null, problem: `${key} 需在 ${low}–${high} 之间` };
    }
    return { value: out, problem: null };
  }

  function setMsg(box, ok, message) {
    box.hidden = !message;
    box.textContent = message || '';
    if (ok !== null) box.className = ok ? 'success' : 'error';
  }

  async function saveSettings() {
    const parsed = readSettings();
    if (!parsed.value) { setMsg(el.setError, false, parsed.problem); setMsg(el.setSuccess, false, ''); return; }
    const cfg = parsed.value;
    setMsg(el.setError, false, '');
    setMsg(el.setSuccess, false, '保存中…');
    el.save.disabled = true;
    el.standard.disabled = true;
    try {
      const response = await fetch('/app/api/protocol', {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(cfg)
      });
      const data = await response.json().catch(() => ({}));
      if (!response.ok || !data || typeof data.t_ms !== 'number') throw new Error((data && data.detail) || '保存失败');
      const local = await tokenOf(cfg.t_ms, cfg.dash_t, cfg.word_gap_t, cfg.msg_gap_t);
      if (local !== data.token) throw new Error('配置未同步：服务器令牌与本地重算不一致');
      // 保存成功后直接刷新本地协议状态，无需手动点"重新同步"。
      state.cfg = { t_ms: data.t_ms, dash_t: data.dash_t, word_gap_t: data.word_gap_t, msg_gap_t: data.msg_gap_t };
      el.protocolToken.textContent = data.token || '—';
      renderProtocolCard();
      renderTimingHint();
      el.protocolState.className = 'badge ok';
      el.protocolState.textContent = '已同步';
      setMsg(el.setSuccess, true, '已保存，令牌比对通过');
      setMsg(el.setError, false, '');
    } catch (e) {
      setMsg(el.setError, false, e.message || '保存失败');
    } finally {
      el.save.disabled = false;
      el.standard.disabled = false;
    }
  }

  // 一键标准格式：填入 ITU 标准摩斯比例并保存（含令牌比对）。
  async function applyStandard() {
    el.set.t.value = STANDARD.t_ms;
    el.set.d.value = STANDARD.dash_t;
    el.set.wg.value = STANDARD.word_gap_t;
    el.set.mg.value = STANDARD.msg_gap_t;
    await saveSettings();
  }

  el.refreshBtn.addEventListener('click', refreshProtocol);
  el.transmit.addEventListener('click', transmit);
  el.stop.addEventListener('click', stopTransmit);
  el.save.addEventListener('click', saveSettings);
  el.standard.addEventListener('click', applyStandard);
  el.brightness.addEventListener('input', () => {
    el.overlay.style.filter = `brightness(${el.brightness.value}%)`;
  });
  el.input.addEventListener('input', () => {
    clearMsgError();
    const text = el.input.value.toUpperCase().replace(/[^A-Z0-9=.,?/-]/g, ' ');
    if (text) showPreview(text); else clearPreview();
  });
  // 页面关闭时释放摄像头轨道。
  window.addEventListener('pagehide', () => {
    if (torch.stream) {
      torch.stream.getTracks().forEach((t) => t.stop());
      torch.stream = null;
    }
  });

  el.transmit.disabled = true;
  updateDeviceMode();
  renderTimingHint();
  refreshProtocol().then(() => { el.transmit.disabled = !state.cfg; });
})();
