if (typeof marked !== 'undefined') {
  marked.setOptions({ breaks: true, gfm: true });
}
var INIT_DATA = {}; var CUR_MD = '';
try { INIT_DATA = JSON.parse(document.getElementById('init-data').textContent || '{}'); CUR_MD = INIT_DATA.raw_md || ''; } catch(e) {}
const DARK_MODE = !!INIT_DATA.is_dark;
var SHOW_ADV_DEBUG = (INIT_DATA.show_adv_debug !== false);
document.documentElement.classList.toggle('dark-mode', DARK_MODE);
document.body.classList.toggle('dark-mode', DARK_MODE);
try {
  if (INIT_DATA.mark_bg) document.documentElement.style.setProperty('--mark-bg', INIT_DATA.mark_bg);
  if (INIT_DATA.mark_bg_dark) document.documentElement.style.setProperty('--mark-bg-dark', INIT_DATA.mark_bg_dark);
} catch(e) {}
function log(m) { if (window.chrome&&window.chrome.webview) window.chrome.webview.postMessage(JSON.stringify(["log", m])); }
window.onerror = function(m,u,l,c,e) { log("JS ERR: "+m+" @ "+l+":"+c); };
var EDIT = false;
var CURRENT_DEBUG_INFO = '';
var COPY_TOAST_TIMER = 0;
var COPY_TOAST_TEXT = INIT_DATA.toast_raw_copied || INIT_DATA.btn_copied || 'Copied';
var COPY_SELECTION_TOAST_TEXT = INIT_DATA.toast_raw_selection_copied || COPY_TOAST_TEXT;
var RAW_SELECT_TRACK = { active: false, moved: false, startX: 0, startY: 0, startAt: 0 };

function isSafeUrl(url, allowDataImage) {
  var v = (url || '').trim().toLowerCase();
  if (!v) return true;
  if (v.indexOf('javascript:') === 0 || v.indexOf('vbscript:') === 0) return false;
  if (v.indexOf('data:') === 0) {
    return !!allowDataImage && v.indexOf('data:image/') === 0;
  }
  return (
    v.indexOf('http://') === 0 ||
    v.indexOf('https://') === 0 ||
    v.indexOf('mailto:') === 0 ||
    v.indexOf('tel:') === 0 ||
    v.indexOf('#') === 0 ||
    v.indexOf('/') === 0 ||
    v.indexOf('./') === 0 ||
    v.indexOf('../') === 0
  );
}

function sanitizeRenderedHtml(html) {
  var template = document.createElement('template');
  template.innerHTML = html || '';

  var allowedTags = {
    A:1, B:1, BLOCKQUOTE:1, BR:1, CODE:1, DEL:1, DIV:1, EM:1,
    H1:1, H2:1, H3:1, H4:1, H5:1, H6:1, HR:1, I:1, IMG:1, LI:1,
    MARK:1, OL:1, P:1, PRE:1, S:1, SPAN:1, STRONG:1, SUB:1, SUP:1,
    TABLE:1, TBODY:1, TD:1, TH:1, THEAD:1, TR:1, U:1, UL:1
  };
  var blockedTags = { SCRIPT:1, STYLE:1, IFRAME:1, OBJECT:1, EMBED:1, LINK:1, META:1 };
  var globalAttrs = { 'class':1, 'title':1, 'aria-label':1, 'aria-hidden':1 };

  function clean(parent) {
    var node = parent.firstChild;
    while (node) {
      var next = node.nextSibling;

      if (node.nodeType === 1) {
        var tag = node.tagName.toUpperCase();
        if (blockedTags[tag]) {
          node.remove();
          node = next;
          continue;
        }
        if (!allowedTags[tag]) {
          while (node.firstChild) parent.insertBefore(node.firstChild, node);
          node.remove();
          node = next;
          continue;
        }

        var attrs = Array.prototype.slice.call(node.attributes || []);
        for (var i = 0; i < attrs.length; i++) {
          var name = attrs[i].name;
          var lower = name.toLowerCase();
          var value = attrs[i].value || '';
          var keep = !!globalAttrs[lower];

          if (tag === 'A' && (lower === 'href' || lower === 'target' || lower === 'rel')) keep = true;
          if (tag === 'IMG' && (lower === 'src' || lower === 'alt')) keep = true;
          if ((tag === 'CODE' || tag === 'PRE' || tag === 'SPAN' || tag === 'DIV' || tag === 'P') && lower === 'class') keep = true;

          if (lower.indexOf('on') === 0 || lower === 'style' || lower === 'srcdoc') keep = false;
          if ((lower === 'href' || lower === 'src') && keep) {
            var allowDataImage = tag === 'IMG' && lower === 'src';
            if (!isSafeUrl(value, allowDataImage)) keep = false;
          }

          if (!keep) node.removeAttribute(name);
        }

        if (tag === 'A') {
          var href = (node.getAttribute('href') || '').trim();
          if (href) node.setAttribute('rel', 'noopener noreferrer');
        }

        clean(node);
      } else if (node.nodeType === 8) {
        node.remove();
      }

      node = next;
    }
  }

  clean(template.content || template);
  return template.innerHTML;
}

function splitDebugFromMarkdown(md) {
  var src = (typeof md === 'string') ? md : '';
  var re = /^```(?:text)?\r?\n\[Advanced API Debug\]\r?\n([\s\S]*?)\r?\n```\s*\r?\n?/;
  var m = src.match(re);
  if (!m) return { md: src, debug: '' };
  return { md: src.slice(m[0].length), debug: (m[1] || '').trim() };
}

function applyDebugBarVisibility() {
  var bar = document.getElementById('debug_bar');
  if (!bar) return;
  bar.style.display = SHOW_ADV_DEBUG ? '' : 'none';
}

function setDebugInfo(text) {
  var el = document.getElementById('debug_lines');
  if (!el) return;
  applyDebugBarVisibility();
  if (!SHOW_ADV_DEBUG) return;
  var v = (text || '').trim();
  if (!v) { el.textContent = 'debug: -'; return; }
  var lines = v.split(/\r?\n/).filter(function(line){ return line.trim().length > 0; });
  el.textContent = lines.length ? lines.join('\n') : 'debug: -';
}

function focusSink() {
  try {
    var kt = document.getElementById('keytrap');
    if (kt && kt.focus) {
      kt.focus({ preventScroll: true });
      return;
    }
    document.body.tabIndex = 0;
    document.body.focus({ preventScroll: true });
  } catch(e) {}
}
function isRawViewVisible() {
  var raw = document.getElementById('raw_view');
  if (!raw) return false;
  return window.getComputedStyle(raw).display !== 'none';
}
function showCopyToast() {
  var toast = document.getElementById('copy_toast');
  if (!toast) return;
  toast.textContent = arguments.length > 0 && arguments[0] ? arguments[0] : COPY_TOAST_TEXT;
  toast.classList.add('show');
  if (COPY_TOAST_TIMER) {
    clearTimeout(COPY_TOAST_TIMER);
    COPY_TOAST_TIMER = 0;
  }
  COPY_TOAST_TIMER = setTimeout(function() {
    toast.classList.remove('show');
    COPY_TOAST_TIMER = 0;
  }, 1200);
}
function syncRawView() {
  var raw = document.getElementById('raw_view');
  if (!raw) return;
  var parsed = splitDebugFromMarkdown(CUR_MD);
  raw.innerText = parsed.md || '';
}
function fallbackCopyText(text) {
  var sel = window.getSelection ? window.getSelection() : null;
  var savedRanges = [];
  if (sel) {
    for (var i = 0; i < sel.rangeCount; i++) {
      savedRanges.push(sel.getRangeAt(i).cloneRange());
    }
  }
  var ta = document.createElement('textarea');
  ta.value = text;
  ta.setAttribute('readonly', 'readonly');
  ta.style.position = 'fixed';
  ta.style.left = '-9999px';
  ta.style.top = '0';
  document.body.appendChild(ta);
  ta.focus();
  ta.select();
  var ok = false;
  try {
    ok = document.execCommand('copy');
  } catch (e) {}
  document.body.removeChild(ta);
  if (sel && savedRanges.length > 0) {
    try {
      sel.removeAllRanges();
      for (var j = 0; j < savedRanges.length; j++) {
        sel.addRange(savedRanges[j]);
      }
    } catch (e) {}
  }
  return ok;
}
function selectionInsideRawView() {
  var raw = document.getElementById('raw_view');
  var sel = window.getSelection ? window.getSelection() : null;
  if (!raw || !sel || sel.rangeCount === 0) return false;
  var range = sel.getRangeAt(0);
  return raw.contains(range.commonAncestorContainer);
}
window.copyTextWithToast = async function(text, toastText) {
  if (!text) return;
  var copied = false;
  try {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      await navigator.clipboard.writeText(text);
      copied = true;
    }
  } catch (e) {}
  if (!copied) copied = fallbackCopyText(text);
  if (copied) showCopyToast(toastText || COPY_TOAST_TEXT);
  return copied;
};
window.copyRawText = async function() {
  var parsed = splitDebugFromMarkdown(CUR_MD);
  var text = parsed.md || '';
  return window.copyTextWithToast(text, COPY_TOAST_TEXT);
};
window.copyRawSelection = async function() {
  if (!isRawViewVisible() || !selectionInsideRawView()) return false;
  var sel = window.getSelection ? window.getSelection() : null;
  var text = sel ? sel.toString() : '';
  if (!text || !text.trim()) return false;
  return window.copyTextWithToast(text, COPY_SELECTION_TOAST_TEXT);
};
function render(md, opts) {
  var parsed = splitDebugFromMarkdown(md || '');
  if (parsed.debug) CURRENT_DEBUG_INFO = parsed.debug;
  else if (!(opts && opts.keepDebug)) CURRENT_DEBUG_INFO = '';
  setDebugInfo(CURRENT_DEBUG_INFO);
  if (typeof marked==='undefined') return;
  // Step 1: Extract LaTeX from backticks (e.g., `$formula$` -> $formula$)
  // Only unwrap if the backticked content is a valid LaTeX expression (properly paired delimiters)
  var text = parsed.md || '';
  text = text.replace(/`([^`]+)`/g, function(match, inner) {
    // Check if content is a complete LaTeX expression with properly paired delimiters
    // Match: $...$, $$...$$, \(...\), or \[...\]
    // Using [\s\S]* to allow empty or any content including newlines
    if (/^(?:\$\$[\s\S]*?\$\$|\$[^\$\n]+\$|\\\([\s\S]*?\\\)|\\\[[\s\S]*?\\\])$/.test(inner.trim())) {
      return inner; // Unwrap backticks
    }
    return match; // Keep original backticks if not valid LaTeX
  });

  // Normalize common LaTeX display environments so KaTeX auto-render can process them
  // through the configured $$...$$ delimiters.
  text = text.replace(
    /\\begin\{(equation\*?|align\*?|gather\*?|multline\*?)\}([\s\S]*?)\\end\{\1\}/g,
    function(_match, _envName, body) {
      var inner = (body || '').trim();
      return '\n$$\n' + inner + '\n$$\n';
    }
  );
  
  // Step 2: Protect all LaTeX expressions before markdown parsing
  var prot = (function(t){
    var b=[], c=0, r=/(\$\$[\s\S]*?\$\$)|(\\\[[\s\S]*?\\\])|(\\\([\s\S]*?\\\))|(\$[^\$\n]+\$)/g;
    return {text: t.replace(r, function(m){ b.push(m); return 'MATHPH'+(c++); }), blocks: b};
  })(text);
  
  // Step 3: Parse markdown
  var h = marked.parse(prot.text);
  h = sanitizeRenderedHtml(h);
  
  // Step 4: Restore protected LaTeX expressions
  // Use regex replace for better performance and reliability with long strings
  // This prevents issues when content is very long or has many formulas
  // Restore in reverse order to avoid prefix collisions (e.g., MATHPH1 vs MATHPH10)
  for (var ri = prot.blocks.length - 1; ri >= 0; ri--) {
    var b = prot.blocks[ri];
    try {
      var placeholder = 'MATHPH' + ri;
      var escapedPlaceholder = placeholder.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
      var regex = new RegExp(escapedPlaceholder, 'g');
      h = h.replace(regex, function() { return b; });
    } catch (e) {
      console.error('Error restoring LaTeX block ' + ri + ':', e);
    }
  }
  
  // Step 5: Render to DOM
  var d = document.getElementById('content');
  if (d) {
    d.innerHTML = h;
    if (window.hljs) hljs.highlightAll();
    
    // Step 6: Render LaTeX with KaTeX
    if (window.renderMathInElement) {
      renderMathInElement(d, {
        delimiters: [
          {left:'$$', right:'$$', display:true},
          {left:'$', right:'$', display:false},
          {left:'\\(', right:'\\)', display:false},
          {left:'\\[', right:'\\]', display:true}
        ],
        throwOnError: false
        // Use KaTeX defaults for ignoredTags/ignoredClasses for safety
      });
    }
  }
}
window.__setAdvancedDebugVisible = function(show) {
  SHOW_ADV_DEBUG = !!show;
  applyDebugBarVisibility();
  render(CUR_MD, { keepDebug: true });
};
window.updateContentFromNative = function(payload) {
  var next = payload;
  if (payload && typeof payload === 'object' && !Array.isArray(payload)) {
    next = payload.raw_md;
  }
  if (typeof next !== 'string') next = '';
  CUR_MD = next;
  if (!EDIT) render(next);
  syncRawView();
  updateActionBar();
};
window.toggleSource = function() {
  if (EDIT) return;
  var c = document.getElementById('content'), r = document.getElementById('raw_view');
  if (!isRawViewVisible()) {
    syncRawView();
    r.style.display='block';
    c.style.display='none';
    if(window.cmd_updateStatus) window.cmd_updateStatus('raw');
    window.copyRawText();
  }
  else { r.style.display='none'; c.style.display='block'; if(window.cmd_updateStatus) window.cmd_updateStatus('view'); focusSink(); }
  updateActionBar();
};
window.toggleEdit = function() {
  var c = document.getElementById('content'), e = document.getElementById('edit_view'), r = document.getElementById('raw_view');
  if (!EDIT) {
    EDIT=true;
    var parsed = splitDebugFromMarkdown(CUR_MD);
    if (parsed.debug) CURRENT_DEBUG_INFO = parsed.debug;
    setDebugInfo(CURRENT_DEBUG_INFO);
    e.value=parsed.md;
    e.style.display='block'; c.style.display='none'; r.style.display='none'; fitEditHeight(); e.focus(); if(window.cmd_updateStatus) window.cmd_updateStatus('edit');
    updateActionBar();
  }
  else {
    EDIT=false;
    CUR_MD=e.value;
    if(window.cmd_updateContent) window.cmd_updateContent(CUR_MD);
    render(CUR_MD, { keepDebug: true });
    c.style.display='block';
    setTimeout(function(){
      e.style.display='none';
      focusSink();
      if(window.cmd_updateStatus) window.cmd_updateStatus('view');
    }, 0);
    updateActionBar();
  }
};
function applyFormat(t) {
  var e = document.getElementById('edit_view'); if (!EDIT || !e) return;
  var start = e.selectionStart, end = e.selectionEnd, val = e.value;
  var tag = (t==='bold'?'b':(t==='underline'?'u':'mark')), txt = val.substring(start, end);
  var ins = '<'+tag+'>'+txt+'</'+tag+'>';
  e.value = val.substring(0, start) + ins + val.substring(end);
  e.selectionStart = start; e.selectionEnd = start + ins.length; e.focus();
  CUR_MD = e.value; if(window.cmd_updateContent) window.cmd_updateContent(CUR_MD);
}
window.toggleScreenshot = function() { if(window.cmd_showScreenshot) window.cmd_showScreenshot(); };
function parseHK(hk) {
  if(!hk) return null; var p = hk.toLowerCase().split('+').map(x=>x.trim()).filter(x=>x.length>0), k = p.pop();
  return {key:k, ctrl:p.includes('ctrl')||p.includes('control'), alt:p.includes('alt'), shift:p.includes('shift')};
}
function matchHK(e, h) { return h && !!e.ctrlKey==!!h.ctrl && !!e.altKey==!!h.alt && !!e.shiftKey==!!h.shift && e.key.toLowerCase()==h.key; }
const HK_V=parseHK(INIT_DATA.key_view), HK_E=parseHK(INIT_DATA.key_edit), HK_S=parseHK(INIT_DATA.key_screenshot), HK_P=parseHK(INIT_DATA.key_prev), HK_N=parseHK(INIT_DATA.key_next), HK_T=parseHK(INIT_DATA.key_tag), HK_B=parseHK(INIT_DATA.key_bold), HK_U=parseHK(INIT_DATA.key_underline), HK_H=parseHK(INIT_DATA.key_highlight);
// Applies bold/underline/highlight to the text selected in the RENDERED view without
// entering edit mode. The selection must appear verbatim in the markdown source; when it
// does not, we tell the user instead of mangling the document.
function applyFormatToSelection(kind) {
  var sel = window.getSelection();
  var text = sel ? String(sel).trim() : '';
  if (!text) return false;

  var idx = CUR_MD.indexOf(text);
  if (idx < 0) {
    if (window.showCopyToast) window.showCopyToast(INIT_DATA.ab_selection_miss || 'Selection not found in source');
    return true; // handled, just not applied
  }

  var open = '**', close = '**';
  if (kind === 'underline') { open = '<u>'; close = '</u>'; }
  else if (kind === 'highlight') { open = '<mark>'; close = '</mark>'; }

  var alreadyWrapped =
    idx >= open.length &&
    CUR_MD.substr(idx - open.length, open.length) === open &&
    CUR_MD.substr(idx + text.length, close.length) === close;

  if (alreadyWrapped) {
    CUR_MD = CUR_MD.slice(0, idx - open.length) + text + CUR_MD.slice(idx + text.length + close.length);
  } else {
    CUR_MD = CUR_MD.slice(0, idx) + open + text + close + CUR_MD.slice(idx + text.length);
  }

  if (window.cmd_updateContent) window.cmd_updateContent(CUR_MD);
  render(CUR_MD, { keepDebug: true });
  if (window.showCopyToast) window.showCopyToast(INIT_DATA.ab_format_applied || 'Formatting applied');
  return true;
}

// Deleting / inserting without entering edit mode. Operates on the rendered selection,
// mapped back to the markdown source. delete removes the selection; newline and space are
// inserted AFTER it (that is the useful case: fixing a missing space or line break).
function applyEditToSelection(kind) {
  var sel = window.getSelection();
  var text = sel ? String(sel).trim() : '';
  if (!text) return false;

  var idx = CUR_MD.indexOf(text);
  if (idx < 0) {
    if (window.showCopyToast) window.showCopyToast(INIT_DATA.ab_selection_miss || 'Selection not found in source');
    return true;
  }

  if (kind === 'delete') {
    CUR_MD = CUR_MD.slice(0, idx) + CUR_MD.slice(idx + text.length);
  } else {
    var inserted = kind === 'newline' ? '\n' : ' ';
    var at = idx + text.length;
    CUR_MD = CUR_MD.slice(0, at) + inserted + CUR_MD.slice(at);
  }

  if (window.cmd_updateContent) window.cmd_updateContent(CUR_MD);
  render(CUR_MD, { keepDebug: true });
  if (window.showCopyToast) window.showCopyToast(INIT_DATA.ab_edit_applied || 'Edited');
  return true;
}

window.addEventListener('keydown', function(e) {
  log("Keydown: " + e.key + " (code: " + e.code + ")");
  var k = (e.key || '').toLowerCase();
  if (k === 'f12' || (e.ctrlKey && e.shiftKey && k === 'i')) {
    if (window.cmd_openDevTools) {
      log('Trigger openDevTools from JS');
      window.cmd_openDevTools();
    } else {
      log('cmd_openDevTools not bound');
    }
    e.preventDefault();
    return;
  }
  var raw = document.getElementById('raw_view');
  var isRawVisible = !!raw && isRawViewVisible();
  if (!EDIT && isRawVisible && e.key === 'Escape') {
    e.preventDefault();
    toggleSource();
    setTimeout(function() { focusSink(); }, 0);
    return;
  }
  if (EDIT) {
    // 编辑框内不再匹配裸键快捷键：默认的编辑键就是裸键 "e"，
    // 在这里匹配会把输入的字母吞掉，导致编辑框根本没法用。
    // 只保留 Esc 退出，以及带修饰键的格式化快捷键。
    if (e.key === 'Escape') { e.preventDefault(); toggleEdit(); return; }
    if (!e.ctrlKey && !e.altKey && !e.metaKey) return;
    if (matchHK(e, HK_E)) { e.preventDefault(); toggleEdit(); return; }
    if (matchHK(e, HK_B)) { e.preventDefault(); applyFormat('bold'); return; }
    if (matchHK(e, HK_U)) { e.preventDefault(); applyFormat('underline'); return; }
    if (matchHK(e, HK_H)) { e.preventDefault(); applyFormat('highlight'); return; }
    return;
  }
  // View mode formatting: works on the current text selection, no edit mode needed.
  if (!EDIT && (e.ctrlKey || e.metaKey) && !e.altKey) {
    if (matchHK(e, HK_B)) { if (applyFormatToSelection('bold')) { e.preventDefault(); return; } }
    if (matchHK(e, HK_U)) { if (applyFormatToSelection('underline')) { e.preventDefault(); return; } }
    if (matchHK(e, HK_H)) { if (applyFormatToSelection('highlight')) { e.preventDefault(); return; } }
    // Ctrl + Backspace / Enter / Space edit the selection directly.
    if (e.key === 'Backspace') { if (applyEditToSelection('delete')) { e.preventDefault(); return; } }
    if (e.key === 'Enter') { if (applyEditToSelection('newline')) { e.preventDefault(); return; } }
    if (e.key === ' ' || e.key === 'Spacebar') { if (applyEditToSelection('space')) { e.preventDefault(); return; } }
  }
  if (matchHK(e, HK_V)){ log("Matched View"); e.preventDefault(); toggleSource(); return; }
  if (matchHK(e, HK_E)){ log("Matched Edit"); e.preventDefault(); toggleEdit(); return; }
  if (matchHK(e, HK_S)){ log("Matched Screenshot"); e.preventDefault(); toggleScreenshot(); return; }
  if (matchHK(e, HK_P)){ e.preventDefault(); if(window.cmd_showPrevious) window.cmd_showPrevious(); return; }
  if (matchHK(e, HK_N)){ e.preventDefault(); if(window.cmd_showNext) window.cmd_showNext(); return; }
  if (matchHK(e, HK_T)){ e.preventDefault(); if(window.cmd_openTagDialog) window.cmd_openTagDialog(); return; }
}, true);
// ---- Action bar: visible buttons that mirror the configurable hotkeys ----
// Each entry maps a hotkey action to a button. `editorOnly` buttons only show up
// while the editor is open, because they are meaningless in the rendered view.
var ACTION_BAR_BUILT = false;

var ACTION_DEFS = [
  { id: 'shot',        icon: '📷',  labelKey: 'ab_screenshot',  hotkeyKey: 'key_screenshot'  },
  { id: 'edit',        icon: '✏️',  labelKey: 'ab_edit',        hotkeyKey: 'key_edit'        },
  { id: 'tag',         icon: '🏷',  labelKey: 'ab_tag',         hotkeyKey: 'key_tag'         },
  { id: 'retranslate', icon: '🔁',  labelKey: 'ab_retranslate', hotkeyKey: 'key_retranslate' },
  { id: 'copyshot',    icon: '📋',  labelKey: 'ab_copy_shot',   hotkeyKey: ''                },
  { id: 'copyctx',     icon: '🗒',  labelKey: 'ab_copy_ctx',    hotkeyKey: ''                },
  { separator: true },
  { id: 'bold',        icon: 'B',  labelKey: 'ab_bold',        hotkeyKey: 'key_bold',      editorOnly: true },
  { id: 'underline',   icon: 'U',  labelKey: 'ab_underline',   hotkeyKey: 'key_underline', editorOnly: true },
  { id: 'highlight',   icon: 'H',  labelKey: 'ab_highlight',   hotkeyKey: 'key_highlight', editorOnly: true }
];

function resolveActionTexts() {
  for (var i = 0; i < ACTION_DEFS.length; i++) {
    var d = ACTION_DEFS[i];
    if (d.separator) continue;
    d.label = INIT_DATA[d.labelKey] || d.labelKey;
    d.key = (INIT_DATA[d.hotkeyKey] || '').trim().toUpperCase();
    d.title = d.label + (d.key ? ' (' + d.key + ')' : '');
  }
}

function runAction(id) {
  switch (id) {
    case 'shot':        if (window.cmd_showScreenshot) window.cmd_showScreenshot(); else log('cmd_showScreenshot not bound'); break;
    case 'edit':        toggleEdit(); break;
    case 'source':      toggleSource(); break;
    case 'tag':         if (window.cmd_openTagDialog) window.cmd_openTagDialog(); else log('cmd_openTagDialog not bound'); break;
    case 'retranslate': if (window.cmd_retranslate) window.cmd_retranslate(); else log('cmd_retranslate not bound'); break;
    case 'copyshot':    if (window.cmd_copyScreenshot) window.cmd_copyScreenshot(); else log('cmd_copyScreenshot not bound'); break;
    case 'copyctx':     if (window.cmd_copyContext) window.cmd_copyContext(); else log('cmd_copyContext not bound'); break;
    case 'bold':        applyFormat('bold'); break;
    case 'underline':   applyFormat('underline'); break;
    case 'highlight':   applyFormat('highlight'); break;
  }
}

function buildActionBar() {
  var bar = document.getElementById('action_bar');
  if (!bar || ACTION_BAR_BUILT) return;
  ACTION_BAR_BUILT = true;
  resolveActionTexts();
  while (bar.firstChild) bar.removeChild(bar.firstChild);

  for (var i = 0; i < ACTION_DEFS.length; i++) {
    var d = ACTION_DEFS[i];
    if (d.separator) {
      var sep = document.createElement('span');
      sep.className = 'ab-sep';
      // This separator only divides the editor-only group, so it must disappear
      // together with that group instead of dangling at the end of the bar.
      sep.setAttribute('data-editor-only', '1');
      bar.appendChild(sep);
      continue;
    }

    var btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'abtn';
    btn.setAttribute('data-act', d.id);
    if (d.editorOnly) btn.setAttribute('data-editor-only', '1');
    btn.title = d.title;

    var ico = document.createElement('span');
    ico.className = 'ab-ico';
    ico.textContent = d.icon;
    btn.appendChild(ico);

    // Only single-letter bindings get an inline badge: a "CTRL+B" style badge is
    // wide enough to wrap the bar, and the tooltip already carries the combo.
    if (d.key.length === 1) {
      var kb = document.createElement('span');
      kb.className = 'ab-key';
      kb.textContent = d.key;
      btn.appendChild(kb);
    }

    btn.addEventListener('click', (function (act) {
      return function (ev) {
        ev.preventDefault();
        ev.stopPropagation();
        runAction(act);
        updateActionBar();
        // Keep the editor focused while editing; otherwise hand focus back to the sink
        // so the single-letter hotkeys keep working.
        if (!EDIT) focusSink();
      };
    })(d.id));

    bar.appendChild(btn);
  }
  updateActionBar();
}

function updateActionBar() {
  var bar = document.getElementById('action_bar');
  if (!bar) return;

  // Editor-only controls (the separator plus B/U/H) are shown in one pass so the
  // bar never keeps a dangling divider when the editor is closed.
  var editorBits = bar.querySelectorAll('[data-editor-only]');
  for (var i = 0; i < editorBits.length; i++) {
    editorBits[i].style.display = EDIT ? '' : 'none';
  }

  var rawVisible = isRawViewVisible();
  var hasImage = !!INIT_DATA.has_image;
  var btns = bar.querySelectorAll('.abtn');
  for (var j = 0; j < btns.length; j++) {
    var btn = btns[j];
    var act = btn.getAttribute('data-act');
    var pressed = (act === 'edit' && EDIT) || (act === 'source' && rawVisible);
    btn.setAttribute('aria-pressed', pressed ? 'true' : 'false');
    if (act === 'shot' || act === 'copyshot') btn.disabled = !hasImage;
  }
}

// Native calls this whenever the shown entry changes, so the image button can
// disable itself when there is no source screenshot to restore.
window.__setHasImage = function (has) {
  INIT_DATA.has_image = !!has;
  updateActionBar();
};

// Native calls this after a language switch; labels are re-resolved in place so
// the action bar does not need a full page reload.
window.__refreshActionBarLabels = function (labels) {
  if (!labels) return;
  for (var i = 0; i < ACTION_DEFS.length; i++) {
    var d = ACTION_DEFS[i];
    if (d.separator || !labels[d.labelKey]) continue;
    INIT_DATA[d.labelKey] = labels[d.labelKey];
  }
  resolveActionTexts();

  var bar = document.getElementById('action_bar');
  if (!bar) return;
  var btns = bar.querySelectorAll('.abtn');
  for (var j = 0; j < btns.length; j++) {
    var act = btns[j].getAttribute('data-act');
    for (var k = 0; k < ACTION_DEFS.length; k++) {
      if (!ACTION_DEFS[k].separator && ACTION_DEFS[k].id === act) {
        btns[j].title = ACTION_DEFS[k].title;
        break;
      }
    }
  }
};

document.addEventListener('DOMContentLoaded', () => {
  // The in-page action bar was folded into the native toolbar; buildActionBar() is no
  // longer called, so nothing is drawn here.
  render(CUR_MD);
  syncRawView();
  var raw = document.getElementById('raw_view');
  if (raw) {
    document.addEventListener('mousedown', function(e) {
      if (e.button !== 0 || !isRawViewVisible() || !raw.contains(e.target)) {
        RAW_SELECT_TRACK.active = false;
        return;
      }
      RAW_SELECT_TRACK.active = true;
      RAW_SELECT_TRACK.moved = false;
      RAW_SELECT_TRACK.startX = e.clientX;
      RAW_SELECT_TRACK.startY = e.clientY;
      RAW_SELECT_TRACK.startAt = Date.now();
    }, true);
    document.addEventListener('mousemove', function(e) {
      if (!RAW_SELECT_TRACK.active) return;
      if (Math.abs(e.clientX - RAW_SELECT_TRACK.startX) > 3 ||
          Math.abs(e.clientY - RAW_SELECT_TRACK.startY) > 3) {
        RAW_SELECT_TRACK.moved = true;
      }
    }, true);
    document.addEventListener('mouseup', function(e) {
      if (e.button !== 0 || !RAW_SELECT_TRACK.active) return;
      var shouldCopy = RAW_SELECT_TRACK.moved || (Date.now() - RAW_SELECT_TRACK.startAt) >= 180;
      RAW_SELECT_TRACK.active = false;
      if (!shouldCopy) return;
      setTimeout(function() {
        window.copyRawSelection();
      }, 0);
    }, true);
  }
  focusSink();
  log("WIN=" + (INIT_DATA.win_id||"?") + " DOMContentLoaded; KEY_VIEW=" + (INIT_DATA.key_view||"") + " KEY_SHOT=" + (INIT_DATA.key_screenshot||"") + " KEY_EDIT=" + (INIT_DATA.key_edit||""));
});

document.addEventListener('mouseup', () => {
  if (!EDIT && !isRawViewVisible()) focusSink();
}, true);

function fitEditHeight() {
  var e = document.getElementById('edit_view');
  if (!e) return;
  e.style.height = 'auto';
  var target = Math.max(200, e.scrollHeight);
  e.style.height = target + 'px';
}
window.addEventListener('resize', fitEditHeight);
window.addEventListener('input', function(e){ if(e && e.target && e.target.id==='edit_view') fitEditHeight(); }, true);
