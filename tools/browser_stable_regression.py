"""Real Chromium/Edge 2.4.2 stable input regression. Requires Python playwright.
Runs only against local HTTP servers and fresh isolated browser contexts.
"""
from pathlib import Path
import argparse,functools,http.server,json,threading,time
from playwright.sync_api import sync_playwright

class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self,*args):pass

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--browser',required=True);ap.add_argument('--output',required=True,type=Path);ap.add_argument('--baseline',type=Path);ap.add_argument('--web-root',type=Path);args=ap.parse_args()
    out=args.output;out.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[1];servers=[];results=[];metrics={};errors=[]
    def serve(folder):
        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Quiet,directory=str(folder)));threading.Thread(target=server.serve_forever,daemon=True).start();servers.append(server);return 'http://127.0.0.1:'+str(server.server_port)
    url=serve(args.web_root or root/'web/dist');baseline=serve(args.baseline) if args.baseline else None
    def check(name,ok):results.append({'test':name,'passed':bool(ok)});print(name+': '+('PASS' if ok else 'FAIL'),flush=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=args.browser,headless=True,args=['--autoplay-policy=no-user-gesture-required'])
        def fresh(mobile=False,address=None):
            ctx=browser.new_context(viewport={'width':960,'height':540},has_touch=mobile,is_mobile=mobile);page=ctx.new_page();page.on('pageerror',lambda e:errors.append(str(e)));page.goto((address or url)+('/?touch=1' if mobile else '/'));page.wait_for_function('window.__canityReady',timeout=120000);page.wait_for_timeout(250);return ctx,page
        def key(page,k):page.keyboard.down(k);page.wait_for_timeout(100);page.keyboard.up(k);page.wait_for_timeout(160)
        def fps(page):
            return page.evaluate("() => new Promise(resolve=>{let first=0,count=0;function tick(t){if(!first)first=t;if(t-first>=4000){resolve(Math.round(count*10000/(t-first))/10);return;}count++;requestAnimationFrame(tick);}requestAnimationFrame(tick);})")
        def state(page,field):return page.evaluate('(i)=>Module._CanityWebInputState(i)',field)
        try:
            if baseline:
                ctx,page=fresh(address=baseline);key(page,'Enter');page.wait_for_timeout(500);metrics['baseline_fps_same_machine']=fps(page);ctx.close()
            ctx,page=fresh();key(page,'Enter');page.wait_for_function('Module._CanityProbeState()===1');metrics['stable_fps_same_machine']=fps(page)
            page.keyboard.down('Tab');page.wait_for_timeout(150);check('TAB hold opens small map',state(page,0)==1)
            page.keyboard.up('Tab');page.wait_for_timeout(150);check('TAB release closes small map',state(page,0)==0)
            key(page,'m');check('M opens toggle map',state(page,0)==3);key(page,'Tab');check('TAB after toggle returns to closed',state(page,0)==0)
            for i in range(5):
                page.locator('#fullscreenBtn').click();page.wait_for_function('!!document.fullscreenElement');page.locator('#fullscreenBtn').click();page.wait_for_function('!document.fullscreenElement');check('fullscreen round trip '+str(i+1),True)
            key(page,'Escape');page.screenshot(path=str(out/'pause.png'));check('pause remains usable',page.evaluate('Module._CanityProbeState()')==2);ctx.close()
            ctx,page=fresh(mobile=True);key(page,'Enter');page.wait_for_function('Module._CanityProbeState()===1');page.wait_for_timeout(500)
            cd=ctx.new_cdp_session(page)
            def touch(kind,x=None,y=None):cd.send('Input.dispatchTouchEvent',{'type':kind,'touchPoints':[] if kind=='touchEnd' else [{'x':x,'y':y,'id':1}]})
            button=page.locator('[data-act="SWING"]').bounding_box();x=button['x']+button['width']/2;y=button['y']+button['height']/2
            touch('touchStart',x,y);page.wait_for_timeout(600);check('mobile hold charges wrench',state(page,1)>=500)
            touch('touchEnd');page.wait_for_function('Module._CanityWebInputState(2)>0',timeout=2000);check('mobile release swings wrench',True);page.wait_for_timeout(2400)
            ctx.close();ctx,page=fresh(mobile=True);key(page,'Enter');page.wait_for_function('Module._CanityProbeState()===1');page.wait_for_timeout(500);cd=ctx.new_cdp_session(page)
            button=page.locator('[data-act="SWING"]').bounding_box();x=button['x']+button['width']/2;y=button['y']+button['height']/2
            touch('touchStart',x,y);page.wait_for_timeout(65);touch('touchEnd');page.wait_for_function('Module._CanityWebInputState(2)>0',timeout=2000);check('short deliberate tap produces a light strike',True);page.wait_for_timeout(300)
            def multi(kind,points):cd.send('Input.dispatchTouchEvent',{'type':kind,'touchPoints':[{'x':x,'y':y,'id':i} for i,x,y in points]})
            left=(10,150,370);multi('touchStart',[left]);left=(10,150,310);multi('touchMove',[left]);page.wait_for_timeout(160)
            check('joystick starts moving',page.evaluate('Module._CanityProbeGameKey(87)&1'))
            multi('touchStart',[left,(11,220,350)]);multi('touchEnd',[(11,220,350)]);page.wait_for_timeout(160)
            check('unrelated second-finger end does not stop joystick',page.evaluate('Module._CanityProbeGameKey(87)&1'))
            box=page.locator('#btns2 [data-act="MAP"]').bounding_box();m=(12,box['x']+box['width']/2,box['y']+box['height']/2)
            multi('touchStart',[left,m]);page.wait_for_timeout(180);multi('touchEnd',[m]);page.wait_for_function('Module._CanityWebInputState(0)===3')
            box=page.locator('#mapClose').bounding_box();m=(12,box['x']+box['width']/2,box['y']+box['height']/2)
            multi('touchStart',[left,m]);page.wait_for_timeout(180);multi('touchEnd',[m]);page.wait_for_function('Module._CanityWebInputState(0)===0');page.wait_for_timeout(200)
            check('map round trip restores still-held joystick',page.evaluate('Module._CanityProbeGameKey(87)&1'))
            before=state(page,6);multi('touchStart',[left,(13,550,270)]);multi('touchMove',[left,(13,550,340)]);page.wait_for_timeout(180)
            check('touch look keeps the camera horizontal',state(page,6)==before==0)
            multi('touchEnd',[(13,550,340)]);page.wait_for_timeout(120);check('look release preserves independent movement',page.evaluate('Module._CanityProbeGameKey(87)&1'))
            multi('touchEnd',[]);page.wait_for_timeout(180);check('joystick release clears movement',not page.evaluate('Module._CanityProbeGameKey(87)&1'))
            box=page.locator('[data-act="DODGE"]').bounding_box();check('escape button is visible',box is not None)
            touch('touchStart',box['x']+box['width']/2,box['y']+box['height']/2);page.wait_for_timeout(60);check('escape button forwards SPACE to control logic',page.evaluate('Module._CanityProbeGameKey(32)&1'));touch('touchEnd');page.wait_for_timeout(180)
            page.locator('#saveToggle').tap();check('mobile save tools can open',page.locator('#entry').is_visible())
            page.evaluate("localStorage.setItem('canity:canity0.sav',btoa('slot-zero'));localStorage.setItem('canity:canity1.sav',btoa('slot-one'))")
            page.locator('#saveSlotChoice').select_option('1');check('explicit slot-one export reads slot one',page.evaluate("String.fromCharCode.apply(null,window.__canityExportBytes())==='slot-one'"))
            page.locator('#saveSlotChoice').select_option('-1');check('active-slot export uses actual caller slot',page.evaluate("String.fromCharCode.apply(null,window.__canityExportBytes())==='slot-zero'"))
            metrics['mobile_audio_sample_rate']=page.evaluate('Module._actx?Module._actx.sampleRate:0')
            page.evaluate("Object.defineProperty(document,'hidden',{configurable:true,get:()=>true});document.dispatchEvent(new Event('visibilitychange'))")
            page.wait_for_timeout(180);check('hidden lifecycle pauses gameplay',page.evaluate('Module._CanityProbeState()')==2)
            check('hidden lifecycle suspends audio',page.evaluate("!Module._actx || Module._actx.state==='suspended'"));ctx.close()
            ctx,page=fresh(mobile=True);key(page,'ArrowDown');key(page,'Enter');page.wait_for_function('Module._CanityProbeState()===5');page.wait_for_timeout(300)
            zoom=state(page,3);page.locator('[data-tree-zoom="1"]').tap();page.wait_for_timeout(150);check('mobile plus zooms meta tree',state(page,3)>zoom)
            page.locator('[data-tree-zoom="-1"]').tap();page.wait_for_timeout(150);check('mobile minus zooms out',abs(state(page,3)-zoom)<=1)
            cd=ctx.new_cdp_session(page);cd.send('Input.dispatchTouchEvent',{'type':'touchStart','touchPoints':[{'x':480,'y':260,'id':2}]});cd.send('Input.dispatchTouchEvent',{'type':'touchMove','touchPoints':[{'x':620,'y':300,'id':2}]});page.wait_for_timeout(150);check('mobile horizontal drag reaches shared tree',state(page,5)>0);cd.send('Input.dispatchTouchEvent',{'type':'touchEnd','touchPoints':[]});page.wait_for_timeout(100);check('mobile drag release clears capture',state(page,5)==0);page.screenshot(path=str(out/'mobile-tree.png'));ctx.close()
            ctx=browser.new_context(viewport={'width':960,'height':540},has_touch=True,is_mobile=False)
            ctx.add_init_script("""const media=window.matchMedia.bind(window);window.matchMedia=function(q){if(q==='(pointer:coarse)' || q==='(pointer: coarse)')return {matches:false,media:q,addListener(){},removeListener(){},addEventListener(){},removeEventListener(){}};if(q==='(hover:hover)' || q==='(hover: hover)')return {matches:true,media:q,addListener(){},removeListener(){},addEventListener(){},removeEventListener(){}};return media(q);};""")
            page=ctx.new_page();page.on('pageerror',lambda e:errors.append(str(e)));page.goto(url);page.wait_for_function('window.__canityReady');page.wait_for_timeout(200)
            check('hybrid mouse capability does not force touch overlay',page.evaluate("!Module._usingTouch && !document.getElementById('touch').classList.contains('on')"))
            key(page,'Enter');page.wait_for_function('Module._CanityProbeState()===1');page.mouse.move(460,250);page.wait_for_timeout(100);before=page.evaluate('Module._CanityProbeDir(0)');page.mouse.move(640,250);page.wait_for_timeout(150);check('hybrid mouse can turn the camera',page.evaluate('Module._CanityProbeDir(0)')!=before);ctx.close()
            check('no browser page errors',not errors)
        except Exception as exc:results.append({'test':'browser execution','passed':False,'error':str(exc)})
        finally:
            browser.close()
            for server in servers:server.shutdown()
    report={'passed':all(r['passed'] for r in results),'tests':results,'metrics':metrics,'page_errors':errors,'scope':'Headless Edge; desktop, mobile touch, hybrid media and lifecycle signal emulation; not physical-phone acceptance'}
    (out/'browser-results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8');print(json.dumps(report,ensure_ascii=False),flush=True);return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
