'use strict';
(() => {
  const status = document.getElementById('feedback');
  let hide;
  function announce(message) {
    clearTimeout(hide);
    status.textContent = message;
    status.classList.add('visible');
    hide = setTimeout(() => status.classList.remove('visible'), 2400);
  }
  async function copy(text) {
    if (navigator.clipboard && window.isSecureContext) {
      try { await navigator.clipboard.writeText(text); return; } catch (_) { /* Fallback below. */ }
    }
    const area = document.createElement('textarea');
    area.value = text;
    area.setAttribute('readonly', '');
    area.style.cssText = 'position:fixed;top:0;left:-10000px';
    document.body.append(area);
    area.select();
    let ok = false;
    try { ok = document.execCommand('copy'); } finally { area.remove(); }
    if (!ok) throw new Error('Clipboard unavailable');
  }
  document.querySelectorAll('[data-copy-target]').forEach(button => {
    button.addEventListener('click', async () => {
      const source = document.getElementById(button.dataset.copyTarget);
      if (!source) return;
      const previous = document.activeElement;
      try { await copy(source.textContent.trim()); announce('已复制到剪贴板'); }
      catch (_) { announce('复制未成功，请选中文本手动复制'); }
      finally { if (previous instanceof HTMLElement) previous.focus({preventScroll: true}); }
    });
  });
})();
