#include "state.h"

Theme themes[3] = {
    { 0x07E0, 0x0320, 0x0000, 0x07E0, 0, 255, 0 },    
    { 0x07FF, 0x01AA, 0x0000, 0x07FF, 0, 200, 255 },  
    { 0xF81F, 0x780F, 0x0000, 0xF81F, 255, 0, 255 }   
};
int current_theme_idx = 0;

#if !defined(NATIVE_BUILD) && !defined(__EMSCRIPTEN__)
Preferences prefs;
VisMode current_vis = VIS_WAV_WIRE; 
LGFX_Sprite canvas(&M5.Display);
LGFX_Sprite bg_sprite(&M5.Display);
#else
VisMode current_vis = VIS_WAV_WIRE; 
#endif

int drawScale = 5; 
float volume_perc = 0.4f; 
int32_t t_raw = 0;
uint8_t wave_buf[240] = {0};
uint8_t last_sample_val = 128;
uint32_t last_draw = 0;

PlayMode current_play_mode = MODE_BYTEBEAT;
int current_sample_rate = DEFAULT_SAMPLE_RATE;

#define EMPTY_PRESET {"t", 8000, MODE_BYTEBEAT}


const char* bankNames[10] = {
    "USER", "BYTEBEAT 1", "BYTEBEAT 2", "FLOATBEAT 1", 
    "FLOATBEAT 2", "IMU", "MIDI 1", "MIDI 2", "MIDI 3", "HELPERS"
};


const PresetConfig defaultBanks[10][10] = {
    // BANK 0: USER
    { EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET },
    
    // BANK 1: CLASSIC (8kHz Bytebeat)
    {
        {"t*[0,8/9,1,9/8,6/5,4/3,3/2]['1242660660555555123155055044444412424405503333332211555555444444'[t>>10]]*4", 8000, MODE_BYTEBEAT},
        {"(t*5&t>>7)|(t*3&t>>10)", 8000, MODE_BYTEBEAT},
        {"(t>>6|t<<1)+(t>>5|t<<3|t>>10)", 8000, MODE_BYTEBEAT},
        {"t*(42&t>>10)", 8000, MODE_BYTEBEAT},
        {"t>>4|t>>5|t%256", 8000, MODE_BYTEBEAT},
        {"t*[0,1,1.2,4/3,3/2,8/5,16/9,2]['110033003333004455003300330030504400220022004400550033003300303011003300333300445500330033003355777766665050404055003300333300557700770066005500444422332200202040606060605050404055003300333333'[t>>9]]*6", 8000, MODE_BYTEBEAT},
        {"(t*t/(1+t>>10))&t>>8", 8000, MODE_BYTEBEAT},
        {"t&t>>8", 8000, MODE_BYTEBEAT},
        {"t*(t>>11&t>>8&123&t>>3)", 8000, MODE_BYTEBEAT},
        {"t*21/4*[0,1,4/3,6/5,8/9]['10101010110010101010101011001010202020202200202030303030330030401010101011001010101010101100102010101010110010101010101011001033'[t/415&127]]", 8000, MODE_BYTEBEAT} 
    },
    // BANK 2: TESTS (8kHz Bytebeat)
    {
        {"g=(n)=>{n<1?1:n%2<1?g(n/2):2*g(n/2)}; g(t>>7)*(t&128|t>>4)", 8000, MODE_BYTEBEAT}, 
        {"tm=(n)=>{n<1?0:n%2<1?tm(n/2):1-tm(n/2)}; t*tm(t>>11)*(t>>5&7)", 8000, MODE_BYTEBEAT},
        {"pat=(s,o,r)=>(c=s[t>>r],c?30&t*2**((c-1)/12-o):0),3*pat('10100030305006008034660000gfbx',2,10)+5*(t&4096?pat('148',0,8)*(4096-(t&4095))>>12:0)", 8000, MODE_BYTEBEAT},
        {"env=(t>>12)%4; env<1?t*3:env<2?t*5:env<3?t*6:t*9", 8000, MODE_BYTEBEAT},
        {"f=(x,dt)=>{x*(t>>dt)};f(t,5)+f(t,7)", 8000, MODE_BYTEBEAT},
        {"v=t>>3,v^=t>>(t>>8),(v&3)?t>>5:t&(t>>4)", 22050, MODE_BYTEBEAT},
        {"a=t*2**([1,2,3][t>>22&3]/4),b=t*2**([1,2,3][t>>11&63]/12+2),(3*a^t>>6&256/'1112'[t>>14&3]-1|a)%256*2/3+(b^b*2)%256/3", 8000, MODE_BYTEBEAT},                                               
        {"a=[1,2,3][(t>>10)%4]*[2,4,5,6][(t>>16)%4]*t,b=a%32+t>>a,c=b%t+a,[(t>>a)+b,c]", 32000, MODE_BYTEBEAT},                                  
        {"t*(4|t>>13&3)>>(~t>>11&1)&128|t*(t>>11&t>>13)*(~t>>9&3)&127", 8000, MODE_BYTEBEAT}, 
        {"t*((t>>12|t>>8)&63&t>>4)", 8000, MODE_BYTEBEAT},   
    },
    // BANK 3: FLOATBEAT (48kHz Floatbeat)
    {
        {"gc=t/(t/[12,13,14,15,16,17,18,19,20,21,22,23,24][t>>12&13]),(sin(t/gc)+sin(t/20)+sin(t/20)+tan(sin(t/t-t/(gc+.01))))/4", 48000, MODE_FLOATBEAT},                                                                 
        {"sin(t/20)*exp(-(t%8000)/1000)", 48000, MODE_FLOATBEAT},                                             
        {"w=t/50, sin(w + sin(w*2)*2)", 48000, MODE_FLOATBEAT},                                               
        {"p=t*2**([0,4,7,12][(t>>13)%4]/12)/2.43, sin(p*PI/16)*exp(-(t%8192)/2000)", 48000, MODE_FLOATBEAT},  
        {"p=(t>>15&t>>13)*[3,2,1.5,0][3&t>>11]/2,b=.5*[1,2][1&t>>13]*[2,2.5,3,3.5][3&t>>15]/2,sin(sin(p*t/4.79)/40*(32-t/256%32)+p*t/40.743665)*(32-t/256%32)/64+sin(sin(b*t/81.48732)/6*(32-t/256%32)+b*t/40.743665)*(32-t/256%32)/48", 48000, MODE_FLOATBEAT},                                  
        {"w=t/20, sin(w)*abs(sin(t/4000))", 48000, MODE_FLOATBEAT},
        {"t/=48000,k=sin((t*4%2+.01)**0.5*200),m=0,g=2**(m/12),b=sin(t*6000)*((1-(t*2.1%1))**12),((t*g%1)-.5)*.4+k*.5+b*.31", 48000, MODE_FLOATBEAT},                                           
        {"e=256,r=40,d=32,p=t*2**([0,12,12,0,12,12,5,7][7&t>>13]/12)/2.43,env=r-t/e%d,sin(sin(sin(p*PI/16)/76*env+p*PI/256)/8*env+p*PI/256)*env/64", 48000, MODE_FLOATBEAT}, 
        {"x = (lr) => sum(3,i=>s(t*0.03*2**(i)*lr+s(t*0.005)*2))/3,[x(1.01),x(0.99)]", 48000, MODE_FLOATBEAT},                                         
        {"sin(t/(t&16384?1.5:1)/[1,2,3,4,1,2,3,4,6,4,5,4,8,8,8,8][15&t>>10])/((t&[1023,1023,1023,2047][3&t>>12])*.01)", 48000, MODE_FLOATBEAT}                                                  
    },
    
    // BANK 4: FLOATBEAT SUM & REDUCE PATTERNS
    {
        {"t%16384||(a=$[220],m=r()*.5+.1,sum(220,i=>a[i]=r()*2-1)),a[t]=a[t]*m+a[t-1]*(1-m)", 48000, MODE_FLOATBEAT},                                                                 
        {"sum(12,i=>s(t*0.015*(i+1))*(4-i))/25", 48000, MODE_FLOATBEAT},                                             
        {"p=1000/(t%123456),sum(12,i=>s(t*p*(i*2+1))/(i*2))", 48000, MODE_FLOATBEAT},                                               
        {"p=t*0.25,sum(4,i=>s(p*(1+i*0.25))*s(t*0.0005*(i+0.25)))/4", 48000, MODE_FLOATBEAT},  
        {"p=t*0.025,x=L=>sum(4,i=>s(p*(i+1)*L+s(t*0.001*i)))/ 4,[x(1.0),x(1.005)]", 48000, MODE_FLOATBEAT},                                  
        {"p=t*pi/16/2**(t/3e6),f=x=>s(x+s(x)),x=L=>sum(3,i=>f(p*L/2**(i*9/12))*5**(-.00007214*(t%(65536*2**(i/2)))))/6,[x(0.105),x(.95)]", 48000, MODE_FLOATBEAT},                                           
        {"[20.0, 10.0, 5.0, 2.5].reduce((a, v) => s(t * v * 0.001 + a * 2.0))", 48000, MODE_FLOATBEAT}, 
        {"[1,3,5,7].reduce((a,v)=>max(a,s(t*v*0.0015)/v))", 48000, MODE_FLOATBEAT},                                         
        {"c=[1.0,1.2,1.5,1.8],o=1+(floor(t/16000)%2),f=c.map(x=>x*100*o),f.sum(n=>s(t *n*0.005))/4*exp(-(t%1000)/500)", 48000, MODE_FLOATBEAT},                                               
        {"t/=48E3,v=t*2**7.4284905,y=a=>32-v/a%32,u=r=>r%256/128-1,i=(u(v*143&1&&255)/4+.5*random()-.25)*y(1)/16,f=sin(16*cbrt(128*v%4096)),z=v*[1,4,4.8,5.4,6,2,1,2,1,4,4.8,5.4,6,6.33,6,4.8,5.4,2.7,1.35,2.7,4.8,3,6,4,1,2,1,2,1,2,1,2][(v>>5)%32]*1.52*PI,sin(z+sin(z)*atan(sin(z/4))*1.09**y(1))/2*y(1)/32+[f,i,f,i,f,i,f,f][(v>>5)%8]/2", 48000, MODE_FLOATBEAT}, 
    },
    
    // BANK 5: ACCELEROMETER & GYROSCOPE & MOUSE
    {
        {"g=gx*gx+gy*gy+gz*gz,o+=(g-o)*.005,((t*10883>>7)*(t>>5)|t>>15)*o", 8000, MODE_BYTEBEAT}, 
        {"s(t * (100 + ax * 150) * 0.01)", 48000, MODE_FLOATBEAT},                                                                 
        {"(r() - 0.5) * min(1.0, abs(gx) * 0.02)", 48000, MODE_FLOATBEAT},                                             
        {"s(t * (0.015 + abs(gy) * 0.0001)) * (0.2 + abs(gy) * 0.01)", 48000, MODE_FLOATBEAT},                                               
        {"s(t * 0.01 + s(t * 0.002) * gz)", 48000, MODE_FLOATBEAT},
        {"v+=((1.0-my+(ay*5))-v)* 0.001, p+=((5.0*(mx+ax))-p)*0.001, ph+=p*tau/sr, s(ph*s(ph))*v*3", 48000, MODE_FLOATBEAT},                                           
        {"v+=(((1.0-my)+ay)-v)*0.001, p+=((mx+ax)-p)*0.001, ph+=(1000.0+p*6000+sin(t*6.0*tau/sr)*0.5)*tau/sr, sin(ph)*(v>0?v:0)*2.5", 8000, MODE_FLOATBEAT}, 
        {"b=.001,px+=(ax-px)*b,vy+=(ay-vy)*b,mz+=(az-mz)*b,vx+=(gx-vx)*b,ty+=(gy-ty)*b,fz+=(gz-fz)*b,ph+=(300+px*400+s(t*6*tau/sr)*vx*50)*tau/sr,s(ph+s(ph*(mz*2+1))*fz*1.5)*(vy>-1?vy+1:0)*.25*(1-ty*.5*s(t*10*tau/sr))", 48000, MODE_FLOATBEAT},                                         
        {"b=.005,g=gx*gx+gy*gy+gz*gz,sw+=(g-sw)*b,ph+=(60+ax*5+sw*120)*tau/sr,s(ph+s(ph*2.01)*.6+sw*s(ph*4.5))*(.3+sw*.5)", 48000, MODE_FLOATBEAT},                                         
        {"b=.003,g=gx*gx+gy*gy+gz*gz,w+=(g-w)*b,ph+=(1500+ax*200+w*1200)*tau/sr,(s(ph+s(ph*1.14)*(1+w*4)+s(ph*.87)*w*6)+s(ph*27.3)*.1)*w*.4", 48000, MODE_FLOATBEAT}                                                  
    },
    
    // BANK 6: MONOPHONIC MIDI (mf & mg parameters)
    { 
        {"(t * mf * 256 / sr) * mg", 8000, MODE_BYTEBEAT}, 
        {"((t * mf * 256 / sr) & ((t>>4)&255)) * mg", 8000, MODE_BYTEBEAT},
        {"T=t*8000/sr,((t*mf/64)*(3+(3&T>>14))>>(3&T>>9)|T>>6)", 8000, MODE_BYTEBEAT},
        {"t * mf * 256 / sr * (1 + ((t>>11)&1)) * mg", 8000, MODE_BYTEBEAT},                                  
        {"((t - t%4) * mf * 256 / sr) * mg", 11025, MODE_BYTEBEAT},
        {"v+=(mg-v)*0.01,p=t*mf*16/sr,128+(p&15)*(-p&15)*((p&16)/8-1)*128/64*v", 8000, MODE_BYTEBEAT}, 
        {"(t * mf * 256 / sr) * (t>>8 & 1) * mg", 8000, MODE_BYTEBEAT},
        {"(t * mf * 256 / sr) % (128 + ((t>>3)&127)) * mg", 8000, MODE_BYTEBEAT},
        {"f=t*mf*20/sr*tau|0,f&f>>2", 8000, MODE_BYTEBEAT},
        {"(t * mf / sr * 128) * mg", 8000, MODE_BYTEBEAT} 
    },
    
    // BANK 7: MONOPHONIC MIDI (Raw mn parameters)
    {
        {"(t * mn) * mg", 8000, MODE_BYTEBEAT}, 
        {"(t * (mn ^ (t >> 10))) * mg", 8000, MODE_BYTEBEAT}, 
        {"t * mn & t", 8000, MODE_BYTEBEAT},
        {"(t * mn & t >> 4) * mg", 8000, MODE_BYTEBEAT},
        {"(t * mn | t * (mn + 7)) * mg", 8000, MODE_BYTEBEAT},
        {"((t * mn) % 255) * mg", 8000, MODE_BYTEBEAT},                                               
        {"t * (mn + (t >> 12 & 7)) * mg", 8000, MODE_BYTEBEAT},                                  
        {"(t * mn ^ t * (mn + 3)) * mg", 8000, MODE_BYTEBEAT}, 
        {"t * mn * (mn & 1 ? 1 : 2)", 8000, MODE_BYTEBEAT}, 
        {"t * (mn ^ 2) & t >> 6 | t >> 4", 8000, MODE_BYTEBEAT}
    }, 
    
    // BANK 8: FLOATBEAT MIDI (mf, mg, & mn)
    {
        {"f=t*mf*tau/sr,e=mg*30,sin(sin(sin(f*16)/76*e+f)/8*e+f)*e/64", 48000, MODE_FLOATBEAT}, 
        {"v+=(mg-v)*0.001, s(t*mf*tau/sr)*v*0.5", 48000, MODE_FLOATBEAT},                                                                  
        {"s(t*mf*6.283/sr + s(t*mf*12.566/sr)*2.0)*mg*0.2", 48000, MODE_FLOATBEAT},
        {"v+=(1-v)*.005,L=t/sr,m=b=>((L*mf*2**((b-54)/12))%1)*2-1,(m(62)*.29+m(53)*.46+m(58)*.34+m(70)*.17)*v*.4", 48000, MODE_FLOATBEAT},                                           
        {"L=t/sr,sum(7,k=>(i=k*PI/3,T=max(0,L-i),W=T%PI,f=mf*(1+i/.75%4)*[.737,.415,.322,.17][T/PI],s(TAU*(T*f%1))*exp(-W*1.5)*min(1,W*50)*(.12-k*.015)))", 48000, MODE_FLOATBEAT}, 
        {"p=(p+mf*PI/sr)%TAU,x=tan(cos(p+s(p)*1.5)*0.9),s(0.15*exp(exp(abs(x))))*0.5", 48000, MODE_FLOATBEAT},  
        {"p=(p+mf*TAU/sr)%TAU,q=(q+mf/1.5*TAU/sr)%TAU,T=t*TAU/sr,(.8*s((.8*(abs(q*INVPI-1)*2-1)+.05*s(p*2))*(2+(1+s(T*.18))*(2+(1+s(T*.04))*12))+T)+.7*s(p))*0.25", 48000, MODE_FLOATBEAT},  
        {"v += (mg - v) * 0.001, s(t * mf * TAU / sr) * s(t * 15 * TAU / sr) * v * 0.5", 48000, MODE_FLOATBEAT},  
        {"v+=(mg-v),T=t*sr,p=(p+mf*TAU/sr)%TAU,E=t%9,s(s(s(s(p*28)/8*E+p*6)/3*E+p*0.5)+p)*v/1.5", 48000, MODE_FLOATBEAT},
        {"v+=(mg-v)*.005,w+=(mg-w)*.00005,P=t*mf*TAU/sr,E=8+32*(v-w),sin(sin(sin(P*16)/100*E+P)/8*E+P)*E/64*v", 48000, MODE_FLOATBEAT}                                                  
    }, 
    // BANK 9: Unorthodox byteeats with bpm, pc, phase, & other custom parameters
    {
        {"d=(t>>12)%8,f=pc(d,2,36),(t*f)>>8", 8000, MODE_BYTEBEAT},
        {"d=(t>>11)%5,o=(t>>12)%2,f=pc(d,o,48,683),(t*f)>>7",8000,MODE_BYTEBEAT},
        {"d=(t>>10|t>>12)%12,f=pc(d,0,60,10526880,24),(t*f)>>8",8000,MODE_BYTEBEAT},
        {"pc(t>>11&9,t>>15&3,48,2741,12,432)*t>>8", 8000, MODE_BYTEBEAT},
        {"bpm=100,t*env(bar,1,1)", 8000, MODE_BYTEBEAT},
        {"((t * 400 >> 12) % 40) * env(beat % 4, 0.15, 2)", 48000, MODE_FLOATBEAT},
        {"f=pc([0,4,7,11][step],2,38,16777215,24,340.0), sweep=lfo(phase(0.25,0.5,0.5),0), ap=(ap+(f*(1.0+sweep*0.15))/sr), lfo(ap,0)*env(step%1.0,10,1.0)*0.3", 48000, MODE_FLOATBEAT},
        {"f=pc([2,7,12,19][step],1,36), ap=(ap+(f*2.0)/sr)%1.0, lfo(ap, 3) * (on(342, step % 8) | on(452, step % 16)) * 0.3", 48000, MODE_FLOATBEAT},
        {"f=pc([1,2,3][t>>10],2,16,76), ap=(ap+(f*4)/sr), lfo(ap,2)*env(step, 100, 0.5) * on(euclid(7, 16, 0), step % 16)", 48000, MODE_FLOATBEAT},
        {"bpm=140,steps=16,t*on(euclid(9, 14),t>>7)", 48000, MODE_BYTEBEAT}
    }
                                       
        
};

SlotState slots[10][10];
int current_bank = 0;

String input_buffer = defaultBanks[0][0].formula;
String active_eval_formula = defaultBanks[0][0].formula;
String status_msg = "";
uint32_t status_timer = 0;
String undo_stack[UNDO_DEPTH];
int cursor_pos = input_buffer.length();
int undo_ptr = 0;
int undo_max = 0;
bool rpn_mode = false;
bool is_playing = true;

char current_top_text[64] = "BYTEBED";

Layout getLayout() {
    int input_h = (input_buffer.length() > 80) ? 60 : 25;
    return { 135 - input_h, 21, (135 - input_h) - 21 };
}

uint8_t getLogVolume() { 
    return (uint8_t)(volume_perc * volume_perc * volume_perc * 255.0f); 
}

void saveUndo() { 
    if (input_buffer == undo_stack[undo_ptr]) return; 
    undo_ptr = (undo_ptr + 1) % UNDO_DEPTH; 
    undo_stack[undo_ptr] = input_buffer; 
    undo_max = undo_ptr; 
}