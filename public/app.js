// app.js
// UI layer. All grade math, array handling and attendance bookkeeping live in the
// C++ core (cpp/class_manager.cpp, compiled to WebAssembly). This file only
// renders the snapshot the core returns and forwards user actions to it.
'use strict';

const STORAGE_KEY = 'classRecords.v1';

const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];
const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const pct = (v) => (v == null ? '–' : v.toFixed(1) + '%');
const letterPill = (l) => (l ? `<span class="pill l-${l}">${l}</span>` : '<span class="text-mute">–</span>');

let Module;   // Emscripten module
let cm;       // cm::ClassManager instance
let snap;     // latest JSON snapshot from cm.toJson()
let activeTab = 'overview';

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------
async function boot() {
  bindUi();
  setTab('overview');
  $('#new-day').value = new Date().toISOString().slice(0, 10);

  try {
    if (typeof createClassModule !== 'function') throw new Error('wasm/class_manager.js was not found.');
    Module = await createClassModule();
    cm = new Module.ClassManager();
  } catch (err) {
    console.error(err);
    $('#setup-error').classList.remove('hidden');
    $('#setup-error-text').innerHTML =
      'The C++ core is not built yet. Run <code class="rounded bg-black/20 px-1">./build.sh</code> ' +
      'from the project root (requires Emscripten), then reload this page. See the README for details.';
    return;
  }

  if (!restore()) loadSample();
  refresh();
}

// ---------------------------------------------------------------------------
// Core calls + rendering
// ---------------------------------------------------------------------------
function refresh(opts = {}) {
  snap = JSON.parse(cm.toJson());
  renderOverview();
  renderRoster();
  if (opts.patchGrades) patchGradebook(); else renderGradebook();
  renderAttendance();
  save();
  if (opts.focus) { const el = $(opts.focus); if (el) el.focus(); }
}

// Run a mutating core call. Negative result codes are errors.
function act(code, opts) {
  if (code < 0) {
    toast(Module.ClassManager.errorMessage(code), true);
    return false;
  }
  refresh(opts);
  return true;
}

function renderOverview() {
  const s = snap.stats;
  const cells = [
    ['Students', s.studentCount, `${snap.limits.maxStudents - s.studentCount} spots free`],
    ['Class average', pct(s.classAverage), s.highest == null ? 'No grades yet' : `Range ${s.lowest.toFixed(1)}% to ${s.highest.toFixed(1)}%`],
    ['Class GPA', s.classGpa == null ? '–' : s.classGpa.toFixed(2), 'On a 4.0 scale'],
    ['Attendance', pct(s.attendanceRate), `${snap.days.length} class ${snap.days.length === 1 ? 'day' : 'days'} added`],
  ];
  $('#ov-stats').innerHTML = cells.map(([label, val, note]) => `
    <div class="bg-white px-5 py-4">
      <dt class="text-sm text-mute">${label}</dt>
      <dd class="tnum mt-1 font-display text-3xl font-bold">${val}</dd>
      <p class="mt-1 text-sm text-mute">${note}</p>
    </div>`).join('');

  const maxCount = Math.max(1, ...Object.values(s.distribution));
  $('#ov-dist').innerHTML = s.gradedCount === 0
    ? '<p class="text-sm text-mute">Enter grades in the Gradebook to see the spread.</p>'
    : ['A', 'B', 'C', 'D', 'F'].map((l) => `
      <div class="flex items-center gap-3">
        <span class="pill l-${l} w-7">${l}</span>
        <div class="h-3 flex-1 rounded-full bg-paper"><div class="h-3 rounded-full bar-${l}" style="width:${(s.distribution[l] / maxCount) * 100}%"></div></div>
        <span class="tnum w-6 text-right text-sm">${s.distribution[l]}</span>
      </div>`).join('');

  const ranked = snap.students.filter((st) => st.rank != null).sort((a, b) => a.rank - b.rank).slice(0, 8);
  $('#ov-standings').innerHTML = ranked.length === 0
    ? '<li class="text-sm text-mute">Standings appear once students have grades.</li>'
    : ranked.map((st) => `
      <li class="flex items-center gap-3 text-sm">
        <span class="tnum w-5 text-right text-mute">${st.rank}</span>
        <span class="min-w-0 flex-1 truncate">${esc(st.name)}</span>
        <span class="tnum">${pct(st.percent)}</span>
        ${letterPill(st.letter)}
      </li>`).join('');

  $('#ov-assign').innerHTML = snap.assignments.length === 0
    ? '<p class="text-sm text-mute">Add an assignment in the Gradebook.</p>'
    : snap.assignments.map((a) => `
      <div>
        <div class="flex justify-between text-sm"><span class="truncate pr-2">${esc(a.name)}</span><span class="tnum">${pct(a.average)}</span></div>
        <div class="mt-1 h-2 rounded-full bg-paper"><div class="h-2 rounded-full bg-board" style="width:${Math.min(100, a.average ?? 0)}%"></div></div>
      </div>`).join('');
}

function renderRoster() {
  const box = $('#roster-table');
  if (snap.students.length === 0) {
    box.innerHTML = '<p class="p-6 text-sm text-mute">No students yet. Add your first student above, or load the sample class.</p>';
    return;
  }
  box.innerHTML = `
    <table class="w-full border-collapse">
      <thead><tr>
        <th class="th">ID</th><th class="th">Name</th><th class="th text-right">Final grade</th>
        <th class="th">Letter</th><th class="th text-right">Attendance</th><th class="th"><span class="sr-only">Actions</span></th>
      </tr></thead>
      <tbody>${snap.students.map((s) => `
        <tr>
          <td class="td tnum text-mute">${esc(s.id)}</td>
          <td class="td font-medium">${esc(s.name)}</td>
          <td class="td tnum text-right">${pct(s.percent)}</td>
          <td class="td">${letterPill(s.letter)}</td>
          <td class="td tnum text-right">${pct(s.attendanceRate)}</td>
          <td class="td text-right">
            <button class="btn-line !px-2.5 !py-1" data-edit-student="${s.index}" type="button">Edit</button>
            <button class="btn-danger !px-2.5 !py-1" data-del-student="${s.index}" type="button" aria-label="Delete ${esc(s.name)}">Delete</button>
          </td>
        </tr>`).join('')}
      </tbody>
    </table>`;
}

function renderGradebook() {
  const box = $('#grade-table');
  const tw = snap.stats.totalWeight;
  $('#weight-note').textContent = snap.assignments.length
    ? `${snap.assignments.length} of ${snap.limits.maxAssignments} assignments. Total weight ${tw}.`
    : '';

  if (snap.assignments.length === 0 || snap.students.length === 0) {
    box.innerHTML = `<p class="p-6 text-sm text-mute">${snap.students.length === 0
      ? 'Add students on the Roster tab first, then enter their grades here.'
      : 'Add an assignment to start entering grades.'}</p>`;
    return;
  }

  const head = snap.assignments.map((a, j) => `
    <th class="th text-right">
      <button type="button" data-edit-assignment="${j}" class="rounded text-right hover:underline focus-visible:outline focus-visible:outline-2 focus-visible:outline-mark" title="Edit assignment">
        <span class="block">${esc(a.name)}</span>
        <span class="block text-xs font-normal text-mute">out of ${a.maxPoints}, weight ${a.weight}</span>
      </button>
    </th>`).join('');

  const rows = snap.students.map((s) => `
    <tr data-row="${s.index}">
      <td class="td sticky-col"><span class="font-medium">${esc(s.name)}</span> <span class="tnum text-xs text-mute">${esc(s.id)}</span></td>
      ${s.grades.map((g, j) => `
        <td class="td text-right"><input class="grade-input" type="number" min="0" step="any" inputmode="decimal"
          data-s="${s.index}" data-a="${j}" value="${g ?? ''}" aria-label="${esc(s.name)}, ${esc(snap.assignments[j].name)}"></td>`).join('')}
      <td class="td tnum text-right font-medium" data-cell="percent">${pct(s.percent)}</td>
      <td class="td" data-cell="letter">${letterPill(s.letter)}</td>
      <td class="td tnum text-right" data-cell="gpa">${s.gpa == null ? '–' : s.gpa.toFixed(1)}</td>
    </tr>`).join('');

  const foot = snap.assignments.map((a, j) => `<td class="td tnum text-right font-medium" data-avg="${j}">${pct(a.average)}</td>`).join('');

  box.innerHTML = `
    <table class="ledger w-full border-collapse">
      <thead><tr>
        <th class="th sticky-col">Student</th>${head}
        <th class="th text-right">Final</th><th class="th">Letter</th><th class="th text-right">GPA pts</th>
      </tr></thead>
      <tbody>${rows}</tbody>
      <tfoot><tr>
        <td class="td sticky-col font-semibold">Class average</td>${foot}
        <td class="td tnum text-right font-semibold" data-avg-final>${pct(snap.stats.classAverage)}</td>
        <td class="td"></td><td class="td tnum text-right font-semibold">${snap.stats.classGpa == null ? '–' : snap.stats.classGpa.toFixed(2)}</td>
      </tr></tfoot>
    </table>`;
}

// Update computed cells in place so keyboard focus is not lost while typing grades.
function patchGradebook() {
  const set = (el, html) => { if (el) el.innerHTML = html; };
  snap.students.forEach((s) => {
    const row = $(`#grade-table tr[data-row="${s.index}"]`);
    if (!row) return;
    set($('[data-cell="percent"]', row), pct(s.percent));
    set($('[data-cell="letter"]', row), letterPill(s.letter));
    set($('[data-cell="gpa"]', row), s.gpa == null ? '–' : s.gpa.toFixed(1));
  });
  snap.assignments.forEach((a, j) => set($(`#grade-table [data-avg="${j}"]`), pct(a.average)));
  set($('#grade-table [data-avg-final]'), pct(snap.stats.classAverage));
}

function renderAttendance() {
  const box = $('#att-table');
  if (snap.students.length === 0 || snap.days.length === 0) {
    box.innerHTML = `<p class="p-6 text-sm text-mute">${snap.students.length === 0
      ? 'Add students on the Roster tab first.'
      : 'Add a class date above to start taking attendance.'}</p>`;
    return;
  }

  const head = snap.days.map((d, i) => `
    <th class="th text-center">
      <div class="tnum">${esc(d.label)}</div>
      <div class="mt-1 flex justify-center gap-1">
        <button type="button" class="rounded border border-line bg-white px-1.5 py-0.5 text-xs font-normal hover:bg-paper" data-all-present="${i}">All present</button>
        <button type="button" class="rounded border border-line bg-white px-1.5 py-0.5 text-xs font-normal text-pencil hover:bg-[#F8E4E1]" data-del-day="${i}" aria-label="Remove ${esc(d.label)}">Remove</button>
      </div>
    </th>`).join('');

  const stateName = (v) => (v === 1 ? 'present' : v === 0 ? 'absent' : 'not marked');
  const rows = snap.students.map((s) => `
    <tr>
      <td class="td sticky-col font-medium">${esc(s.name)}</td>
      ${s.attendance.map((v, d) => `
        <td class="td text-center"><button type="button" class="att ${v === 1 ? 'att-p' : v === 0 ? 'att-a' : ''}"
          data-s="${s.index}" data-d="${d}" data-state="${v ?? ''}"
          aria-label="${esc(s.name)}, ${esc(snap.days[d].label)}: ${stateName(v)}">${v === 1 ? 'P' : v === 0 ? 'A' : ''}</button></td>`).join('')}
      <td class="td tnum text-right">${s.present}</td>
      <td class="td tnum text-right">${s.recorded - s.present}</td>
      <td class="td tnum text-right font-medium">${pct(s.attendanceRate)}</td>
    </tr>`).join('');

  const foot = snap.days.map((d) => `<td class="td tnum text-center font-medium">${pct(d.rate)}</td>`).join('');

  box.innerHTML = `
    <table class="ledger w-full border-collapse">
      <thead><tr>
        <th class="th sticky-col">Student</th>${head}
        <th class="th text-right">Present</th><th class="th text-right">Absent</th><th class="th text-right">Rate</th>
      </tr></thead>
      <tbody>${rows}</tbody>
      <tfoot><tr>
        <td class="td sticky-col font-semibold">Daily rate</td>${foot}
        <td class="td"></td><td class="td"></td><td class="td tnum text-right font-semibold">${pct(snap.stats.attendanceRate)}</td>
      </tr></tfoot>
    </table>`;
}

// ---------------------------------------------------------------------------
// UI events
// ---------------------------------------------------------------------------
function bindUi() {
  // tabs
  $$('[data-tab]').forEach((b) => b.addEventListener('click', () => setTab(b.dataset.tab)));

  // class name
  $('#class-name').addEventListener('input', () => cm && save());

  // roster
  $('#student-form').addEventListener('submit', (e) => {
    e.preventDefault();
    if (act(cm.addStudent($('#new-id').value, $('#new-name').value), { focus: '#new-id' })) {
      $('#new-id').value = '';
      $('#new-name').value = '';
    }
  });
  $('#sort-mode').addEventListener('change', (e) => {
    act(cm.sortRoster(Number(e.target.value)));
    e.target.selectedIndex = 0;
  });
  $('#roster-table').addEventListener('click', (e) => {
    const edit = e.target.closest('[data-edit-student]');
    const del = e.target.closest('[data-del-student]');
    if (edit) {
      const s = snap.students[Number(edit.dataset.editStudent)];
      $('#edit-student-index').value = s.index;
      $('#edit-student-id').value = s.id;
      $('#edit-student-name').value = s.name;
      $('#student-dialog').showModal();
    } else if (del) {
      const s = snap.students[Number(del.dataset.delStudent)];
      if (confirm(`Delete ${s.name}? Their grades and attendance will be removed.`)) act(cm.deleteStudent(s.index));
    }
  });
  $('#student-edit-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const ok = act(cm.editStudent(Number($('#edit-student-index').value), $('#edit-student-id').value, $('#edit-student-name').value));
    if (ok) $('#student-dialog').close();
  });

  // gradebook
  $('#add-assignment').addEventListener('click', () => openAssignmentDialog(-1));
  $('#grade-table').addEventListener('change', (e) => {
    const input = e.target.closest('.grade-input');
    if (!input) return;
    const raw = input.value.trim();
    const score = raw === '' ? -1 : Number(raw);
    act(cm.setGrade(Number(input.dataset.s), Number(input.dataset.a), score), { patchGrades: true });
  });
  $('#grade-table').addEventListener('click', (e) => {
    const b = e.target.closest('[data-edit-assignment]');
    if (b) openAssignmentDialog(Number(b.dataset.editAssignment));
  });
  $('#assignment-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const idx = Number($('#assignment-index').value);
    const name = $('#assignment-name').value;
    const max = Number($('#assignment-max').value);
    const weight = Number($('#assignment-weight').value);
    const code = idx < 0 ? cm.addAssignment(name, max, weight) : cm.editAssignment(idx, name, max, weight);
    if (act(code)) $('#assignment-dialog').close();
  });
  $('#assignment-delete').addEventListener('click', () => {
    const idx = Number($('#assignment-index').value);
    const a = snap.assignments[idx];
    if (a && confirm(`Delete "${a.name}"? All scores for it will be removed.`)) {
      if (act(cm.deleteAssignment(idx))) $('#assignment-dialog').close();
    }
  });

  // attendance
  $('#day-form').addEventListener('submit', (e) => {
    e.preventDefault();
    act(cm.addDay($('#new-day').value));
  });
  $('#att-table').addEventListener('click', (e) => {
    const cell = e.target.closest('.att');
    const all = e.target.closest('[data-all-present]');
    const del = e.target.closest('[data-del-day]');
    if (cell) {
      const s = Number(cell.dataset.s), d = Number(cell.dataset.d);
      const cur = cell.dataset.state;                       // '' | '1' | '0'
      const next = cur === '' ? 1 : cur === '1' ? 0 : -1;   // not marked -> present -> absent -> not marked
      act(cm.setAttendance(s, d, next), { focus: `.att[data-s="${s}"][data-d="${d}"]` });
    } else if (all) {
      act(cm.markDayAllPresent(Number(all.dataset.allPresent)));
    } else if (del) {
      const day = snap.days[Number(del.dataset.delDay)];
      if (confirm(`Remove ${day.label} and its attendance marks?`)) act(cm.deleteDay(Number(del.dataset.delDay)));
    }
  });

  // dialogs: close buttons + click on backdrop
  $$('dialog').forEach((dlg) => {
    $$('[data-close]', dlg).forEach((b) => b.addEventListener('click', () => dlg.close()));
    dlg.addEventListener('click', (e) => { if (e.target === dlg) dlg.close(); });
  });

  // import / export / sample / clear
  $('#export-json').addEventListener('click', () => {
    if (!cm) return;
    download(`${slug()}.json`, JSON.stringify({ ...JSON.parse(cm.toJson()), className: className() }, null, 2), 'application/json');
  });
  $('#export-csv').addEventListener('click', () => cm && download(`${slug()}.csv`, cm.toCsv(), 'text/csv'));
  $('#import-file').addEventListener('change', onImportFile);
  $('#load-sample').addEventListener('click', () => {
    if (cm && confirm('Replace the current class with the sample class?')) { loadSample(); refresh(); toast('Sample class loaded.'); }
  });
  $('#clear-all').addEventListener('click', () => {
    if (cm && confirm('Remove all students, grades, and attendance?')) { cm.clear(); refresh(); toast('All data cleared.'); }
  });
}

function setTab(name) {
  activeTab = name;
  $$('[data-tab]').forEach((b) => b.setAttribute('aria-selected', String(b.dataset.tab === name)));
  $$('[data-panel]').forEach((p) => p.classList.toggle('hidden', p.dataset.panel !== name));
}

function openAssignmentDialog(index) {
  const editing = index >= 0;
  const a = editing ? snap.assignments[index] : { name: '', maxPoints: 100, weight: 10 };
  $('#assignment-dialog-title').textContent = editing ? 'Edit assignment' : 'Add assignment';
  $('#assignment-index').value = index;
  $('#assignment-name').value = a.name;
  $('#assignment-max').value = a.maxPoints;
  $('#assignment-weight').value = a.weight;
  $('#assignment-delete').classList.toggle('hidden', !editing);
  $('#assignment-dialog').showModal();
}

// ---------------------------------------------------------------------------
// Import / export / persistence
// ---------------------------------------------------------------------------
const className = () => $('#class-name').value.trim() || 'My class';
const slug = () => className().toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'class-records';

function download(filename, text, type) {
  const url = URL.createObjectURL(new Blob([text], { type }));
  const a = Object.assign(document.createElement('a'), { href: url, download: filename });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

async function onImportFile(e) {
  const file = e.target.files[0];
  e.target.value = '';
  if (!file || !cm) return;
  if (!confirm('Importing replaces all current data. Continue?')) return;
  try {
    const text = await file.text();
    const data = /\.csv$/i.test(file.name) ? csvToData(parseCsv(text)) : JSON.parse(text);
    const skipped = importData(data);
    refresh();
    toast(skipped ? `Imported. ${skipped} student${skipped > 1 ? 's were' : ' was'} skipped (duplicate ID or limit reached).` : 'Import complete.');
  } catch (err) {
    console.error(err);
    toast(`Could not import that file: ${err.message}`, true);
  }
}

// Rebuild the C++ model from a plain object (JSON backup, CSV conversion, or saved state).
function importData(data) {
  if (!data || typeof data !== 'object' || !Array.isArray(data.students)) throw new Error('This file does not look like class records.');
  cm.clear();
  if (typeof data.className === 'string') $('#class-name').value = data.className;

  const aMap = (data.assignments || []).map((a, i) => cm.addAssignment(
    String(a.name ?? `Assignment ${i + 1}`), Number(a.maxPoints ?? 100), a.weight == null ? 1 : Number(a.weight)));
  const dMap = (data.days || []).map((d) => cm.addDay(String(typeof d === 'string' ? d : d.label ?? '')));

  let skipped = 0;
  data.students.forEach((st) => {
    const si = cm.addStudent(String(st.id ?? ''), String(st.name ?? ''));
    if (si < 0) { skipped++; return; }
    (st.grades || []).forEach((g, i) => {
      if (g != null && g !== '' && aMap[i] >= 0 && Number.isFinite(Number(g))) cm.setGrade(si, aMap[i], Number(g));
    });
    (st.attendance || []).forEach((v, i) => {
      if (v != null && dMap[i] >= 0) cm.setAttendance(si, dMap[i], v ? 1 : 0);
    });
  });
  return skipped;
}

// Minimal RFC 4180 CSV parser (handles quoted fields, escaped quotes, CRLF).
function parseCsv(text) {
  const rows = [];
  let row = [], field = '', inQuotes = false;
  text = text.replace(/^\uFEFF/, '');
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (inQuotes) {
      if (c === '"') { if (text[i + 1] === '"') { field += '"'; i++; } else inQuotes = false; }
      else field += c;
    } else if (c === '"') inQuotes = true;
    else if (c === ',') { row.push(field); field = ''; }
    else if (c === '\n' || c === '\r') {
      if (c === '\r' && text[i + 1] === '\n') i++;
      row.push(field); field = ''; rows.push(row); row = [];
    } else field += c;
  }
  if (field !== '' || row.length) { row.push(field); rows.push(row); }
  return rows.filter((r) => r.some((f) => f.trim() !== ''));
}

// CSV layout written by cm.toCsv(): ID, Name, <assignments...>, Final %, Letter, GPA Points, Attendance %
// followed by "Max Points" and "Weight" rows. Columns from "Final %" onward are ignored on import.
function csvToData(rows) {
  if (rows.length < 2) throw new Error('The CSV needs a header row and at least one student.');
  const head = rows[0].map((h) => h.trim());
  let end = head.findIndex((h) => /^final\b/i.test(h));
  if (end < 0) end = head.length;
  const names = head.slice(2, end);

  let r = 1;
  let max = names.map(() => 100), weight = names.map(() => 1);
  if (/^max/i.test((rows[r]?.[0] || '').trim())) {
    max = names.map((_, k) => Number(rows[r][2 + k]) || 100);
    r++;
  }
  if (/^weight/i.test((rows[r]?.[0] || '').trim())) {
    weight = names.map((_, k) => { const v = parseFloat(rows[r][2 + k]); return Number.isFinite(v) ? v : 1; });
    r++;
  }
  return {
    assignments: names.map((name, k) => ({ name, maxPoints: max[k], weight: weight[k] })),
    days: [],
    students: rows.slice(r).map((row) => ({
      id: (row[0] || '').trim(),
      name: (row[1] || '').trim(),
      grades: names.map((_, k) => { const v = (row[2 + k] || '').trim(); return v === '' ? null : Number(v); }),
    })),
  };
}

function save() {
  try { localStorage.setItem(STORAGE_KEY, JSON.stringify({ ...snap, className: className() })); } catch { /* storage unavailable */ }
}

function restore() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return false;
    importData(JSON.parse(raw));
    return true;
  } catch { return false; }
}

function loadSample() {
  importData({
    className: 'Grade 9 Science',
    assignments: [
      { name: 'Quiz 1', maxPoints: 20, weight: 10 },
      { name: 'Homework', maxPoints: 50, weight: 15 },
      { name: 'Midterm', maxPoints: 100, weight: 30 },
      { name: 'Project', maxPoints: 100, weight: 25 },
      { name: 'Final exam', maxPoints: 100, weight: 20 },
    ],
    days: ['2026-10-05', '2026-10-06', '2026-10-07', '2026-10-08', '2026-10-09'],
    students: [
      { id: 'S001', name: 'Amara Okafor', grades: [18, 46, 88, 92, null], attendance: [1, 1, 1, 1, 1] },
      { id: 'S002', name: 'Kofi Mensah', grades: [15, 41, 74, 81, null], attendance: [1, 0, 1, 1, 0] },
      { id: 'S003', name: 'Lena Fischer', grades: [20, 50, 95, 98, null], attendance: [1, 1, 1, 1, 1] },
      { id: 'S004', name: 'Mateo Ruiz', grades: [12, 33, 61, 70, null], attendance: [1, 0, 0, 1, 0] },
      { id: 'S005', name: 'Priya Nair', grades: [17, 44, 83, 79, null], attendance: [1, 1, 1, 0, 1] },
      { id: 'S006', name: 'Jonas Berg', grades: [null, 38, 55, 64, null], attendance: [0, 1, 0, 1, 1] },
      { id: 'S007', name: 'Hana Kim', grades: [19, 48, 91, 87, null], attendance: [1, 1, 1, 1, 0] },
      { id: 'S008', name: 'Sam Carter', grades: [14, 36, 69, 72, null], attendance: [1, 1, 0, 1, 1] },
    ],
  });
}

// ---------------------------------------------------------------------------
// Toasts
// ---------------------------------------------------------------------------
let toastTimer;
function toast(message, isError = false) {
  const box = $('#toast');
  box.innerHTML = `<div class="pointer-events-auto max-w-md rounded-md px-4 py-2.5 text-sm shadow-lg ${isError ? 'bg-pencil text-white' : 'bg-board text-chalk'}">${esc(message)}</div>`;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { box.innerHTML = ''; }, 4500);
}

boot();
