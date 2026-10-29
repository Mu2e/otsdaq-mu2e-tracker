/* NoiseRun controls the tracker Gateway's Iterator. Loading/closing this page never starts/stops a scan. */
(function(root) {
    'use strict';
    const api = {}, check = (ok, message) => { if(!ok) throw new Error(message); };
    const values = (xml, tag) => Array.from(xml.getElementsByTagName(tag)).map(n => n.getAttribute('value') || '');
    function one(xml, tag) { const a=values(xml,tag); check(a.length===1, 'Missing/ambiguous Iterator field: '+tag); return a[0]; }
    // The Gateway appends the noise_run_* fields only while the TrackerNoiseRun Iterator
    // plugin owns the Iterator ('active_plugin' names it); otherwise NoiseRun is Inactive.
    const PLUGIN='TrackerNoiseRun', PLUGIN_URL='StateMachineXgiHandler?StateMachine=iteratePluginCommand&plugin='+PLUGIN;
    api.parseStatus = function(xml) {
        check(values(xml,'active_plugin').length===1, 'Gateway Iterator plugin support is unavailable. Activate the rebuilt Gateway before using this action.');
        const activePlugin=one(xml,'active_plugin');
        check(activePlugin==='' || activePlugin===PLUGIN, 'Iterator plugin "'+activePlugin+'" owns the Iterator; stop it first.');
        const common={error:one(xml,'error_message'), plan:one(xml,'active_plan'), planStatus:one(xml,'active_plan_status')};
        if(activePlugin!==PLUGIN)
            return Object.assign({state:'Inactive', owned:false, scans:0, summary:null, excluded:'', unresolved:0, fsm:''}, common);
        const state=one(xml,'noise_run_state'), owned=one(xml,'noise_run_owned');
        check(['Inactive','Starting','Running','Stopping','Unresolved','Error'].includes(state) && ['0','1'].includes(owned),'Invalid NoiseRun Iterator state.');
        const text=one(xml,'noise_run_last_summary'), summary=text ? JSON.parse(text) : null;
        const scans=Number(one(xml,'noise_run_scans')), unresolved=Number(one(xml,'noise_run_unresolved_ops'));
        check(Number.isSafeInteger(scans) && scans>=0 && Number.isSafeInteger(unresolved) && unresolved>=0,'Invalid Iterator scan count.');
        if(summary) check(Number.isFinite(summary.total_hz) && summary.total_hz>=0 && Array.isArray(summary.dtcs),'Invalid Iterator rate summary.');
        return Object.assign({state, owned:owned==='1', scans, summary, excluded:one(xml,'noise_run_excluded_rocs'), unresolved, fsm:one(xml,'noise_run_fsm')}, common);
    };
    api.browserDependencies = async function() {
        const dc=root.DesktopContent;
        check(dc,'Open Tracker from the OTS desktop.');
        const gateway=Number(dc._remoteServerUrnLid)||Number(dc._serverUrnLid);
        check(gateway>0 && Number(dc._localUrnLid)>0 && dc._localOrigin,'Tracker request routing is not ready; reopen NoiseRun after the page loads.');
        function request(url) {
            return new Promise((resolve,reject) => {
                dc.XMLHttpRequest(url,'',(req,unused,err) => {
                    try {
                        check(!err && req && req.responseXML,err || 'Iterator connection lost. Run state is unknown; commands were not retried.');
                        const xml=req.responseXML;
                        const errors=values(xml,'Error').concat(values(xml,'state_transition_attempted_err')).filter(Boolean);
                        check(!errors.length,errors.join('; '));
                        resolve(xml);
                    } catch(e) { reject(e); }
                },0,0,true,true,false,false,false,gateway);
            });
        }
        return {
            status:async()=>api.parseStatus(await request('Request?RequestType=getIterationPlanStatus')),
            start:async()=>{
                const s=await api.parseStatus(await request('Request?RequestType=getIterationPlanStatus'));
                check(!s.owned && s.planStatus==='Inactive','Iterator is busy; Stop its current plan first.');
                const xml=await request(PLUGIN_URL+'&pluginCommand=Start&fsmWindowName=');
                check(one(xml,'noise_run_accepted')==='1','Iterator did not accept NoiseRun Start. Check status before retrying.');
            },
            stop:async()=>{
                const s=api.parseStatus(await request('Request?RequestType=getIterationPlanStatus'));
                check(s.owned,'NoiseRun is not the active Iterator plan. No other plan was stopped.');
                // after a frontend stopped answering mid-macro the outcome is unknown; the
                // operator must check the frontend/ROC masks and then acknowledge the stop
                const command = s.unresolved>0 ? 'StopAcknowledge' : 'Stop';
                if(command==='StopAcknowledge')
                    check(root.confirm(s.unresolved+' NoiseRun hardware operation(s) have an unknown outcome. Have you checked the frontend logs and ROC masks? Press OK to release the Iterator.'),'Stop cancelled; NoiseRun still owns the Iterator.');
                const xml=await request(PLUGIN_URL+'&pluginCommand='+command+'&fsmWindowName=');
                check(one(xml,'noise_run_stop_requested')==='1','Iterator did not confirm Stop. Check status before retrying.');
            }
        };
    };
    root.TrackerRatesRun=api;
    if(typeof module!=='undefined' && module.exports) module.exports=api;
})(typeof window!=='undefined'?window:globalThis);
