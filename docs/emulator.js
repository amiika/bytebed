let wasm, inputPtr, audioCtx;
let workletNode = null;
let is_playing = false, is_rpn = false, is_auto = true;

let t_audio = 0; 
let t_viz = 0;   
let t_frac = 0;
let mv = 0.0;
let lastX = 0, lastY = 0;
let has_moved = false;
let targetSampleRate = 8000;
let isFloatbeat = false;
const rates = [8000, 11025, 22050, 32000, 44100, 48000];
let currentRateIdx = 0;

let current_midi_note = 0;
let current_midi_freq = 440.0;

let midiInputs = [];
// Load persisted states immediately
let currentMidiPortIdx = parseInt(localStorage.getItem('bytebed_midi_port')) || -2; 
let currentMidiChannel = parseInt(localStorage.getItem('bytebed_midi_chan')) || -1; 

let currentBank = 1; 
let currentPatch = 0;

const canvasElem = document.getElementById('viz');
const ctx = canvasElem.getContext('2d');
const formulaInput = document.getElementById('formula');
const modeBtn = document.getElementById('mode-btn');
const autoBtn = document.getElementById('btn-auto');
const playBtn = document.getElementById('play-btn');
const shareBtn = document.getElementById('share-btn');
const visBtn = document.getElementById('vis-btn');
const midiBtn = document.getElementById('midi-btn');
const midiChanBtn = document.getElementById('midi-chan-btn');
const bankBtn = document.getElementById('bank-btn');
const patchBtn = document.getElementById('patch-btn');

/**
 * Computes a proper mathematical modulo supporting negative numbers.
 * @param a The dividend
 * @param b The divisor
 * @return The positive remainder
 */
function mod(a, b) {
    return ((a % b) + b) % b;
}

/**
 * Encodes a string into a URL-safe Base64 format.
 * @param str The raw UTF-8 string input
 * @return The URL-safe Base64 encoded string
 */
function base64UrlEncode(str) {
    const utf8Bytes = new TextEncoder().encode(str);
    let binaryStr = "";
    for (let i = 0; i < utf8Bytes.length; i++) {
        binaryStr += String.fromCharCode(utf8Bytes[i]);
    }
    const base64 = btoa(binaryStr);
    return base64.replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

/**
 * Decodes a URL-safe Base64 string back into standard text.
 * @param base64Url The URL-safe Base64 encoded string
 * @return The decoded UTF-8 string
 */
function base64UrlDecode(base64Url) {
    let base64 = base64Url.replace(/-/g, '+').replace(/_/g, '/');
    while (base64.length % 4) {
        base64 += '=';
    }
    const binaryStr = atob(base64);
    const utf8Bytes = new Uint8Array(binaryStr.length);
    for (let i = 0; i < binaryStr.length; i++) {
        utf8Bytes[i] = binaryStr.charCodeAt(i);
    }
    return new TextDecoder().decode(utf8Bytes);
}

/**
 * Handles Web Audio API visualization buffering and canvas rendering.
 */
class Scope {
    constructor() {
        this.analyser = [null, null];
        this.analyserData = [null, null];
        this.canvasCtx = null;
        this.canvasElem = null;
        this.canvasHeight = 256; 
        this.canvasTimeCursor = null;
        this.canvasWidth = 1024;
        this.colorChannels = [1, 0, 2];
        this.colorDiagram = [0, 100, 0];
        this.colorStereoRGB = [null, null];
        this.colorWaveform = [0, 255, 0];
        this.drawBuffer = [];
        this.drawEndBuffer = [];
        this.drawMode = 'Waveform';
        this.drawScale = 0;
        this.fftSize = 10;
        this.maxDecibels = -10;
        this.minDecibels = -120;
    }
    
    get timeCursorEnabled() {
        return (targetSampleRate >> this.drawScale) < 2000;
    }
    
    clearCanvas() {
        this.canvasCtx.clearRect(0, 0, this.canvasWidth, this.canvasHeight);
        this.canvasCtx.globalCompositeOperation = this.drawMode === 'FFT' ? 'lighter' : 'source-over';
    }
    
    drawGraphics(endTime) {
        if(!isFinite(endTime)) {
            resetT();
            return;
        }
        const buffer = this.drawBuffer;
        const bufferLen = buffer.length;
        if(!bufferLen) {
            return;
        }
        const ctx = this.canvasCtx;
        const width = this.canvasWidth;
        const height = this.canvasHeight;
        
        if(this.drawMode === 'FFT') {
            this.clearCanvas();
            const minFreq = Math.max(48000 / 2 ** this.fftSize, 10);
            const maxFreq = 24000; 
            
            let ch = 1;
            while(ch--) {
                ctx.beginPath();
                ctx.strokeStyle = `rgb(${ this.colorWaveform.join(',') })`;
                if(this.analyser[ch]) {
                    this.analyser[ch].getByteFrequencyData(this.analyserData[ch]);
                    for(let i = 0, len = this.analyserData[ch].length; i < len; ++i) {
                        const y = height * (1 - this.analyserData[ch][i] / 256);
                        if(i) {
                            const ratio = maxFreq / minFreq;
                            ctx.lineTo(width * Math.log(i / len * ratio) / Math.log(ratio), y);
                            continue;
                        }
                        ctx.moveTo(0, y);
                    }
                    ctx.stroke();
                }
            }
            this.drawBuffer = this.drawBuffer.slice(-200);
            return;
        }
        
        const scale = this.drawScale;
        let startTime = buffer[0].t;
        let startX = mod(this.getX(startTime), width);
        let endX = Math.floor(startX + this.getX(endTime - startTime));
        startX = Math.floor(startX);
        let drawWidth = Math.abs(endX - startX) + 1;
        
        if(drawWidth > width) {
            startTime = (this.getX(endTime) - width) * (1 << scale);
            startX = mod(this.getX(startTime), width);
            endX = Math.floor(startX + this.getX(endTime - startTime));
            startX = Math.floor(startX);
            drawWidth = Math.abs(endX - startX) + 1;
        }
        startX = Math.min(startX, endX);
        
        const imageData = ctx.createImageData(drawWidth, height);
        const { data } = imageData;
        const status = [];
        if(scale) {
            const x = 0;
            for(let y = 0; y < height; ++y) {
                const drawEndBuffer = this.drawEndBuffer[y];
                if(drawEndBuffer) {
                    let idx = drawWidth * (255 - y) + x;
                    status[idx] = drawEndBuffer[3];
                    idx <<= 2;
                    data[idx++] = drawEndBuffer[0];
                    data[idx++] = drawEndBuffer[1];
                    data[idx] = drawEndBuffer[2];
                }
            }
        }
        for(let x = 0; x < drawWidth; ++x) {
            for(let y = 0; y < height; ++y) {
                data[((drawWidth * y + x) << 2) + 3] = 255;
            }
        }
        
        const isCombined = this.drawMode === 'Combined';
        const isDiagram = this.drawMode === 'Diagram';
        const isWaveform = this.drawMode === 'Waveform';
        const { colorDiagram } = this;
        const colorCh = this.colorChannels;
        const colorPoints = this.colorWaveform;
        const colorWaveformArr = [.65 * colorPoints[0] | 0, .65 * colorPoints[1] | 0, .65 * colorPoints[2] | 0];
        let ch, drawPoint;
        
        for(let i = 0; i < bufferLen; ++i) {
            const curY = buffer[i].value;
            const prevY = buffer[i - 1]?.value ?? [NaN, NaN];
            const isNaNCurY = [isNaN(curY[0]), isNaN(curY[1])];
            const curTime = buffer[i].t;
            const nextTime = buffer[i + 1]?.t ?? endTime;
            const curX = mod(Math.floor(this.getX(curTime)) - startX, width);
            const nextX = mod(Math.ceil(this.getX(nextTime)) - startX, width);
            let diagramSize, diagramStart;
            
            if(isCombined || isDiagram) {
                diagramSize = Math.max(1, 256 >> scale);
                diagramStart = diagramSize * mod(curTime, 1 << scale);
            } else if(isNaNCurY[0] || isNaNCurY[1]) {
                for(let x = curX; x !== nextX; x = mod(x + 1, width)) {
                    for(let y = 0; y < height; ++y) {
                        const idx = (drawWidth * y + x) << 2;
                        if(!data[idx + 1] && !data[idx + 2]) data[idx] = 100;
                    }
                }
            }
            
            if((curY[0] === curY[1] || isNaNCurY[0] && isNaNCurY[1]) && prevY[0] === prevY[1]) {
                ch = 1; drawPoint = this.drawPointMono;
            } else {
                ch = 2; drawPoint = this.drawPointStereo;
            }
            
            while(ch--) {
                const curYCh = curY[ch];
                if(!isNaNCurY[ch] && !isDiagram) {
                    for(let x = curX; x !== nextX; x = mod(x + 1, width)) {
                        const idx = drawWidth * (255 - curYCh) + x;
                        status[idx] = 1;
                        drawPoint(data, idx << 2, colorPoints, colorCh, ch);
                    }
                    if(isCombined || isWaveform) {
                        const prevYCh = prevY[ch];
                        if(isNaN(prevYCh)) continue;
                        const x = curX;
                        for(let dy = prevYCh < curYCh ? 1 : -1, y = prevYCh; y !== curYCh; y += dy) {
                            const idx = drawWidth * (255 - y) + x;
                            if(status[idx] != 1) { 
                                status[idx] = 2; 
                                drawPoint(data, idx << 2, colorWaveformArr, colorCh, ch);
                            }
                        }
                    }
                }
                if(isCombined || isDiagram) {
                    const isNaNCurYCh = isNaNCurY[ch];
                    const value = (curYCh & 255) / 256;
                    const color = [value * colorDiagram[0] | 0, value * colorDiagram[1] | 0, value * colorDiagram[2] | 0];
                    for(let x = curX; x !== nextX; x = mod(x + 1, width)) {
                        for(let y = 0; y < diagramSize; ++y) {
                            const idx = drawWidth * (diagramStart + y) + x;
                            const s = status[idx];
                            if(s != 1 && s != 2) { 
                                if(isNaNCurYCh) {
                                    data[idx << 2] = 100;
                                } else {
                                    drawPoint(data, idx << 2, color, colorCh, ch);
                                }
                            }
                        }
                    }
                }
            }
        }
        
        if(scale) {
            const x = drawWidth - 1;
            for(let y = 0; y < height; ++y) {
                let idx = drawWidth * (255 - y) + x;
                const s = status[idx];
                idx <<= 2;
                this.drawEndBuffer[y] = [data[idx++], data[idx++], data[idx], s];
            }
        }
        ctx.putImageData(imageData, startX, 0);
        if(endX >= width) {
            ctx.putImageData(imageData, startX - width, 0);
        } else if(endX <= 0) {
            ctx.putImageData(imageData, startX + width, 0);
        }
        this.drawBuffer = [{ t: endTime, value: buffer[bufferLen - 1].value }];
    }
    
    drawPointMono(data, i, color) {
        data[i++] = color[0];
        data[i++] = color[1];
        data[i] = color[2];
    }
    
    drawPointStereo(data, i, color, colorCh, isRight) {
        if(isRight) {
            data[i + colorCh[1]] = color[colorCh[1]];
            data[i + colorCh[2]] = color[colorCh[2]];
        } else {
            data[i + colorCh[0]] = color[colorCh[0]];
        }
    }
    
    getX(t) {
        return t / (1 << this.drawScale);
    }
    
    setFFTAnalyzer() {
        if(this.analyser[0]) {
            this.analyser[0].fftSize = 2 ** this.fftSize;
            this.analyserData[0] = new Uint8Array(this.analyser[0].frequencyBinCount);
        }
        if(this.analyser[1]) {
            this.analyser[1].fftSize = 2 ** this.fftSize;
            this.analyserData[1] = new Uint8Array(this.analyser[1].frequencyBinCount);
        }
    }
}

const scope = new Scope();

const workletCode = `
class BytebeatWorklet extends AudioWorkletProcessor {
    constructor() {
        super();
        this.wasm = null;
        this.t_audio = 0;
        this.t_frac = 0;
        this.targetSampleRate = 8000;
        this.is_playing = false;

        this.port.onmessage = async (e) => {
            const msg = e.data;
            if (msg.type === 'init') {
                const mod = await WebAssembly.instantiate(msg.wasmBytes, { env: { memory: new WebAssembly.Memory({ initial: 256 }) } });
                this.wasm = mod.instance.exports;
                if (this.wasm._initialize) this.wasm._initialize();
                this.targetSampleRate = msg.sampleRate;
                
                this.port.postMessage({ type: 'ready' });
                
            } else if (msg.type === 'compile') {
                if (!this.wasm) return;
                const encoded = msg.encoded; 
                const view = new Uint8Array(this.wasm.memory.buffer);
                const ptr = this.wasm.get_input_buffer();
                view.set(encoded, ptr);
                view[ptr + encoded.length] = 0;
                this.wasm.wasm_compile(msg.is_rpn);
            } else if (msg.type === 'play') {
                this.is_playing = msg.playing;
            } else if (msg.type === 'rate') {
                this.targetSampleRate = msg.rate;
                if (this.wasm && this.wasm.wasm_set_sample_rate) this.wasm.wasm_set_sample_rate(msg.rate);
            } else if (msg.type === 'mode') {
                if (this.wasm && this.wasm.wasm_set_play_mode) this.wasm.wasm_set_play_mode(msg.mode);
                if (this.wasm && this.wasm.wasm_reset_vm) this.wasm.wasm_reset_vm();
            } else if (msg.type === 'reset') {
                this.t_audio = 0;
                this.t_frac = 0;
                if (this.wasm && this.wasm.wasm_reset_vm) this.wasm.wasm_reset_vm();
                this.port.postMessage({ type: 'sync', t_audio: this.t_audio });
            } else if (msg.type === 'mouse') {
                if (this.wasm && this.wasm.wasm_set_mouse) this.wasm.wasm_set_mouse(msg.mx, msg.my, msg.mv);
            } else if (msg.type === 'imu') {
                if (this.wasm && this.wasm.wasm_set_imu) this.wasm.wasm_set_imu(msg.ax, msg.ay, msg.az, msg.gx, msg.gy, msg.gz);
            } else if (msg.type === 'midi') {
                if (this.wasm && this.wasm.wasm_set_midi) this.wasm.wasm_set_midi(msg.freq, msg.gate, msg.note);
            }
        };
    }

    process(inputs, outputs, parameters) {
        const out = outputs[0][0];
        if (!this.wasm || !this.is_playing) {
            out.fill(0);
            return true;
        }

        const timeStep = this.targetSampleRate / sampleRate;
        for (let i = 0; i < out.length; i++) {
            let sample = this.wasm.wasm_execute(this.t_audio);
            out[i] = (sample - 128) / 128.0; 
            
            this.t_frac += timeStep;
            if (this.t_frac >= 1.0) {
                let steps = Math.floor(this.t_frac);
                this.t_audio += steps;
                this.t_frac -= steps;
            }
        }
        
        this.port.postMessage({ type: 'sync', t_audio: this.t_audio });
        return true;
    }
}
registerProcessor('bytebeat-worklet', BytebeatWorklet);
`;

/**
 * Updates the MIDI UI button label and title text based on current inputs and selection.
 */
function updateMidiButton() {
    if (!midiBtn) return;
    if (midiInputs.length === 0) {
        midiBtn.innerText = "M: NONE";
        midiBtn.title = "No MIDI devices found";
        midiBtn.classList.remove('active');
        if (midiChanBtn) midiChanBtn.style.display = "none";
    } else if (currentMidiPortIdx === -2) {
        midiBtn.innerText = "M: OFF";
        midiBtn.title = "MIDI Disabled";
        midiBtn.classList.remove('active');
        if (midiChanBtn) midiChanBtn.style.display = "none";
    } else if (currentMidiPortIdx === -1) {
        midiBtn.innerText = "M: ALL";
        midiBtn.title = "Listening to all MIDI inputs";
        midiBtn.classList.add('active');
        if (midiChanBtn) midiChanBtn.style.display = "inline-flex";
    } else {
        midiBtn.innerText = "M: " + currentMidiPortIdx;
        let name = midiInputs[currentMidiPortIdx].name || "Unknown Device";
        midiBtn.title = name;
        midiBtn.classList.add('active');
        if (midiChanBtn) midiChanBtn.style.display = "inline-flex";
    }
}

/**
 * Updates the MIDI Channel button display.
 */
function updateChannelButton() {
    if (!midiChanBtn) return;
    midiChanBtn.innerText = currentMidiChannel === -1 ? "C: ALL" : "C: " + (currentMidiChannel + 1);
    if (currentMidiChannel === -1) {
        midiChanBtn.classList.add('active');
    } else {
        midiChanBtn.classList.remove('active');
    }
}

/**
 * Cycles through available MIDI input ports on the button click and persists the state.
 * @param e Native UI event block
 */
window.cycleMIDI = function(e) {
    if(e) e.stopPropagation();
    if (midiInputs.length === 0) return;
    
    currentMidiPortIdx++;
    if (currentMidiPortIdx >= midiInputs.length) {
        currentMidiPortIdx = -2; 
    }
    
    localStorage.setItem('bytebed_midi_port', currentMidiPortIdx.toString());
    updateMidiButton();
};

/**
 * Cycles through available MIDI channels (ALL or 1-16) on the button click and persists the state.
 * @param e Native UI event block
 */
window.cycleMIDIChannel = function(e) {
    if(e) e.stopPropagation();
    currentMidiChannel++;
    if (currentMidiChannel > 15) {
        currentMidiChannel = -1; 
    }
    
    localStorage.setItem('bytebed_midi_chan', currentMidiChannel.toString());
    updateChannelButton();
};

/**
 * Queries WASM memory to seamlessly load the C++ hardware default patches.
 */
async function loadPreset() {
    if (!wasm || !wasm.wasm_get_preset_formula) return;
    
    const ptr = wasm.wasm_get_preset_formula(currentBank, currentPatch);
    const view = new Uint8Array(wasm.memory.buffer);
    let str = ""; 
    let i = ptr;
    while (view[i] !== 0) { 
        str += String.fromCharCode(view[i]); 
        i++; 
    }
    
    if (str === "") return;

    if (is_playing) {
        if (!audioBooted) await bootAudio();
        if (audioCtx && audioCtx.state === 'suspended') await audioCtx.resume();
        if (workletNode) {
            workletNode.port.postMessage({ type: 'play', playing: false });
        }
    }
    
    const rate = wasm.wasm_get_preset_rate(currentBank, currentPatch);
    const mode = wasm.wasm_get_preset_mode(currentBank, currentPatch);

    formulaInput.value = str;
    isFloatbeat = (mode === 1);
    
    const formatBtn = document.getElementById('format-btn');
    if (formatBtn) {
        formatBtn.innerText = isFloatbeat ? "FLOATBEAT" : "BYTEBEAT";
    }

    targetSampleRate = rate;
    const foundIdx = rates.indexOf(targetSampleRate);
    if (foundIdx !== -1) currentRateIdx = foundIdx;
    const rateBtn = document.getElementById('rate-btn');
    if (rateBtn) rateBtn.innerText = targetSampleRate + "HZ";

    if (wasm) {
        if (wasm.wasm_set_play_mode) wasm.wasm_set_play_mode(mode);
        if (wasm.wasm_set_sample_rate) wasm.wasm_set_sample_rate(rate);
        if (wasm.wasm_reset_vm) wasm.wasm_reset_vm();
    }
    
    if (workletNode) {
        workletNode.port.postMessage({ type: 'rate', rate: targetSampleRate });
        workletNode.port.postMessage({ type: 'mode', mode: mode });
        workletNode.port.postMessage({ type: 'reset' });
    }

    t_audio = 0; 
    t_viz = 0;
    scope.clearCanvas();
    scope.drawBuffer = [];
    
    compile(formulaInput.value);

    autoExpand();

    if (is_playing) {
        if (playBtn) {
            playBtn.innerText = "⏸\uFE0E";
            playBtn.classList.add('active');
        }
        if (workletNode) {
            workletNode.port.postMessage({ type: 'play', playing: true });
        }
    }
}

/**
 * Steps the active bank forward through 0-9 index bounds.
 * @param e Native UI event block
 */
window.cycleBank = function(e) {
    if (e) e.stopPropagation();
    currentBank = (currentBank + 1) % 10;
    if (bankBtn) bankBtn.innerText = "B: " + currentBank;
    loadPreset();
};

/**
 * Steps the active patch slot forward through 0-9 index bounds.
 * @param e Native UI event block
 */
window.cyclePatch = function(e) {
    if (e) e.stopPropagation();
    currentPatch = (currentPatch + 1) % 10;
    if (patchBtn) patchBtn.innerText = "P: " + currentPatch;
    loadPreset();
};

/**
 * Cycles through the available playback sample rates.
 * @param e Native UI event block
 */
window.cycleRate = function(e) {
    if(e) e.stopPropagation();
    currentRateIdx = (currentRateIdx + 1) % rates.length;
    targetSampleRate = rates[currentRateIdx];
    e.target.innerText = targetSampleRate + "HZ";
    
    if (wasm && wasm.wasm_set_sample_rate) wasm.wasm_set_sample_rate(targetSampleRate);
    if (workletNode) workletNode.port.postMessage({ type: 'rate', rate: targetSampleRate });
};

/**
 * Toggles the compilation format between classic Bytebeat and Floatbeat.
 * @param e Native UI event block
 */
window.toggleFormat = function(e) {
    if(e) e.stopPropagation();
    isFloatbeat = !isFloatbeat;
    e.target.innerText = isFloatbeat ? "FLOATBEAT" : "BYTEBEAT";
    
    if (wasm) {
        if (wasm.wasm_set_play_mode) wasm.wasm_set_play_mode(isFloatbeat ? 1 : 0);
        if (wasm.wasm_reset_vm) wasm.wasm_reset_vm(); 
        
        if (workletNode) {
            workletNode.port.postMessage({ type: 'mode', mode: isFloatbeat ? 1 : 0 });
        }
        compile(formulaInput.value);
    }
};

const visModes = ['Waveform', 'Diagram', 'Combined', 'FFT'];
let currentVisIdx = 0;

/**
 * Cycles through available scope visualization modes.
 * @param e Native UI event block
 */
window.toggleVis = function(e) {
    if (e) e.stopPropagation();
    setVisMode((currentVisIdx + 1) % visModes.length);
};

/**
 * Updates the active visualization rendering mode.
 * @param idx Index corresponding to the targeted mode in visModes array
 */
function setVisMode(idx) {
    currentVisIdx = idx;
    scope.drawMode = visModes[currentVisIdx];
    if (visBtn) visBtn.innerText = scope.drawMode.toUpperCase();
    scope.clearCanvas();
}

/**
 * Increases the timeline scale for zooming in on the canvas representation.
 * @param e Native UI event block
 */
window.zoomIn = function(e) {
    if(e) e.stopPropagation();
    scope.drawScale = Math.max(0, scope.drawScale - 1);
    scope.clearCanvas();
};

/**
 * Decreases the timeline scale for zooming out on the canvas representation.
 * @param e Native UI event block
 */
window.zoomOut = function(e) {
    if(e) e.stopPropagation();
    scope.drawScale = Math.min(10, scope.drawScale + 1);
    scope.clearCanvas();
};

/**
 * Dynamically updates the textarea sizing relative to its inner content limit.
 */
function autoExpand() {
    formulaInput.style.height = 'auto';
    formulaInput.style.height = formulaInput.scrollHeight + 'px';
}

/**
 * Serializes formula and system configuration state for browser URL sharing.
 * @param e Native UI event block
 */
function shareCode(e) {
    if(e) e.stopPropagation();
    const safeBase64 = base64UrlEncode(formulaInput.value);
    const mode = is_rpn ? 'r' : 'i';
    const fmt = isFloatbeat ? 'f' : 'b';
    const url = `${window.location.origin}${window.location.pathname}?m=${mode}&fmt=${fmt}&sr=${targetSampleRate}&code=${safeBase64}`;
    
    navigator.clipboard.writeText(url).then(() => {
        const oldText = shareBtn.innerText;
        shareBtn.innerText = "✓";
        setTimeout(() => { shareBtn.innerText = oldText; }, 1500);
    });
}

/**
 * Restores shared parameters back from the browser URL address bar state.
 * @return True if successful load from URL parameter, false otherwise.
 */
function checkURLParams() {
    const urlParams = new URLSearchParams(window.location.search);
    const code = urlParams.get('code');
    const mode = urlParams.get('m');
    const fmt = urlParams.get('fmt');
    const sr = urlParams.get('sr');
    
    if (code) {
        try {
            const decoded = base64UrlDecode(code);
            formulaInput.value = decoded;
            
            is_rpn = (mode === 'r');
            modeBtn.innerText = is_rpn ? "RPN" : "INFIX";
            
            if (fmt === 'f') {
                isFloatbeat = true;
                document.getElementById('format-btn').innerText = "FLOATBEAT";
                document.getElementById('format-btn').classList.remove('active');
            } else if (fmt === 'b') {
                isFloatbeat = false;
                document.getElementById('format-btn').innerText = "BYTEBEAT";
                document.getElementById('format-btn').classList.add('active');
            }
            
            if (sr) {
                const parsedRate = parseInt(sr, 10);
                if (!isNaN(parsedRate)) {
                    targetSampleRate = parsedRate;
                    const foundIdx = rates.indexOf(targetSampleRate);
                    if (foundIdx !== -1) currentRateIdx = foundIdx;
                    document.getElementById('rate-btn').innerText = targetSampleRate + "HZ";
                }
            }
            
            if (wasm) {
                if (wasm.wasm_set_play_mode) wasm.wasm_set_play_mode(isFloatbeat ? 1 : 0);
                if (wasm.wasm_set_sample_rate) wasm.wasm_set_sample_rate(targetSampleRate);
                if (wasm.wasm_reset_vm) wasm.wasm_reset_vm();
            }
            
            autoExpand();
            return true;
        } catch(e) { 
            console.error("URL Restore Failed", e); 
        }
    }
    return false;
}

/**
 * Invokes the WASM decompiler.
 * @param toRPN Boolean designating compilation target translation
 * @return Retained decompiled result text block.
 */
function getDecompiledText(toRPN) {
    if (!wasm || !wasm.wasm_decompile) return "";
    const ptr = wasm.wasm_decompile(toRPN);
    const view = new Uint8Array(wasm.memory.buffer);
    let str = ""; let i = ptr;
    while (view[i] !== 0) { str += String.fromCharCode(view[i]); i++; }
    return str;
}

/**
 * Polls error buffer states from internal runtime allocation context tracking.
 * @return Local cached exception output format string.
 */
function getLastError() {
    if (!wasm || !wasm.wasm_get_last_error) return "ERR: Compilation Failed";
    const ptr = wasm.wasm_get_last_error();
    if (ptr === 0) return "";
    
    const view = new Uint8Array(wasm.memory.buffer);
    let str = ""; 
    let i = ptr;
    while (view[i] !== 0) { 
        str += String.fromCharCode(view[i]); 
        i++; 
    }
    return str;
}

/**
 * Handles RPN and INFIX mode toggle switching logic updating compilation target.
 * @param e Native UI event block.
 */
function toggleMode(e) {
    if(e) e.stopPropagation();
    if (!wasm) return;
    if (compile(formulaInput.value)) {
        is_rpn = !is_rpn;
        modeBtn.innerText = is_rpn ? "RPN" : "INFIX";
        const translated = getDecompiledText(is_rpn);
        if (translated) { formulaInput.value = translated; autoExpand(); }
        if (wasm.wasm_reset_vm) wasm.wasm_reset_vm();
        compile(formulaInput.value);
    }
}

/**
 * Marshals native device motion triggers directly into VM parameters variables.
 * @param event The generic DOM sensor dispatch event structure
 */
function handleMotion(event) {
    let local_ax = 0, local_ay = 0, local_az = 0;
    let local_gx = 0, local_gy = 0, local_gz = 0;
    if (event.accelerationIncludingGravity && event.accelerationIncludingGravity.x !== null) {
        local_ax = (event.accelerationIncludingGravity.x || 0) / 9.81;
        local_ay = (event.accelerationIncludingGravity.y || 0) / 9.81;
        local_az = (event.accelerationIncludingGravity.z || 0) / 9.81;
    }
    if (event.rotationRate && event.rotationRate.alpha !== null) {
        local_gx = event.rotationRate.alpha || 0;
        local_gy = event.rotationRate.beta || 0;
        local_gz = event.rotationRate.gamma || 0;
    }
    
    if (wasm && wasm.wasm_set_imu) wasm.wasm_set_imu(local_ax, local_ay, local_az, local_gx, local_gy, local_gz);
    if (workletNode) workletNode.port.postMessage({ type: 'imu', ax: local_ax, ay: local_ay, az: local_az, gx: local_gx, gy: local_gy, gz: local_gz });
}

/**
 * Monitors and processes canvas hover input streams to internal engine mapping.
 * @param clientX X-axis coordinates over the viz box
 * @param clientY Y-axis coordinates over the viz box
 */
function handlePointer(clientX, clientY) {
    const rect = canvasElem.getBoundingClientRect();
    let mx = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
    let my = Math.max(0, Math.min(1, (clientY - rect.top) / rect.height));

    if (has_moved) {
        let dx = clientX - lastX;
        let dy = clientY - lastY;
        let instant_v = Math.sqrt(dx * dx + dy * dy) / 50; 
        mv += instant_v;
        if (mv > 1.0) mv = 1.0;
    }

    lastX = clientX;
    lastY = clientY;
    has_moved = true;

    if (wasm && wasm.wasm_set_mouse) wasm.wasm_set_mouse(mx, my, mv);
    if (workletNode) workletNode.port.postMessage({ type: 'mouse', mx: mx, my: my, mv: mv });
}

const vizWrapper = document.getElementById('viz-wrapper');
vizWrapper.addEventListener('pointermove', (e) => {
    if (!e.isPrimary) return; 
    handlePointer(e.clientX, e.clientY);
});

let wasmBytesGlobal = null;
let audioBooted = false;
let isBooting = false;

let wasmReadyPromise = (async function initWASM() {
    try {
        const response = await fetch('bytebed.wasm');
        wasmBytesGlobal = await response.arrayBuffer();
        
        const results = await WebAssembly.instantiate(wasmBytesGlobal, { env: { memory: new WebAssembly.Memory({ initial: 256 }) } });
        wasm = results.instance.exports;
        if (wasm._initialize) wasm._initialize(); 
        inputPtr = wasm.get_input_buffer();
        
        if (wasm.wasm_set_sample_rate) wasm.wasm_set_sample_rate(targetSampleRate);
        if (wasm.wasm_set_play_mode) wasm.wasm_set_play_mode(isFloatbeat ? 1 : 0);

        scope.canvasElem = canvasElem;
        scope.canvasCtx = ctx;
        scope.canvasWidth = canvasElem.clientWidth;
        canvasElem.width = scope.canvasWidth;
        canvasElem.height = scope.canvasHeight;
        
        if (!checkURLParams()) {
            loadPreset(); 
        } else {
            compile(formulaInput.value);
            autoExpand();
        }

        initMIDI(); 
        updateChannelButton(); // Ensure channel button UI matches loaded state
        
        requestAnimationFrame(renderLoop);
    } catch (err) {
        document.getElementById('status').innerText = `ERR: ${err.message}`;
    }
})();

/**
 * Initializes the root AudioWorklet runtime context safely adhering to browser lock requirements.
 */
async function bootAudio() {
    if (audioBooted || isBooting) return;
    isBooting = true;

    await wasmReadyPromise;

    if (typeof DeviceMotionEvent !== 'undefined' && typeof DeviceMotionEvent.requestPermission === 'function') {
        try {
            const permission = await DeviceMotionEvent.requestPermission();
            if (permission === 'granted') window.addEventListener('devicemotion', handleMotion);
        } catch (e) { console.warn("IMU Permission Denied/Ignored", e); }
    } else if (typeof window.DeviceMotionEvent !== 'undefined') {
        window.addEventListener('devicemotion', handleMotion);
    }

    const AudioContext = window.AudioContext || window.webkitAudioContext;
    audioCtx = new AudioContext(); 
    await audioCtx.resume();
    
    const blob = new Blob([workletCode], { type: 'application/javascript' });
    const workletUrl = URL.createObjectURL(blob);
    await audioCtx.audioWorklet.addModule(workletUrl);
    
    workletNode = new AudioWorkletNode(audioCtx, 'bytebeat-worklet');
    
    const analyserNode = audioCtx.createAnalyser();
    workletNode.connect(analyserNode);
    analyserNode.connect(audioCtx.destination);
    
    scope.analyser = [analyserNode, analyserNode];
    scope.setFFTAnalyzer();

    await new Promise((resolve) => {
        workletNode.port.onmessage = (e) => {
            if (e.data.type === 'ready') {
                resolve();
            } else if (e.data.type === 'sync') {
                t_audio = e.data.t_audio; 
            }
        };
        workletNode.port.postMessage({ type: 'init', wasmBytes: wasmBytesGlobal, sampleRate: targetSampleRate });
    });

    workletNode.port.postMessage({ type: 'mode', mode: isFloatbeat ? 1 : 0 });

    compile(formulaInput.value);

    audioBooted = true;
    isBooting = false;
}

/**
 * Controls DSP sequence progression start state handling contextual audio suspension triggers.
 * @param e Native UI event block
 */
async function togglePlay(e) {
    if(e) e.stopPropagation();
    
    if (!audioBooted) await bootAudio();
    if (audioCtx && audioCtx.state === 'suspended') await audioCtx.resume();
    
    is_playing = !is_playing;
    playBtn.innerText = is_playing ? "⏸\uFE0E" : "▶\uFE0E";
    if (is_playing) playBtn.classList.add('active'); else playBtn.classList.remove('active');
    
    if (workletNode) {
        workletNode.port.postMessage({ type: 'play', playing: is_playing });
    }
}

/**
 * Resets processing timing sequences completely halting playback streams and memory contexts.
 * @param e Native UI event block
 */
function resetT(e) { 
    if(e) e.stopPropagation(); 
    
    is_playing = false;
    if (playBtn) {
        playBtn.innerText = "▶\uFE0E";
        playBtn.classList.remove('active');
    }
    
    if (workletNode) {
        workletNode.port.postMessage({ type: 'play', playing: false });
        workletNode.port.postMessage({ type: 'reset' });
    }
    
    t_audio = 0; 
    t_viz = 0;
    scope.clearCanvas();
    scope.drawBuffer = [];
    
    if (wasm) {
        if (wasm.wasm_reset_vm) wasm.wasm_reset_vm();
        compile(formulaInput.value);
    }
}

/**
 * Issues core parsing translation payload over shared WASM memory pointer block structure.
 * @param str Base target string algorithm representation.
 * @return True output evaluation compilation validity.
 */
function compile(str) {
    if (!wasm) return false;
    const encoder = new TextEncoder();
    const encoded = encoder.encode(str);
    
    const view = new Uint8Array(wasm.memory.buffer);
    view.set(encoded, inputPtr);
    view[inputPtr + encoded.length] = 0; 
    
    const success = wasm.wasm_compile(is_rpn);
    const errorDisplay = document.getElementById('error-display');
    
    if (success) {
        if (errorDisplay) {
            errorDisplay.style.display = "none";
            errorDisplay.innerText = "";
        }
        
        if (workletNode) {
            workletNode.port.postMessage({ type: 'compile', encoded: encoded, is_rpn: is_rpn });
        }
    } else {
        if (errorDisplay) {
            const errMsg = getLastError();
            errorDisplay.innerText = errMsg;
            errorDisplay.style.display = "block";
        }
    }
    
    return success;
}

/**
 * UI toggle linking state change auto compilation validation events sequence.
 * @param e Native UI event block
 */
function toggleAuto(e) { if(e) e.stopPropagation(); is_auto = !is_auto; autoBtn.classList.toggle('active', is_auto); }

/**
 * Native framerate bounded GUI draw event polling core dispatch function target loop.
 */
function renderLoop() {
    requestAnimationFrame(renderLoop);
    
    if (mv > 0) {
        mv *= 0.92; 
        if (mv < 0.001) mv = 0;
        let mx = Math.max(0, Math.min(1, (lastX - canvasElem.getBoundingClientRect().left) / canvasElem.clientWidth));
        let my = Math.max(0, Math.min(1, (lastY - canvasElem.getBoundingClientRect().top) / canvasElem.clientHeight));
        
        if (wasm && wasm.wasm_set_mouse) wasm.wasm_set_mouse(mx, my, mv);
        if (workletNode) workletNode.port.postMessage({ type: 'mouse', mx: mx, my: my, mv: mv });
    }

    if (!wasm || !is_playing) return;

    let end_t = Math.floor(t_audio);
    let start_t = Math.floor(t_viz);
    
    if (end_t - start_t > 4000) start_t = end_t - 4000;
    
    for (let t = start_t; t < end_t; t++) {
        let val = wasm.wasm_execute(t);
        scope.drawBuffer.push({ t: t, value: [val, val] });
    }
    
    t_viz = end_t;
    scope.drawGraphics(t_viz);
}

window.addEventListener('keydown', (e) => {
    if (e.key === "Tab") { e.preventDefault(); toggleMode(); return; }
    if (e.key === "Enter" && document.activeElement === formulaInput && !e.shiftKey) {
        e.preventDefault(); if (wasm) compile(formulaInput.value); 
        if (!is_playing) togglePlay();
    }
    if (e.key === "Escape") togglePlay();
    
    if (e.ctrlKey || e.metaKey) {
        if (e.key === "=" || e.key === "+") { e.preventDefault(); zoomIn(); }
        if (e.key === "-") { e.preventDefault(); zoomOut(); }
    }

    if (e.code.startsWith('F') && e.code.length <= 3) {
        let fNum = parseInt(e.code.substring(1), 10);
        if (fNum >= 1 && fNum <= 10) {
            e.preventDefault(); 
            currentBank = fNum === 10 ? 0 : fNum; 
            if (bankBtn) bankBtn.innerText = "B: " + currentBank;
            loadPreset();
            return;
        }
    }

    if (e.code && e.code.startsWith('Digit')) {
        let num = parseInt(e.code.replace('Digit', ''), 10);
        if (!isNaN(num)) {
            if ((e.ctrlKey || e.metaKey) && !e.shiftKey && !e.altKey) {
                e.preventDefault();
                currentPatch = num;
                if (patchBtn) patchBtn.innerText = "P: " + currentPatch;
                loadPreset();
            }
        }
    }
});

formulaInput.addEventListener('input', (e) => { autoExpand(); if (wasm && is_auto) compile(e.target.value); });

/**
 * Refreshes the internal MIDI input device array list tracking available hardware blocks.
 * @param midiAccess Web MIDI API native dispatch structure reference instance object
 */
function refreshMidiInputs(midiAccess) {
    midiInputs = Array.from(midiAccess.inputs.values());
    
    // Bounds check the loaded state against physically available devices
    if (currentMidiPortIdx >= midiInputs.length) {
        currentMidiPortIdx = -1;
        localStorage.setItem('bytebed_midi_port', '-1');
    }
    
    updateMidiButton();
}

/**
 * Handles clearing MIDI gate thresholds upon note release triggering.
 * @param note The absolute target standard MIDI note number
 */
function handleNoteOff(note) {
    const released_freq = 440.0 * Math.pow(2.0, (note - 69) / 12.0);
    if (Math.abs(current_midi_freq - released_freq) < 0.1) {
        if (wasm && wasm.wasm_set_midi) wasm.wasm_set_midi(current_midi_freq, 0.0, note);
        if (workletNode) workletNode.port.postMessage({ type: 'midi', freq: current_midi_freq, gate: 0.0, note: note });
    }
}

/**
 * Generic MIDI stream parser routing standard note messages to WebAssembly virtual machines.
 * @param message DOM element referencing natively bounded hardware IO streams structure
 */
function onMIDIMessage(message) {
    if (currentMidiPortIdx === -2) return;

    const status = message.data[0];
    const command = status & 0xf0;
    const channel = status & 0x0f;

    if (currentMidiChannel !== -1 && channel !== currentMidiChannel) {
        return;
    }

    if (currentMidiPortIdx !== -1) {
        const targetInput = midiInputs[currentMidiPortIdx];
        if (message.currentTarget !== targetInput && message.target !== targetInput) {
            return;
        }
    }

    const note = message.data[1];
    const velocity = (message.data.length > 2) ? message.data[2] : 0;

    if (command === 144) { 
        if (velocity > 0) {
            current_midi_note = note;
            current_midi_freq = 440.0 * Math.pow(2.0, (note - 69) / 12.0);
            const gate = velocity / 127.0;
            
            if (wasm && wasm.wasm_set_midi) wasm.wasm_set_midi(current_midi_freq, gate, note);
            if (workletNode) workletNode.port.postMessage({ type: 'midi', freq: current_midi_freq, gate: gate, note: note });
        } else {
            handleNoteOff(note);
        }
    } else if (command === 128) { 
        handleNoteOff(note);
    }
}

/**
 * Starts standard context initialization connecting physical system MIDI controllers.
 */
function initMIDI() {
    if (navigator.requestMIDIAccess) {
        navigator.requestMIDIAccess().then(
            (midiAccess) => {
                refreshMidiInputs(midiAccess);
                for (let input of midiInputs) {
                    input.onmidimessage = onMIDIMessage;
                }
                midiAccess.onstatechange = (e) => {
                    refreshMidiInputs(midiAccess);
                    if (e.port.type === 'input' && e.port.state === 'connected') {
                        e.port.onmidimessage = onMIDIMessage;
                    }
                };
                console.log("Web MIDI initialized");
            },
            (err) => {
                console.log("MIDI access failed: ", err);
                updateMidiButton();
            }
        );
    } else {
        console.log("Web MIDI API not supported in this browser.");
        updateMidiButton();
    }
}