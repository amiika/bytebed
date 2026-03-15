let wasm, inputPtr, audioCtx, t_audio = 0, t_viz = 0, t_frac = 0;
let is_playing = false, is_rpn = false, is_auto = true;
let x_scale = 8; 

// --- NEW: Global State for UI ---
let targetSampleRate = 8000;
let isFloatbeat = false;
const rates = [8000, 11025, 22050, 32000, 44100, 48000];
let currentRateIdx = 0;

const canvas = document.getElementById('viz');
const ctx = canvas.getContext('2d');
const formulaInput = document.getElementById('formula');
const modeBtn = document.getElementById('mode-btn');
const autoBtn = document.getElementById('btn-auto');
const playBtn = document.getElementById('play-btn');
const shareBtn = document.getElementById('share-btn');

// --- NEW: UI Handlers ---
window.cycleRate = function(e) {
    if(e) e.stopPropagation();
    currentRateIdx = (currentRateIdx + 1) % rates.length;
    targetSampleRate = rates[currentRateIdx];
    e.target.innerText = targetSampleRate + "HZ";
    if (wasm && wasm.wasm_set_sample_rate) {
        wasm.wasm_set_sample_rate(targetSampleRate);
    }
};

window.toggleFormat = function(e) {
    if(e) e.stopPropagation();
    isFloatbeat = !isFloatbeat;
    e.target.innerText = isFloatbeat ? "FLOATBEAT" : "BYTEBEAT";
    if (isFloatbeat) e.target.classList.remove('active');
    else e.target.classList.add('active');
    if (wasm && wasm.wasm_set_play_mode) {
        wasm.wasm_set_play_mode(isFloatbeat ? 1 : 0);
    }
};

function autoExpand() {
    formulaInput.style.height = 'auto';
    formulaInput.style.height = formulaInput.scrollHeight + 'px';
}

function checkURLParams() {
    const urlParams = new URLSearchParams(window.location.search);
    const code = urlParams.get('code');
    const mode = urlParams.get('m');
    if (code) {
        try {
            let base64 = decodeURIComponent(code).replace(/-/g, '+').replace(/_/g, '/');
            while (base64.length % 4) base64 += '=';
            const decoded = decodeURIComponent(atob(base64).split('').map(c => '%' + ('00' + c.charCodeAt(0).toString(16)).slice(-2)).join(''));
            formulaInput.value = decodeURIComponent(decoded);
            is_rpn = (mode === 'r');
            modeBtn.innerText = is_rpn ? "RPN" : "INFIX";
            autoExpand();
            return true;
        } catch(e) { console.error("URL Restore Failed", e); }
    }
    return false;
}

checkURLParams();

function togglePlay(e) {
    if(e) e.stopPropagation();
    is_playing = !is_playing;
    playBtn.innerText = is_playing ? "STOP" : "START";
    if (is_playing) playBtn.classList.add('active'); else playBtn.classList.remove('active');
}

function shareCode(e) {
    if(e) e.stopPropagation();
    const utf8Bytes = encodeURIComponent(formulaInput.value).replace(/%([0-9A-F]{2})/g, (m, p1) => String.fromCharCode('0x' + p1));
    const safeBase64 = btoa(utf8Bytes).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
    const mode = is_rpn ? 'r' : 'i';
    const url = `${window.location.origin}${window.location.pathname}?m=${mode}&code=${encodeURIComponent(safeBase64)}`;
    navigator.clipboard.writeText(url).then(() => {
        const oldText = shareBtn.innerText;
        shareBtn.innerText = "COPIED!";
        setTimeout(() => { shareBtn.innerText = oldText; }, 1500);
    });
}

function getDecompiledText(toRPN) {
    if (!wasm || !wasm.wasm_decompile) return "";
    const ptr = wasm.wasm_decompile(toRPN);
    const view = new Uint8Array(wasm.memory.buffer);
    let str = ""; let i = ptr;
    while (view[i] !== 0) { str += String.fromCharCode(view[i]); i++; }
    return str;
}

function toggleMode(e) {
    if(e) e.stopPropagation();
    if (!wasm) return;
    if (compile(formulaInput.value)) {
        is_rpn = !is_rpn;
        modeBtn.innerText = is_rpn ? "RPN" : "INFIX";
        const translated = getDecompiledText(is_rpn);
        if (translated) { formulaInput.value = translated; autoExpand(); }
        compile(formulaInput.value);
    }
}

async function boot() {
    if (audioCtx) return; 
    const AudioContext = window.AudioContext || window.webkitAudioContext;
    
    // Let the browser pick the hardware sample rate (e.g., 44100 or 48000)
    audioCtx = new AudioContext(); 
    await audioCtx.resume();
    
    try {
        const response = await fetch('../wasm/bytebed.wasm');
        const bytes = await response.arrayBuffer();
        const results = await WebAssembly.instantiate(bytes, { env: { memory: new WebAssembly.Memory({ initial: 256 }) } });
        wasm = results.instance.exports;
        if (wasm._initialize) wasm._initialize(); 
        inputPtr = wasm.get_input_buffer();
        
        // Sync initial UI state to Wasm
        if (wasm.wasm_set_sample_rate) wasm.wasm_set_sample_rate(targetSampleRate);
        if (wasm.wasm_set_play_mode) wasm.wasm_set_play_mode(isFloatbeat ? 1 : 0);

        const node = audioCtx.createScriptProcessor(1024, 1, 1);
        node.onaudioprocess = (e) => {
            const out = e.outputBuffer.getChannelData(0);
            if (!wasm || !is_playing) { out.fill(0); return; }
            
            // Calculate fractional step based on UI Target vs Hardware Target
            const timeStep = targetSampleRate / audioCtx.sampleRate;
            
            for (let i = 0; i < out.length; i++) {
                let sample = wasm.wasm_execute(t_audio);
                out[i] = (sample - 128) / 128.0;
                
                // Advance fractional time
                t_frac += timeStep;
                if (t_frac >= 1.0) {
                    let steps = Math.floor(t_frac);
                    t_audio += steps;
                    t_frac -= steps;
                }
            }
        };
        node.connect(audioCtx.destination);
        canvas.width = canvas.clientWidth;
        canvas.height = canvas.clientHeight;
        checkURLParams();
        compile(formulaInput.value);
        autoExpand();
        is_playing = true;
        playBtn.innerText = "STOP";
        playBtn.classList.add('active');
        requestAnimationFrame(renderLoop);
        window.history.replaceState({}, document.title, window.location.pathname);
    } catch (err) { 
        document.getElementById('status').innerText = `ERR: ${err.message}`; 
    }
}

function compile(str) {
    if (!wasm) return false;
    const encoder = new TextEncoder();
    const view = new Uint8Array(wasm.memory.buffer);
    const encoded = encoder.encode(str);
    view.set(encoded, inputPtr);
    view[inputPtr + encoded.length] = 0; 
    const success = wasm.wasm_compile(is_rpn);
    return success;
}

function toggleAuto(e) { if(e) e.stopPropagation(); is_auto = !is_auto; autoBtn.classList.toggle('active', is_auto); }

// Clears the fractional time when resetting to keep sync tight
function resetT(e) { if(e) e.stopPropagation(); t_audio = 0; t_frac = 0; t_viz = 0; }

function renderLoop() {
    requestAnimationFrame(renderLoop);
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    if (!wasm || !is_playing) return;
    ctx.beginPath();
    ctx.strokeStyle = '#00ff00'; ctx.lineWidth = 1.5; ctx.shadowBlur = 10; ctx.shadowColor = '#00ff00';
    for (let x = 0; x < canvas.width; x += 4) {
        const val = wasm.wasm_execute(Math.floor(t_viz + (x * x_scale))); 
        const y = canvas.height - (val * canvas.height / 255);
        if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.stroke();
    // Scale visualizer speed based on sample rate to match audio feel
    t_viz += 20 * (targetSampleRate / 8000);
}

window.addEventListener('mousedown', boot, { once: true });
window.addEventListener('keydown', boot, { once: true });

window.addEventListener('keydown', (e) => {
    if (e.key === "Tab") { e.preventDefault(); toggleMode(); return; }
    if (e.key === "Enter" && document.activeElement === formulaInput && !e.shiftKey) {
        e.preventDefault(); if (wasm) compile(formulaInput.value); 
        if (!is_playing) togglePlay();
    }
    if (e.key === "Escape") togglePlay();
    if (e.ctrlKey || e.metaKey) {
        if (e.key === "=" || e.key === "+") { e.preventDefault(); x_scale = Math.max(0.2, x_scale / 1.5); }
        if (e.key === "-") { e.preventDefault(); x_scale = Math.min(128, x_scale * 1.5); }
    }
});

formulaInput.addEventListener('input', (e) => { autoExpand(); if (wasm && is_auto) compile(e.target.value); });