// Mu2eHardware.js — Reusable library for Mu2e hardware queries and macro execution.
// Any OTSDAQ page can include this to access FE structure, macro lists, and macro execution.
// Requires: Globals.js, Debug.js, DesktopContent.js, ConfigurationAPI.js loaded first.

var Mu2eHardware = Mu2eHardware || {};

(function () {
	"use strict";

	// =========================================================================
	// Internal state
	// =========================================================================

	var _feClassToFEsMap = {};   // feClass -> [uid, uid, ...]
	var _feToMacrosMap = {};     // uid -> { macroName -> { inputs, outputs, feClass, supervisor, lid, ... } }
	var _feStructure = null;     // parsed JSON from DTCInterfaceTable structure
	var _configGuiLid = 0;       // discovered at runtime via getAppId
	var _macroMakerLid = 0;      // captured on first use from the page's original LID

	// =========================================================================
	// Public accessors
	// =========================================================================

	Mu2eHardware.getFeClassToFEsMap = function () { return _feClassToFEsMap; };
	Mu2eHardware.getFeToMacrosMap = function () { return _feToMacrosMap; };
	Mu2eHardware.getStructure = function () { return _feStructure; };

	Mu2eHardware.getMacrosForDevice = function (uid) {
		return _feToMacrosMap[uid] || null;
	};

	Mu2eHardware.getDevicesByClass = function (feClass) {
		return _feClassToFEsMap[feClass] || [];
	};

	Mu2eHardware.getAllClasses = function () {
		return Object.keys(_feClassToFEsMap);
	};

	// =========================================================================
	// fetchActiveConfig — get active configuration group info
	//   callback({context: {groupName, groupKey}, config: {groupName, groupKey},
	//             aliases: [{alias, name, key, groupType}, ...]})
	// =========================================================================

	Mu2eHardware.fetchActiveConfig = function (callback) {
		var doFetch = function () {
			_setConfigGuiLid();
			ConfigurationAPI.getAliasesAndGroups(function (retObj) {
			var result = {};

			// Active groups
			if (retObj && retObj.activeGroups) {
				result.context = retObj.activeGroups.Context || {};
				result.config = retObj.activeGroups.Configuration || {};
			}

			// Find which alias matches the active groups
			result.contextAlias = "";
			result.configAlias = "";
			if (retObj && retObj.aliases) {
				var ctxAliases = retObj.aliases.Context || [];
				for (var i = 0; i < ctxAliases.length; ++i) {
					if (ctxAliases[i].name === result.context.groupName &&
						ctxAliases[i].key === result.context.groupKey) {
						result.contextAlias = ctxAliases[i].alias;
						break;
					}
				}
				var cfgAliases = retObj.aliases.Configuration || [];
				for (var i = 0; i < cfgAliases.length; ++i) {
					if (cfgAliases[i].name === result.config.groupName &&
						cfgAliases[i].key === result.config.groupKey) {
						result.configAlias = cfgAliases[i].alias;
						break;
					}
				}
			}

			if (callback) callback(result);
		}, false, false);
		};

		if (_configGuiLid) {
			doFetch();
		} else {
			_discoverConfigGuiLid(doFetch);
		}
	};

	// =========================================================================
	// fetchStructure — get DTC/ROC hierarchy from DTCInterfaceTable
	// =========================================================================

	Mu2eHardware.fetchStructure = function (callback) {
		Debug.log("Mu2eHardware.fetchStructure()");

		var doFetch = function () {
			_setConfigGuiLid();

			ConfigurationAPI.getStructureStatus("DTCInterfaceTable",
				function (json) {

					if (!json) {
						Debug.log("Mu2eHardware: No structure data returned.", Debug.HIGH_PRIORITY);
						_feStructure = null;
						if (callback) callback(null);
						return;
					}
					try {
						_feStructure = JSON.parse(json);
					} catch (e) {
						Debug.log("Mu2eHardware: Error parsing structure JSON: " + e, Debug.HIGH_PRIORITY);
						_feStructure = null;
					}
					Debug.log("Mu2eHardware: structure loaded", _feStructure);
					if (callback) callback(_feStructure);
				});
		};

		if (_configGuiLid) {
			doFetch();
		} else {
			_discoverConfigGuiLid(doFetch);
		}
	};

	// =========================================================================
	// fetchMacroList — get all FE macros from MacroMakerSupervisor
	// =========================================================================

	Mu2eHardware.fetchMacroList = function (callback) {
		Debug.log("Mu2eHardware.fetchMacroList()");

		var doFetch = function () {
			_setMacroMakerLid();
			DesktopContent.XMLHttpRequest("Request?RequestType=getFEMacroList", "",
			function (req) {
				_feClassToFEsMap = {};
				_feToMacrosMap = {};

				if (!req || !req.responseXML) {
					Debug.log("Mu2eHardware: No macro list response.", Debug.HIGH_PRIORITY);
					if (callback) callback(_feClassToFEsMap, _feToMacrosMap);
					return;
				}

				var HEADER_FIELDS = 4;
				var feMacros = req.responseXML.getElementsByTagName("FEMacros");

				for (var i = 0; i < feMacros.length; ++i) {
					var macroVar = feMacros[i].getAttribute("value");
					macroVar = macroVar.split(";");

					var supervisor = macroVar[0];
					var lid = macroVar[1];
					var feClass = macroVar[2];
					var feUID = macroVar[3];

					if (!_feClassToFEsMap[feClass])
						_feClassToFEsMap[feClass] = [];
					_feClassToFEsMap[feClass].push(feUID);

					var c = HEADER_FIELDS;
					while (c < macroVar.length) {
						var macroName = macroVar[c];
						if (!_feToMacrosMap[feUID])
							_feToMacrosMap[feUID] = {};

						_feToMacrosMap[feUID][macroName] = {
							"requiredPermissions": macroVar[c + 1],
							"tooltip": decodeURIComponent(macroVar[c + 2]),
							"inputs": [],
							"outputs": [],
							"supervisor": supervisor,
							"lid": lid,
							"feClass": feClass,
						};
						c += 3;
						for (var j = 0; j < (macroVar[c] | 0); ++j)
							_feToMacrosMap[feUID][macroName].inputs.push(
								decodeURIComponent(macroVar[c + 1 + j]));
						c += 1 + (macroVar[c] | 0);
						for (var j = 0; j < (macroVar[c] | 0); ++j)
							_feToMacrosMap[feUID][macroName].outputs.push(
								decodeURIComponent(macroVar[c + 1 + j]));
						c += 1 + (macroVar[c] | 0);
					}
				}

				Debug.log("Mu2eHardware: feClassToFEsMap", _feClassToFEsMap);
				Debug.log("Mu2eHardware: feToMacrosMap", _feToMacrosMap);

				if (callback) callback(_feClassToFEsMap, _feToMacrosMap);
			},
			0, 0, true, true);
		};

		if (_macroMakerLid) {
			doFetch();
		} else {
			_discoverMacroMakerLid(doFetch);
		}
	};

	// =========================================================================
	// runMacro — execute a macro on a single device
	//
	//   uid:       target FE UID
	//   macroName: name of the macro
	//   inputs:    { argName: value, ... }  (or null if none)
	//   callback:  function(result) where result is:
	//              { error, targets: [{ uid, hostname, context, supervisor,
	//                                   execTime, outputs: {name: value} }] }
	//   onProgress: optional function(progressInfo) for async polling updates
	// =========================================================================

	Mu2eHardware.runMacro = function (uid, macroName, inputs, callback, onProgress) {
		var macroObj = _feToMacrosMap[uid] && _feToMacrosMap[uid][macroName];
		if (!macroObj) {
			Debug.log("Mu2eHardware.runMacro: macro '" + macroName + "' not found for " + uid,
				Debug.HIGH_PRIORITY);
			if (callback) callback({ error: "Macro not found" });
			return;
		}

		var postData = "inputArgs=";
		var inputArr = macroObj.inputs;
		for (var i = 0; i < inputArr.length; ++i) {
			if (i) postData += ";";
			var val = (inputs && inputs[inputArr[i]] !== undefined) ? inputs[inputArr[i]] : "";
			postData += encodeURIComponent(inputArr[i]) + "," + encodeURIComponent(val);
		}

		postData += "&outputArgs=";
		var outputArr = macroObj.outputs;
		for (var i = 0; i < outputArr.length; ++i) {
			if (i) postData += ",";
			postData += encodeURIComponent(outputArr[i]);
		}

		var requestUrl = "Request?RequestType=runFEMacro" +
			"&feClassSelected=" + macroObj.feClass +
			"&feUIDSelected=" + uid +
			"&macroType=fe" +
			"&macroName=" + macroName +
			"&saveOutputs=0";

		function handleResponse(req, reqParam, errStr) {
			if (errStr) {
				if (callback) callback({ error: errStr });
				return;
			}

			var err = DesktopContent.getXMLValue(req, "Error");
			if (err) {
				if (callback) callback({ error: err });
				return;
			}

			// Async macro — poll for completion
			var notDoneID = DesktopContent.getXMLValue(req, "NotDoneID");
			if (notDoneID) {
				var progressInfo = [];
				var progress = DesktopContent.getXMLChildren(req, "feMacroProgress");
				if (progress && progress.length) {
					for (var p = 0; p < progress.length; ++p) {
						progressInfo.push({
							uid: progress[p].getAttribute("value"),
							percent: DesktopContent.getXMLValue(progress[p], "progress") || 0,
						});
					}
				}
				if (onProgress) onProgress(progressInfo);

				window.setTimeout(function () {
					_setMacroMakerLid();
					DesktopContent.XMLHttpRequest(
						"Request?RequestType=runFEMacro&NotDoneID=" + notDoneID,
						"", handleResponse,
						0, 0, true, true);
				}, 3000);
				return;
			}

			// Parse completed results
			var result = { error: null, targets: [] };
			var feExecs = req.responseXML.getElementsByTagName("feMacroExec");

			for (var f = 0; f < feExecs.length; ++f) {
				var target = {
					uid: DesktopContent.getXMLValue(feExecs[f], "fe_uid"),
					hostname: DesktopContent.getXMLValue(feExecs[f], "fe_hostname"),
					context: DesktopContent.getXMLValue(feExecs[f], "fe_context"),
					supervisor: DesktopContent.getXMLValue(feExecs[f], "fe_supervisor"),
					execTime: DesktopContent.getXMLValue(feExecs[f], "exec_time"),
					outputs: {},
				};
				var outNames = feExecs[f].getElementsByTagName("outputArgs_name");
				var outValues = feExecs[f].getElementsByTagName("outputArgs_value");
				for (var i = 0; i < outNames.length; ++i) {
					var outputName = _decodeURIComponentSafe(
						outNames[i].getAttribute("value"));
					target.outputs[outputName] = _decodeURIComponentSafe(
						outValues[i].getAttribute("value"));
				}
				result.targets.push(target);
			}

			// Also check top-level run args (older response format)
			if (feExecs.length === 0) {
				var runArgNames = req.responseXML.getElementsByTagName("feMacroRunArgs_name");
				var runArgValues = req.responseXML.getElementsByTagName("feMacroRunArgs_value");
				if (runArgNames.length) {
					var target = { uid: uid, outputs: {} };
					for (var i = 0; i < runArgNames.length; ++i) {
						var outputName = _decodeURIComponentSafe(
							runArgNames[i].getAttribute("value"));
						target.outputs[outputName] = _decodeURIComponentSafe(
							runArgValues[i].getAttribute("value"));
					}
					result.targets.push(target);
				}
			}

			if (callback) callback(result);
		}

		_setMacroMakerLid();
		DesktopContent.XMLHttpRequest(requestUrl, postData, handleResponse,
			0, 0, true, true);
	};

	// =========================================================================
	// getMacroInputDefaults — fetch read-only defaults for one macro target
	//
	//   The current input values are sent because a DTC-level ROC wrapper needs
	//   the selected ROC link before it can delegate to the child ROC provider.
	// =========================================================================

	Mu2eHardware.getMacroInputDefaults = function (uid, macroName, inputs, callback) {
		var macroObj = _feToMacrosMap[uid] && _feToMacrosMap[uid][macroName];
		if (!macroObj) {
			if (callback) callback({ error: "Macro not found", defaults: {} });
			return;
		}

		var inputData = "";
		for (var i = 0; i < macroObj.inputs.length; ++i) {
			if (i) inputData += ";";
			var inputName = macroObj.inputs[i];
			var inputValue = (inputs && inputs[inputName] !== undefined) ?
				inputs[inputName] : "";
			inputData += encodeURIComponent(inputName) + "," +
				encodeURIComponent(inputValue);
		}

		var requestUrl = "Request?RequestType=getFEMacroInputDefaults" +
			"&feUIDSelected=" + encodeURIComponent(uid) +
			"&macroName=" + encodeURIComponent(macroName);

		_setMacroMakerLid();
		DesktopContent.XMLHttpRequest(requestUrl, "inputArgs=" + inputData,
			function (req, reqParam, errStr) {
				if (errStr) {
					if (callback) callback({ error: errStr, defaults: {} });
					return;
				}

				var error = DesktopContent.getXMLValue(req, "Error");
				if (error) {
					if (callback) callback({ error: error, defaults: {} });
					return;
				}

				var defaults = {};
				var encodedDefaults = DesktopContent.getXMLValue(req, "InputDefaults");
				if (encodedDefaults) {
					var pairs = encodedDefaults.split(";");
					for (var i = 0; i < pairs.length; ++i) {
						var comma = pairs[i].indexOf(",");
						if (comma < 0) continue;
						defaults[_decodeURIComponentSafe(pairs[i].substring(0, comma))] =
							_decodeURIComponentSafe(pairs[i].substring(comma + 1));
					}
				}
				if (callback) callback({ error: null, defaults: defaults });
			},
			0, 0, true, true);
	};

	// =========================================================================
	// getActiveDevicesByClass — return UIDs of enabled devices of a given class
	// =========================================================================

	Mu2eHardware.getActiveDevicesByClass = function (feClass) {
		var all = _feClassToFEsMap[feClass] || [];
		if (!_feStructure || !_feStructure.apps) return all;

		var active = [];
		for (var i = 0; i < _feStructure.apps.length; ++i) {
			var app = _feStructure.apps[i];
			if (app.enabled !== "1") continue;
			for (var j = 0; j < app.dtcs.length; ++j) {
				if (app.dtcs[j].enabled === "1" &&
					all.indexOf(app.dtcs[j].name) >= 0)
					active.push(app.dtcs[j].name);
			}
		}
		return active.length ? active : all;
	};

	// =========================================================================
	// getIncludedTrackerDTCs — enabled DTCs under enabled tracker applications
	//
	// Unlike getActiveDevicesByClass(), this intentionally has no fallback to
	// every discovered FE.  A tracker-wide command must never include disabled
	// DTCs just because the active set happens to be empty.
	// =========================================================================

	Mu2eHardware.getIncludedTrackerDTCs = function () {
		if (!_feStructure || !_feStructure.apps) return [];

		var included = [];
		var seen = {};
		for (var i = 0; i < _feStructure.apps.length; ++i) {
			var app = _feStructure.apps[i];
			if (app.enabled !== "1") continue;

			for (var j = 0; j < app.dtcs.length; ++j) {
				var dtc = app.dtcs[j];
				if (dtc.enabled !== "1" || seen[dtc.name]) continue;
				if (!_feToMacrosMap[dtc.name]) continue;

				seen[dtc.name] = true;
				included.push(dtc.name);
			}
		}
		return included;
	};

	// =========================================================================
	// getAllROCInputs — build the forwarding inputs for all ROCs on one DTC
	// =========================================================================

	Mu2eHardware.getAllROCInputs = function (uid, macroName) {
		var macroObj = _feToMacrosMap[uid] && _feToMacrosMap[uid][macroName];
		if (!macroObj) return null;

		var inputs = {};
		var foundTarget = false;
		for (var i = 0; i < macroObj.inputs.length; ++i) {
			var argName = macroObj.inputs[i];
			if (/target\s+roc|roc.*(?:target|mask)/i.test(argName)) {
				inputs[argName] = "-1";
				foundTarget = true;
			}
		}
		return foundTarget ? inputs : null;
	};

	// Validate all global inputs and explicit enabled-link masks before dispatch.
	Mu2eHardware.thresholdScanSettings = function (threshold, tolerance, directory) {
		threshold = String(threshold).trim();
		tolerance = String(tolerance).trim();
		directory = String(directory || "").trim();
		if (!threshold || !tolerance || !isFinite(Number(threshold)) ||
			!isFinite(Number(tolerance)) || Number(tolerance) <= 0)
			throw new Error("Enter a finite threshold and a positive tolerance in mV.");
		if (directory && (directory[0] !== "/" || /\.json\/?$/i.test(directory)))
			throw new Error("Use an absolute root directory (writes slot_XX/thresholds-YY-mV/MNxxx.json), or leave it blank for configured per-panel paths.");
		return {"Threshold (mV)": String(Number(threshold)),
			"Tolerance (mV)": String(Number(tolerance)), "Filesystem path": directory};
	};

	Mu2eHardware.thresholdScanInputs = function (uid, settings) {
		var name = "ROC FEMacro - Find and serialize thresholds";
		var macro = (Mu2eHardware.getMacrosForDevice(uid) || {})[name];
		if (!macro || !macro.outputs || macro.outputs.indexOf("Resolved path") < 0)
			throw new Error(uid + ": updated threshold-save macro is not loaded; compile/load it and refresh first.");
		var inputs = Mu2eHardware.getAllROCInputs(uid, name);
		if (!inputs) throw new Error(uid + ": ROC target input is unavailable.");
		var mask = 0;
		Mu2eHardware.getROCsForDTC(uid).forEach(function (roc) {
			if (!roc.enabled) return;
			if (!Number.isInteger(roc.linkIndex) || roc.linkIndex < 0 || roc.linkIndex > 5)
				throw new Error(uid + ": invalid enabled ROC link.");
			mask |= 1 << (4 * roc.linkIndex);
		});
		if (!mask) throw new Error(uid + ": no enabled ROCs.");
		Object.keys(inputs).forEach(function (key) { inputs[key] = "0x" + mask.toString(16); });
		Object.keys(settings).forEach(function (key) { inputs[key] = settings[key]; });
		return inputs;
	};

	Mu2eHardware.thresholdScanSucceeded = function (result) {
		if (!result || result.error || !result.targets || !result.targets.length) return false;
		function numbers(value) {
			try {
				var parsed = JSON.parse(String(value).replace(/\s*\(0x[0-9a-f]+\)/gi, ""));
				var values = Array.isArray(parsed) ? parsed : [parsed];
				return values.length && values.every(Number.isInteger) ? values : null;
			} catch (e) { return null; }
		}
		return result.targets.every(function (target) {
			var outputs = target.outputs || {};
			var failed = numbers(outputs["Failed count"]);
			var saved = numbers(outputs["Serialization successful"]);
			return failed && saved && failed.length === saved.length &&
				failed.every(function (n) { return n === 0; }) &&
				saved.every(function (n) { return n === 1; });
		});
	};

	// =========================================================================
	// runMacroOnUIDs — parallel batch execution for an explicit device list
	//
	// inputs may be a shared object, or function(uid) returning an input object.
	// Each DTC gets an independent request and result, while all requests are
	// dispatched without waiting for another DTC to finish.
	// =========================================================================

	Mu2eHardware.runMacroOnUIDs = function (uids, macroName, inputs, callback, onEach) {
		var targets = (uids || []).slice();
		if (!targets.length) {
			if (callback) callback([]);
			return;
		}

		var results = [];
		var remaining = targets.length;

		function finish(uid, result) {
			results.push({ uid: uid, result: result });
			if (onEach) onEach(uid, result, results.length, targets.length);
			remaining--;
			if (remaining === 0) {
				if (callback) callback(results);
			}
		}

		function runOne(uid) {
			var uidInputs = (typeof inputs === "function") ? inputs(uid) : inputs;
			if (uidInputs === null) {
				var inputError = { error: "Required batch target input was not found" };
				finish(uid, inputError);
				return;
			}

			Mu2eHardware.runMacro(uid, macroName, uidInputs,
				function (result) {
					finish(uid, result);
				}
			);
		}

		for (var i = 0; i < targets.length; ++i)
			runOne(targets[i]);
	};

	// =========================================================================
	// runMacroOnAll — run a macro on all active devices of a given class
	//
	//   feClass:   e.g. "DTCFrontEndInterface"
	//   macroName: e.g. "DTC Read"
	//   inputs:    { argName: value } — applied to every device
	//   callback:  function(results) — results = [{uid, result}, ...]
	//   onEach:    optional function(uid, result) — called after each device
	// =========================================================================

	Mu2eHardware.runMacroOnAll = function (feClass, macroName, inputs, callback, onEach) {
		var uids = Mu2eHardware.getActiveDevicesByClass(feClass);
		Mu2eHardware.runMacroOnUIDs(uids, macroName, inputs, callback, onEach);
	};

	// =========================================================================
	// findFeClassForUID — look up which FE class a UID belongs to
	// =========================================================================

	Mu2eHardware.findFeClassForUID = function (uid) {
		var allClasses = Object.keys(_feClassToFEsMap);
		for (var i = 0; i < allClasses.length; ++i) {
			var uids = _feClassToFEsMap[allClasses[i]];
			for (var j = 0; j < uids.length; ++j) {
				if (uids[j] === uid) return allClasses[i];
			}
		}
		return null;
	};

	// =========================================================================
	// getDeviceType — determine if a UID is a "dtc", "roc", or "other"
	//   based on the structure data
	// =========================================================================

	Mu2eHardware.getDeviceType = function (uid) {
		if (!_feStructure || !_feStructure.apps) return "other";
		for (var i = 0; i < _feStructure.apps.length; ++i) {
			var app = _feStructure.apps[i];
			for (var j = 0; j < app.dtcs.length; ++j) {
				if (app.dtcs[j].name === uid) return "dtc";
				for (var k = 0; k < app.dtcs[j].rocs.length; ++k) {
					if (app.dtcs[j].rocs[k].name === uid) return "roc";
				}
			}
		}
		return "other";
	};

	// =========================================================================
	// getROCsForDTC — return array of {name, enabled, linkIndex} for a DTC
	// =========================================================================

	Mu2eHardware.getROCsForDTC = function (dtcUID) {
		if (!_feStructure || !_feStructure.apps) return [];
		for (var i = 0; i < _feStructure.apps.length; ++i) {
			var app = _feStructure.apps[i];
			for (var j = 0; j < app.dtcs.length; ++j) {
				if (app.dtcs[j].name === dtcUID) {
					var result = [];
					for (var k = 0; k < app.dtcs[j].rocs.length; ++k) {
						var roc = app.dtcs[j].rocs[k];
						result.push({
							name: roc.name,
							enabled: roc.enabled === "1",
							linkIndex: k,
						});
					}
					return result;
				}
			}
		}
		return [];
	};

	// =========================================================================
	// getParentDTC — for a ROC UID, return the parent DTC name (or null)
	// =========================================================================

	Mu2eHardware.getParentDTC = function (rocUID) {
		if (!_feStructure || !_feStructure.apps) return null;
		for (var i = 0; i < _feStructure.apps.length; ++i) {
			var app = _feStructure.apps[i];
			for (var j = 0; j < app.dtcs.length; ++j) {
				for (var k = 0; k < app.dtcs[j].rocs.length; ++k) {
					if (app.dtcs[j].rocs[k].name === rocUID)
						return app.dtcs[j].name;
				}
			}
		}
		return null;
	};

	// =========================================================================
	// getROCLinkIndex — return the link index (position) of a ROC within its DTC
	// =========================================================================

	Mu2eHardware.getROCLinkIndex = function (rocUID) {
		if (!_feStructure || !_feStructure.apps) return -1;
		for (var i = 0; i < _feStructure.apps.length; ++i) {
			var app = _feStructure.apps[i];
			for (var j = 0; j < app.dtcs.length; ++j) {
				for (var k = 0; k < app.dtcs[j].rocs.length; ++k) {
					if (app.dtcs[j].rocs[k].name === rocUID)
						return k;
				}
			}
		}
		return -1;
	};

	// =========================================================================
	// getROCMacros — get ROC-relevant macros from the parent DTC
	//
	//   Returns { dtcUID, macros: {name: macroObj, ...}, linkIndex }
	//   The macros are either "ROC FEMacro - X" (same-type ROCs) or
	//   "Link<N>_<ROC_UID>_X" (mixed-type ROCs), plus DTC-level ROC
	//   commands like "ROC Read", "ROC Write", etc.
	// =========================================================================

	Mu2eHardware.getROCMacros = function (rocUID) {
		var dtcUID = Mu2eHardware.getParentDTC(rocUID);
		if (!dtcUID) return null;

		var dtcMacros = _feToMacrosMap[dtcUID];
		if (!dtcMacros) return null;

		var linkIndex = Mu2eHardware.getROCLinkIndex(rocUID);
		var result = {};

		for (var name in dtcMacros) {
			// "ROC FEMacro - X" pattern (all ROCs same type)
			if (name.indexOf("ROC FEMacro - ") === 0) {
				result[name] = dtcMacros[name];
				continue;
			}
			// "Link<N>_<ROC_UID>_X" pattern (mixed ROC types)
			if (name.indexOf("Link" + linkIndex + "_" + rocUID + "_") === 0) {
				result[name] = dtcMacros[name];
				continue;
			}
			// DTC-level ROC commands
			if (name === "ROC Read" || name === "ROC Write" ||
				name === "ROC Block Read" || name === "ROC Block Write" ||
				name === "ROC Setup" || name === "ROC Firmware Inventory") {
				result[name] = dtcMacros[name];
			}
		}

		return {
			dtcUID: dtcUID,
			macros: result,
			linkIndex: linkIndex,
		};
	};

	// =========================================================================
	// fetchDeviceSettings — query config table fields for a device UID
	//
	//   Uses getFieldsOfRecords with depth to follow links into child tables
	//   (ROC type parameters, slow controls channels, etc.), then fetches
	//   all discovered field values.
	//
	//   callback(fields) where fields = [{name, value, table}, ...] or null
	// =========================================================================

	Mu2eHardware.fetchDeviceSettings = function (uid, callback) {
		var devType = Mu2eHardware.getDeviceType(uid);
		var tableName;

		if (devType === "dtc")
			tableName = "DTCInterfaceTable";
		else if (devType === "roc")
			tableName = "ROCInterfaceTable";
		else {
			if (callback) callback(null);
			return;
		}

		_setConfigGuiLid();
		ConfigurationAPI.getFieldsOfRecords(
			tableName, uid,
			"",  // all fields
			5,   // follow links up to 5 levels deep
			function (fieldObjs) {
				if (!fieldObjs || !fieldObjs.length) {
					if (callback) callback(null);
					return;
				}

				// Filter out noise
				var skipCols = {
					"CommentDescription": 1, "Author": 1,
					"RecordInsertionTime": 1,
				};
				var filtered = [];
				for (var i = 0; i < fieldObjs.length; ++i) {
					var col = fieldObjs[i].fieldColumnName;
					var colType = fieldObjs[i].fieldColumnType || "";
					if (skipCols[col]) continue;
					if (colType.indexOf("GroupID") >= 0) continue;
					if (colType.indexOf("ChildLink") === 0 &&
						colType.indexOf("UID") < 0) continue;
					if (colType.indexOf("ChildLinkGroupID") === 0) continue;
					if (colType === "ChildLinkUID" ||
						colType.indexOf("ChildLinkUID") === 0) continue;
					filtered.push(fieldObjs[i]);
				}

				if (!filtered.length) {
					if (callback) callback(null);
					return;
				}

				_setConfigGuiLid();
				ConfigurationAPI.getFieldValuesForRecords(
					tableName, uid, filtered,
					function (fieldValues, errMsg) {
						if (errMsg || !fieldValues || !fieldValues.length) {
							if (callback) callback(null);
							return;
						}
						var result = [];
						for (var i = 0; i < fieldValues.length; ++i) {
							// Show just the final field name, not full path
							var path = fieldValues[i].fieldPath;
							var lastSlash = path.lastIndexOf("/");
							var shortName = lastSlash >= 0 ?
								path.substring(lastSlash + 1) : path;
							var tbl = filtered[i] ?
								filtered[i].fieldTableName : "";
							result.push({
								name: shortName,
								value: fieldValues[i].fieldValue,
								table: tbl,
							});
						}
						if (callback) callback(result);
					},
					undefined, true
				);
			}
		);
	};

	// =========================================================================
	// fetchChannels — get channel records for a ROC
	//
	//   Tries multiple link paths to find the channels table and group:
	//     1. Tracker path: ROCTypeLinkTable → SubsystemTrackerParametersTable
	//        → LinkToTrackerROCChannelsTable (SubsystemTrackerChannelsTable)
	//     2. Slow controls path: LinkToSlowControlsChannelTable
	//        (FESlowControlsTable)
	//
	//   callback(result) where result = {
	//     channels: [{uid, fields: {name:val, ...}}, ...],
	//     tableName: string
	//   } or null
	// =========================================================================

	// Channel link paths to try, in order
	var _CHANNEL_PATHS = [
		{
			groupField: "ROCTypeLinkTable/LinkToTrackerROCChannelsTableGroupID",
			tableName: "SubsystemTrackerChannelsTable",
			groupCol: "GroupID",
		},
		{
			groupField: "LinkToSlowControlsChannelGroupID",
			tableName: "FESlowControlsTable",
			groupCol: "FEGroupID",
		},
	];

	Mu2eHardware.fetchChannels = function (uid, callback) {
		if (Mu2eHardware.getDeviceType(uid) !== "roc") {
			if (callback) callback(null);
			return;
		}

		_tryChannelPath(uid, 0, callback);
	};

	function _captureMacroMakerLid() {
		if (!_macroMakerLid && DesktopContent._localUrnLid &&
			DesktopContent._localUrnLid != _configGuiLid)
			_macroMakerLid = DesktopContent._localUrnLid;
	}

	function _setConfigGuiLid() {
		_captureMacroMakerLid();
		if (_configGuiLid) DesktopContent._localUrnLid = _configGuiLid;
	}

	Mu2eHardware.setConfigGuiLid = function () { _setConfigGuiLid(); };
	Mu2eHardware.discoverConfigGuiLid = function (cb) { _discoverConfigGuiLid(cb); };

	function _setMacroMakerLid() {
		_captureMacroMakerLid();
		if (_macroMakerLid)
			DesktopContent._localUrnLid = _macroMakerLid;
	}

	function _tryChannelPath(uid, pathIdx, callback) {
		if (pathIdx >= _CHANNEL_PATHS.length) {
			if (callback) callback(null);
			return;
		}

		var pathInfo = _CHANNEL_PATHS[pathIdx];

		_setConfigGuiLid();
		ConfigurationAPI.getFieldValuesForRecords(
			"ROCInterfaceTable", uid,
			[pathInfo.groupField],
			function (fieldValues, errMsg) {
				var groupID = fieldValues && fieldValues.length ?
					fieldValues[0].fieldValue : null;

				if (!groupID || groupID === "NO_LINK" || groupID === "DEFAULT") {
					_tryChannelPath(uid, pathIdx + 1, callback);
					return;
				}

				_setConfigGuiLid();
				ConfigurationAPI.getSubsetRecords(
					pathInfo.tableName,
					pathInfo.groupCol + "=" + groupID,
					function (records) {
						if (!records || !records.length) {
							_tryChannelPath(uid, pathIdx + 1, callback);
							return;
						}

						_setConfigGuiLid();
						ConfigurationAPI.getFieldsOfRecords(
							pathInfo.tableName, records[0], "", 1,
							function (fieldObjs) {
								var dataFields = [];
								for (var i = 0; fieldObjs && i < fieldObjs.length; ++i) {
									var ct = fieldObjs[i].fieldColumnType || "";
									var cn = fieldObjs[i].fieldColumnName;
									if (cn === "CommentDescription" || cn === "Author" ||
										cn === "RecordInsertionTime") continue;
									if (ct.indexOf("GroupID") >= 0) continue;
									dataFields.push(fieldObjs[i].fieldColumnName);
								}

								_setConfigGuiLid();
								ConfigurationAPI.getFieldValuesForRecords(
									pathInfo.tableName, records, dataFields,
									function (allValues) {
										var channels = [];
										var chanMap = {};
										for (var i = 0; allValues && i < allValues.length; ++i) {
											var v = allValues[i];
											if (!chanMap[v.fieldUID]) {
												chanMap[v.fieldUID] = { uid: v.fieldUID, fields: {} };
												channels.push(chanMap[v.fieldUID]);
											}
											chanMap[v.fieldUID].fields[v.fieldPath] = v.fieldValue;
										}
										if (callback) callback({
											channels: channels,
											tableName: pathInfo.tableName,
										});
									},
									undefined, true
								);
							}
						);
					}
				);
			},
			undefined, true
		);
	}

	// =========================================================================
	// toggleDeviceStatus — purely local: flip status in pending changes map
	//   Instant, no server call. Returns the new status ("1" or "0").
	// =========================================================================

	var _pendingChanges = {};  // uid -> { table, newStatus }

	Mu2eHardware.toggleDeviceStatus = function (uid) {
		var devType = Mu2eHardware.getDeviceType(uid);
		var tableName;
		if (devType === "dtc")
			tableName = "FEInterfaceTable";
		else if (devType === "roc")
			tableName = "ROCInterfaceTable";
		else
			return null;

		if (_pendingChanges[uid]) {
			// Already toggled — flip back or forward
			var cur = _pendingChanges[uid].newStatus;
			var toggled = (cur === "1") ? "0" : "1";
			_pendingChanges[uid].newStatus = toggled;
			// If back to original, remove from pending
			if (toggled === _pendingChanges[uid].origStatus)
				delete _pendingChanges[uid];
			else
				_pendingChanges[uid].newStatus = toggled;
			return toggled;
		}

		// First toggle — read current from structure data
		var origStatus = _getDeviceStatus(uid);
		var newStatus = (origStatus === "1") ? "0" : "1";
		_pendingChanges[uid] = {
			table: tableName,
			origStatus: origStatus,
			newStatus: newStatus,
		};
		return newStatus;
	};

	Mu2eHardware.hasUnsavedChanges = function () {
		return Object.keys(_pendingChanges).length > 0;
	};

	Mu2eHardware.getPendingChanges = function () {
		return _pendingChanges;
	};

	// =========================================================================
	// saveChanges — write all pending status changes to server at once
	//   callback(success)
	// =========================================================================

	Mu2eHardware.saveChanges = function (callback) {
		var uids = Object.keys(_pendingChanges);
		if (!uids.length) {
			if (callback) callback(true);
			return;
		}

		var modifiedTables;
		var idx = 0;

		function writeNext() {
			if (idx >= uids.length) {
				_finishSave(modifiedTables, false, callback);
				return;
			}

			var uid = uids[idx];
			var ch = _pendingChanges[uid];
			idx++;

			_setConfigGuiLid();
			ConfigurationAPI.setFieldValuesForRecords(
				ch.table, uid, ["Status"], [ch.newStatus],
				function (mt) {
					if (!mt || !mt.length) {
						_finishSave(null, true, callback);
						return;
					}
					modifiedTables = mt;
					writeNext();
				},
				modifiedTables
			);
		}

		writeNext();
	};

	function _finishSave(modifiedTables, hadError, callback) {
		if (hadError || !modifiedTables) {
			Debug.log("Mu2eHardware.saveChanges: error writing changes.",
				Debug.HIGH_PRIORITY);
			if (callback) callback(false);
			return;
		}

		_setConfigGuiLid();
		ConfigurationAPI.saveModifiedTables(
			modifiedTables,
			function (savedTables, savedGroups, savedAliases) {
				if (!savedTables || !savedTables.length) {
					Debug.log("Mu2eHardware.saveChanges: error saving.",
						Debug.HIGH_PRIORITY);
					if (callback) callback(false);
					return;
				}

				Debug.log("Mu2eHardware: saved " + savedTables.length +
					" table(s), " + savedGroups.length + " group(s), " +
					savedAliases.length + " alias(es).");

				_pendingChanges = {};
				if (callback) callback(true);
			}
		);
	}

	function _getDeviceStatus(uid) {
		if (!_feStructure || !_feStructure.apps) return "1";
		for (var i = 0; i < _feStructure.apps.length; ++i) {
			var app = _feStructure.apps[i];
			for (var j = 0; j < app.dtcs.length; ++j) {
				if (app.dtcs[j].name === uid) return app.dtcs[j].enabled;
				for (var k = 0; k < app.dtcs[j].rocs.length; ++k) {
					if (app.dtcs[j].rocs[k].name === uid)
						return app.dtcs[j].rocs[k].enabled;
				}
			}
		}
		return "1";
	}

	// =========================================================================
	// buildTreeHTML — generate the hardware tree as an HTML string
	//
	//   Returns an HTML string showing:
	//     Section 1: DTC/ROC hierarchy (from structure data)
	//     Section 2: Other FE interfaces (from macro list, not in structure)
	//
	//   onSelectAttr: the onclick attribute string for device nodes,
	//     with __UID__ as placeholder, e.g. "selectDevice('__UID__', event)"
	// =========================================================================

	Mu2eHardware.buildTreeHTML = function (onSelectAttr) {
		var html = "";
		var structure = _feStructure;
		var dtcUIDs = {};

		// Section 1: DTC/ROC hierarchy
		if (structure && structure.apps && structure.apps.length) {
			html += "<div class='tree-section-header'>DTC / ROC Hierarchy</div>";

			for (var i = 0; i < structure.apps.length; ++i) {
				var app = structure.apps[i];
				var appId = "app-" + i;
				html += "<div class='tree-context' onclick='Mu2eHardware.toggleTreeNode(\"" + appId + "\", this)'>";
				html += "<span class='tree-arrow'>&#9660;</span>";
				html += "<span class='status-dot " +
					(app.enabled === "1" ? "status-on" : "status-off") + "'></span>";
				html += "<span>" + _esc(app.name) + "</span>";
				html += "</div>";
				html += "<div class='tree-context-children' id='" + appId + "'>";

				for (var j = 0; j < app.dtcs.length; ++j) {
					var dtc = app.dtcs[j];
					var dtcId = appId + "-dtc-" + j;
					dtcUIDs[dtc.name] = true;

					html += "<div class='tree-dtc' " +
						"id='node-" + _esc(dtc.name) + "' " +
						"onclick='" + onSelectAttr.replace(/__UID__/g, _escAttr(dtc.name)) + "'>";
					html += "<span class='tree-arrow' " +
						"onclick='Mu2eHardware.toggleTreeNode(\"" + dtcId + "\", this.parentElement); event.stopPropagation();'>&#9660;</span>";
					html += "<span class='status-dot " +
						(dtc.enabled === "1" ? "status-on" : "status-off") + "'></span>";
					html += "<span>" + _esc(dtc.name) + "</span>";
					html += "</div>";
					html += "<div class='tree-context-children' id='" + dtcId + "'>";

					for (var k = 0; k < dtc.rocs.length; ++k) {
						var roc = dtc.rocs[k];
						dtcUIDs[roc.name] = true;

						html += "<div class='tree-roc' " +
							"id='node-" + _esc(roc.name) + "' " +
							"onclick='" + onSelectAttr.replace(/__UID__/g, _escAttr(roc.name)) + "'>";
						html += "<span class='status-dot " +
							(roc.enabled === "1" ? "status-on" : "status-off") + "'></span>";
						html += "<span>" + _esc(roc.name) + "</span>";
						html += "</div>";
					}
					html += "</div>";
				}
				html += "</div>";
			}
		}

		// Section 2: Other FE interfaces not in the structure tree
		var otherClasses = [];
		var allClasses = Object.keys(_feClassToFEsMap);
		for (var ci = 0; ci < allClasses.length; ++ci) {
			var cls = allClasses[ci];
			var uids = _feClassToFEsMap[cls];
			var hasNew = false;
			for (var u = 0; u < uids.length; ++u) {
				if (!dtcUIDs[uids[u]]) { hasNew = true; break; }
			}
			if (hasNew) otherClasses.push(cls);
		}

		if (otherClasses.length) {
			html += "<div class='tree-section-header'>Other FE Interfaces</div>";

			for (var ci = 0; ci < otherClasses.length; ++ci) {
				var cls = otherClasses[ci];
				var groupId = "fegroup-" + ci;
				html += "<div class='tree-fe-group' onclick='Mu2eHardware.toggleTreeNode(\"" + groupId + "\", this)'>";
				html += "<span class='tree-arrow'>&#9660;</span>";
				html += "<span>" + _esc(cls) + "</span>";
				html += "</div>";
				html += "<div class='tree-context-children' id='" + groupId + "'>";

				var uids = _feClassToFEsMap[cls];
				for (var u = 0; u < uids.length; ++u) {
					if (dtcUIDs[uids[u]]) continue;
					var feUID = uids[u];
					html += "<div class='tree-fe-device' " +
						"id='node-" + _esc(feUID) + "' " +
						"onclick='" + onSelectAttr.replace(/__UID__/g, _escAttr(feUID)) + "'>";
					html += "<span class='status-dot status-unknown'></span>";
					html += "<span>" + _esc(feUID) + "</span>";
					html += "</div>";
				}
				html += "</div>";
			}
		}

		if (!html) {
			html = "<div class='placeholder-text'>" +
				"No hardware found. Is the system configured?" +
				"</div>";
		}

		return html;
	};

	// =========================================================================
	// buildGridHTML — generate a grid/table view of the hardware
	//
	//   Rows = apps (nodes/contexts)
	//   Columns = DTCs, with ROCs shown inside each DTC cell
	//   Other FE types shown in a separate section below
	//
	//   onSelectAttr: onclick string with __UID__ placeholder
	// =========================================================================

	Mu2eHardware.buildGridHTML = function (onSelectAttr, onToggleAttr) {
		var html = "";
		var structure = _feStructure;
		var dtcUIDs = {};

		if (structure && structure.apps && structure.apps.length) {

			// Find the max number of DTCs across all apps (for column count)
			var maxDtcs = 0;
			for (var i = 0; i < structure.apps.length; ++i) {
				if (structure.apps[i].dtcs.length > maxDtcs)
					maxDtcs = structure.apps[i].dtcs.length;
			}

			html += "<table class='hw-grid'>";

			// Header row
			html += "<thead><tr>";
			html += "<th class='hw-grid-node-header'>Node</th>";
			for (var d = 0; d < maxDtcs; ++d)
				html += "<th class='hw-grid-dtc-header'>DTC " + d + "</th>";
			html += "</tr></thead>";

			html += "<tbody>";
			for (var i = 0; i < structure.apps.length; ++i) {
				var app = structure.apps[i];
				var appOn = app.enabled === "1";
				var statusCls = appOn ? "status-on" : "status-off";

				// Extract short hostname from the first DTC's parentApp field
				// parentApp format: "https://mu2edaq09.fnal.gov:2015/ctx/app"
				var nodeName = app.name;
				if (app.dtcs.length && app.dtcs[0].parentApp) {
					nodeName = _extractHostname(app.dtcs[0].parentApp);
				}

				html += "<tr>";

				// Node/App cell — compact
				html += "<td class='hw-grid-node'>";
				html += "<span class='status-dot " + statusCls + "'></span> ";
				html += "<span>" + _esc(nodeName) + "</span>";
				html += "</td>";

				// DTC cells
				for (var j = 0; j < maxDtcs; ++j) {
					if (j < app.dtcs.length) {
						var dtc = app.dtcs[j];
						dtcUIDs[dtc.name] = true;

						var dtcSelfOn = dtc.enabled === "1";
						var dtcEffective = appOn && dtcSelfOn;
						var dtcStatusCls = dtcSelfOn ? "status-on" : "status-off";

						html += "<td class='hw-grid-dtc-cell" +
							(!dtcEffective ? " hw-grid-cell-disabled" : "") + "'>";
						// Dot is outside the clickable device div so clicks don't conflict
						html += _statusDotHTML(dtc.name, dtcStatusCls, onToggleAttr);
						html += "<span class='hw-grid-dtc' " +
							"id='node-" + _esc(dtc.name) + "' " +
							"onclick='" + onSelectAttr.replace(/__UID__/g, _escAttr(dtc.name)) + "'>";
						html += _esc(dtc.name);
						html += "</span>";

						// ROCs inside this DTC cell
						if (dtc.rocs.length) {
							html += "<div class='hw-grid-rocs'>";
							for (var k = 0; k < dtc.rocs.length; ++k) {
								var roc = dtc.rocs[k];
								dtcUIDs[roc.name] = true;

								var rocSelfOn = roc.enabled === "1";
								var rocStatusCls = rocSelfOn ? "status-on" : "status-off";

								html += "<div class='hw-grid-roc" +
									(!dtcEffective ? " hw-grid-item-disabled" : "") + "'>";
								html += _statusDotHTML(roc.name, rocStatusCls, onToggleAttr);
								html += "<span " +
									"id='node-" + _esc(roc.name) + "' " +
									"onclick='" + onSelectAttr.replace(/__UID__/g, _escAttr(roc.name)) + "'>";
								html += _esc(roc.name);
								html += "</span>";
								html += "</div>";
							}
							html += "</div>";
						}
						html += "</td>";
					} else {
						html += "<td class='hw-grid-empty'></td>";
					}
				}
				html += "</tr>";
			}
			html += "</tbody></table>";
		}

		// Other FE interfaces section
		var otherClasses = [];
		var allClasses = Object.keys(_feClassToFEsMap);
		for (var ci = 0; ci < allClasses.length; ++ci) {
			var cls = allClasses[ci];
			var uids = _feClassToFEsMap[cls];
			var hasNew = false;
			for (var u = 0; u < uids.length; ++u) {
				if (!dtcUIDs[uids[u]]) { hasNew = true; break; }
			}
			if (hasNew) otherClasses.push(cls);
		}

		if (otherClasses.length) {
			html += "<div class='hw-grid-other-header'>Other FE Interfaces</div>";
			html += "<div class='hw-grid-other'>";
			for (var ci = 0; ci < otherClasses.length; ++ci) {
				var cls = otherClasses[ci];
				html += "<div class='hw-grid-other-group'>";
				html += "<div class='hw-grid-other-class'>" + _esc(cls) + "</div>";

				var uids = _feClassToFEsMap[cls];
				for (var u = 0; u < uids.length; ++u) {
					if (dtcUIDs[uids[u]]) continue;
					var feUID = uids[u];
					html += "<div class='hw-grid-other-device' " +
						"id='node-" + _esc(feUID) + "' " +
						"onclick='" + onSelectAttr.replace(/__UID__/g, _escAttr(feUID)) + "'>";
					html += "<span class='status-dot status-unknown'></span> ";
					html += "<span>" + _esc(feUID) + "</span>";
					html += "</div>";
				}
				html += "</div>";
			}
			html += "</div>";
		}

		if (!html) {
			html = "<div class='placeholder-text'>" +
				"No hardware found. Is the system configured?" +
				"</div>";
		}

		return html;
	};

	// =========================================================================
	// toggleTreeNode — expand/collapse a collapsible tree section
	// =========================================================================

	Mu2eHardware.toggleTreeNode = function (childId, parentEl) {
		var children = document.getElementById(childId);
		if (!children) return;

		var arrow = parentEl.querySelector(".tree-arrow");
		if (children.style.display === "none") {
			children.style.display = "";
			if (arrow) arrow.classList.remove("collapsed");
		} else {
			children.style.display = "none";
			if (arrow) arrow.classList.add("collapsed");
		}
	};

	// =========================================================================
	// highlightTreeNode — set the selected highlight on a tree node
	// =========================================================================

	Mu2eHardware.highlightTreeNode = function (uid) {
		var oldSel = document.querySelector(".tree-node-selected");
		if (oldSel) oldSel.classList.remove("tree-node-selected");

		if (uid) {
			var node = document.getElementById("node-" + uid);
			if (node) node.classList.add("tree-node-selected");
		}
	};

	// =========================================================================
	// formatMacroResult — render a macro result object as HTML
	//
	//   macroName: the macro that was run
	//   result:    the result object from runMacro callback
	//   deviceUID: the target device UID (for display if result lacks it)
	//   Returns an HTML string
	// =========================================================================

	Mu2eHardware.formatReadPanelIDTable = function (outputs) {
		var rocLinks = _tryParseNumericArray(outputs["Target ROC"]);
		var panelIDs = _tryParseNumericArray(outputs["Panel ID"]);
		var statuses = _tryParseNumericArray(outputs["Success"]);
		if (!rocLinks || !panelIDs || !statuses)
			return "";

		var rowCount = Math.max(rocLinks.length, panelIDs.length, statuses.length);
		if (rowCount < 2)
			return "";

		var html = "<table class='settings-table' " +
			"style='width:auto;min-width:320px;margin-top:4px;font-size:inherit;'>";
		html += "<tr>" +
			"<td class='settings-name'><b>ROC link</b></td>" +
			"<td class='settings-name'><b>Panel ID</b></td>" +
			"<td class='settings-name'><b>Success</b></td></tr>";

		for (var i = 0; i < rowCount; ++i) {
			var rocText = i < rocLinks.length ? String(rocLinks[i]) : "-";
			var panelText = i < panelIDs.length ? String(panelIDs[i]) : "-";
			var statusText = i < statuses.length ? String(statuses[i]) : "-";
			var statusHTML;
			if (statusText === "0")
				statusHTML = "<span class='goodValue'>0 (OK)</span>";
			else if (statusText === "-")
				statusHTML = "-";
			else
				statusHTML = "<span class='badValue'>" + _esc(statusText) + "</span>";

			html += "<tr><td class='settings-value'>" + _esc(rocText) +
				"</td><td class='settings-value'>" + _esc(panelText) +
				"</td><td class='settings-value'>" + statusHTML + "</td></tr>";
		}

		html += "</table>";
		return html;
	};

	function _parseROCStatusBlocks(statusText) {
		if (typeof statusText !== "string") return null;

		var headers = [];
		var headerPattern = /^[ \t,\[]*Register\s+ROC(\d+)\s+Description\s*$/gmi;
		var headerMatch;
		while ((headerMatch = headerPattern.exec(statusText)) !== null)
			headers.push({
				link: Number(headerMatch[1]),
				start: headerMatch.index,
				bodyStart: headerPattern.lastIndex,
			});
		if (!headers.length) return null;

		var blocks = [];
		var rowPattern = new RegExp(
			"^\\s*(reg\\(\\s*\\d+\\)(?:\\s*<<\\s*16\\s*\\|\\s*reg\\(\\s*\\d+\\))?)\\s+" +
			"(0x[0-9a-f]+|[0-9]+)(?:\\s+(.*?))?\\s*$", "i");
		for (var blockIndex = 0; blockIndex < headers.length; ++blockIndex) {
			var header = headers[blockIndex];
			var blockEnd = blockIndex + 1 < headers.length ?
				headers[blockIndex + 1].start : statusText.length;
			var lines = statusText.substring(header.bodyStart, blockEnd)
				.replace(/\r/g, "").split("\n");
			var rows = [];
			for (var lineIndex = 0; lineIndex < lines.length; ++lineIndex) {
				var rowMatch = lines[lineIndex].match(rowPattern);
				if (!rowMatch) continue;
				rows.push({
					register: rowMatch[1].replace(/\s+/g, ""),
					value: rowMatch[2],
					description: rowMatch[3] || "",
				});
			}
			if (!rows.length) return null;
			blocks.push({link: header.link, rows: rows});
		}

		return blocks;
	}

	// The DTC forwarding wrapper runs Print Status once per selected ROC and
	// concatenates the text results.  Merge their common register rows so the
	// ROCs appear as adjacent columns instead of repeated vertical reports.
	Mu2eHardware.formatROCStatusTable = function (outputs, dtcUID) {
		if (Mu2eHardware.getDeviceType(dtcUID) !== "dtc") return "";

		var blocks = _parseROCStatusBlocks(outputs["Output"]);
		var targetLinks = _parseROCTargets(outputs["Target ROC"]);
		if (!blocks || !targetLinks || blocks.length !== targetLinks.length)
			return "";

		var registerOrder = [];
		var descriptions = {};
		var valuesByLink = {};
		for (var blockIndex = 0; blockIndex < blocks.length; ++blockIndex) {
			var link = targetLinks[blockIndex];
			if (!isFinite(link) || blocks[blockIndex].link !== link) return "";
			valuesByLink[link] = {};
			for (var rowIndex = 0; rowIndex < blocks[blockIndex].rows.length; ++rowIndex) {
				var row = blocks[blockIndex].rows[rowIndex];
				if (registerOrder.indexOf(row.register) < 0)
					registerOrder.push(row.register);
				valuesByLink[link][row.register] = row.value;
				if (!descriptions[row.register] && row.description)
					descriptions[row.register] = row.description;
			}
		}
		if (!registerOrder.length) return "";

		var rocNames = {};
		var configuredROCs = Mu2eHardware.getROCsForDTC(dtcUID);
		for (var rocIndex = 0; rocIndex < configuredROCs.length; ++rocIndex)
			rocNames[configuredROCs[rocIndex].linkIndex] = configuredROCs[rocIndex].name;

		var html = "<div style='overflow:auto;max-height:520px;margin-top:4px;'>";
		html += "<table class='settings-table' " +
			"style='width:auto;min-width:720px;font-size:inherit;'>";
		html += "<tr><td class='settings-name'><b>Register</b></td>";
		for (var targetIndex = 0; targetIndex < targetLinks.length; ++targetIndex) {
			var targetLink = targetLinks[targetIndex];
			var title = rocNames[targetLink] || ("ROC link " + targetLink);
			html += "<td class='settings-name' title='" + _esc(title) +
				"'><b>ROC" + _esc(String(targetLink)) + "</b></td>";
		}
		html += "<td class='settings-name'><b>Description</b></td></tr>";

		for (var registerIndex = 0; registerIndex < registerOrder.length; ++registerIndex) {
			var registerName = registerOrder[registerIndex];
			html += "<tr><td class='settings-name'><code>" + _esc(registerName) +
				"</code></td>";
			for (var valueIndex = 0; valueIndex < targetLinks.length; ++valueIndex) {
				var values = valuesByLink[targetLinks[valueIndex]] || {};
				html += "<td class='settings-value'><code>" +
					_esc(values[registerName] || "-") + "</code></td>";
			}
			html += "<td class='settings-name'>" +
				_esc(descriptions[registerName] || "") + "</td></tr>";
		}

		return html + "</table></div>";
	};

	function _parseSPIBlocks(spiText) {
		if (typeof spiText !== "string") return null;

		var headers = [];
		var headerPattern = /^[ \t,\[]*link\s+(\d+)\s*$/gmi;
		var headerMatch;
		while ((headerMatch = headerPattern.exec(spiText)) !== null)
			headers.push({
				link: Number(headerMatch[1]),
				start: headerMatch.index,
				bodyStart: headerPattern.lastIndex,
			});
		if (!headers.length) return null;

		var blocks = [];
		var rowPattern = new RegExp(
			"^\\s*([^:\\r\\n]+?)\\s*:\\s*" +
			"([-+]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][-+]?\\d+)?)\\s*$"
		);
		for (var blockIndex = 0; blockIndex < headers.length; ++blockIndex) {
			var header = headers[blockIndex];
			var blockEnd = blockIndex + 1 < headers.length ?
				headers[blockIndex + 1].start : spiText.length;
			var lines = spiText.substring(header.bodyStart, blockEnd)
				.replace(/\r/g, "").split("\n");
			var rows = [];
			for (var lineIndex = 0; lineIndex < lines.length; ++lineIndex) {
				var rowMatch = lines[lineIndex].match(rowPattern);
				if (!rowMatch) continue;
				rows.push({name: rowMatch[1].trim(), value: rowMatch[2]});
			}
			if (!rows.length) return null;
			blocks.push({link: header.link, rows: rows});
		}

		return blocks;
	}

	// Read SPI returns the same 36 converted monitoring values for each ROC.
	// At DTC scope, merge the forwarded per-ROC blocks into adjacent columns.
	Mu2eHardware.formatSPITable = function (outputs, dtcUID) {
		if (Mu2eHardware.getDeviceType(dtcUID) !== "dtc") return "";

		var blocks = _parseSPIBlocks(outputs["Output"]);
		var targetLinks = _parseROCTargets(outputs["Target ROC"]);
		if (!blocks || !targetLinks || blocks.length !== targetLinks.length)
			return "";

		var returnCodes = _tryParseNumericArray(outputs["Return Code"]);
		if (!returnCodes) {
			var returnCode = Number(outputs["Return Code"]);
			if (isFinite(returnCode)) returnCodes = [returnCode];
		}

		var measurementOrder = [];
		var valuesByLink = {};
		for (var blockIndex = 0; blockIndex < blocks.length; ++blockIndex) {
			var link = targetLinks[blockIndex];
			if (!isFinite(link) || blocks[blockIndex].link !== link) return "";
			valuesByLink[link] = {};
			for (var rowIndex = 0; rowIndex < blocks[blockIndex].rows.length; ++rowIndex) {
				var row = blocks[blockIndex].rows[rowIndex];
				if (measurementOrder.indexOf(row.name) < 0)
					measurementOrder.push(row.name);
				valuesByLink[link][row.name] = row.value;
			}
		}
		if (!measurementOrder.length) return "";

		var rocNames = {};
		var configuredROCs = Mu2eHardware.getROCsForDTC(dtcUID);
		for (var rocIndex = 0; rocIndex < configuredROCs.length; ++rocIndex)
			rocNames[configuredROCs[rocIndex].linkIndex] = configuredROCs[rocIndex].name;

		var html = "<div style='overflow:auto;max-height:520px;margin-top:4px;'>";
		html += "<table class='settings-table' " +
			"style='width:auto;min-width:620px;font-size:inherit;'>";
		html += "<tr><td class='settings-name'><b>Measurement</b></td>";
		for (var targetIndex = 0; targetIndex < targetLinks.length; ++targetIndex) {
			var targetLink = targetLinks[targetIndex];
			var title = rocNames[targetLink] || ("ROC link " + targetLink);
			if (returnCodes && targetIndex < returnCodes.length)
				title += "; return code " + returnCodes[targetIndex];
			html += "<td class='settings-name' title='" + _esc(title) +
				"'><b>ROC" + _esc(String(targetLink)) + "</b></td>";
		}
		html += "</tr>";

		for (var measurementIndex = 0;
			measurementIndex < measurementOrder.length; ++measurementIndex) {
			var measurement = measurementOrder[measurementIndex];
			html += "<tr><td class='settings-name'>" + _esc(measurement) + "</td>";
			for (var valueIndex = 0; valueIndex < targetLinks.length; ++valueIndex) {
				var values = valuesByLink[targetLinks[valueIndex]] || {};
				html += "<td class='settings-value'><code>" +
					_esc(values[measurement] || "-") + "</code></td>";
			}
			html += "</tr>";
		}

		return html + "</table></div>";
	};

	function _parseChannelRateTables(rateText) {
		if (typeof rateText !== "string") return null;

		var tables = [];
		var current = null;
		// The ROC formatter returns: Channel, Cal, HV, coincidence/total.
		var rowPattern = /^\s*(\d{1,2})\s+(\S+)\s+(\S+)\s+(\S+)\s*$/gm;
		var match;
		while ((match = rowPattern.exec(rateText)) !== null) {
			var channel = Number(match[1]);
			if (channel === 0) {
				current = [];
				tables.push(current);
			}
			if (!current || channel !== current.length)
				return null;
			current.push({
				cal: match[2],
				hv: match[3],
				total: match[4],
			});
		}

		if (!tables.length) return null;
		for (var i = 0; i < tables.length; ++i)
			if (tables[i].length !== 96) return null;
		return tables;
	}

	function _parseROCTargets(value) {
		var targets = _tryParseNumericArray(value);
		if (targets) return targets;
		var scalar = Number(String(value === undefined ? "" : value).trim());
		return isFinite(scalar) ? [scalar] : null;
	}

	// ROC/utils.c channel_map at c2d8bf5: raw DIGI index -> logical channel.
	// Saved words are CAL 0x0B/0x0E/0x0D followed by HV 0x0B/0x0E/0x0D.
	var _rateHardwareChannelMap = [
		91, 85, 79, 73, 67, 61, 55, 49,
		43, 37, 31, 25, 19, 13, 7, 1,
		90, 84, 78, 72, 66, 60, 54, 48,
		42, 36, 30, 24, 18, 12, 6, 0,
		93, 87, 81, 75, 69, 63, 57, 51,
		45, 39, 33, 27, 21, 15, 9, 3,
		44, 38, 32, 26, 20, 14, 8, 2,
		92, 86, 80, 74, 68, 62, 56, 50,
		47, 41, 35, 29, 23, 17, 11, 5,
		95, 89, 83, 77, 71, 65, 59, 53,
		46, 40, 34, 28, 22, 16, 10, 4,
		94, 88, 82, 76, 70, 64, 58, 52
	];

	function _parseSavedHardwareMasks(value) {
		if (typeof value !== "string") return null;
		var byLink = {}, match, count = 0;
		var pattern = /ROC ([0-5]): CAL (0x[0-9a-f]{4}) (0x[0-9a-f]{4}) (0x[0-9a-f]{4}); HV (0x[0-9a-f]{4}) (0x[0-9a-f]{4}) (0x[0-9a-f]{4})(?![0-9a-f])/gi;
		while ((match = pattern.exec(value)) !== null) {
			var link = Number(match[1]);
			if (byLink[link]) return null; // ambiguous association: do not colour
			var words = match.slice(2).map(function (word) { return parseInt(word, 16); });
			var excluded = new Array(96);
			for (var index = 0; index < 96; ++index)
				excluded[_rateHardwareChannelMap[index]] =
					(words[Math.floor(index / 16)] & (1 << (index % 16))) === 0;
			byLink[link] = excluded;
			++count;
		}
		return count ? byLink : null;
	}

	function _rateCellHTML(value, excluded) {
		var className = "settings-value";
		var title = "";
		if (excluded === true) {
			className += " rate-masked";
			title = "Masked off in hardware before this rate measurement";
		} else if (excluded === false && isFinite(Number(value)) && Number(value) > 10) {
			className += " rate-high";
			title = "Unmasked channel rate above 10 kHz";
		}
		return "<td class='" + className + "'" + (title ? " title='" + title + "'" : "") +
			">" + _esc(value) + "</td>";
	}

	function _rateExclusionLegend(exclusions, rocNames) {
		var html = "<div class='rate-exclusion-legend'>Grey: masked off in hardware before measurement; " +
			"red: unmasked rate above 10 kHz.</div>";
		var byROC = exclusions.byROC;
		var unavailable = rocNames.filter(function (uid) { return !byROC[uid]; });
		if (unavailable.length) html += "<div>Saved hardware masks unavailable for " +
			_esc(unavailable.join(", ")) + "; those rates are unmarked.</div>";
		return html;
	}

	// The same forwarded ROC macro serves both scopes.  A selected ROC gets all
	// three counters; a DTC gets the Total counter as one column per ROC.
	Mu2eHardware.formatChannelRatesTable = function (outputs, deviceUID) {
		var tables = _parseChannelRateTables(outputs["Rates"]);
		if (!tables) return "";

		var isROC = Mu2eHardware.getDeviceType(deviceUID) === "roc";
		var byROC = {};
		var exclusions = {byROC: byROC};
		var hardwareMasks = _parseSavedHardwareMasks(outputs["Saved hardware masks"]);
		var html = "";
		var configuredROCs = Mu2eHardware.getROCsForDTC(deviceUID);
		var rocNames = {};
		for (var r = 0; r < configuredROCs.length; ++r)
			rocNames[configuredROCs[r].linkIndex] = configuredROCs[r].name;
		var rocLinks = isROC ? [] : _parseROCTargets(outputs["Target ROC"]);
		if (!isROC && (!rocLinks || rocLinks.length !== tables.length)) return "";
		if (hardwareMasks) {
			if (isROC) {
				var maskLinks = Object.keys(hardwareMasks);
				if (maskLinks.length === 1 && tables.length === 1)
					byROC[deviceUID] = hardwareMasks[maskLinks[0]];
			} else {
				rocLinks.forEach(function (link) {
					byROC[rocNames[link] || ("ROC link " + link)] = hardwareMasks[link];
				});
			}
		}
		html += _rateExclusionLegend(exclusions, isROC ? [deviceUID] : rocLinks.map(function (link) {
			return rocNames[link] || ("ROC link " + link);
		}));
		html += "<div style='overflow:auto;max-height:520px;margin-top:4px;'>";
		html += "<table class='settings-table' " +
			"style='width:auto;min-width:420px;font-size:inherit;'>";

		if (isROC) {
			html += "<tr><td class='settings-name'><b>Channel</b></td>" +
				"<td class='settings-name'><b>HV</b><br>kHz</td>" +
				"<td class='settings-name'><b>CAL</b><br>kHz</td>" +
				"<td class='settings-name'><b>Total</b><br>kHz</td></tr>";
			for (var channel = 0; channel < 96; ++channel) {
				var rate = tables[0][channel];
				var excluded = byROC[deviceUID] && byROC[deviceUID][channel];
				html += "<tr><td class='settings-name'>" + channel + "</td>" +
					_rateCellHTML(rate.hv, excluded) +
					_rateCellHTML(rate.cal, excluded) +
					_rateCellHTML(rate.total, excluded) + "</tr>";
			}
			return html + "</table></div>";
		}

		var returnCodes = _tryParseNumericArray(outputs["Return Code"]);
		if (!returnCodes) {
			var returnCode = Number(outputs["Return Code"]);
			if (isFinite(returnCode)) returnCodes = [returnCode];
		}

		html += "<tr><td class='settings-name'><b>Channel</b></td>";
		for (var tableIndex = 0; tableIndex < tables.length; ++tableIndex) {
			var link = rocLinks[tableIndex];
			var label = rocNames[link] || ("ROC link " + link);
			var returnText = returnCodes && tableIndex < returnCodes.length ?
				"; return code " + returnCodes[tableIndex] : "";
			html += "<td class='settings-name' title='Link " + _esc(link) +
				returnText + "'><b>" + _esc(label) + "</b><br>Total (kHz)</td>";
		}
		html += "</tr>";

		for (var dtcChannel = 0; dtcChannel < 96; ++dtcChannel) {
			html += "<tr><td class='settings-name'>" + dtcChannel + "</td>";
			for (var rocIndex = 0; rocIndex < tables.length; ++rocIndex) {
				var mask = byROC[rocNames[rocLinks[rocIndex]] || ("ROC link " + rocLinks[rocIndex])];
				html += _rateCellHTML(
					tables[rocIndex][dtcChannel].total, mask && mask[dtcChannel]);
			}
			html += "</tr>";
		}

		return html + "</table></div>";
	};

	// DTC-level Measure Thresholds returns one formatted 96-channel table per
	// targeted ROC in the same order as Target ROC.  Collapse those tables into
	// a matrix whose ROC columns contain only the Total threshold value.
	Mu2eHardware.formatMeasureThresholdTotalsTable = function (outputs, dtcUID) {
		var rocLinks = _tryParseNumericArray(outputs["Target ROC"]);
		var returnCodes = _tryParseNumericArray(outputs["Return code"]);
		var thresholdText = outputs["Thresholds"];
		if (!rocLinks || !rocLinks.length || typeof thresholdText !== "string")
			return "";

		var tables = [];
		var current = null;
		var rowPattern = /^\s*(\d{1,2})\s+(\S+)\s+(\S+)\s+(\S+)\s*$/gm;
		var match;
		while ((match = rowPattern.exec(thresholdText)) !== null) {
			var channel = Number(match[1]);
			if (channel === 0) {
				current = [];
				tables.push(current);
			}
			if (!current || channel !== current.length || !isFinite(Number(match[4])))
				return "";
			current.push(match[4]);
		}

		if (tables.length !== rocLinks.length) return "";
		for (var t = 0; t < tables.length; ++t)
			if (tables[t].length !== 96) return "";

		var rocNames = {};
		var configuredROCs = Mu2eHardware.getROCsForDTC(dtcUID);
		for (var r = 0; r < configuredROCs.length; ++r)
			rocNames[configuredROCs[r].linkIndex] = configuredROCs[r].name;

		var html = "<div style='overflow:auto;max-height:520px;margin-top:4px;'>";
		html += "<table class='settings-table' " +
			"style='width:auto;min-width:420px;font-size:inherit;'>";
		html += "<tr><td class='settings-name'><b>Channel</b></td>";
		for (var linkIndex = 0; linkIndex < rocLinks.length; ++linkIndex) {
			var link = rocLinks[linkIndex];
			var label = rocNames[link] || ("ROC link " + link);
			var returnText = returnCodes && linkIndex < returnCodes.length ?
				"; return code " + returnCodes[linkIndex] : "";
			html += "<td class='settings-name' title='Link " + _esc(link) +
				returnText + "'><b>" + _esc(label) + "</b><br>Total</td>";
		}
		html += "</tr>";

		for (var channel = 0; channel < 96; ++channel) {
			html += "<tr><td class='settings-name'>" + channel + "</td>";
			for (var tableIndex = 0; tableIndex < tables.length; ++tableIndex)
				html += "<td class='settings-value'>" +
					_esc(tables[tableIndex][channel]) + "</td>";
			html += "</tr>";
		}

		html += "</table></div>";
		return html;
	};

	// Parse the multiline RegisterFormatter output returned by Get Simple Status.
	// Each register is represented by an address/value line followed by one or
	// more register-name/decorated-value lines.  Some versions flatten the first
	// name/value pair onto the address line, so accept both forms.
	Mu2eHardware.parseRegisterDump = function (value) {
		if (typeof value !== "string" || !/Register Dump\s*:/i.test(value))
			return null;

		var lines = value.replace(/\r/g, "").split("\n");
		var registers = [];
		var current = null;

		function finishCurrent() {
			if (!current) return;
			current.name = current.name || "Unnamed register";
			registers.push(current);
			current = null;
		}

		for (var i = 0; i < lines.length; ++i) {
			var trimmed = lines[i].trim();
			if (!trimmed || /^Register Dump\s*:?$/i.test(trimmed) || /^={3,}$/.test(trimmed))
				continue;

			var addressMatch = trimmed.match(
				/^(0x[0-9a-f]+)\s*\|\s*(0x[0-9a-f]+)\s*\|\s*(.*)$/i);
			if (addressMatch) {
				finishCurrent();
				current = {
					address: addressMatch[1],
					value: addressMatch[2],
					name: "",
					details: []
				};
				var tail = addressMatch[3].trim();
				if (tail) {
					var tailParts = tail.split("|");
					current.name = tailParts.shift().trim();
					for (var tailIndex = 0; tailIndex < tailParts.length; ++tailIndex)
						if (tailParts[tailIndex].trim())
							current.details.push(tailParts[tailIndex].trim());
				}
				continue;
			}

			if (!current) continue;
			var pipeIndex = lines[i].indexOf("|");
			if (pipeIndex < 0) {
				current.details.push(trimmed);
				continue;
			}

			var left = lines[i].substring(0, pipeIndex).trim();
			var right = lines[i].substring(pipeIndex + 1).trim();
			if (left && !current.name) current.name = left;
			if (right) current.details.push(right);
		}
		finishCurrent();

		return registers.length ? registers : null;
	};

	Mu2eHardware.formatRegisterDump = function (value, outputName) {
		var registers = Mu2eHardware.parseRegisterDump(value);
		if (!registers) return "";

		var html = "<div class='register-dump'>";
		html += "<div class='register-dump-heading'><span>" +
			_esc(outputName || "Register status") + "</span><span>" +
			registers.length + " registers</span></div>";

		for (var i = 0; i < registers.length; ++i) {
			var register = registers[i];
			var detailItems = [];
			for (var d = 0; d < register.details.length; ++d) {
				var pieces = register.details[d].split(/\s*\|\s*/);
				for (var p = 0; p < pieces.length; ++p)
					if (pieces[p].trim()) detailItems.push(pieces[p].trim());
			}

			var hasAlert = /(?:DEAD|FAIL(?:ED)?|ERROR|MISSING|BAD|Not Locked)/i.test(
				detailItems.join(" "));
			html += "<details class='register-card'" + (hasAlert ? " open" : "") + ">";
			html += "<summary><code class='register-address'>" +
				_esc(register.address) + "</code><span class='register-name'>" +
				_esc(register.name) + "</span><code class='register-value'>" +
				_esc(register.value) + "</code></summary>";
			if (detailItems.length) {
				html += "<div class='register-details'>";
				for (var detailIndex = 0; detailIndex < detailItems.length; ++detailIndex)
					html += "<div>" + Mu2eHardware.colorizeValue(
						_esc(detailItems[detailIndex])) + "</div>";
				html += "</div>";
			}
			html += "</details>";
		}

		return html + "</div>";
	};

	Mu2eHardware.formatMacroResult = function (macroName, result, deviceUID) {
		if (result.error) {
			return "<div class='macro-result-error'><b>Macro failed</b><div>" +
				_esc(result.error) + "</div></div>";
		}

		var str = "<div class='macro-result'>";
		var now = new Date();
		str += "<div class='macro-result-summary'><strong>" + _esc(
			Mu2eHardware.macroDisplayName(macroName)) + "</strong>";
		str += "<span>Completed " + _esc(now.toLocaleTimeString()) + "</span>";
		str += "<span>" + result.targets.length + " target" +
			(result.targets.length === 1 ? "" : "s") + "</span></div>";

		for (var f = 0; f < result.targets.length; ++f) {
			var t = result.targets[f];
			str += "<section class='macro-target-result'>";
			str += "<div class='macro-target-header'><strong>" +
				_esc(t.uid || deviceUID || "Target") + "</strong>";
			var locationParts = [];
			if (t.hostname) locationParts.push(t.hostname);
			if (t.context) locationParts.push(t.context);
			if (t.supervisor) locationParts.push(t.supervisor);
			if (locationParts.length)
				str += "<span>" + _esc(locationParts.join(" · ")) + "</span>";
			str += "</div>";

			var outKeys = Object.keys(t.outputs);
			var specializedHTML = "";
			if (macroName === "ROC FEMacro - Read Panel ID") {
				var panelIDTable = Mu2eHardware.formatReadPanelIDTable(t.outputs);
				if (panelIDTable) specializedHTML = panelIDTable;
			}
			if (!specializedHTML &&
				macroName === "ROC FEMacro - Measure Channel Rates") {
				var ratesTable = Mu2eHardware.formatChannelRatesTable(
					t.outputs, deviceUID);
				if (ratesTable) specializedHTML = ratesTable;
			}
			if (!specializedHTML && macroName === "ROC FEMacro - Measure Thresholds" &&
				Mu2eHardware.getDeviceType(deviceUID) === "dtc") {
				var totalsTable = Mu2eHardware.formatMeasureThresholdTotalsTable(
					t.outputs, deviceUID);
				if (totalsTable) specializedHTML = totalsTable;
			}
			if (!specializedHTML && macroName === "ROC FEMacro - Print Status") {
				var statusTable = Mu2eHardware.formatROCStatusTable(
					t.outputs, deviceUID);
				if (statusTable) specializedHTML = statusTable;
			}
			if (!specializedHTML && macroName === "ROC FEMacro - Read SPI") {
				var spiTable = Mu2eHardware.formatSPITable(t.outputs, deviceUID);
				if (spiTable) specializedHTML = spiTable;
			}

			if (specializedHTML) {
				str += specializedHTML;
			} else if (outKeys.length) {
				str += "<div class='macro-output-list'>";
				for (var i = 0; i < outKeys.length; ++i) {
					var name = outKeys[i];
					var value = t.outputs[name];
					var registerDump = macroName === "Get Simple Status" ?
						Mu2eHardware.formatRegisterDump(value, name) : "";
					if (registerDump) {
						str += registerDump;
						continue;
					}
					str += "<div class='macro-output-item'><div class='macro-output-name'>" +
						_esc(name) + "</div><div class='macro-output-value'>" +
						Mu2eHardware.colorizeValue(_esc(value), name) + "</div></div>";
				}
				str += "</div>";
			} else {
				str += "<div class='macro-empty-output'>(no output arguments)</div>";
			}
			str += "</section>";
		}

		if (!result.targets.length)
			str += "<div class='macro-empty-output'>(no response data)</div>";

		return str + "</div>";
	};

	// =========================================================================
	// colorizeValue — apply status color coding to an output value string
	// =========================================================================

	Mu2eHardware.colorizeValue = function (value, outputName) {
		// Return codes use zero for success, the opposite of an enabled bit.
		// Keep the numbers and array structure visible, including single-ROC results.
		if (/^\s*return\s+code\s*$/i.test(outputName || "")) {
			var codes;
			try {
				codes = JSON.parse(value);
			} catch (e) {
				return value;
			}
			if (!Array.isArray(codes)) codes = [codes];
			if (codes.length === 1 && Array.isArray(codes[0])) codes = codes[0];
			if (!codes.length) return value;
			for (var codeIndex = 0; codeIndex < codes.length; ++codeIndex)
				if (typeof codes[codeIndex] !== "number" || !isFinite(codes[codeIndex]) ||
					Math.floor(codes[codeIndex]) !== codes[codeIndex]) return value;
			return value.replace(/-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?/g, function (code) {
				return "<span class='" + (Number(code) === 0 ? "goodValue" : "badValue") +
					"'>" + code + "</span>";
			});
		}
		// Detect array-like values and render as bitmap
		var bitmap = _tryParseBitArray(value);
		if (bitmap)
			return Mu2eHardware.renderBitmap(bitmap);

		return value
			.replace(/OK/g, "<span class='goodValue'>OK</span>")
			.replace(/DONE/g, "<span class='goodValue'>DONE</span>")
			.replace(/GOOD/g, "<span class='goodValue'>GOOD</span>")
			.replace(/LOCKED/g, "<span class='goodValue'>LOCKED</span>")
			.replace(/DEAD/g, "<span class='badValue'>DEAD</span>")
			.replace(/FAILED/g, "<span class='badValue'>FAILED</span>")
			.replace(/FAIL/g, "<span class='badValue'>FAIL</span>")
			.replace(/ERROR/g, "<span class='badValue'>ERROR</span>")
			.replace(/MISSING/g, "<span class='badValue'>MISSING</span>")
			.replace(/BAD/g, "<span class='badValue'>BAD</span>");
	};

	// =========================================================================
	// renderBitmap — render an array of 0/1 values as a compact color grid
	//
	//   bits: array of numbers (0 or 1)
	//   Returns HTML string
	// =========================================================================

	Mu2eHardware.renderBitmap = function (bits) {
		var html = "<div class='bitmap-grid'>";
		for (var i = 0; i < bits.length; ++i) {
			var on = (bits[i] == 1);
			html += "<span class='bitmap-cell " +
				(on ? "bitmap-on" : "bitmap-off") +
				"' title='ch " + i + ": " + (on ? "ON" : "OFF") + "'></span>";
		}
		html += "</div>";
		return html;
	};

	// Get DTC Counter Values returns numeric fields indexed by explicit ROC link ID.
	var TRACKER_COUNTER_ROWS = [
		"TX EWM Count", "TX Data Request Count", "TX Heartbeat Count", "RX Data Header Count"
	];
	var TRACKER_COUNTER_FIELDS = ["txEwm", "txDataRequest", "txHeartbeat", "rxDataHeader"];

	// Route exactly once through an enabled tracker ROC plugin on this DTC.
	// Its handler reads the parent DTC, including all six link counters.
	Mu2eHardware.getDTCCounterInputs = function (uid, enabledLinks) {
		var name = "ROC FEMacro - Get DTC Counter Values";
		var macro = (Mu2eHardware.getMacrosForDevice(uid) || {})[name];
		if (!macro || !Array.isArray(macro.inputs) || macro.inputs.length !== 1 ||
			!Array.isArray(macro.outputs) || macro.outputs.indexOf("Counter Values") < 0 ||
			!/^Target ROC(?: or Mask)?(?: \(.*\))?$/.test(macro.inputs[0])) return null;
		if (!Array.isArray(enabledLinks) || enabledLinks.length !== 6) return null;
		var link = enabledLinks.indexOf(true);
		if (link < 0) return null;
		var inputs = {};
		inputs[macro.inputs[0]] = String(link);
		return inputs;
	};

	Mu2eHardware.parseDTCCounters = function (result) {
		var parsed = {rows: {}, errors: []};
		if (!result || result.error) {
			parsed.errors.push(result && result.error ? String(result.error) : "No response received");
			return parsed;
		}
		if (!Array.isArray(result.targets) || result.targets.length !== 1) {
			parsed.errors.push("Expected counter values from exactly one DTC");
			return parsed;
		}
		var outputs = result.targets[0] && result.targets[0].outputs;
		if (!outputs || typeof outputs["Counter Values"] !== "string") {
			parsed.errors.push("Missing Counter Values output; the DTC frontend must provide ROC FEMacro - Get DTC Counter Values");
			return parsed;
		}
		var values;
		try {
			values = JSON.parse(outputs["Counter Values"]);
		} catch (e) {
			parsed.errors.push("Invalid Counter Values JSON");
			return parsed;
		}
		if (!values || values.schemaVersion !== 1 || !Array.isArray(values.links) || values.links.length !== 6) {
			parsed.errors.push("Expected Counter Values schema version 1 with six ROC links");
			return parsed;
		}
		var byLink = Object.create(null);
		for (var i = 0; i < values.links.length; ++i) {
			var item = values.links[i];
			if (!item || !Number.isInteger(item.link) || item.link < 0 || item.link > 5 || byLink[item.link]) {
				parsed.errors.push("Expected unique ROC link IDs 0-5 in Counter Values");
				return parsed;
			}
			byLink[item.link] = item;
		}
		TRACKER_COUNTER_ROWS.forEach(function (label, row) {
			var field = TRACKER_COUNTER_FIELDS[row], counts = [];
			for (var link = 0; link < 6; ++link) {
				var count = byLink[link][field];
				if (!Number.isInteger(count) || count < 0 || count > 0xFFFFFFFF) {
					parsed.errors.push(label + ": ROC " + link + " must be an unsigned 32-bit integer");
					return;
				}
				counts.push(count);
			}
			parsed.rows[label] = counts;
		});
		return parsed;
	};

	// Capture the same configured ROC enable flags used by the controls at read time.
	Mu2eHardware.snapshotCounterLinks = function (targets) {
		var byUID = Object.create(null);
		(targets || []).forEach(function (uid) {
			var links = [false, false, false, false, false, false];
			Mu2eHardware.getROCsForDTC(uid).forEach(function (roc) {
				if (Number.isInteger(roc.linkIndex) && roc.linkIndex >= 0 && roc.linkIndex < 6)
					links[roc.linkIndex] = roc.enabled === true;
			});
			byUID[uid] = links;
		});
		return byUID;
	};

	Mu2eHardware.checkDTCCounters = function (result, enabledLinks) {
		var parsed = Mu2eHardware.parseDTCCounters(result);
		parsed.differences = [];
		parsed.failedLinks = [];
		for (var link = 0; link < 6; ++link) {
			if (!enabledLinks || enabledLinks[link] !== true) continue;
			if (!TRACKER_COUNTER_ROWS.every(function (label) { return !!parsed.rows[label]; })) continue;
			var values = TRACKER_COUNTER_ROWS.map(function (label) { return Number(parsed.rows[label][link]); });
			var difference = Math.max.apply(null, values) - Math.min.apply(null, values);
			parsed.differences[link] = difference;
			if (difference > 10) parsed.failedLinks.push(link);
		}
		parsed.success = parsed.errors.length === 0 && parsed.failedLinks.length === 0;
		return parsed;
	};

	Mu2eHardware.formatTrackerCounters = function (targets, results, complete, enabledByUID) {
		enabledByUID = enabledByUID || Mu2eHardware.snapshotCounterLinks(targets);
		var byUID = Object.create(null), groups = Object.create(null), unmatched = [];
		(results || []).forEach(function (item) {
			byUID[item.uid] = Mu2eHardware.checkDTCCounters(item.result, enabledByUID[item.uid]);
		});
		(targets || []).forEach(function (uid) {
			var match = /^Trk(\d+)_DTC([01])$/i.exec(uid);
			if (!match) { unmatched.push(uid); return; }
			var node = String(Number(match[1]));
			if (!groups[node]) groups[node] = {name: uid.slice(0, uid.lastIndexOf("_")), dtcs: [null, null]};
			groups[node].dtcs[Number(match[2])] = uid;
		});
		var html = "<div class='tracker-counter-note'>ROC links 0&ndash;5 for each DTC. " +
			"Counts since last reset; DTCs are read independently. " +
			"Grey: disabled or not configured (excluded from comparison). " +
			"Red: an enabled ROC's largest and smallest counts differ by more than 10. " +
			"Each DTC with a mismatch or read error counts once as failed.</div>";
		Object.keys(groups).sort(function (a, b) { return Number(a) - Number(b); }).forEach(function (node) {
			var group = groups[node];
			function columnAttributes(col) {
				var uid = group.dtcs[Math.floor(col / 6)], link = col % 6;
				var enabled = uid && enabledByUID[uid] && enabledByUID[uid][link] === true;
				var parsed = uid && byUID[uid];
				var classes = link === 0 ? ["counter-dtc-start"] : [];
				var title = "";
				if (!enabled) {
					classes.push("counter-disabled");
					title = "Disabled or not configured; excluded from comparison";
				} else if (parsed && parsed.failedLinks.indexOf(link) >= 0) {
					classes.push("counter-mismatch");
					title = "Counter difference " + parsed.differences[link] + " exceeds 10";
				}
				return (classes.length ? " class='" + classes.join(" ") + "'" : "") +
					(title ? " title='" + title + "'" : "");
			}
			html += "<section class='tracker-counter-node'><h3>" + _esc(group.name) + "</h3>";
			group.dtcs.forEach(function (uid, dtc) {
				var parsed = uid && byUID[uid];
				var note = !uid ? "DTC" + dtc + ": not included in this read" :
					!parsed ? uid + (complete ? ": no response received" : ": pending") :
					parsed.errors.length ? uid + ": " + parsed.errors.join("; ") : "";
				if (note) html += "<div class='tracker-counter-note" +
					((parsed && parsed.errors.length) || (uid && complete && !parsed) ? " badValue" : "") +
					"'>" + _esc(note) + "</div>";
				if (parsed && parsed.failedLinks.length)
					html += "<div class='tracker-counter-note badValue'>" + _esc(uid) +
						": " + parsed.failedLinks.map(function (link) {
							return "ROC " + link + " difference " + parsed.differences[link];
						}).join("; ") + " (limit 10)</div>";
			});
			html += "<div class='tracker-counters-scroll'><table class='tracker-counters-table' " +
				"aria-label='" + _esc(group.name) + " performance counters'><thead><tr>" +
				"<th rowspan='2' scope='col'>Counter</th>";
			for (var dtc = 0; dtc < 2; ++dtc)
				html += "<th colspan='6' scope='colgroup' class='counter-dtc-start'>" +
					_esc(group.dtcs[dtc] || "DTC" + dtc + " (not included)") + "</th>";
			html += "</tr><tr>";
			for (var col = 0; col < 12; ++col)
				html += "<th scope='col'" + columnAttributes(col) +
					">ROC " + (col % 6) + "</th>";
			html += "</tr></thead><tbody>";
			TRACKER_COUNTER_ROWS.forEach(function (label) {
				html += "<tr><th scope='row'>" + _esc(label) + "</th>";
				for (var col = 0; col < 12; ++col) {
					var uid = group.dtcs[Math.floor(col / 6)], parsed = uid && byUID[uid];
					var values = parsed && parsed.rows[label];
					var value = values ? values[col % 6] : uid && !parsed && !complete ? "Pending" : "N/A";
					html += "<td" + columnAttributes(col) + ">" + _esc(String(value)) + "</td>";
				}
				html += "</tr>";
			});
			html += "</tbody></table></div></section>";
		});
		unmatched.forEach(function (uid) {
			html += "<div class='badValue'>Cannot place " + _esc(uid) +
				" in a tracker table: expected a Trkxx_DTC0 or Trkxx_DTC1 UID.</div>";
		});
		return html;
	};

	// =========================================================================
	// Promoted macros config — which macros to show as quick-action buttons
	// =========================================================================

	Mu2eHardware.PROMOTED_MACROS = {
		"DTCFrontEndInterface": [
			"DTC Soft Reset",
			"ROC FEMacro - Init Readout",
			"Get DTC Counters",
			"ROC FEMacro - Read SPI",
			"ROC FEMacro - Print Digis",
			"ROC FEMacro - Print Status",
			"ROC FEMacro - Measure Channel Rates",
			"Get Simple Status",
			"Get Firmware Version",
			"Get Link Lock Status",
			"DTC Read",
			"DTC Write",
			"ROC Read",
			"ROC Write",
			"ROC FEMacro - Read Register",
			"ROC FEMacro - Read DIGI Register",
			"ROC FEMacro - Write DIGI Register",
			"ROC FEMacro - Read Panel ID",
			"ROC FEMacro - Find Alignment",
			"ROC FEMacro - Deserialize and set thresholds",
			"ROC FEMacro - Find and serialize thresholds",
			"ROC FEMacro - Measure Thresholds",
		],
	};

	// Exact DTC-forwarded macro names approved for per-ROC buttons.
	Mu2eHardware.ROC_BUTTON_ALLOWLIST = [
		"ROC FEMacro - Init Readout",
		"ROC FEMacro - Read Register",
		"ROC FEMacro - Read SPI",
		"ROC FEMacro - Read DIGI Register",
		"ROC FEMacro - Write DIGI Register",
		"ROC FEMacro - Print Digis",
		"ROC FEMacro - Print Status",
		"ROC FEMacro - Measure Channel Rates",
		"ROC FEMacro - Read Panel ID",
		"ROC FEMacro - Read Serial Number",
		"ROC FEMacro - Find Alignment",
		"ROC FEMacro - Deserialize and set thresholds",
		"ROC FEMacro - Find and serialize thresholds",
		"ROC FEMacro - Measure Thresholds",
	];

	// Keys approved for fleet-wide buttons. Available keys are defined in
	// HardwareOverview.html.
	Mu2eHardware.ALL_TRACKER_BUTTON_ALLOWLIST = [
		"counters",
		"status",
		"preflight",
		"init-readout",
		"link-status",
		"read-panel-id",
		"reset-counters",
		"find-alignment",
		"set-thresholds",
		"find-save-thresholds",
	];

	// =========================================================================
	// macroDisplayName — strip ROC FEMacro prefix for cleaner button labels
	// =========================================================================

	Mu2eHardware.macroDisplayName = function (name) {
		if (name === "ROC FEMacro - Find and serialize thresholds")
			return "Find & Save Thresholds";
		if (name === "ROC FEMacro - Deserialize and set thresholds")
			return "Set Thresholds";
		if (name === "ROC FEMacro - Measure Channel Rates")
			return "Read Rates";
		if (name === "ROC FEMacro - Print Status")
			return "ROC Status";
		if (name.indexOf("ROC FEMacro - ") === 0)
			return name.substring(14);
		if (name.indexOf("ROC FEMacro -") === 0)
			return name.substring(13).trim();
		var linkMatch = name.match(/^Link\d+_[^_]+_(.+)$/);
		if (linkMatch) return linkMatch[1];
		return name;
	};

	// =========================================================================
	// renderSettingsHTML — render settings fields as grouped accordion HTML
	//   fields: [{name, value, table}, ...]
	//   Returns HTML string
	// =========================================================================

	Mu2eHardware.renderSettingsHTML = function (fields) {
		if (!fields || !fields.length)
			return "<span class='placeholder-text'>No settings available.</span>";

		var groups = [];
		var groupMap = {};
		for (var i = 0; i < fields.length; ++i) {
			var tbl = fields[i].table || "Settings";
			if (!groupMap[tbl]) {
				groupMap[tbl] = [];
				groups.push(tbl);
			}
			groupMap[tbl].push(fields[i]);
		}

		var h = "";
		for (var g = 0; g < groups.length; ++g) {
			var gName = groups[g];
			var gFields = groupMap[gName];
			var open = g === 0 ? " accordion-open" : "";
			var displayName = gName.replace(/Table$/, "");

			h += "<div class='accordion-section'>";
			h += "<div class='accordion-header' onclick='this.nextElementSibling.classList.toggle(\"accordion-open\")'>" +
				displayName + " <span class='accordion-count'>(" +
				gFields.length + ")</span></div>";
			h += "<div class='accordion-body" + open + "'>";
			h += "<table class='settings-table'>";
			for (var i = 0; i < gFields.length; ++i) {
				h += "<tr><td class='settings-name'>" + _esc(gFields[i].name) +
					"</td><td class='settings-value'>" +
					Mu2eHardware.colorizeValue(_esc(gFields[i].value)) + "</td></tr>";
			}
			h += "</table></div></div>";
		}
		return h;
	};

	// =========================================================================
	// renderChannelsHTML — render channel records as accordion table HTML
	//   result: { channels: [{uid, fields: {name:val}}, ...], tableName }
	//   Returns HTML string
	// =========================================================================

	Mu2eHardware.renderChannelsHTML = function (result) {
		if (!result || !result.channels || !result.channels.length)
			return "";

		var chans = result.channels;
		var cols = Object.keys(chans[0].fields);

		var h = "<div class='accordion-section'>";
		h += "<div class='accordion-header' onclick='this.nextElementSibling.classList.toggle(\"accordion-open\")'>" +
			"Channels <span class='accordion-count'>(" +
			chans.length + ")</span></div>";
		h += "<div class='accordion-body' id='channelsBody'>";
		h += "<table class='settings-table'>";
		h += "<tr>";
		for (var c = 0; c < cols.length; ++c)
			h += "<td class='settings-name' style='font-size:9px;'>" +
				_esc(cols[c]) + "</td>";
		h += "</tr>";
		for (var i = 0; i < chans.length; ++i) {
			h += "<tr>";
			for (var c = 0; c < cols.length; ++c)
				h += "<td class='settings-value'>" +
					_esc(chans[i].fields[cols[c]] || "") + "</td>";
			h += "</tr>";
		}
		h += "</table></div></div>";
		return h;
	};

	// =========================================================================
	// Internal helpers
	// =========================================================================

	// Try to parse a JSON array (optionally nested once) into numeric values.
	function _tryParseNumericArray(str) {
		if (!str || str.length < 3) return null;
		var s = str.trim();
		if (s[0] !== "[") return null;
		try {
			var arr = JSON.parse(s);
			// Unwrap nested array: [[1,1,0,...]]
			if (Array.isArray(arr) && arr.length === 1 && Array.isArray(arr[0]))
				arr = arr[0];
			if (!Array.isArray(arr) || arr.length < 2) return null;
			for (var i = 0; i < arr.length; ++i)
				if (typeof arr[i] !== "number") return null;
			return arr;
		} catch (e) {
			return null;
		}
	}

	// Only true bit arrays belong in the red/green bitmap renderer. Numeric
	// result arrays such as ROC links and panel IDs must remain readable values.
	function _tryParseBitArray(str) {
		var arr = _tryParseNumericArray(str);
		if (!arr) return null;
		for (var i = 0; i < arr.length; ++i)
			if (arr[i] !== 0 && arr[i] !== 1) return null;
		return arr;
	}

	function _decodeURIComponentSafe(value) {
		try {
			return decodeURIComponent(value || "");
		} catch (e) {
			return value || "";
		}
	}

	function _esc(str) {
		if (!str) return "";
		var div = document.createElement("div");
		div.appendChild(document.createTextNode(str));
		return div.innerHTML;
	}

	function _escAttr(str) {
		return _esc(str).replace(/'/g, "&#39;").replace(/"/g, "&quot;");
	}

	function _statusDotHTML(uid, statusCls, onToggleAttr) {
		if (onToggleAttr) {
			return "<span class='status-dot status-dot-clickable " + statusCls + "' " +
				"id='dot-" + _esc(uid) + "' " +
				"onclick='" + onToggleAttr.replace(/__UID__/g, _escAttr(uid)) +
				"; event.stopPropagation();' " +
				"title='Click to toggle on/off'></span> ";
		}
		return "<span class='status-dot " + statusCls + "'></span> ";
	}

	// Extract short hostname from parentApp string
	// e.g. "https://mu2edaq09.fnal.gov:2015/ctx/app" → "mu2edaq09"
	Mu2eHardware.extractHostname = _extractHostname;

	function _extractHostname(parentApp) {
		var host = parentApp;
		// strip protocol
		var protoEnd = host.indexOf("://");
		if (protoEnd >= 0) host = host.substring(protoEnd + 3);
		// strip port and path
		var portOrPath = host.search(/[:/]/);
		if (portOrPath >= 0) host = host.substring(0, portOrPath);
		// strip common domain suffixes
		host = host.replace(/\.fnal\.gov$/i, "")
			.replace(/\.local$/i, "");
		return host;
	}

	function _discoverMacroMakerLid(callback) {
		DesktopContent.XMLHttpRequest(
			"Request?RequestType=getAppId&classNeedle=ots::MacroMakerSupervisor",
			"",
			function (req) {
				if (!req || !req.responseXML) {
					Debug.log("Mu2eHardware: Could not discover MacroMaker supervisor.",
						Debug.HIGH_PRIORITY);
					if (callback) callback();
					return;
				}
				_macroMakerLid = DesktopContent.getXMLValue(req, "id") | 0;
				Debug.log("Mu2eHardware: MacroMaker LID = " + _macroMakerLid);
				if (callback) callback();
			},
			0, 0, true, false, true /* targetGatewaySupervisor */);
	}

	function _discoverConfigGuiLid(callback) {
		DesktopContent.XMLHttpRequest(
			"Request?RequestType=getAppId&classNeedle=ots::ConfigurationGUISupervisor",
			"",
			function (req) {
				if (!req || !req.responseXML) {
					Debug.log("Mu2eHardware: Could not discover ConfigGUI supervisor.",
						Debug.HIGH_PRIORITY);
					if (callback) callback();
					return;
				}
				_configGuiLid = DesktopContent.getXMLValue(req, "id") | 0;
				Debug.log("Mu2eHardware: ConfigGUI LID = " + _configGuiLid);
				if (callback) callback();
			},
			0, 0, true, false, true /* targetGatewaySupervisor */);
	}

})();
