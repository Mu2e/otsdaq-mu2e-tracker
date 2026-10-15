// Fresh tracker rates -> current table CSV -> verified upload -> activation.
// Uses the signed-in DesktopContent session. No Configure, alias or mask-write macro.
(function (root) {
    "use strict";
    const CHANNELS = "SubsystemTrackerChannelsTable";
    const PARAMETERS = "SubsystemTrackerParametersTable";
    const ROCS = "ROCInterfaceTable";
    const MACRO = "ROC FEMacro - Measure Channel Rates";
    const AUDIT = new Set(["Author", "RecordInsertionTime"]);
    const api = {};
    function requireOK(test, message) { if (!test) throw new Error(message); }
    function decode(s) { return decodeURIComponent(String(s)); }
    function values(xml, name) {
        return Array.from(xml.getElementsByTagName(name)).map(n => decode(n.getAttribute("value") || ""));
    }
    function value(xml, name) { return values(xml, name)[0] || ""; }
    function required(xml, name) {
        const s = value(xml, name);
        requireOK(s !== "", "Missing response field: " + name);
        return s;
    }
    api.threshold = function (s) {
        requireOK(String(s).trim() !== "" && Number.isFinite(Number(s)) && Number(s) >= 0,
            "Enter a finite, nonnegative rate threshold in kHz.");
        return Number(s);
    };
    api.table = function (xml, name, version) {
        requireOK(required(xml, "TableName") === name && required(xml, "TableVersion") === String(version),
            "Table identity/version mismatch: " + name);
        const headers = values(xml, "ColumnHeader");
        const columns = Array.from(xml.getElementsByTagName("ColCSV")).map(n => {
            const s = n.getAttribute("value") || "";
            return s === "" ? [] : s.split(",").map(decode);
        });
        requireOK(headers.length && new Set(headers).size === headers.length &&
            columns.length === headers.length && columns.every(c => c.length === columns[0].length),
            "Malformed table: " + name);
        const rows = columns[0].map((_, i) => Object.fromEntries(headers.map((h, j) => [h, columns[j][i]])));
        return {name, version: String(version), headers, rows};
    };
    function persistent(s) { return /^\d+$/.test(String(s)); }
    api.group = function (xml, name, key) {
        requireOK(required(xml, "TableGroupName") === name && required(xml, "TableGroupKey") === String(key),
            "Configuration group identity mismatch.");
        const names = values(xml, "MemberName"), versions = values(xml, "MemberVersion");
        requireOK(names.length && names.length === versions.length &&
            new Set(names).size === names.length && versions.every(persistent), "Malformed group members.");
        const treeWarnings = values(xml, "TreeErrors").filter(Boolean);
        return {name, key: String(key), members: Object.fromEntries(names.map((n, i) => [n, versions[i]])),
            treeWarnings};
    };
    function index(rows, key) {
        const result = new Map();
        rows.forEach(r => { requireOK(r[key] && !result.has(r[key]), "Missing/duplicate " + key); result.set(r[key], r); });
        return result;
    }
    // The deployed DTC table plugin reads XDAQContextTable.getContexts(), an
    // initialization cache that can be empty even when the active tables exist.
    // Follow the persisted links instead, including every parent Status flag.
    api.structure = function (tables) {
        const contexts = index(tables.XDAQContextTable.rows, "ContextUID");
        index(tables.XDAQApplicationTable.rows, "ApplicationUID");
        const supervisors = index(tables.FESupervisorTable.rows, "SupervisorUID");
        index(tables.FEInterfaceTable.rows, "FEInterfaceUID");
        const dtcs = index(tables.DTCInterfaceTable.rows, "UID");
        index(tables[ROCS].rows, "UID");
        const result = {apps: []}, seen = new Set();
        function enabled(row) {
            requireOK(["On", "Off"].includes(row.Status), "Invalid topology Status.");
            return row.Status === "On";
        }
        contexts.forEach(context => {
            requireOK(context.LinkToApplicationTable === "XDAQApplicationTable", "Unsupported Context application link.");
            tables.XDAQApplicationTable.rows.filter(a => a.ApplicationGroupID === context.LinkToApplicationGroupID &&
                a.Class === "ots::FESupervisor").forEach(app => {
                requireOK(app.LinkToSupervisorTable === "FESupervisorTable", "Unsupported FE supervisor link.");
                const supervisor = supervisors.get(app.LinkToSupervisorUID);
                requireOK(supervisor && supervisor.LinkToFEInterfaceTable === "FEInterfaceTable", "Missing FE supervisor/interface link.");
                const parentEnabled = enabled(context) && enabled(app);
                const entry = {name: context.ContextUID + "_" + app.ApplicationUID, enabled: parentEnabled ? "1" : "0", dtcs: []};
                tables.FEInterfaceTable.rows.filter(fe => fe.FEInterfaceGroupID === supervisor.LinkToFEInterfaceGroupID &&
                    fe.FEInterfacePluginName === "DTCFrontEndInterface").forEach(fe => {
                    requireOK(!seen.has(fe.FEInterfaceUID), "DTC linked to multiple applications: " + fe.FEInterfaceUID);
                    seen.add(fe.FEInterfaceUID);
                    requireOK(fe.LinkToFETypeTable === "DTCInterfaceTable", "Unsupported DTC type link.");
                    const dtc = dtcs.get(fe.LinkToFETypeUID);
                    requireOK(dtc && [ROCS, "NO_LINK"].includes(dtc.LinkToROCGroupTable), "Missing/unsupported DTC ROC link.");
                    const rocs = dtc.LinkToROCGroupTable === "NO_LINK" ? [] : tables[ROCS].rows.filter(r => r.ROCGroupID === dtc.LinkToROCGroupID);
                    rocs.sort((a, b) => Number(a.linkID) - Number(b.linkID));
                    entry.dtcs.push({name: fe.FEInterfaceUID, enabled: enabled(fe) ? "1" : "0",
                        parentEnabled: entry.enabled,
                        parentApp: context.Address + ":" + context.Port + "/" + context.ContextUID + "/" + app.ApplicationUID,
                        rocs: rocs.map(r => ({name: r.UID, enabled: enabled(r) ? "1" : "0", link: /^\d+$/.test(r.linkID) ? Number(r.linkID) : null}))});
                });
                result.apps.push(entry);
            });
        });
        requireOK(result.apps.some(a => a.dtcs.some(d => /^Trk\d+_DTC[01]$/.test(d.name))), "No tracker DTCs in the active linked tables.");
        return result;
    };
    api.mapping = function (targets, tables) {
        requireOK(targets.length && new Set(targets.map(t => t.uid)).size === targets.length, "No targets or duplicate DTCs.");
        const rocs = index(tables[ROCS].rows, "UID"), params = index(tables[PARAMETERS].rows, "SubsystemTrackerParametersUID");
        index(tables[CHANNELS].rows, "ChannelUID");
        const channelRows = new Map(), groupOwners = new Map();
        tables[CHANNELS].rows.forEach((r, i) => {
            requireOK(r.GroupID && /^\d+$/.test(r.Channel) && Number(r.Channel) < 96 && ["Yes", "No"].includes(r.Enabled),
                "Invalid channel row: " + r.ChannelUID);
            const key = r.GroupID + ":" + Number(r.Channel);
            requireOK(!channelRows.has(key), "Duplicate logical channel: " + key);
            channelRows.set(key, i);
        });
        targets.forEach(t => {
            requireOK(t.rocs.length > 0 && /^Trk\d+_DTC[01]$/.test(t.uid), "Invalid/empty DTC target: " + t.uid);
            const links = new Set();
            t.rocs.forEach(r => {
                const row = rocs.get(r.uid);
                requireOK(row && row.Status === "On" && row.ROCTypeLinkTable === PARAMETERS,
                    "Missing/enabled-state mismatch or unsupported ROC mapping: " + r.uid);
                const link = Number(row.linkID);
                requireOK(/^\d+$/.test(row.linkID) && link >= 0 && link <= 5 && !links.has(link) &&
                    r.uid === t.uid + "_ROC" + link && (r.link === undefined || r.link === link), "Invalid/duplicate ROC link: " + r.uid);
                links.add(link); r.link = link;
                const param = params.get(row.ROCTypeLinkUID);
                requireOK(param && param.LinkToTrackerROCChannelsTable === CHANNELS, "Missing channel link: " + r.uid);
                r.group = param.LinkToTrackerROCChannelsTableGroupID;
                requireOK(r.group && !groupOwners.has(r.group), "Missing/shared channel group: " + r.group);
                groupOwners.set(r.group, r.uid);
                r.rowIndices = Array.from({length: 96}, (_, ch) => {
                    const i = channelRows.get(r.group + ":" + ch);
                    requireOK(i !== undefined, "Missing channel " + r.group + ":" + ch);
                    return i;
                });
            });
            t.rocs.sort((a, b) => a.link - b.link);
        });
        return targets;
    };
    api.scanTargets = function (targets) {
        requireOK(targets.length && new Set(targets.map(t => t.uid)).size === targets.length, "No targets or duplicate DTCs.");
        targets.forEach(t => {
            requireOK(/^Trk\d+_DTC[01]$/.test(t.uid) && t.rocs.length, "Invalid/empty DTC target: " + t.uid);
            const links = new Set();
            t.rocs.forEach(r => {
                requireOK(Number.isInteger(r.link) && r.link >= 0 && r.link <= 5 && !links.has(r.link) &&
                    r.uid === t.uid + "_ROC" + r.link, "Invalid/duplicate scan ROC: " + r.uid);
                links.add(r.link);
            });
        });
        return targets;
    };
    function numberList(s) {
        const p = JSON.parse(String(s));
        const a = Array.isArray(p) ? p : [p];
        requireOK(a.length && a.every(Number.isInteger), "Malformed ROC/return-code list.");
        return a;
    }
    api.rates = function (target, result) {
        requireOK(result && !result.error, result && result.error || "No rate response.");
        requireOK(result.targets && result.targets.length === 1 && result.targets[0].uid === target.uid,
            "Rate response target mismatch: " + target.uid);
        const out = result.targets[0].outputs || {};
        const links = numberList(out["Target ROC"]), codes = numberList(out["Return Code"]);
        requireOK(links.length === target.rocs.length && new Set(links).size === links.length &&
            links.every(l => target.rocs.some(r => r.link === l)) && codes.length === links.length && codes.every(c => c === 0),
            "Incomplete ROC coverage or nonzero rate/restoration return code: " + target.uid);
        // The successful source routine restores and verifies all six saved mask words.
        const masks = [], maskPattern = /ROC\s+(\d+)\s*:\s*CAL\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s*;\s*HV\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)/gi;
        for (const m of String(out["Saved hardware masks"] || "").matchAll(maskPattern)) {
            requireOK(m.slice(2).every(w => Number(w) <= 65535), "Invalid saved mask word.");
            masks.push(Number(m[1]));
        }
        requireOK(masks.length === links.length && new Set(masks).size === links.length &&
            masks.every(l => links.includes(l)), "Saved hardware mask coverage missing: " + target.uid);
        const tables = []; let rows;
        const pattern = /^\s*(\d{1,2})\s+(\S+)\s+(\S+)\s+(\S+)\s*$/gm;
        for (const m of String(out.Rates || "").matchAll(pattern)) {
            const ch = Number(m[1]);
            if (ch === 0) { rows = []; tables.push(rows); }
            requireOK(rows && ch === rows.length, "Nonsequential rate channels.");
            const rates = m.slice(2).map(Number);
            requireOK(rates.every(n => Number.isFinite(n) && n >= 0), "Invalid/missing rate value.");
            rows.push({channel: ch, cal_khz: rates[0], hv_khz: rates[1], total_khz: rates[2]});
        }
        requireOK(tables.length === links.length && tables.every(t => t.length === 96), "Incomplete rate tables.");
        return links.flatMap((link, i) => tables[i].map(r => Object.assign({dtc: target.uid, roc: link}, r)));
    };
    api.plan = function (table, targets, rates, threshold) {
        const rows = table.rows.map(r => Object.assign({}, r)), changes = [], aboveThreshold = [],
            enabledAboveThreshold = [], disabledBelowThreshold = [], seen = new Set();
        const rocs = new Map(targets.flatMap(t => t.rocs.map(r => [t.uid + ":" + r.link, r])));
        rates.forEach(rate => {
            const key = rate.dtc + ":" + rate.roc + ":" + rate.channel;
            requireOK(!seen.has(key), "Duplicate rate: " + key); seen.add(key);
            const roc = rocs.get(rate.dtc + ":" + rate.roc);
            requireOK(roc && Number.isInteger(rate.channel) && rate.channel >= 0 && rate.channel < 96 &&
                Number.isFinite(rate.total_khz) && rate.total_khz >= 0, "Unmapped/invalid rate: " + key);
            const rowIndex = roc.rowIndices[rate.channel];
            // Classify using the downloaded configuration, before planning any masks.
            const straw = Object.assign({uid: roc.uid, panel: roc.group, rowIndex,
                channelUID: table.rows[rowIndex].ChannelUID, enabled: table.rows[rowIndex].Enabled}, rate);
            if (rate.total_khz > threshold) {
                aboveThreshold.push(straw);
                if (straw.enabled === "Yes") {
                    enabledAboveThreshold.push(straw);
                    rows[rowIndex].Enabled = "No";
                    changes.push(straw);
                }
            } else if (rate.total_khz < threshold && straw.enabled === "No") disabledBelowThreshold.push(straw);
        });
        requireOK(seen.size === rocs.size * 96, "Incomplete rate coverage; no configuration saved.");
        return {rows, changes, aboveThreshold, enabledAboveThreshold, disabledBelowThreshold};
    };
    api.serialize = function (table, rows) {
        const hs = table.headers.filter(h => !AUDIT.has(h));
        return rows.map(r => hs.map(h => encodeURIComponent(r[h])).join(",") + ",;").join("");
    };
    // Same values-only, quoted CSV format used by ConfigurationGUI download.
    api.toCSV = function (table, rows) {
        return (rows || table.rows).map(r => table.headers.map(h => {
            requireOK(typeof r[h] === "string", "Invalid CSV cell: " + h);
            return '"' + r[h].replace(/"/g, '""') + '"';
        }).join(",")).join("\n") + "\n";
    };
    api.fromCSV = function (csv, table) {
        const data = [], row = []; let cell = "", quoted = false, closed = false, started = false;
        function finishCell() { row.push(cell); cell = ""; closed = false; started = false; }
        function finishRow() { finishCell(); data.push(row.splice(0)); }
        for (let i = 0; i < csv.length; ++i) {
            const ch = csv[i];
            if (quoted) {
                if (ch !== '"') cell += ch;
                else if (csv[i + 1] === '"') { cell += '"'; ++i; }
                else { quoted = false; closed = true; }
                continue;
            }
            requireOK(!closed || [",", "\r", "\n"].includes(ch), "Unexpected text after quoted CSV field.");
            if (ch === '"') { requireOK(!started, "Unexpected CSV quote."); quoted = true; started = true; }
            else if (ch === ",") finishCell();
            else if (ch === "\r" || ch === "\n") { finishRow(); if (ch === "\r" && csv[i + 1] === "\n") ++i; }
            else { cell += ch; started = true; }
        }
        requireOK(!quoted, "Unclosed CSV quote.");
        if (started || closed || row.length) finishRow();
        requireOK(data.length === table.rows.length && data.every(r => r.length === table.headers.length), "CSV row/column count mismatch.");
        return data.map(r => Object.fromEntries(table.headers.map((h, i) => [h, r[i]])));
    };
    api.verifyTable = function (expected, actual) {
        requireOK(JSON.stringify(expected.headers) === JSON.stringify(actual.headers) && expected.rows.length === actual.rows.length,
            "Saved table schema/row count mismatch.");
        expected.rows.forEach((r, i) => expected.headers.forEach(h => {
            if (!AUDIT.has(h)) requireOK(r[h] === actual.rows[i][h], "Saved table mismatch at row " + i + ", " + h);
        }));
    };
    api.sameGroup = function (a, b) {
        return a.name === b.name && String(a.key) === String(b.key);
    };
    function sameMembers(a, b) {
        const ks = Object.keys(a).sort();
        return JSON.stringify(ks) === JSON.stringify(Object.keys(b).sort()) && ks.every(k => String(a[k]) === String(b[k]));
    }
    // Dependency injection lets offline tests exercise every failure boundary.
    api.supportsReportOnly = true;
    api.run = async function (deps, threshold, report, progress, options) {
        options = options || {};
        requireOK(options.reportOnly === undefined || typeof options.reportOnly === "boolean", "Invalid report-only option.");
        const reportOnly = options.reportOnly === true;
        report.report_only = reportOnly;
        threshold = api.threshold(threshold);
        report.threshold_khz = threshold; report.started_at = new Date().toISOString();
        progress("Checking stopped DAQ and active configuration...");
        const baseline = await deps.active(); report.originalGroup = baseline;
        await deps.guard(baseline);
        report.targets = api.scanTargets(await deps.targets());
        report.excluded_rocs = report.targets.excluded || [];
        report.unmeasured_hosts = Array.from({length: 18}, (_, i) => "Trk" + String(i + 1).padStart(2, "0"))
            .filter(h => !report.targets.some(t => t.uid.startsWith(h + "_")));
        report.rates = []; report.above_threshold = [];
        report.measurement_mode = "parallel";
        report.measurements = report.targets.map(t => ({dtc: t.uid, status: "pending"}));
        const targetRates = new Array(report.targets.length);
        let completed = 0;
        progress("Measuring " + report.targets.length + " DTCs in parallel...");
        // Wait for EVERY started macro, even after a failure: it must finish
        // restoring its own masks before the UI unlocks or another scan starts.
        const measurements = await Promise.allSettled(report.targets.map(async (target, i) => {
            const measurement = report.measurements[i];
            try {
                await deps.guard(baseline);
                measurement.status = "running"; measurement.started_at = new Date().toISOString();
                report.measurement_started = true;
                const result = await deps.measure(target);
                const rates = api.rates(target, result);
                targetRates[i] = rates.map(r => Object.assign({measured_at: new Date().toISOString()}, r));
                measurement.status = "completed";
            } catch (error) {
                measurement.status = "failed";
                measurement.error = String(error && error.message || error);
                throw error;
            } finally {
                measurement.finished_at = new Date().toISOString();
                ++completed;
                report.rates = targetRates.flat();
                report.above_threshold = report.rates.filter(r => r.total_khz > threshold);
                const failures = report.measurements.filter(m => m.status === "failed").length;
                progress("DTC measurements finished: " + completed + "/" + report.targets.length +
                    (failures ? "; " + failures + " failed; waiting for remaining measurements before stopping." : "."));
            }
        }));
        const failures = measurements.flatMap((m, i) => m.status === "rejected" ?
            [report.targets[i].uid + ": " + report.measurements[i].error] : []);
        requireOK(!failures.length, "Rate scan failed; all started measurements finished. " + failures.join("; "));
        await deps.guard(baseline);
        progress("Rates complete: " + report.above_threshold.length + " channels above " + threshold + " kHz. Downloading current channel-table CSV...");
        const group = await deps.group(baseline); report.originalGroup = group;
        requireOK(persistent(group.members[CHANNELS]), "Missing persistent channel-table member.");
        const download = await deps.downloadCSV(group.members[CHANNELS]);
        report.originalCSV = download.csv;
        const tables = {[CHANNELS]: download.table};
        tables[CHANNELS].rows = api.fromCSV(report.originalCSV, download.table);
        report.originalTable = tables[CHANNELS];
        for (const name of [ROCS, PARAMETERS]) {
            requireOK(persistent(group.members[name]), "Missing persistent mapping member: " + name);
            tables[name] = await deps.table(name, group.members[name]);
        }
        api.mapping(report.targets, tables);
        const plan = api.plan(tables[CHANNELS], report.targets, report.rates, threshold);
        report.above_threshold = plan.aboveThreshold;
        report.enabled_above_threshold = plan.enabledAboveThreshold;
        report.disabled_below_threshold = plan.disabledBelowThreshold;
        if (reportOnly) {
            report.changes = [];
            report.finished_at = new Date().toISOString();
            progress("Report complete: " + report.enabled_above_threshold.length + " enabled straws above " + threshold +
                " kHz; " + report.disabled_below_threshold.length + " disabled straws below it. Configuration unchanged.");
            return report;
        }
        report.changes = plan.changes;
        report.editedCSV = api.toCSV(tables[CHANNELS], plan.rows);
        api.verifyTable({headers: tables[CHANNELS].headers, rows: plan.rows}, {headers: tables[CHANNELS].headers, rows: api.fromCSV(report.editedCSV, tables[CHANNELS])});
        progress("Scan complete: " + report.rates.length + " channels; " + plan.changes.length + " new masks.");
        if (!plan.changes.length) { report.no_changes = true; report.finished_at = new Date().toISOString(); return report; }
        // Re-read baseline before write: preserve its complete data, not a cached/latest version.
        api.verifyTable(tables[CHANNELS], await deps.table(CHANNELS, group.members[CHANNELS]));
        await deps.guard(baseline);
        progress("Uploading channel-table CSV with " + plan.changes.length + " Enabled cells changed to No...");
        report.save_started = true;
        const version = await deps.uploadCSV(tables[CHANNELS], report.editedCSV, report);
        requireOK(persistent(version) && String(version) !== group.members[CHANNELS], "Expected a new persistent channel-table version.");
        report.savedTableVersion = String(version);
        api.verifyTable({headers: tables[CHANNELS].headers, rows: plan.rows}, await deps.table(CHANNELS, version));
        report.upload_verified = true;
        await deps.guard(baseline);
        const members = Object.assign({}, group.members, {[CHANNELS]: String(version)});
        const key = await deps.saveGroup(group.name, members, report);
        requireOK(persistent(key) && String(key) !== group.key, "Expected a new Configuration group key.");
        report.savedGroup = {name: group.name, key: String(key)};
        const saved = await deps.group(report.savedGroup);
        requireOK(sameMembers(members, saved.members), "Saved group members differ; activation blocked.");
        await deps.guard(baseline);
        progress("Activating " + saved.name + "(" + saved.key + ")...");
        report.activation_started = true;
        await deps.activate(saved);
        requireOK(api.sameGroup(saved, await deps.active()), "Activation readback mismatch; check active state before retrying.");
        report.activated = true;
        report.finished_at = new Date().toISOString();
        return report;
    };
    api.browserDependencies = async function () {
        // Explicit URN routing survives DesktopContent's authentication retries,
        // without changing the page's LID or the existing authenticated session.
        let configLid;
        // Tracker may be hosted by a different gateway from its enclosing desktop.
        // targetGatewaySupervisor=true uses the desktop's _serverOrigin. Use the
        // current page's _localOrigin and an explicit gateway LID instead.
        function gatewayId() {
            return Number(DesktopContent._remoteServerUrnLid) || Number(DesktopContent._serverUrnLid);
        }
        function routingReady() {
            return Number.isInteger(gatewayId()) && gatewayId() > 0 &&
                Number(DesktopContent._localUrnLid) > 0 && !!DesktopContent._localOrigin;
        }
        // init() starts application discovery and linked-table discovery together.
        // Give their asynchronous callbacks time to supply the page's application
        // ID before calling DesktopContent, whose zero-ID fallback uses the parent.
        const deadline = Date.now() + 10000;
        while (!routingReady() && Date.now() < deadline)
            await new Promise(resolve => setTimeout(resolve, 25));
        requireOK(routingReady(), "Tracker application discovery did not initialize request routing within 10 seconds. " +
            "Gateway LID=" + (gatewayId() || "missing") + ", local application LID=" +
            (DesktopContent._localUrnLid || "missing") + ". Check application discovery and reload Tracker.");
        const gatewayLid = gatewayId();
        function request(type, query, body, gateway) {
            return new Promise((resolve, reject) => {
                const url = "Request?RequestType=" + encodeURIComponent(type) +
                    Object.keys(query || {}).map(k => "&" + encodeURIComponent(k) + "=" + encodeURIComponent(query[k])).join("");
                try {
                    DesktopContent.XMLHttpRequest(url, body || "", (req, unused, err) => {
                        if (err || !req || !req.responseXML) {
                            reject(new Error(type + ": " + (err || "Non-XML response; check login/operational lock. Do not retry a save blindly."))); return;
                        }
                        try {
                            const errors = values(req.responseXML, "Error").concat(type === "getSpecificTableGroup" ? [] : values(req.responseXML, "TreeErrors")).filter(Boolean);
                            requireOK(!errors.length, type + ": " + errors.join("; "));
                            resolve(req.responseXML);
                        } catch (e) { reject(e); }
                    }, 0, 0, true, true, false, false, false, gateway ? gatewayLid : configLid);
                } catch (e) { reject(e); }
            });
        }
        configLid = required(await request("getAppId", {classNeedle: "ots::ConfigurationGUISupervisor"}, "", true), "id");
        requireOK(/^\d+$/.test(configLid) && Number(configLid) > 0, "Configuration GUI unavailable.");
        const deps = {};
        deps.active = async () => {
            const xml = await request("getActiveTableGroups");
            return {name: required(xml, "Configuration-ActiveGroupName"), key: required(xml, "Configuration-ActiveGroupKey"),
                context: {name: required(xml, "Context-ActiveGroupName"), key: required(xml, "Context-ActiveGroupKey")}};
        };
        deps.guard = async group => {
            const state = await request("getCurrentState", {}, "", true);
            const currentState = value(state, "current_state"), transition = value(state, "in_transition");
            requireOK(currentState === "Configured" && transition === "0",
                "Tracker at " + (DesktopContent._localOrigin || "this page's origin") +
                " must be Configured and stopped, with no transition in progress. Received state=" +
                (currentState || "missing") + ", in_transition=" + (transition === undefined ? "missing" : transition) + ".");
            const active = await deps.active();
            requireOK(api.sameGroup(group, active), "Active Configuration group changed; operation stopped.");
            requireOK(!group.context || api.sameGroup(group.context, active.context), "Active Context group changed; operation stopped.");
            const status = await request("getAppStatus", {}, "", true);
            requireOK(values(status, "detail").some(d => d.includes("translates to " + group.name + "(" + group.key + ").")),
                "Loaded front-end configuration does not match the active group. Configure the selected baseline first.");
        };
        async function groupMetadata(g) {
            return api.group(await request("getSpecificTableGroup", {groupName: g.name, groupKey: g.key}), g.name, g.key);
        }
        // Read immutable member/version metadata. TreeErrors from this reader
        // describe its standalone group view, not the combined active graph.
        // Actual scope links are checked by structure() and mapping() from rows.
        deps.group = groupMetadata;
        deps.table = async (name, version) => api.table(await request("getSpecificTable", {tableName: name, version,
            dataOffset: 0, chunkSize: 0, rawData: 0, allowIllegalColumns: 1}), name, version);
        deps.downloadCSV = async version => {
            const table = await deps.table(CHANNELS, version);
            return {table, csv: api.toCSV(table)};
        };
        deps.structure = async () => {
            const active = await deps.active(), configuration = await deps.group(active), context = await deps.group(active.context);
            const tables = {};
            for (const name of ["XDAQContextTable", "XDAQApplicationTable", "FESupervisorTable", "FEInterfaceTable", "DTCInterfaceTable", ROCS]) {
                const members = ["XDAQContextTable", "XDAQApplicationTable"].includes(name) ? context.members : configuration.members;
                requireOK(persistent(members[name]), "Missing topology table: " + name);
                tables[name] = await deps.table(name, members[name]);
            }
            const after = await deps.active();
            requireOK(api.sameGroup(active, after) && api.sameGroup(active.context, after.context), "Active topology changed during discovery; operation stopped.");
            const structure = api.structure(tables);
            structure.groups = {configuration: active, context: active.context};
            return structure;
        };
        deps.targets = async () => {
            await new Promise(resolve => Mu2eHardware.fetchMacroList(resolve));
            const structure = await new Promise(resolve => Mu2eHardware.fetchStructure(resolve));
            requireOK(structure && Array.isArray(structure.apps), "Cannot discover tracker hardware.");
            const targets = [], excluded = [];
            structure.apps.forEach(app => (app.dtcs || []).forEach(dtc => {
                if (!/^Trk\d+_DTC[01]$/.test(dtc.name)) return;
                const enabled = app.enabled === "1" && dtc.enabled === "1";
                const rocs = (dtc.rocs || []).filter(r => {
                    if (!enabled || r.enabled !== "1") { excluded.push(r.name); return false; }
                    return true;
                }).map(r => ({uid: r.name, link: r.link}));
                if (rocs.length) targets.push({uid: dtc.name, rocs});
            }));
            targets.sort((a, b) => a.uid.localeCompare(b.uid, undefined, {numeric: true}));
            targets.excluded = excluded;
            targets.forEach(t => {
                const m = (Mu2eHardware.getMacrosForDevice(t.uid) || {})[MACRO];
                requireOK(m && m.inputs.length === 1 && /^Target ROC or Mask/.test(m.inputs[0]) &&
                    ["Rates", "Return Code", "Target ROC", "Saved hardware masks"].every(k => m.outputs.includes(k)),
                    "Updated rate/restoration macro unavailable: " + t.uid);
            });
            return targets;
        };
        deps.measure = target => new Promise((resolve, reject) => {
            const m = Mu2eHardware.getMacrosForDevice(target.uid)[MACRO];
            const mask = target.rocs.reduce((n, r) => n + (1 << (4 * r.link)), 0);
            Mu2eHardware.runMacro(target.uid, MACRO, {[m.inputs[0]]: "0x" + mask.toString(16)}, resolve);
            // No command retry/timeout: an in-flight hardware macro must finish restoration.
        });
        function comment(report) { return "Tracker Total > " + report.threshold_khz + " kHz; " + report.started_at + "; " + report.changes.length + " new masks"; }
        deps.uploadCSV = async (table, csv, report) => {
            // ConfigurationGUI's CSV upload parses values then serializes cells
            // for saveSpecificTable; it has no raw multipart CSV upload endpoint.
            const rows = api.fromCSV(csv, table);
            const xml = await request("saveSpecificTable", {tableName: CHANNELS, version: table.version,
                dataOffset: 0, chunkSize: 0, temporary: 0, sourceTableAsIs: 0, lookForEquivalent: 1,
                tableComment: comment(report)}, "data=" + api.serialize(table, rows));
            requireOK(required(xml, "savedName") === CHANNELS, "Unexpected saved table name.");
            return required(xml, "savedVersion");
        };
        deps.saveGroup = async (name, members, report) => {
            // The established tracker group contains cross-group Context links.
            // Preserve every link/member; table readback already verified only Enabled changes.
            const xml = await request("saveNewTableGroup", {groupName: name, allowDuplicates: 0,
                lookForEquivalent: 1, ignoreWarnings: 1, groupComment: comment(report)},
                "tableList=" + Object.keys(members).sort().map(n => n + "," + members[n] + ",").join(""));
            return required(xml, "TableGroupKey");
        };
        deps.activate = g => request("activateTableGroup", {groupName: g.name, groupKey: g.key, ignoreWarnings: 0});
        return deps;
    };
    root.TrackerRateMask = api;
    if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof window !== "undefined" ? window : globalThis);
