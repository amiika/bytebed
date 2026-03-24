const char* index_html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Bytebed - Firmly Embedded Bytebeats</title>
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0">
    <style>
        body { background: #050505; display: flex; flex-direction: column; justify-content: center; align-items: center; height: 100vh; margin: 0; }
        #screen-container { position: relative; width: 100vw; max-width: 960px; aspect-ratio: 240/135; box-shadow: 0 0 30px #00ff0026; border-radius: 12px; overflow: hidden; }
        canvas { width: 100%; height: 100%; background: #000000; }
        #overlay { position: absolute; top: 0; left: 0; right: 0; bottom: 0; background: #000000cc; color: #ffffff; display: flex; flex-direction: column; justify-content: center; align-items: center; font-family: monospace; font-size: min(4vw, 24px); cursor: pointer; z-index: 30; border: 2px solid #00ff00; box-sizing: border-box; border-radius: 12px; transition: opacity 0.3s; }
        .hint { font-size: 12px; color: #888888; margin-top: 15px; text-align: center; padding: 0 10px; }
        
        #hijack-btn { position: absolute; top: 2%; right: 2%; z-index: 20; background: #000; border: 1px solid; padding: 4px 10px; font-family: monospace; font-size: min(3vw, 14px); font-weight: bold; cursor: pointer; outline: none; transition: transform 0.1s; }
        #hijack-btn:active { transform: scale(0.95); }
        
        #editor { position: absolute; bottom: 0; left: 0; width: 100%; z-index: 20; border: none; border-top: 1px solid; resize: none; outline: none; background: #000; font-family: monospace; font-size: min(3.5vw, 18px); font-weight: bold; padding: 8px 12px; box-sizing: border-box; word-break: break-all; caret-shape: block; }
        #editor:focus { border-top: 2px solid; } 
    </style>
</head>
<body>
    <div id="screen-container">
        <canvas id="m5"></canvas>
        <div id="overlay">
            <div id="loading-txt">LOADING WASM...</div>
        </div>
        <button id="hijack-btn" onclick="toggleHijack(event)">HIJACK</button>
        <textarea id="editor" spellcheck="false" autocomplete="off" autocorrect="off" autocapitalize="off" readonly></textarea>
    </div>
    
    <script>
        let wasm = null;
        let inputBufferPtr = 0;
        let state = null; 
        let history_log = []; 
        
        // --- DECOUPLED JS RING BUFFER ---
        const VIS_RING_SIZE = 8192;
        const vis_ring_val = new Uint8Array(VIS_RING_SIZE);
        const vis_ring_t = new Int32Array(VIS_RING_SIZE);
        let vis_head = 0;
        let vis_tail = 0;

        WebAssembly.instantiateStreaming(fetch('/bytebed.wasm'), {
            env: { memory: new WebAssembly.Memory({ initial: 256 }) }
        }).then(results => {
            wasm = results.instance.exports;
            
            if (wasm._initialize) {
                wasm._initialize(); 
            } else if (wasm.__wasm_call_ctors) {
                wasm.__wasm_call_ctors(); 
            }

            inputBufferPtr = wasm.get_input_buffer();
            document.getElementById('loading-txt').innerText = "BYTE ME!";
        }).catch(err => {
            console.error("WASM failed to load", err);
            document.getElementById('loading-txt').innerText = "WASM INIT FAIL";
        });

        function compileToWASM(str) {
            if (!wasm) return false;
            const encoder = new TextEncoder();
            const view = new Uint8Array(wasm.memory.buffer);
            const encoded = encoder.encode(str);
            view.set(encoded, inputBufferPtr);
            view[inputBufferPtr + encoded.length] = 0; 
            
            let is_rpn = (state && state.r) ? true : false;
            return wasm.wasm_compile(is_rpn); 
        }

        function tickWASM() {
            let val = 128;
            if (wasm) {
                val = wasm.wasm_execute(t_audio);
            }
            
            let next_head = (vis_head + 1) % VIS_RING_SIZE;
            if (next_head !== vis_tail) {
                vis_ring_val[vis_head] = val;
                vis_ring_t[vis_head] = t_audio;
                vis_head = next_head;
            }
            
            t_audio++;
            return val;
        }

        let audioCtx, audioNode;
        let c, n;
        
        const SC = 4;
        const FONT_SMALL = (8 * SC) + 'px monospace';
        const FONT_LARGE = (9 * SC) + 'px monospace';
        
        const mainCanvas = document.getElementById('m5');
        const mainCtx = mainCanvas.getContext('2d');
        mainCanvas.width = 240 * SC; 
        mainCanvas.height = 135 * SC;
        
        const bgSprite = document.createElement('canvas');
        bgSprite.width = 240; bgSprite.height = 135;
        const bgCtx = bgSprite.getContext('2d');
        
        const bgImg = bgCtx.createImageData(240, 135);
        const bgPixels = new Uint32Array(bgImg.data.buffer);
        bgPixels.fill(0xFF000000); 

        let t_audio = 0; 
        let last_val = 128;
        let is_hijacked = false;

        let colorLUT32 = new Uint32Array(256);
        window.themeColor = '#00ff00';
        window.dimColor = '#006600';
        window.shadowColor = '#00ff0026';

        function toHex(num) { return num.toString(16).padStart(2, '0'); }

        function updateColorLUT(r, g, b) {
            for(let i = 0; i < 256; i++) {
                let cR = Math.floor((i/255)*r);
                let cG = Math.floor((i/255)*g);
                let cB = Math.floor((i/255)*b);
                colorLUT32[i] = (255 << 24) | (cB << 16) | (cG << 8) | cR;
            }
            let hR = toHex(r); let hG = toHex(g); let hB = toHex(b);
            window.themeColor = '#' + hR + hG + hB;
            window.shadowColor = '#' + hR + hG + hB + '26'; 
            window.dimColor = '#' + toHex(Math.floor(r * 0.4)) + toHex(Math.floor(g * 0.4)) + toHex(Math.floor(b * 0.4));
            
            const btn = document.getElementById('hijack-btn');
            btn.style.color = window.themeColor;
            btn.style.borderColor = window.dimColor;
            
            const ed = document.getElementById('editor');
            ed.style.color = window.themeColor;
            ed.style.borderTopColor = window.dimColor;
            ed.style.caretColor = window.themeColor; 
        }
        updateColorLUT(0, 255, 0);

        let LATENCY_SAMPLES = 1024; 
        
        document.getElementById('editor').addEventListener('keydown', function(e) {
            if (e.key === 'Enter') {
                e.preventDefault();
                if (is_hijacked) {
                    updateHijack(); 
                    if (ws.readyState === WebSocket.OPEN) {
                        ws.send('E' + this.value); 
                    }
                }
            }
        });

        window.toggleHijack = function(e) {
            e.stopPropagation();
            is_hijacked = !is_hijacked;
            const btn = document.getElementById('hijack-btn');
            const ed = document.getElementById('editor');
            if (is_hijacked) {
                btn.innerText = "STREAM";
                ed.readOnly = false;
                ed.value = state ? state.f : ""; 
                ed.focus();
            } else {
                btn.innerText = "HIJACK";
                ed.readOnly = true;
                ed.blur();
                if (state) {
                    let cp = (state.cp !== undefined) ? state.cp : state.f.length;
                    ed.value = state.f.substring(0, cp) + '\u258C' + state.f.substring(cp); // Half-block
                    compileToWASM(state.ef); 
                }
            }
        };

        window.updateHijack = function() {
            if (!is_hijacked) return;
            let ed = document.getElementById('editor');
            ed.value = ed.value.replace(/[\r\n]/g, ''); 
            compileToWASM(ed.value);
        };

        const ws = new WebSocket('ws://' + location.hostname + '/ws'); 
        
        ws.onmessage = function(event) {
            try {
                let newState = JSON.parse(event.data);
                let oldState = state;
                state = newState; 
                
                if (oldState && (oldState.v !== state.v || oldState.s !== state.s || oldState.r !== state.r)) {
                    bgPixels.fill(0xFF000000); 
                    bgCtx.putImageData(bgImg, 0, 0);
                }
                
                if (!oldState || oldState.r !== state.r || oldState.g !== state.g || oldState.b !== state.b) {
                    updateColorLUT(state.r, state.g, state.b);
                    document.getElementById('screen-container').style.boxShadow = '0 0 30px ' + window.shadowColor;
                    document.getElementById('overlay').style.borderColor = window.themeColor;
                }
                
                if (!oldState || oldState.pm !== state.pm) {
                    if (wasm && wasm.wasm_set_play_mode) wasm.wasm_set_play_mode(state.pm);
                }
                if (oldState && oldState.sr !== state.sr) {
                    if (wasm && wasm.wasm_set_sample_rate) wasm.wasm_set_sample_rate(state.sr);
                    if (c) { 
                        c.close(); c = null; 
                        setTimeout(() => document.getElementById('overlay').click(), 10); 
                    }
                }

                if (!is_hijacked) {
                    let ed = document.getElementById('editor');
                    
                    if (!oldState || oldState.f !== state.f || oldState.cp !== state.cp) {
                        let cp = (state.cp !== undefined) ? state.cp : state.f.length;
                        ed.value = state.f.substring(0, cp) + '\u258C' + state.f.substring(cp); // Half-block
                    }
                    
                    if (!oldState || oldState.ef !== state.ef) {
                        compileToWASM(state.ef); 
                        if (history_log[0] !== state.ef) {
                            history_log.unshift(state.ef);
                            if (history_log.length > 10) history_log.pop();
                        }
                    }
                    
                    let input_h = state.f.length > 80 ? 60 : 25;
                    ed.style.height = (input_h / 135 * 100) + '%';
                }

                if (!c || c.state === 'suspended') {
                    let target_audio_t = state.t;
                    if (t_audio < target_audio_t) {
                        let catchup = target_audio_t - t_audio;
                        if (catchup > 8192) t_audio = target_audio_t - 8192; 
                        while (t_audio < target_audio_t) {
                            tickWASM(); 
                        }
                    }
                } else {
                    let os_latency = (c.baseLatency || 0.03) + (c.outputLatency || 0.05);
                    let network_flight_time = 0.05; 
                    let cur_rate = (state && state.sr) ? state.sr : 8000;
                    let buffer_latency = 1024 / cur_rate;
                    LATENCY_SAMPLES = Math.floor((os_latency + network_flight_time + buffer_latency) * cur_rate);

                    let target_audio_t = state.t + LATENCY_SAMPLES;
                    if (Math.abs(t_audio - target_audio_t) > 4000) {
                        t_audio = target_audio_t;
                    }
                }

            } catch(e) {} 
        };
        
        setInterval(() => { if (ws.readyState === WebSocket.OPEN) ws.send('P'); }, 33);

        document.getElementById('overlay').addEventListener('click', async function() {
            if (!wasm) return;
            
            this.style.opacity = '0';
            setTimeout(() => this.style.display = 'none', 300);
            
            if(!c) {
                let current_rate = (state && state.sr) ? state.sr : 8000;
                c = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: current_rate });
                
                n = c.createScriptProcessor(1024, 1, 1);
                n.onaudioprocess = function(e) {
                    let out = e.outputBuffer.getChannelData(0);
                    for (let i=0; i<out.length; i++) {
                        out[i] = (state && state.p) ? ((tickWASM()) - 128) / 128.0 : 0;
                    }
                };
                n.connect(c.destination);
                
                if (state) {
                    t_audio = state.t + LATENCY_SAMPLES;
                }
            }
            if(c.state === 'suspended') c.resume();
        });

        function renderUI() {
            requestAnimationFrame(renderUI);
            if (!state) return;

            let w = 240, h = 135;
            let scale_pow = Math.pow(2, state.s);
            
            let input_h = state.f.length > 80 ? 60 : 25;
            if (is_hijacked) {
                let ed = document.getElementById('editor');
                input_h = ed.value.length > 80 ? 60 : 25;
                ed.style.height = (input_h / 135 * 100) + '%';
            }
            let vis_y = 21, input_y = 135 - input_h, vis_h = input_y - vis_y;

            if (state.p && state.v !== 4) {
                let drawn = false;
                let processed = 0;
                
                while (vis_tail !== vis_head && processed < 4000) {
                    let val = vis_ring_val[vis_tail];
                    let t_v = vis_ring_t[vis_tail];
                    vis_tail = (vis_tail + 1) % VIS_RING_SIZE;
                    processed++;

                    let x = Math.floor(t_v / scale_pow) % w;
                    
                    if (t_v % scale_pow === 0) {
                        let clrX = (x + 1) % w;
                        for(let y = vis_y; y < input_y; y++) bgPixels[y * w + clrX] = 0xFF000000; 
                    }

                    let curY = Math.floor((val * vis_h) / 255);
                    let prevY = Math.floor((last_val * vis_h) / 255);

                    if (state.v === 1) { 
                        let y_center = input_y - 1 - Math.floor(vis_h / 2);
                        let y_c = input_y - 1 - curY;
                        let minY = Math.min(y_c, y_center);
                        let maxY = Math.max(y_c, y_center);
                        let c32 = colorLUT32[val];
                        for(let y = minY; y <= maxY; y++) bgPixels[y * w + x] = c32;
                    }
                    else if (state.v === 2) { 
                        let dSize = Math.max(1, Math.floor(256 / scale_pow)); 
                        let rS = dSize * (t_v % scale_pow);
                        let c32 = colorLUT32[val];
                        if (rS < vis_h) {
                            for(let y = vis_y + rS; y < vis_y + rS + dSize; y++) {
                                if (y < input_y) bgPixels[y * w + x] = c32;
                            }
                        }
                    }
                    else if (state.v === 0 || state.v === 3) { 
                        let y_c = input_y - 1 - curY;
                        let y_p = input_y - 1 - prevY;
                        let minY = Math.min(y_p, y_c);
                        let maxY = Math.max(y_p, y_c);
                        let c32 = (state.v === 0) ? colorLUT32[255] : colorLUT32[Math.max(48, val)];
                        for(let y = minY; y <= maxY; y++) bgPixels[y * w + x] = c32;
                    }
                    last_val = val;
                    drawn = true;
                }
                
                if (vis_tail !== vis_head && processed >= 4000) {
                    vis_tail = vis_head; 
                }
                
                if (drawn) bgCtx.putImageData(bgImg, 0, 0); 
            }

            mainCtx.fillStyle = '#000000';
            mainCtx.fillRect(0, 0, 240 * SC, 135 * SC);

            if (state.v !== 4) {
                mainCtx.imageSmoothingEnabled = false; 
                mainCtx.drawImage(bgSprite, 0, 0, 240 * SC, 135 * SC); 
            } else {
                mainCtx.fillStyle = window.dimColor;
                mainCtx.font = FONT_SMALL;
                mainCtx.fillText("HISTORY LOG VISIBLE ON DEVICE", 5 * SC, 40 * SC);
                
                mainCtx.fillStyle = window.themeColor;
                mainCtx.textBaseline = 'top';
                for (let i = 0; i < history_log.length; i++) {
                    let line = (i === 0 ? "> " : "  ") + history_log[i];
                    if (line.length > 38) line = line.substring(0, 36) + "..";
                    mainCtx.fillText(line, 5 * SC, (50 + (i * 10)) * SC);
                }
                vis_tail = vis_head; 
            }

            mainCtx.fillStyle = '#000000';
            mainCtx.fillRect(0, 0, 240 * SC, 21 * SC);
            
            mainCtx.fillStyle = window.themeColor;
            mainCtx.font = FONT_LARGE;
            mainCtx.textBaseline = 'top';
            mainCtx.fillText(state.m, 5 * SC, 6 * SC); 
            
            if (state.sm && !is_hijacked) {
                mainCtx.textAlign = 'right';
                mainCtx.fillText(state.sm, 240 * SC - 55 * SC, 6 * SC);
                mainCtx.textAlign = 'left'; 
            }
            
            mainCtx.fillStyle = window.dimColor;
            mainCtx.fillRect(0, 21 * SC, 240 * SC, 1 * SC);
        }
        
        renderUI();
    </script>
</body>
</html>
)rawliteral";