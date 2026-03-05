#pragma once

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
        
        #hijack-btn { position: absolute; top: 2%; right: 2%; z-index: 20; background: #000; border: 1px solid; padding: 4px 10px; font-family: monospace; font-size: min(3vw, 14px); cursor: pointer; outline: none; transition: transform 0.1s; }
        #hijack-btn:active { transform: scale(0.95); }
        
        #editor { position: absolute; bottom: 0; left: 0; width: 100%; z-index: 20; border: none; border-top: 1px solid; resize: none; outline: none; background: #000; font-family: monospace; font-size: min(3.5vw, 18px); padding: 8px 12px; box-sizing: border-box; word-break: break-all; }
        #editor:focus { border-top: 2px solid; } 
    </style>
</head>
<body>
    <div id="screen-container">
        <canvas id="m5"></canvas>
        <div id="overlay">
            <div>BYTE ME!</div>
        </div>
        <button id="hijack-btn" onclick="toggleHijack(event)">HIJACK</button>
        <textarea id="editor" spellcheck="false" autocomplete="off" autocorrect="off" autocapitalize="off" oninput="updateHijack()" readonly></textarea>
    </div>
    
    <script>
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

        let state = null;
        let last_eval_str = "";
        let vis_f = (t) => 128; 
        
        let t_audio = 0; 
        let t_vis = 0;   
        let target_vis = 0;
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
        }
        updateColorLUT(0, 255, 0);

        let LATENCY_SAMPLES = 1024; 
        
        document.getElementById('editor').addEventListener('keydown', function(e) {
            if (e.key === 'Enter') {
                e.preventDefault();
                this.blur();
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
                ed.focus();
            } else {
                btn.innerText = "HIJACK";
                ed.readOnly = true;
                ed.blur();
                if (state) {
                    ed.value = state.f;
                    last_eval_str = state.ef;
                    try { vis_f = new Function('t', 'with(Math) return (' + state.ef + ')&255;'); } catch(err) {}
                }
            }
        };

        window.updateHijack = function() {
            if (!is_hijacked) return;
            let ed = document.getElementById('editor');
            ed.value = ed.value.replace(/[\r\n]/g, ''); 
            try {
                vis_f = new Function('t', 'with(Math) return (' + ed.value + ')&255;');
            } catch(err) {} 
        };

        const ws = new WebSocket('ws://192.168.4.1/ws');
        
        ws.onmessage = function(event) {
            try {
                let newState = JSON.parse(event.data);
                
                if (state && (state.v !== newState.v || state.s !== newState.s || state.r !== newState.r)) {
                    bgPixels.fill(0xFF000000); 
                    bgCtx.putImageData(bgImg, 0, 0);
                }
                
                if (!state || state.r !== newState.r || state.g !== newState.g || state.b !== newState.b) {
                    updateColorLUT(newState.r, newState.g, newState.b);
                    document.getElementById('screen-container').style.boxShadow = '0 0 30px ' + window.shadowColor;
                    document.getElementById('overlay').style.borderColor = window.themeColor;
                }

                if (!is_hijacked) {
                    let ed = document.getElementById('editor');
                    if (ed.value !== newState.f) ed.value = newState.f;
                    
                    let input_h = newState.f.length > 80 ? 60 : 25;
                    ed.style.height = (input_h / 135 * 100) + '%';
                    
                    if (newState.ef !== last_eval_str) {
                        last_eval_str = newState.ef;
                        try { vis_f = new Function('t', 'with(Math) return (' + newState.ef + ')&255;'); } catch(e) {}
                    }
                }

                if (c) {
                    let os_latency = (c.baseLatency || 0.03) + (c.outputLatency || 0.05);
                    let network_flight_time = 0.05; 
                    let buffer_latency = 1024 / 8000;
                    LATENCY_SAMPLES = Math.floor((os_latency + network_flight_time + buffer_latency) * 8000);
                }

                let target_audio_t = newState.t + LATENCY_SAMPLES;
                if (Math.abs(t_audio - target_audio_t) > 2000) {
                    t_audio = target_audio_t;
                }
                
                target_vis = newState.t; 
                state = newState;
            } catch(e) {} 
        };
        
        setInterval(() => { if (ws.readyState === WebSocket.OPEN) ws.send('P'); }, 33);

        document.getElementById('overlay').addEventListener('click', async function() {
            this.style.opacity = '0';
            setTimeout(() => this.style.display = 'none', 300);
            
            if(!c) {
                c = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 8000 });
                
                n = c.createScriptProcessor(1024, 1, 1);
                n.onaudioprocess = function(e) {
                    let out = e.outputBuffer.getChannelData(0);
                    for (let i=0; i<out.length; i++) {
                        out[i] = (state && state.p) ? ((vis_f(t_audio) & 255) - 128) / 128.0 : 0;
                        t_audio++; 
                    }
                };
                n.connect(c.destination);
                
                if (state) {
                    t_audio = state.t + LATENCY_SAMPLES;
                    t_vis = state.t;
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

            if (state.p && state.v !== 4 && target_vis > t_vis) {
                if (target_vis - t_vis > 8000) {
                    t_vis = target_vis - (240 * scale_pow); 
                    bgPixels.fill(0xFF000000); 
                }

                let chunk_end = target_vis;
                if (target_vis - t_vis > 800) {
                    chunk_end = t_vis + 800;
                }
                
                let drawn = false;
                while (t_vis < chunk_end) {
                    let val = vis_f(t_vis); 
                    let x = Math.floor(t_vis / scale_pow) % w;
                    
                    if (t_vis % scale_pow === 0) {
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
                        let rS = dSize * (t_vis % scale_pow);
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
                    t_vis++;
                    drawn = true;
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