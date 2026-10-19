/* TestFlight coordinates existing FE macros. Loading this module never executes hardware commands. */
(function (root) {
    'use strict';
    const api = {};
    const BUFFER = 'Buffer Test Detached';
    const COUNTERS = 'ROC FEMacro - Get DTC Counter Values';
    const CFO = 'Compile, Set, and Launch Fixed-width Event Window Template Run Plan';
    const fields = ['txEwm', 'txDataRequest', 'txHeartbeat', 'rxDataHeader'];
    const labels = ['TX EWM Count', 'TX Data Request Count', 'TX Heartbeat Count', 'RX Data Header Count'];
    const requireOK = (ok, message) => { if (!ok) throw new Error(message); };
    const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
    api.options = function (duration, markers) {
        const m = /^\s*(\d+(?:\.\d+)?)\s*(s|ms|us|ns|clocks)\s*$/.exec(String(duration));
        requireOK(m, 'Use a positive duration with units: s, ms, us, ns, or clocks.');
        const unit = {s: 1000, ms: 1, us: .001, ns: .000001, clocks: .000025}[m[2]];
        const ms = Number(m[1]) * unit;
        requireOK(Number.isFinite(ms) && ms >= .000025 * (1 - 1e-12), 'Duration must be at least one 25 ns clock.');
        requireOK(/^\d+$/.test(String(markers)) && Number.isSafeInteger(Number(markers)) && Number(markers) >= 2 && Number(markers) <= 0xffffffff,
            'Marker count must be an integer between 2 and 4294967295; infinite tests are not supported.');
        requireOK(ms * Number(markers) <= 60000, 'Choose a finite test lasting at most 60 seconds.');
        return {duration: m[1] + m[2], markers: Number(markers), duration_ms: ms, run_ms: ms * (Number(markers) - 1)};
    };
    api.readerInputs = function (macro, command) {
        requireOK(macro && macro.inputs && /^Command to 0\/Status/.test(macro.inputs[0]), 'Unsupported Buffer Test Detached macro.');
        const defaults = ['0', 'true', '0', 'false', 'false', '', 'false', '0', 'false'];
        requireOK(macro.inputs.length === defaults.length, 'Unexpected Buffer Test Detached inputs; reload Tracker.');
        return Object.fromEntries(macro.inputs.map((name, i) => [name, i === 0 ? String(command) : defaults[i]]));
    };
    api.cfoInputs = function (macro, options, enabled) {
        requireOK(macro && macro.inputs && macro.inputs.length === 11 && /^Enable CFO Run Plan Execution/.test(macro.inputs[0]) &&
            /^Number of Event Window Markers/.test(macro.inputs[2]), 'Unsupported CFO fixed-width run-plan macro.');
        const values = [enabled ? 'true' : 'false', options.duration, String(options.markers), 'Default', '1', 'true', 'true', 'false', 'false', 'false', 'false'];
        return Object.fromEntries(macro.inputs.map((name, i) => [name, values[i]]));
    };
    api.output = function (result, uid) {
        requireOK(result && !result.error, result && result.error || 'Missing macro response.');
        requireOK(result.targets && result.targets.length === 1 && result.targets[0].uid === uid, 'Expected exactly one result from ' + uid + '.');
        return result.targets[0].outputs;
    };
    api.reader = function (result, uid, enabledLinks) {
        const out = api.output(result, uid), text = out.Result;
        requireOK(typeof text === 'string', uid + ': missing reader status.');
        const running = /Detached thread running:\s*(true|false)/.exec(text);
        const count = /Subevents count:\s*(\d+)/.exec(text);
        requireOK(running && count && Number.isSafeInteger(Number(count[1])), uid + ': unrecognized reader status.');
        const errorDetails = [], ignoredTimeouts = [];
        if (/Error identified|Detached thread caught error|Exception caught|stuck running/i.test(text))
            errorDetails.push('detached reader reports an error; see raw status');
        // Disabled ROCs generate timeout placeholders. Match each counter to
        // its ROC before applying the active topology, as the FE reader does.
        for (const m of text.matchAll(/Roc-([0-5])\s+(Fragment Errors|Subevent Timeouts|Fragment Timeouts|Fragment Header Timeouts) count:\s*(\d+)/g)) {
            const link = Number(m[1]), count = Number(m[3]);
            if (!count) continue;
            const detail = 'ROC' + link + ' ' + m[2] + ': ' + count;
            if (enabledLinks && !enabledLinks[link] && m[2] !== 'Fragment Errors') ignoredTimeouts.push(detail);
            else errorDetails.push(detail);
        }
        return {running: running[1] === 'true', count: Number(count[1]), errors: errorDetails.length > 0,
            error_details: errorDetails, ignored_timeouts: ignoredTimeouts, text};
    };
    api.readerProblem = r => (!r.running ? ['reader is not running'] : []).concat(r.error_details).join('; ');
    api.counters = function (result, uid) {
        const out = api.output(result, uid);
        let value;
        try { value = JSON.parse(out['Counter Values']); } catch (_) { throw new Error(uid + ': invalid Counter Values output.'); }
        requireOK(value.schemaVersion === 1 && Array.isArray(value.links) && value.links.length === 6, uid + ': unsupported counter schema.');
        const seen = new Set();
        value.links.forEach(link => {
            requireOK(Number.isInteger(link.link) && link.link >= 0 && link.link < 6 && !seen.has(link.link), uid + ': invalid ROC link IDs.');
            seen.add(link.link);
            fields.forEach(f => requireOK(Number.isInteger(link[f]) && link[f] >= 0 && link[f] <= 0xffffffff, uid + ': invalid counter ' + f));
        });
        return value;
    };
    api.delta = function (before, after) {
        const byLink = Object.fromEntries(before.links.map(l => [l.link, l]));
        return {schemaVersion: 1, links: after.links.map(l => Object.assign({link: l.link}, Object.fromEntries(fields.map(f =>
            [f, (l[f] - byLink[l.link][f] + 0x100000000) % 0x100000000]))))};
    };
    api.run = async function (deps, options, report, progress, control) {
        control = control || {}; progress = progress || (() => {});
        report.options = options; report.started_at = new Date().toISOString();
        report.owned = []; report.reused = []; report.readers = {}; report.before = {}; report.after = {}; report.cleanup = [];
        let session, launched = false, dirty = false;
        const checkStop = () => requireOK(!control.stop, 'TestFlight stopped by operator.');
        const batch = async (targets, task) => {
            const settled = await Promise.allSettled(targets.map(task));
            const failures = settled.map((r, i) => r.status === 'rejected' ? (r.reason.message.startsWith(targets[i].uid + ':') ? r.reason.message : targets[i].uid + ': ' + r.reason.message) : null).filter(Boolean);
            requireOK(!failures.length, failures.join('\n'));
            return settled.map(r => r.value);
        };
        try {
            progress('Checking active tracker and CFO configuration…');
            session = await deps.prepare(); report.targets = session.targets; report.cfo = session.cfo; report.groups = session.groups;
            await deps.guard(session); checkStop();
            // Existing readers are reused without restarting or claiming ownership.
            const initial = await batch(session.targets, async t => {
                const r = api.reader(await deps.buffer(t, 0), t.uid, t.links);
                requireOK(!r.errors, t.uid + ': existing reader reports errors: ' + r.error_details.join('; ') + '. Halt it and inspect the report before retrying.');
                return r;
            });
            initial.forEach((r, i) => { if (r.running) report.reused.push(session.targets[i].uid); });
            progress('Starting detached readers on ' + session.targets.length + ' tracker DTCs in parallel…');
            await batch(session.targets, async (t) => {
                checkStop();
                if (!report.reused.includes(t.uid)) report.owned.push(t.uid); // include uncertain starts in cleanup
                dirty = true;
                const r = api.reader(await deps.buffer(t, 1), t.uid, t.links);
                requireOK(r.running && !r.errors, t.uid + ': detached reader did not start cleanly: ' + api.readerProblem(r));
            });
            await deps.guard(session); checkStop();
            progress('Taking baseline counters and reader counts…');
            await batch(session.targets, async t => {
                report.before[t.uid] = api.counters(await deps.counters(t), t.uid);
                const r = api.reader(await deps.buffer(t, 0), t.uid, t.links);
                requireOK(r.running && !r.errors, t.uid + ': reader is not ready: ' + api.readerProblem(r));
                report.readers[t.uid] = {before: r};
            });
            await deps.guard(session); checkStop();
            progress('Launching CFO: ' + options.markers + ' markers at ' + options.duration + '…');
            launched = true; // an ambiguous launch response still requires disabling the test plan
            const launch = api.output(await deps.cfo(session, options, true), session.cfo.uid);
            requireOK(typeof launch.response === 'string' && launch.response.includes('Launched CFO Run Plan!'), 'CFO launch was not confirmed.');
            report.launch_response = launch.response;
            const until = deps.now() + options.run_ms;
            while (deps.now() < until) { checkStop(); await deps.guard(session); await deps.wait(Math.min(500, until - deps.now())); }
            progress('Waiting for tracker data to drain…');
            const deadline = deps.now() + 10000; let stable = 0, previous;
            while (deps.now() <= deadline) {
                checkStop(); await deps.guard(session);
                const states = await batch(session.targets, async t => {
                    const r = api.reader(await deps.buffer(t, 0), t.uid, t.links);
                    report.readers[t.uid].after = r;
                    requireOK(r.running && !r.errors, t.uid + ': ' + api.readerProblem(r));
                    return r;
                });
                const counts = states.map(r => r.count).join(',');
                const received = states.every((r, i) => r.count - report.readers[session.targets[i].uid].before.count >= options.markers - 1);
                stable = received && counts === previous ? stable + 1 : 0; previous = counts;
                if (stable >= 2) { report.drained = true; break; }
                await deps.wait(500);
            }
            requireOK(report.drained, 'Data did not drain within 10 seconds. See per-DTC subevent counts and counter differences.');
        } catch (e) { report.error = e.message; }
        finally {
            // Finish all submitted starts before entering here (batch uses allSettled).
            if (session && launched) {
                progress('Stopping the CFO test plan…');
                try {
                    const stopped = api.output(await deps.cfo(session, options, false), session.cfo.uid);
                    requireOK(typeof stopped.response === 'string' && stopped.response.includes('Halted CFO Emulator!'), 'CFO disable was not confirmed.');
                    report.cfo_stopped = true;
                } catch (e) { report.cleanup.push('CFO stop unconfirmed: ' + e.message); }
            }
            if (session && dirty) {
                progress('Collecting final counters and stopping readers started by TestFlight…');
                await Promise.all(session.targets.map(async t => {
                    try { report.after[t.uid] = api.counters(await deps.counters(t), t.uid); }
                    catch (e) { report.cleanup.push(t.uid + ' final counters: ' + e.message); }
                }));
                await Promise.all(session.targets.filter(t => report.owned.includes(t.uid)).map(async t => {
                    try {
                        const r = api.reader(await deps.buffer(t, 2), t.uid, t.links);
                        requireOK(!r.running, 'reader is still running');
                        report.cleanup.push(t.uid + ': reader stopped');
                    } catch (e) { report.cleanup.push(t.uid + ' halt unconfirmed: ' + e.message); }
                }));
            }
            report.finished_at = new Date().toISOString();
            report.success = !!report.drained && !report.error && report.cfo_stopped && !report.cleanup.some(s => /unconfirmed|final counters:/.test(s));
            report.rows = [];
            if (session) session.targets.forEach(t => {
                if (!report.before[t.uid] || !report.after[t.uid]) return;
                const delta = api.delta(report.before[t.uid], report.after[t.uid]);
                delta.links.forEach(l => {
                    if (!t.links[l.link]) return;
                    const expected = [options.markers, options.markers - 1, options.markers, options.markers - 1];
                    const pass = fields.every((f, i) => l[f] === expected[i]);
                    if (!pass) report.success = false;
                    report.rows.push({uid: t.uid, link: l.link, counts: fields.map(f => l[f]), expected, pass});
                });
            });
            progress(report.success ? 'TestFlight completed.' : 'TestFlight finished with errors or counter mismatches.');
        }
        return report;
    };
    function values(xml, tag) { return Array.from(xml.getElementsByTagName(tag)).map(n => n.getAttribute('value') || ''); }
    function one(xml, tag) { const v = values(xml, tag); requireOK(v.length === 1 && v[0] !== '', 'Missing/ambiguous ' + tag); return v[0]; }
    // Same authentication protocol as DesktopContent, with an explicit origin for
    // the separate CFO gateway. The session stays in memory and is never logged.
    api.request = function (scope, lid, type, query, body) {
        return new Promise((resolve, reject) => {
            const dc = root.DesktopContent;
            requireOK(dc && dc._cookieCodeMailbox && !dc._sequence, 'Open Tracker from the authenticated OTS desktop.');
            const xhr = new XMLHttpRequest();
            // OTS decodeURIComponent decodes %20 but preserves '+'. Do not use
            // URLSearchParams' form encoding for macro names or query values.
            const params = Object.entries(Object.assign({RequestType: type}, query || {}))
                .map(([key, value]) => encodeURIComponent(key) + '=' + encodeURIComponent(String(value))).join('&');
            const url = scope.origin + '/urn:xdaq-application:lid=' + lid + '/Request?' + params;
            xhr.open('POST', url, true); xhr.timeout = type === 'runFEMacro' ? 120000 : 25000;
            xhr.setRequestHeader('Content-Type', 'text/plain;charset=UTF-8');
            xhr.onerror = () => reject(new Error(type + ': connection failed; outcome may be uncertain. No command was retried.'));
            xhr.ontimeout = () => reject(new Error(type + ': request timed out; outcome may be uncertain. No command was retried.'));
            xhr.onload = () => {
                try {
                    const xml = xhr.responseXML || new DOMParser().parseFromString(xhr.responseText, 'application/xml');
                    requireOK(xhr.status === 200 && !xml.getElementsByTagName('parsererror').length && xml.getElementsByTagName('DATA').length,
                        type + ': login or XML response unavailable. Open the CFO/Tracker desktop and check login/lock.');
                    const errors = api.responseErrors(xml, type);
                    requireOK(!errors.length, errors.join('\n')); resolve(xml);
                } catch (e) { reject(e); }
            };
            xhr.send('RequestOrigin=' + encodeURIComponent(dc._topServerOrigin || dc._serverOrigin) + '&CookieCode=' + dc._cookieCodeMailbox + (body ? '&' + body : ''));
        });
    };
    api.parseMacros = function (xml) {
        const fes = {};
        values(xml, 'FEMacros').forEach(text => {
            const a = text.split(';'), uid = a[3]; let i = 4;
            requireOK(a.length >= 4 && !fes[uid], 'Invalid/duplicate FE macro target.');
            const fe = fes[uid] = {uid, feClass: a[2], macros: {}};
            while (i < a.length && a[i]) {
                const name = a[i]; i += 3;
                const inputs = a.slice(i + 1, i + 1 + Number(a[i])).map(decodeURIComponent); i += 1 + Number(a[i]);
                const outputs = a.slice(i + 1, i + 1 + Number(a[i])).map(decodeURIComponent); i += 1 + Number(a[i]);
                fe.macros[name] = {inputs, outputs};
            }
        });
        return fes;
    };
    api.cfoRoute = function (xml) {
        const icons = one(xml, 'iconList').split(','), routes = new Map();
        requireOK(icons.length % 7 === 0, 'Unsupported desktop icon list (' + icons.length + ' fields).');
        for (let i = 0; i < icons.length; i += 7) {
            if (!/CFO/i.test(icons[i + 6])) continue;
            const u = new URL(icons[i + 5], root.location.origin);
            if (u.searchParams.get('forcedWizMode') === '1') continue;
            const origin = u.searchParams.get('remoteServerOrigin'), lid = u.searchParams.get('remoteServerUrnLid');
            if (origin && /^\d+$/.test(lid || '')) {
                requireOK(['http:', 'https:'].includes(new URL(origin).protocol), 'Invalid CFO origin.');
                routes.set(origin + '/' + lid, {origin: new URL(origin).origin, lid: Number(lid)});
            }
        }
        requireOK(routes.size === 1, 'Cannot uniquely discover the CFO gateway from desktop icons (' + routes.size + ' routes). Open Tracker from the top-level OTS desktop.');
        return Array.from(routes.values())[0];
    };
    api.responseErrors = function (xml, type) {
        const errors = values(xml, 'Error').filter(Boolean);
        if (type !== 'getDesktopIcons' || !errors.length) return errors;
        // getDesktopIcons has no subsystem filter. It returns usable CFO icons
        // alongside errors for offline, unrelated remote gateways. Require the
        // unique CFO route first; retain every CFO, login, or unknown error.
        try { api.cfoRoute(xml); } catch (e) { return ['CFO discovery: ' + e.message]; }
        const unrelatedFailure = /(?:\|GatewaySupervisor:\s*[^|\n]+\|\s*)?Connection failed for '[^']*' icon retrieval from Remote Gateway '([^']+)' at [^\n]*?\. Please check that the target is up and running at the correct IP address\.\s*/g;
        return errors.map(error => error.replace(unrelatedFailure, (text, gateway) => /CFO/i.test(gateway) ? text : '')).filter(error => error.trim());
    };
    api.assertState = function (xml, name, allowed) {
        const current = one(xml, 'current_state'), transition = one(xml, 'in_transition');
        requireOK(allowed.includes(current) && transition === '0', name + ' is ' + current + ' (transition=' + transition + '). Expected ' + allowed.join(' or ') + ', with no transition in progress.');
    };
    api.activatedGroups = async function (scope) {
        // getActiveTableGroups describes the user's Configuration GUI editor.
        // Ask the gateway for persisted activation records instead, then still
        // verify the loaded configuration before permitting a hardware test.
        const xml = await api.request(scope, scope.config, 'getLastTableGroups');
        const group = type => {
            const name = one(xml, 'LastActivated' + type + 'GroupName');
            const key = one(xml, 'LastActivated' + type + 'GroupKey');
            requireOK(/^\d+$/.test(key) && Number.isSafeInteger(Number(key)) && Number(key) <= 0xfffffffe,
                'Invalid ' + scope.name + ' last activated ' + type + ' group key.');
            return {name, key};
        };
        try {
            return Object.assign(group('Config'), {context: group('Context')});
        } catch (e) { throw new Error(scope.name + ' saved active groups: ' + e.message); }
    };
    api.browserDependencies = async function () {
        const trackerDeps = await TrackerRateMask.browserDependencies();
        const dc = DesktopContent;
        const tracker = {name: 'Tracker', origin: dc._localOrigin, lid: Number(dc._remoteServerUrnLid || dc._serverUrnLid)};
        const top = {name: 'Top-level DAQ', origin: dc._topServerOrigin || dc._serverOrigin, lid: Number(dc._topServerUrnLid || dc._serverUrnLid)};
        const app = async (scope, classname) => Number(one(await api.request(scope, scope.lid, 'getAppId', {classNeedle: classname}), 'id'));
        const cfo = api.cfoRoute(await api.request(top, top.lid, 'getDesktopIcons')); cfo.name = 'CFO';
        cfo.mm = await app(cfo, 'ots::MacroMakerSupervisor'); cfo.config = await app(cfo, 'ots::ConfigurationGUISupervisor');
        const trackerMM = await app(tracker, 'ots::MacroMakerSupervisor');
        const activeCFO = () => api.activatedGroups(cfo);
        const state = async scope => {
            const xml = await api.request(scope, scope.lid, 'getCurrentState');
            api.assertState(xml, scope.name, scope === top ? ['Configured', 'Halted', 'Initial'] : ['Configured']);
        };
        async function macro(scope, mm, fe, name, inputs) {
            const m = fe.macros[name]; requireOK(m, 'Missing macro ' + name + ' on ' + fe.uid);
            const body = 'inputArgs=' + m.inputs.map(n => encodeURIComponent(n) + ',' + encodeURIComponent(inputs[n] === undefined ? '' : inputs[n])).join(';') +
                '&outputArgs=' + m.outputs.map(encodeURIComponent).join(',');
            let xml = await api.request(scope, mm, 'runFEMacro', {feClassSelected: fe.feClass, feUIDSelected: fe.uid, macroType: 'fe', macroName: name, saveOutputs: 0}, body);
            const deadline = Date.now() + 120000;
            while (values(xml, 'NotDoneID').length) {
                requireOK(Date.now() < deadline, 'Macro completion is unconfirmed after two minutes. Inspect the device before retrying.');
                const id = one(xml, 'NotDoneID'); await sleep(500);
                xml = await api.request(scope, mm, 'runFEMacro', {NotDoneID: id});
            }
            const targets = Array.from(xml.getElementsByTagName('feMacroExec')).map(n => {
                const names = values(n, 'outputArgs_name'), vals = values(n, 'outputArgs_value');
                return {uid: one(n, 'fe_uid'), outputs: Object.fromEntries(names.map((s, i) => [decodeURIComponent(s), decodeURIComponent(vals[i])]))};
            });
            if (!targets.length && values(xml, 'feMacroRunArgs_name').length) {
                const names = values(xml, 'feMacroRunArgs_name'), vals = values(xml, 'feMacroRunArgs_value');
                targets.push({uid: fe.uid, outputs: Object.fromEntries(names.map((s, i) => [decodeURIComponent(s), decodeURIComponent(vals[i])]))});
            }
            return {error: null, targets};
        }
        const deps = {now: () => Date.now(), wait: sleep}; let trackerFEs, cfoFE;
        async function guardTrigger() {
            const xml = await api.request(top, top.lid, 'getRemoteSubsystemStatus');
            const names = values(xml, 'subsystem_name'), states = values(xml, 'subsystem_status');
            const i = names.indexOf('Trigger_Gateway');
            requireOK(i >= 0 && ['Configured', 'Halted', 'Initial'].includes(states[i]), 'Trigger DAQ is running, transitioning, or unavailable. Stop data taking before TestFlight.');
        }
        deps.guard = async session => {
            await trackerDeps.guard(session.groups.tracker);
            await Promise.all([state(cfo), state(top), guardTrigger()]);
            const current = await activeCFO();
            requireOK(TrackerRateMask.sameGroup(session.groups.cfo, current) && TrackerRateMask.sameGroup(session.groups.cfo.context, current.context), 'CFO active configuration changed.');
        };
        deps.prepare = async () => {
            await Promise.all([state(tracker), state(cfo), state(top), guardTrigger()]);
            const topology = await trackerDeps.structure();
            trackerFEs = api.parseMacros(await api.request(tracker, trackerMM, 'getFEMacroList'));
            const targets = [];
            topology.apps.forEach(a => (a.dtcs || []).forEach(d => {
                if (a.enabled !== '1' || d.enabled !== '1' || !/^Trk\d+_DTC[01]$/.test(d.name)) return;
                const links = Array.from({length: 6}, (_, i) => d.rocs.some(r => r.enabled === '1' && r.link === i));
                if (!links.some(Boolean)) return;
                const fe = trackerFEs[d.name]; requireOK(fe && fe.feClass === 'DTCFrontEndInterface', 'Missing active DTC ' + d.name);
                api.readerInputs(fe.macros[BUFFER], 1);
                const counter = fe.macros[COUNTERS];
                requireOK(counter && counter.inputs.length === 1 && /^Target ROC/.test(counter.inputs[0]) && counter.outputs.includes('Counter Values'), d.name + ': structured counters unavailable.');
                targets.push({uid: d.name, links});
            }));
            targets.sort((a, b) => a.uid.localeCompare(b.uid, undefined, {numeric: true}));
            requireOK(targets.length, 'No enabled tracker DTCs found.');
            const cfoFEs = api.parseMacros(await api.request(cfo, cfo.mm, 'getFEMacroList'));
            const cfos = Object.values(cfoFEs).filter(f => f.feClass === 'CFOFrontEndInterface');
            requireOK(cfos.length === 1, 'Expected one configured CFO interface.'); cfoFE = cfos[0];
            api.cfoInputs(cfoFE.macros[CFO], api.options('1ms', '1000'), true);
            const groups = {tracker: topology.groups.configuration, cfo: await activeCFO()};
            groups.tracker.context = topology.groups.context;
            const group = TrackerRateMask.group(await api.request(cfo, cfo.config, 'getSpecificTableGroup', {groupName: groups.cfo.name, groupKey: groups.cfo.key}), groups.cfo.name, groups.cfo.key);
            const table = async name => TrackerRateMask.table(await api.request(cfo, cfo.config, 'getSpecificTable', {tableName: name, version: group.members[name], dataOffset: 0, chunkSize: 0, rawData: 0, allowIllegalColumns: 1}), name, group.members[name]);
            const interfaces = await table('FEInterfaceTable'), settings = await table('CFOInterfaceTable'), globals = await table('Mu2eGlobalsTable');
            const fe = interfaces.rows.find(r => r.FEInterfaceUID === cfoFE.uid && r.Status === 'On' && r.FEInterfacePluginName === 'CFOFrontEndInterface');
            const row = fe && settings.rows.find(r => r.UID === fe.LinkToFETypeUID);
            requireOK(row && ['On', 'Yes', '1', 'true'].includes(row.AutoFixedWidthRunPlanEnable),
                'CFO automatic production-plan reload is disabled or unverifiable. TestFlight will not replace its run plan.');
            requireOK(globals.rows.length === 1 && ['EventBuildingMode', 'EventBuildingAndSyncMode'].includes(globals.rows[0].GlobalOperatingMode),
                'CFO operating mode does not support automatic production-plan reload.');
            const loaded = await api.request(cfo, cfo.lid, 'getAppStatus');
            requireOK(values(loaded, 'detail').some(d => d.includes('translates to ' + groups.cfo.name + '(' + groups.cfo.key + ').')), 'Loaded CFO configuration differs from active group.');
            return {targets, groups, cfo: {uid: cfoFE.uid, origin: cfo.origin, auto_reload: true}};
        };
        deps.buffer = (t, command) => macro(tracker, trackerMM, trackerFEs[t.uid], BUFFER, api.readerInputs(trackerFEs[t.uid].macros[BUFFER], command));
        deps.counters = t => { const m = trackerFEs[t.uid].macros[COUNTERS]; return macro(tracker, trackerMM, trackerFEs[t.uid], COUNTERS, {[m.inputs[0]]: String(t.links.indexOf(true))}); };
        deps.cfo = (session, options, enabled) => macro(cfo, cfo.mm, cfoFE, CFO, api.cfoInputs(cfoFE.macros[CFO], options, enabled));
        return deps;
    };
    const escape = s => String(s == null ? '' : s).replace(/[&<>"']/g, c => ({'&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;'}[c]));
    api.render = function (report, message, entry) {
        let html = '<div class="tracker-result-summary"><b>TestFlight</b> · ' + escape(message) + '</div>';
        if (report.error) html += '<pre class="badValue">' + escape(report.error) + '</pre>';
        if (report.cfo) html += '<p>CFO: ' + escape(report.cfo.uid) + ' at ' + escape(report.cfo.origin) + '</p>';
        if (report.cfo_stopped) html += '<p>CFO test stopped. Normal Start reloads the configured production run plan.</p>';
        if (report.reused && report.reused.length) html += '<p class="badValue">Previously running readers left running: ' + escape(report.reused.join(', ')) + '. Halt these before a normal run.</p>';
        if (report.rows && report.rows.length) {
            html += '<p>Counter increases during this test. Expected: ' + report.options.markers + ' EWM/heartbeat packets and ' + (report.options.markers - 1) + ' data requests/headers per enabled ROC (one final null heartbeat).</p><div class="tracker-counters-scroll"><table class="tracker-counters-table"><thead><tr><th>DTC</th><th>ROC</th>' + labels.map(l => '<th>' + l + '</th>').join('') + '<th>Counter result</th></tr></thead><tbody>';
            report.rows.forEach(r => { html += '<tr><th>' + escape(r.uid) + '</th><td>' + r.link + '</td>' + r.counts.map((n, i) => '<td' + (n === r.expected[i] ? '' : ' class="counter-mismatch"') + '>' + n + '</td>').join('') + '<td>' + (r.pass ? 'PASS' : 'MISMATCH') + '</td></tr>'; });
            html += '</tbody></table></div>';
        }
        if (report.readers) {
            const ignored = Object.entries(report.readers).flatMap(([uid, r]) => (r.after || r.before).ignored_timeouts || []).length;
            if (ignored) html += '<p>Timeout counts on disabled ROC links are excluded from reader checks. Enabled-link errors and reader exceptions still fail the test.</p>';
            html += '<details><summary>Reader status and raw outputs</summary>';
            Object.keys(report.readers).forEach(uid => { const r = report.readers[uid]; html += '<h4>' + escape(uid) + '</h4><pre>' + escape(r.after ? r.after.text : r.before.text) + '</pre>'; });
            html += '</details>';
        }
        if (report.cleanup && report.cleanup.length) html += '<details><summary>Cleanup</summary><pre>' + escape(report.cleanup.join('\n')) + '</pre></details>';
        if (entry) html += '<button type="button" onclick="downloadTestFlightReport(\'' + escape(entry) + '\')">Download report</button>';
        return html;
    };
    root.TrackerTestFlight = api;
    if (typeof module !== 'undefined') module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
