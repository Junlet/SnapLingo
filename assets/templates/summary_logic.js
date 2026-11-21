if (document.body) { applyDarkMode(IS_DARK); } else { document.addEventListener('DOMContentLoaded', () => applyDarkMode(IS_DARK), {once:true}); }
var LAST_ENTRY = null;
document.addEventListener('DOMContentLoaded', function() {
    if (document.body) {
        document.body.tabIndex = -1;
        document.body.focus();
    }
    if (typeof RESTORE_SCROLL !== 'undefined') {
        window.scrollTo(0, RESTORE_SCROLL);
        setTimeout(() => window.scrollTo(0, RESTORE_SCROLL), 50);
    }
    log('DOMContentLoaded; KEY_VIEW='+KEY_VIEW+' KEY_SHOT='+KEY_SHOT+' KEY_EDIT='+KEY_EDIT);
});
function currentEntry() {
    var active = document.activeElement;
    if (active) {
        var e = active.closest('.entry');
        if (e) {
            LAST_ENTRY = e;
            return e;
        }
    }
    if (LAST_ENTRY) return LAST_ENTRY;
    return document.querySelector('.entry');
}
function updateStatus(mode) {
   var ind = document.getElementById('status-indicator');
   if (ind) {
       if (mode === 'edit') ind.innerText = 'MODE: EDIT';
       else if (mode === 'raw') ind.innerText = 'MODE: RAW';
       else ind.innerText = 'MODE: VIEW';
   }
}

function isSafeUrl(url, allowDataImage) {
   var v = (url || '').trim().toLowerCase();
   if (!v) return true;
   if (v.indexOf('javascript:') === 0 || v.indexOf('vbscript:') === 0) return false;
   if (v.indexOf('data:') === 0) return !!allowDataImage && v.indexOf('data:image/') === 0;
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

function protectMathJs(text) {
   var blocks = [];
   var counter = 0;
   var regex = /(\$\$[\s\S]*?\$\$)|(\\\[[\s\S]*?\\\])|(\\\([\s\S]*?\\\))|(\$[^\$\n]+\$)/g;
   var protectedText = text.replace(regex, function(match) {
       var ph = 'MATHBLOCKPH' + counter++;
       blocks.push(match);
       return ph;
   });
   return {text: protectedText, blocks: blocks};
}

function stripArchiveDebugPrefix(md) {
    var src = (typeof md === 'string') ? md : '';
    if (typeof SHOW_ADV_DEBUG_ARCHIVE !== 'undefined' && SHOW_ADV_DEBUG_ARCHIVE) return src;

    // Current format:
    // ```text
    // [Advanced API Debug]
    // ...
    // ```
    src = src.replace(/^[\uFEFF\s]*```(?:text)?\r?\n\[Advanced API Debug\]\r?\n[\s\S]*?\r?\n```\s*\r?\n*/i, '');

    // Legacy plain format:
    // [Advanced API Debug]
    // key = value
    src = src.replace(/^[\uFEFF\s]*\[Advanced API Debug\]\r?\n(?:[A-Za-z0-9_.$\[\]-]+\s*=.*\r?\n)+\r?\n*/i, '');
    return src;
}

function extractMarkdown(rawEl) {
    if (!rawEl) return '';
    var html = rawEl.innerHTML || '';
    html = html.replace(/<div><br\s*\/?>\s*<\/div>/gi, '\n');
    html = html.replace(/<div>/gi, '\n').replace(/<\/div>/gi, '');
    html = html.replace(/<p>/gi, '').replace(/<\/p>/gi, '\n');
    html = html.replace(/<br\s*\/?>/gi, '\n');
    html = html.replace(/&nbsp;/gi, ' ');
    var tmp = document.createElement('div');
    tmp.style.whiteSpace = 'pre-wrap';
    tmp.innerHTML = html;
    return (tmp.textContent || '').replace(/\r\n?/g, '\n');
}

function ensureMarkedOptions() {
    if (typeof marked !== 'undefined') {
         marked.setOptions({ breaks: true, gfm: true });
    }
}

function renderContent(id, markdownOverride) {
    ensureMarkedOptions();
    var raw = document.getElementById('raw_' + id);
    var rendered = document.getElementById('rendered_' + id);
    var markdown = (typeof markdownOverride === 'string') ? markdownOverride : (raw ? raw.textContent : '');
    markdown = stripArchiveDebugPrefix(markdown);
    
    // Step 1: Extract LaTeX from backticks (e.g., `$formula$` -> $formula$)
    // Only unwrap if the backticked content is a valid LaTeX expression (properly paired delimiters)
    markdown = markdown.replace(/`([^`]+)`/g, function(match, inner) {
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
    markdown = markdown.replace(
      /\\begin\{(equation\*?|align\*?|gather\*?|multline\*?)\}([\s\S]*?)\\end\{\1\}/g,
      function(_match, _envName, body) {
        var inner = (body || '').trim();
        return '\n$$\n' + inner + '\n$$\n';
      }
    );
    
    // Step 2: Protect math expressions before markdown parsing
    var p = protectMathJs(markdown);
    
    // Step 3: Parse markdown
    var html = marked.parse(p.text);
    html = sanitizeRenderedHtml(html);
    
    // Step 4: Restore protected math blocks (reverse order to avoid prefix collisions, e.g., PH1 vs PH10)
    for (var r = p.blocks.length - 1; r >= 0; r--) {
         var block = p.blocks[r];
         try {
           var placeholder = 'MATHBLOCKPH' + r;
           var escapedPlaceholder = placeholder.replace(/[.*+?^${}()|[\\]\\]/g, '\\$&');
           var regex = new RegExp(escapedPlaceholder, 'g');
           html = html.replace(regex, function() { return block; });
         } catch (e) {
           console.error('Error restoring math block ' + r + ':', e);
         }
    }
    
    rendered.innerHTML = html;
    
    // Step 5: Render LaTeX with KaTeX
    requestAnimationFrame(function() {
      renderMathInElement(rendered, {
        delimiters: [
          {left: '$$', right: '$$', display: true}, 
          {left: '$', right: '$', display: false}, 
          {left: '\\(', right: '\\)', display: false}, 
          {left: '\\[', right: '\\]', display: true}
        ], 
        throwOnError: false
        // Use KaTeX defaults for ignoredTags/ignoredClasses for safety
      });
    });
}

function insertMarkdown(startTag, endTag) {
   var sel = window.getSelection();
   if (!sel.rangeCount) return;
   var range = sel.getRangeAt(0);
   var text = range.toString();
   document.execCommand('insertText', false, startTag + text + endTag);
}

function matchHotkey(e, hotkeyStr) {
   if (!hotkeyStr) return false;
   var parts = hotkeyStr.toLowerCase().split('+').map(function(p){ return p.trim(); }).filter(Boolean);
   if (!parts.length) return false;
   var key = parts.pop();
   var ctrl = parts.includes('ctrl') || parts.includes('control');
   var alt = parts.includes('alt');
   var shift = parts.includes('shift');
   return (e.key.toLowerCase() === key && e.ctrlKey === ctrl && e.altKey === alt && e.shiftKey === shift);
}

function toggleView(id) {
  var raw = document.getElementById('raw_' + id);
  var rendered = document.getElementById('rendered_' + id);
  if (raw.style.display === 'none') {
    raw.style.display = 'block';
    rendered.style.display = 'none';
    updateStatus('raw');
  } else {
    raw.style.display = 'none';
    rendered.style.display = 'block';
    updateStatus('view');
  }
  updateEntryActions(document.getElementById('entry_' + id));
}

function toggleEdit(entry) {
   var id = entry.getAttribute('data-id');
   var rendered = document.getElementById('rendered_' + id);
   var raw = document.getElementById('raw_' + id);
   
   if (entry.classList.contains('mode-edit')) {
       entry.classList.remove('mode-edit');
       entry.classList.add('mode-view');
       updateStatus('view');
       raw.contentEditable = 'false';

       var markdown = extractMarkdown(raw);
       raw.innerText = markdown;
       renderContent(id, markdown);

       raw.style.display = 'none';
       rendered.style.display = 'block';
       
       // Notify C++ of edit
       if (window.cmd_updateEntry) window.cmd_updateEntry(id, markdown);

       entry.focus();
   } else {
       entry.classList.remove('mode-view');
       entry.classList.add('mode-edit');
       updateStatus('edit');
       rendered.style.display = 'none';
       raw.style.display = 'block';
       raw.contentEditable = 'true';
       raw.focus();
   }
   updateEntryActions(entry);
}

document.addEventListener('focusin', function(e) {
   var entry = e.target.closest('.entry');
   if (entry) {
       var id = entry.getAttribute('data-id');
       var raw = document.getElementById('raw_' + id);
       var isRaw = raw && raw.style.display !== 'none';
       if (entry.classList.contains('mode-edit')) updateStatus('edit');
       else if (isRaw) updateStatus('raw');
       else updateStatus('view');
       LAST_ENTRY = entry;
   } else { updateStatus('view'); }
});

// View-mode formatting for one archive entry: rewrites that entry's markdown source.
// The selection has to appear verbatim in the source; otherwise nothing is changed.
function applyEntryFormat(entry, kind) {
   if (!entry) return false;

   var sel = window.getSelection();
   var text = sel ? String(sel).trim() : '';
   if (!text) return false;

   var id = entry.getAttribute('data-id');
   var raw = document.getElementById('raw_' + id);
   if (!raw) return false;

   var md = extractMarkdown(raw);
   var idx = md.indexOf(text);
   if (idx < 0) {
       log('format: selection not found in source');
       return true; // handled, just not applied
   }

   var open = '**', close = '**';
   if (kind === 'underline') { open = '<u>'; close = '</u>'; }
   else if (kind === 'highlight') { open = '<mark>'; close = '</mark>'; }

   var wrapped = idx >= open.length &&
      md.substr(idx - open.length, open.length) === open &&
      md.substr(idx + text.length, close.length) === close;

   if (wrapped) {
       md = md.slice(0, idx - open.length) + text + md.slice(idx + text.length + close.length);
   } else {
       md = md.slice(0, idx) + open + text + close + md.slice(idx + text.length);
   }

   raw.textContent = md; // round-trips cleanly through extractMarkdown()
   renderContent(id, md);
   if (window.cmd_updateEntry) window.cmd_updateEntry(id, md);
   return true;
}

// Same idea as applyEntryFormat, but for delete / newline / space on the rendered selection.
function applyEntryEdit(entry, kind) {
   if (!entry) return false;

   var sel = window.getSelection();
   var text = sel ? String(sel).trim() : '';
   if (!text) return false;

   var id = entry.getAttribute('data-id');
   var raw = document.getElementById('raw_' + id);
   if (!raw) return false;

   var md = extractMarkdown(raw);
   var idx = md.indexOf(text);
   if (idx < 0) {
       log('edit: selection not found in source');
       return true;
   }

   if (kind === 'delete') {
       md = md.slice(0, idx) + md.slice(idx + text.length);
   } else {
       var inserted = kind === 'newline' ? '\n' : ' ';
       var at = idx + text.length;
       md = md.slice(0, at) + inserted + md.slice(at);
   }

   raw.textContent = md;
   renderContent(id, md);
   if (window.cmd_updateEntry) window.cmd_updateEntry(id, md);
   return true;
}

function handleKey(e) {
   log(`handleKey enter key=${e.key} ctrl=${e.ctrlKey} alt=${e.altKey} shift=${e.shiftKey}`);
   var keyLower = e.key ? e.key.toLowerCase() : '';
   if (keyLower === 'f12' || (e.ctrlKey && e.shiftKey && keyLower === 'i')) {
       if (window.cmd_openDevTools) {
           log('Trigger openDevTools from JS');
           window.cmd_openDevTools();
       } else {
           log('cmd_openDevTools not bound');
       }
       e.preventDefault();
       return;
   }
   if (SELECTION_MODE && e.key === 'Escape') {
       if (window.cmd_exitSelectionMode) window.cmd_exitSelectionMode();
       e.preventDefault();
       return;
   }
   var entry = currentEntry();
   if (!entry) { log('handleKey: no entry'); return; }
   log(`handleKey currentEntry id=${entry.getAttribute('data-id')}`);
   
   var isEditing = entry.classList.contains('mode-edit');
   var k = keyLower;
   log(`KEY_VIEW=${KEY_VIEW} KEY_SHOT=${KEY_SHOT} k=${k} isEditing=${isEditing}`);

   // Batch selection mode toggle (only when not editing).
   // Default is Ctrl+S, but it's configurable via KEY_SELECT.
   if (!isEditing && matchHotkey(e, (typeof KEY_SELECT !== 'undefined') ? KEY_SELECT : 'ctrl+s')) {
       if (window.cmd_toggleSelectionMode) window.cmd_toggleSelectionMode();
       e.preventDefault();
       return;
   }

   // While editing, a bare "e" must reach the editable: the default edit key is
   // literally "e", so matching it here would swallow the letter and drop out of
   // edit mode. Esc still exits, and modifier combos still work.
   var plainKey = !e.ctrlKey && !e.altKey && !e.metaKey;
   if (!(isEditing && plainKey) && matchHotkey(e, KEY_EDIT)) {
       e.preventDefault(); toggleEdit(entry); return; 
   }
   
   if (isEditing) {
      if (e.key === 'Escape') {
          e.preventDefault(); toggleEdit(entry); return;
      }
      if (matchHotkey(e, KEY_BOLD)) {
          e.preventDefault(); insertMarkdown('**', '**'); return;
      }
      if (matchHotkey(e, KEY_UNDERLINE)) {
          e.preventDefault(); insertMarkdown('<u>', '</u>'); return;
      }
      if (matchHotkey(e, KEY_HIGHLIGHT)) {
          e.preventDefault(); insertMarkdown('<mark>', '</mark>'); return;
      }
      return; 
    }
    if (!isEditing) {
        // View-mode formatting: works on the selected rendered text, no edit mode needed.
        if ((e.ctrlKey || e.metaKey) && !e.altKey) {
            if (matchHotkey(e, KEY_BOLD)) { if (applyEntryFormat(entry, 'bold')) { e.preventDefault(); return; } }
            if (matchHotkey(e, KEY_UNDERLINE)) { if (applyEntryFormat(entry, 'underline')) { e.preventDefault(); return; } }
            if (matchHotkey(e, KEY_HIGHLIGHT)) { if (applyEntryFormat(entry, 'highlight')) { e.preventDefault(); return; } }
            // Ctrl + Backspace / Enter / Space edit the selection directly.
            if (e.key === 'Backspace') { if (applyEntryEdit(entry, 'delete')) { e.preventDefault(); return; } }
            if (e.key === 'Enter') { if (applyEntryEdit(entry, 'newline')) { e.preventDefault(); return; } }
            if (e.key === ' ' || e.key === 'Spacebar') { if (applyEntryEdit(entry, 'space')) { e.preventDefault(); return; } }
        }
        // Esc exits RAW mode back to VIEW (when currently in RAW)
        if (e.key === 'Escape') {
            var id0 = entry.getAttribute('data-id');
            var raw0 = document.getElementById('raw_' + id0);
            if (raw0 && raw0.style.display !== 'none') {
                e.preventDefault();
                toggleView(id0);
                return;
            }
        }
        if (matchHotkey(e, KEY_VIEW)) { toggleView(entry.getAttribute('data-id')); e.preventDefault(); return; }
        if (matchHotkey(e, KEY_SHOT)) { 
            // Call native webview binding
            window.cmd_restore(entry.getAttribute('data-id'));
            e.preventDefault(); return; 
        }
        
        // Handle 'dd' for deletion
        if (k === 'd') {
            e.preventDefault();
            var now = Date.now();
            if (entry.lastDTime && (now - entry.lastDTime < 500)) {
                 // Double press detected
                 var id = entry.getAttribute('data-id');
                 // No confirm dialog as requested
                 window.cmd_delete(id);
                 entry.remove();
                 entry.lastDTime = 0;
            } else {
                 entry.lastDTime = now;
            }
            return;
        }
    }
}

if (!window.__INIT_ONCE__) {
    window.__INIT_ONCE__ = true;
    window.__FOCUS_GUARD_ID__ = (Math.random().toString(16).slice(2) + Date.now().toString(16));
    log('init_guard gid=' + window.__FOCUS_GUARD_ID__);
    window.addEventListener('keydown', handleKey, true);
    window.addEventListener('keydown', function(e){
        log(`keydown key=${e.key} ctrl=${e.ctrlKey} alt=${e.altKey} shift=${e.shiftKey} target=${(e.target && e.target.tagName)}`);
    }, true);
    // 非 capture：避免把元素级 focus/blur 误记为 window focus/blur。
    window.addEventListener('focus', ()=>log('window focus gid=' + window.__FOCUS_GUARD_ID__));
    window.addEventListener('blur', ()=>log('window blur gid=' + window.__FOCUS_GUARD_ID__));
    document.addEventListener('visibilitychange', ()=>log('visibility='+document.visibilityState), true);
}

function updateEntryInDom(id, newMarkdown) {
   var raw = document.getElementById('raw_' + id);
   if (raw) {
       raw.innerText = stripArchiveDebugPrefix(newMarkdown);
       var entry = raw.closest('.entry');
       if (entry && !entry.classList.contains('mode-edit')) {
           renderContent(id, stripArchiveDebugPrefix(newMarkdown));
       }
   }
}

// ---- Per-entry action buttons (mirrors the archive hotkeys) ----
// The archive hotkeys are configurable and invisible; these buttons are the
// discoverable equivalent. KEY_* globals are injected by SummaryWindow_Html.cpp.
function keyBadge(hotkey) {
   var v = (hotkey || '').trim();
   if (v.length !== 1) return '';   // combos stay in the tooltip to keep the row compact
   return v.toUpperCase();
}

// Titles are derived in one place so a language switch can refresh them without
// rebuilding the list.
function entryActionTitle(act) {
   function hk(v) { return v ? ' (' + v.toUpperCase() + ')' : ''; }
   if (act === 'edit') return AB_EDIT + hk(KEY_EDIT);
   if (act === 'source') return AB_SOURCE + hk(KEY_VIEW);
   if (act === 'shot') return AB_SHOT + hk(KEY_SHOT);
   if (act === 'delete') return AB_DELETE + ' \u00d72';
   return '';
}

function formatTitle(act) {
   function hk(v) { return v ? ' (' + v.toUpperCase() + ')' : ''; }
   if (act === 'bold') return AB_BOLD + hk(KEY_BOLD);
   if (act === 'underline') return AB_UNDERLINE + hk(KEY_UNDERLINE);
   if (act === 'highlight') return AB_HIGHLIGHT + hk(KEY_HIGHLIGHT);
   return '';
}

window.__setEntryActionLabels = function (labels) {
   if (!labels) return;
   if (labels.ab_entry_edit) AB_EDIT = labels.ab_entry_edit;
   if (labels.ab_entry_source) AB_SOURCE = labels.ab_entry_source;
   if (labels.ab_entry_shot) AB_SHOT = labels.ab_entry_shot;
   if (labels.ab_entry_delete) AB_DELETE = labels.ab_entry_delete;
   if (labels.ab_entry_delete_confirm) AB_DELETE_CONFIRM = labels.ab_entry_delete_confirm;
   if (labels.ab_entry_bold) AB_BOLD = labels.ab_entry_bold;
   if (labels.ab_entry_underline) AB_UNDERLINE = labels.ab_entry_underline;
   if (labels.ab_entry_highlight) AB_HIGHLIGHT = labels.ab_entry_highlight;

   var btns = document.querySelectorAll('.entry-actions .eabtn');
   for (var i = 0; i < btns.length; i++) {
      var act = btns[i].getAttribute('data-act');
      if (!btns[i].classList.contains('confirm'))
         btns[i].title = (act === 'bold' || act === 'underline' || act === 'highlight')
            ? formatTitle(act)
            : entryActionTitle(act);
   }
};

function buildEntryActions(entryEl, id) {
   var actions = document.createElement('div');
   actions.className = 'entry-actions';

   function addBtn(act, icon, label, hotkey, title, danger, editorOnly) {
      var btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'eabtn' + (danger ? ' danger' : '');
      btn.setAttribute('data-act', act);
      if (editorOnly) btn.setAttribute('data-editor-only', '1');
      btn.title = title;

      var ico = document.createElement('span');
      ico.className = 'eko';
      ico.textContent = icon;
      btn.appendChild(ico);

      var badge = keyBadge(hotkey);
      if (badge) {
         var kb = document.createElement('span');
         kb.className = 'ekey';
         kb.textContent = badge;
         btn.appendChild(kb);
      }
      actions.appendChild(btn);
      return btn;
   }

   addBtn('edit', '✏️', '', KEY_EDIT, entryActionTitle('edit'),
          false).addEventListener('click', function (e) {
      e.preventDefault(); e.stopPropagation();
      toggleEdit(entryEl);
   });

   addBtn('source', '</>', '', KEY_VIEW, entryActionTitle('source'),
          false).addEventListener('click', function (e) {
      e.preventDefault(); e.stopPropagation();
      toggleView(id);
   });

   addBtn('shot', '📷', '', KEY_SHOT, entryActionTitle('shot'),
          false).addEventListener('click', function (e) {
      e.preventDefault(); e.stopPropagation();
      if (window.cmd_restore) window.cmd_restore(id);
   });

   // Deleting a translation is irreversible and this window offers no undo, so the
   // button asks for a second click instead of deleting straight away.
   var del = addBtn('delete', '🗑', '', '', entryActionTitle('delete'), true);
   var delTimer = 0;
   function resetDelete() {
      del.classList.remove('confirm');
      del.title = entryActionTitle('delete');
      if (delTimer) { clearTimeout(delTimer); delTimer = 0; }
   }
   del.addEventListener('click', function (e) {
      e.preventDefault(); e.stopPropagation();
      if (!del.classList.contains('confirm')) {
         del.classList.add('confirm');
         del.title = AB_DELETE_CONFIRM;
         delTimer = setTimeout(resetDelete, 2500);
         return;
      }
      resetDelete();
      if (window.cmd_delete) window.cmd_delete(id);
      if (entryEl && entryEl.parentNode) entryEl.parentNode.removeChild(entryEl);
   });

   // Formatting buttons: only meaningful while this entry is being edited, so they
   // appear together with raw-text editing and disappear again afterwards.
   var formats = [
      { act: 'bold', icon: 'B', open: '**', close: '**' },
      { act: 'underline', icon: 'U', open: '<u>', close: '</u>' },
      { act: 'highlight', icon: 'H', open: '<mark>', close: '</mark>' }
   ];
   for (var fi = 0; fi < formats.length; fi++) {
      (function (f) {
         var fb = addBtn(f.act, f.icon, '', '', formatTitle(f.act), false, true);
         // mousedown would move focus (and the caret) out of the contentEditable entry,
         // so the selection is kept alive by preventing the default focus change.
         fb.addEventListener('mousedown', function (e) { e.preventDefault(); });
         fb.addEventListener('click', function (e) {
            e.preventDefault();
            e.stopPropagation();
            insertMarkdown(f.open, f.close);
         });
      })(formats[fi]);
   }

   // Keep a confirmation from leaking onto another entry after a re-render.
   entryEl.addEventListener('mode-change', resetDelete);
   return actions;
}

function updateEntryActions(entryEl) {
   if (!entryEl) return;
   var id = entryEl.getAttribute('data-id');
   var raw = document.getElementById('raw_' + id);
   var rawVisible = !!(raw && raw.style.display !== 'none');
   var editing = entryEl.classList.contains('mode-edit');

   var btns = entryEl.querySelectorAll('.entry-actions .eabtn');
   for (var i = 0; i < btns.length; i++) {
      var isEditorOnly = btns[i].getAttribute('data-editor-only') === '1';
      // B / U / H only make sense inside the raw editor.
      btns[i].style.display = (!isEditorOnly || editing) ? '' : 'none';
      if (isEditorOnly) continue;

      var act = btns[i].getAttribute('data-act');
      var pressed = (act === 'edit' && editing) || (act === 'source' && rawVisible && !editing);
      btns[i].setAttribute('aria-pressed', pressed ? 'true' : 'false');
   }
}

function addEntryToDom(id, time, markdown, mathBlocks, originalRaw, isSelectionMode, tags, contextTurns) {
  var div = document.createElement('div');
  div.className = 'entry mode-view';
  div.id = 'entry_' + id;
  div.setAttribute('data-id', id);
  div.tabIndex = 0;

  var checkboxDisplay = isSelectionMode ? 'block' : 'none';

  var header = document.createElement('div');
  header.className = 'entry-header';

  var checkbox = document.createElement('input');
  checkbox.type = 'checkbox';
  checkbox.className = 'selection-checkbox';
  checkbox.style.display = checkboxDisplay;
  checkbox.setAttribute('data-id', id);
  header.appendChild(checkbox);

  var info = document.createElement('div');
  info.className = 'entry-info';

  var timeLine = document.createElement('div');
  timeLine.textContent = time;
  info.appendChild(timeLine);

  if (tags.length) {
      var tagLine = document.createElement('div');
      tagLine.textContent = 'Tags: ' + tags.join(', ');
      info.appendChild(tagLine);
  }

  header.appendChild(info);
  // Persisted marker: this entry was translated with earlier translations as context.
  if (contextTurns > 0) {
     var ctxFlag = document.createElement('span');
     ctxFlag.className = 'context-flag';
     ctxFlag.textContent = '\uD83D\uDD17 ' + contextTurns;
     ctxFlag.title = 'continued from context (' + contextTurns + ')';
     info.appendChild(ctxFlag);
  }
  header.appendChild(buildEntryActions(div, id));
  div.appendChild(header);

  var contentArea = document.createElement('div');
  contentArea.className = 'content-area';

  var rendered = document.createElement('div');
  rendered.id = 'rendered_' + id;
  rendered.className = 'rendered-html';
  contentArea.appendChild(rendered);

  var raw = document.createElement('div');
  raw.id = 'raw_' + id;
  raw.className = 'raw-text';
  raw.style.display = 'none';
  raw.spellcheck = false;
  contentArea.appendChild(raw);

  div.appendChild(contentArea);
  document.body.appendChild(div);
  
  var rawContainer = document.getElementById('raw_' + id);
  if (rawContainer) {
      rawContainer.textContent = stripArchiveDebugPrefix(originalRaw);
      renderContent(id);
  }
}

function addEntryFromNative(entry, isSelectionMode) {
    if (!entry || typeof entry !== 'object') return;
    var tags = Array.isArray(entry.tags) ? entry.tags : [];
    addEntryToDom(
        String(entry.id || ''),
        String(entry.time || ''),
        '',
        [],
        String(entry.originalRaw || ''),
        !!isSelectionMode,
        tags,
        Number(entry.contextTurns) || 0
    );
}

function bootstrapEntriesFromNative(scriptId, isSelectionMode) {
    var dataNode = document.getElementById(scriptId);
    if (!dataNode) return;
    var raw = dataNode.textContent || '[]';
    var entries = [];
    try {
        entries = JSON.parse(raw);
    } catch (e) {
        console.error('Failed to parse initial entry JSON:', e);
        return;
    }
    if (!Array.isArray(entries)) return;
    SELECTION_MODE = !!isSelectionMode;
    entries.forEach(function(entry) {
        addEntryFromNative(entry, isSelectionMode);
    });
}

// The archive page starts empty until the native side pushes rows. Show a hint
// instead of a blank page, and hide it again as soon as one row exists.
function updateArchiveEmptyState(hasEntries) {
    var el = document.getElementById('archive-empty-state');
    if (!el) return;
    el.style.display = hasEntries ? 'none' : 'block';
}

function toggleSelectionMode(show) {
    SELECTION_MODE = !!show;
    var checkboxes = document.querySelectorAll('.selection-checkbox');
    checkboxes.forEach(cb => {
        cb.style.display = show ? 'block' : 'none';
        if (!show) {
            cb.checked = false;
            var entry = cb.closest('.entry');
            if (entry) entry.classList.remove('selected');
        }
    });
    if (!show) clearSelectionOrder();
    // Per-entry action buttons would toggle selection instead of acting, so they
    // step aside while the user is picking entries for a batch operation.
    document.querySelectorAll('.entry-actions').forEach(function(a) {
        a.style.display = show ? 'none' : '';
    });
}

// Selection order is recorded explicitly: DOM order is newest-first, so batch copy and
// export used to come out reversed. The array below is the order the user checked them.
var SELECT_ORDER = [];

function updateSelectionBadges() {
   var old = document.querySelectorAll('.selection-order');
   for (var i = 0; i < old.length; i++) old[i].remove();

   for (var j = 0; j < SELECT_ORDER.length; j++) {
      var entry = document.getElementById('entry_' + SELECT_ORDER[j]);
      if (!entry) continue;
      var header = entry.querySelector('.entry-header');
      if (!header) continue;
      var badge = document.createElement('span');
      badge.className = 'selection-order';
      badge.textContent = String(j + 1);
      header.insertBefore(badge, header.firstChild);
   }
}

function setSelected(cb, state) {
   if (!cb) return;
   cb.checked = !!state;
   var entry = cb.closest('.entry');
   if (entry) entry.classList.toggle('selected', !!state);

   var id = cb.getAttribute('data-id');
   var pos = SELECT_ORDER.indexOf(id);
   if (state && pos === -1) SELECT_ORDER.push(id);
   else if (!state && pos !== -1) SELECT_ORDER.splice(pos, 1);

   updateSelectionBadges();
}

function clearSelectionOrder() {
   SELECT_ORDER = [];
   updateSelectionBadges();
}

function getSelectedIds() {
   var checked = {};
   var boxes = document.querySelectorAll('.selection-checkbox:checked');
   for (var i = 0; i < boxes.length; i++) checked[boxes[i].getAttribute('data-id')] = true;
   // Return in the order the entries were picked, not in DOM (newest-first) order.
   return SELECT_ORDER.filter(function (id) { return checked[id]; });
}

function selectAllEntries(select) {
   var checkboxes = document.querySelectorAll('.selection-checkbox');
   checkboxes.forEach(cb => {
        setSelected(cb, select);
   });
}

// Click-to-select in selection mode
document.addEventListener('click', function(e) {
    if (!SELECTION_MODE) return;
    var entry = e.target.closest('.entry');
    if (!entry) return;
    var cb = entry.querySelector('.selection-checkbox');
    if (!cb) return;
    setSelected(cb, !cb.checked);
    e.preventDefault();
});

// Drag rectangle selection
let dragSelect = false;
let dragStart = {x:0, y:0};
let selectionRectEl = null;
document.addEventListener('mousedown', function(e){
    if (!SELECTION_MODE || e.button !== 0) return;
    dragSelect = true;
    dragStart = {x: e.clientX, y: e.clientY};
    selectionRectEl = document.createElement('div');
    selectionRectEl.className = 'selection-rect';
    document.body.appendChild(selectionRectEl);
});
document.addEventListener('mousemove', function(e){
    if (!dragSelect || !selectionRectEl) return;
    var x1 = Math.min(dragStart.x, e.clientX);
    var y1 = Math.min(dragStart.y, e.clientY);
    var x2 = Math.max(dragStart.x, e.clientX);
    var y2 = Math.max(dragStart.y, e.clientY);
    selectionRectEl.style.left = x1 + 'px';
    selectionRectEl.style.top = y1 + 'px';
    selectionRectEl.style.width = (x2 - x1) + 'px';
    selectionRectEl.style.height = (y2 - y1) + 'px';
    var checkboxes = document.querySelectorAll('.selection-checkbox');
    checkboxes.forEach(cb => {
        var entry = cb.closest('.entry');
        if (!entry) return;
        var rect = entry.getBoundingClientRect();
        var overlap = rect.left < x2 && rect.right > x1 && rect.top < y2 && rect.bottom > y1;
        setSelected(cb, overlap);
    });
});
document.addEventListener('mouseup', function(e){
    if (!dragSelect) return;
    dragSelect = false;
    if (selectionRectEl) {
        selectionRectEl.remove();
        selectionRectEl = null;
    }
});
