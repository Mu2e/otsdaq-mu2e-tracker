/* NoiseRun controls the tracker Gateway's Iterator. Loading/closing this page never starts/stops a scan. */
(function(root) {
    'use strict';
    const api = {}, check = (ok, message) => { if(!ok) throw new Error(message); };
    const values = (xml, tag) => Array.from(xml.getElementsByTagName(tag)).map(n => n.getAttribute('value') || '');
    function one(xml, tag) { const a=values(xml,tag); check(a.length===1, 'Missing/ambiguous Iterator field: '+tag); return a[0]; }
    api.parseStatus = function(xml) {
        check(one(xml,'noise_run_supported')==='1', 'Gateway NoiseRun Iterator support is unavailable. Activate the rebuilt Gateway before using this action.');
        const state=one(xml,'noise_run_state'), owned=one(xml,'noise_run_owned');
        check(['Inactive','Starting','Running','Stopping','Error'].includes(state) && ['0','1'].includes(owned),'Invalid NoiseRun Iterator state.');
        const text=one(xml,'noise_run_last_summary'), summary=text ? JSON.parse(text) : null;
        const scans=Number(one(xml,'noise_run_scans'));
        check(Number.isSafeInteger(scans) && scans>=0,'Invalid Iterator scan count.');
        if(summary) check(Number.isFinite(summary.total_hz) && summary.total_hz>=0 && Array.isArray(summary.dtcs),'Invalid Iterator rate summary.');
        return {state, owned:owned==='1', scans, summary, excluded:one(xml,'noise_run_excluded_rocs'),
            error:one(xml,'error_message'), plan:one(xml,'active_plan'), planStatus:one(xml,'active_plan_status'), fsm:one(xml,'noise_run_fsm')};
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
                const xml=await request('StateMachineXgiHandler?StateMachine=iterateNoiseRunStart&fsmName='+encodeURIComponent(s.fsm)+'&fsmWindowName=');
                check(one(xml,'noise_run_accepted')==='1','Iterator did not accept NoiseRun Start. Check status before retrying.');
            },
            stop:async()=>{
                const s=api.parseStatus(await request('Request?RequestType=getIterationPlanStatus'));
                check(s.owned,'NoiseRun is not the active Iterator plan. No other plan was stopped.');
                const xml=await request('StateMachineXgiHandler?StateMachine=iterateNoiseRunStop&fsmName='+encodeURIComponent(s.fsm)+'&fsmWindowName=');
                check(one(xml,'noise_run_stop_requested')==='1','Iterator did not confirm Stop. Check status before retrying.');
            }
        };
    };
    root.TrackerRatesRun=api;
    if(typeof module!=='undefined' && module.exports) module.exports=api;
})(typeof window!=='undefined'?window:globalThis);
