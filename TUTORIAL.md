# Floatbeat tutorial

TBD ...

# Part 1

Basic saw:

t/=48000, 
n=1,
m=2**((n+70)/12), 
t*m%1


Sequence:

t/=48000, 
m='0004445555221225555'[t*4.3],
n=2**((m+70)/12), 
t*n%1

t/=48000, 
m='0004445555221225555'[t*6],
n=2**((m+70)/12), 
((t*n*255&255)>n)*0.5

# Part 2: PWM

We turn that ramp into a Square Wave with Pulse-Width Modulation. The duty cycle (the "thinness" of the sound) will shift over 8 seconds.
t/=48000, 
m='0004445555221225555'[t*4.3],
n=2**((m+70)/12), 
o=(t*n%1 > t/8%1*.25+.2) ? .54 : -.54

# Part 3: Kick
Phase 3: The Electronic Kick Drum
Before we add the filter, let’s build the percussion. This uses a high-frequency sine wave that "drops" its pitch exponentially every time the beat resets.

t/=48000, 
kick = .3 * s((t*4.3%2 + .01)**0.4 * 300),
kick

# Part 4: Filter

Now we add the state variables v0 and v1. This is a classic integrator. The variable c controls the cutoff frequency.

JavaScript
t/=48000, 
m='0004445555221225555'[t*4.3],
n=2**((m+70)/12), 
o=(t*n%1 > t/8%1*.25+.2) ? .54 : -.54,
c=.15, // Static cutoff for now
v0+=c*(o-v1-.4*v0),
v1+=v0,
v1 // This is the filtered synth output

# Part 5: Modulation

We add the LFO (Low Frequency Oscillator) to the filter cutoff c to give it that "wah-wah" movement and mix in the kick drum.

JavaScript
t/=48000, 
m='0004445555221225555'[t*4.3],
n=2**((m+70)/12), 
o=(t*n%1 > t/8%1*.25+.2) ? .54 : -.54,
c=.15 + .1*s(t*.5), // Filter sweeps slowly
v0+=c*(o-v1-.4*v0),
v1+=v0,
.3*s((t*4.3%2+.01)**0.4*300) + v1 // Kick + Synth



# Sine

t/=48000, 
n='1535674'[t*4],
m=2**((n+70)/12), 
sin(t*m*pi*2)


t/=48000, 
n='1535674'[t*4],
m=2**((n+70)/12), 
e=sin((t*4)*pi),
sin(t*m*pi*2)*e


# Bleeps:


t/=48000, 
k=sin((t*4%2+.01)**0.5*200),
m='00044300235023'[t],
g=2**((m+70)/12), 
b1=sin(t*6000)*((1-(t*2%1))**12),
b2=sin(t*6000)*((1-(t*2.01%1))**12),
((t*g%1)-.5) * .4 + k * .4 + (b1+b2) * .31


t/=48000, 
k=sin((t*4%2+.05)**0.5*200),
m='thisisjustbase62'[t*4],
g=2**((m+60)/12), 
b=n=>{sin(t*6000)*((1-(t*n%1))**12)},
b1=b(2),b2=b(8.01),b3=b(2.1),
((t*g%1)-.5) * .4 + k * .4 + (b1+b2+b3) * .31