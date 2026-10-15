"""Sound effects for the Washi interface, synthesised with the standard library only (no samples, nothing to license). Mono, 48 kHz,
16-bit. They are all short and soft: a held D-pad makes many of them. Run: python3 -I tools/sfx.py (writes tools/out/sfx/)"""
import math, os, random, struct, wave

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out", "sfx")
SR = 48000
os.makedirs(OUT, exist_ok=True)


def lowpass(x, a):
    y, out = 0.0, []
    for v in x:
        y += a * (v - y)
        out.append(y)
    return out


def highpass(x, a):
    lp = lowpass(x, a)
    return [v - l for v, l in zip(x, lp)]


def noise(n, seed):
    r = random.Random(seed)
    return [r.uniform(-1, 1) for _ in range(n)]


def env_exp(n, tau):
    return [math.exp(-(i / SR) / tau) for i in range(n)]


TARGET_RMS, PEAK_CAP = 0.14, 0.92


def write(name, x, peak=None):
    """every sound is levelled to the same loudness (RMS over the part that is actually sounding), then capped so it cannot clip.
    The mix between sounds is set by the player (GAIN in washi-app.html), not here."""
    m = max(abs(v) for v in x) or 1.0
    act = [v for v in x if abs(v) > 0.03 * m]
    rms = math.sqrt(sum(v * v for v in act) / len(act))
    k = min(TARGET_RMS / rms, PEAK_CAP / m)
    x = [v * k for v in x]
    for i in range(int(SR * 0.006)):  # a 6 ms fade-out, so nothing clicks at the end
        x[-1 - i] *= i / (SR * 0.006)
    with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, v)) * 32767)) for v in x))
    return len(x) / SR, rms * k, max(abs(v) for v in x)


def peel():
    """tape coming off, softened: a short run of tiny stick-slip pulses, mostly low-mid, rounded off, no hiss"""
    n = int(SR * 0.22)
    nz = lowpass(highpass(noise(n, 1), 0.07), 0.5)
    out, phase = [0.0] * n, 0.0
    burst_left, burst_env = 0, 0.0
    for i in range(n):
        t = i / n
        f = 34 + 70 * t ** 1.3
        phase += f / SR
        if phase >= 1:
            phase -= 1
            burst_left = int(SR * (0.004 + 0.002 * (1 - t)))
            burst_env = 1.0
        if burst_left > 0:
            out[i] = nz[i] * burst_env
            burst_env *= 0.9984 ** (SR / 8000)
            burst_left -= 1
        out[i] *= (math.sin(math.pi * min(1, t * 1.1)) ** 0.8) * (1 - 0.3 * t)
    return write("peel", lowpass(out, 0.72), 0.45)


def a_of(fc):
    """one-pole filter coefficient for a cutoff of fc Hz"""
    return 1.0 - math.exp(-2 * math.pi * fc / SR)


def bp(x, lo, hi):
    """band-limit noise to roughly lo..hi Hz (two one-pole low-passes and two high-passes, so the edges are soft but real)"""
    y = lowpass(lowpass(x, a_of(hi)), a_of(hi))
    return highpass(highpass(y, a_of(lo)), a_of(lo))


def hit_env(n, t0, att, tau):
    """a soft contact: a raised-sine attack of att seconds starting at t0, then an exponential decay of time constant tau"""
    out = []
    for i in range(n):
        t = i / SR - t0
        out.append(0.0 if t < 0 else math.sin(0.5 * math.pi * min(1.0, t / att)) ** 2 * math.exp(-t / tau))
    return out


def swell_env(n, t0, dur):
    """a breath: sin^2 rising and falling over dur seconds from t0"""
    return [math.sin(math.pi * (i / SR - t0) / dur) ** 2 if 0 <= i / SR - t0 < dur else 0.0 for i in range(n)]


def grains(n, seed, times, lo, hi, amps, tau=0.0005):
    """a few tiny uneven crackles of band-limited noise (paper or adhesive letting go), each about 1 ms"""
    nz = bp(noise(n, seed), lo, hi)
    out = [0.0] * n
    for t, a in zip(times, amps):
        i0 = int(t * SR)
        for j in range(int(SR * 0.0025)):
            if i0 + j < n:
                out[i0 + j] += nz[i0 + j] * a * math.exp(-j / (SR * tau)) * min(1.0, j / (SR * 0.0004))
    return out


def mix(*parts):
    return [sum(v) for v in zip(*parts)]


def scale(x, k):
    return [v * k for v in x]


# The Switch's speakers do not play much under 200 Hz, and a single clear pitch with a decaying tail sounds like a hollow box. Paper put on cork
# has almost no pitch at all: so the stick and stamp sounds are pure band-limited noise (mostly 500 Hz to 5 kHz), shaped in time. Three sets of
# stick and stamp (the file names with no suffix, _b and _c) differ in idea so they can be compared on the console.
def stick():
    """a: a dry paper tap on cork. Band-limited noise, a very short soft contact and a quick decay, and a tiny second touch as the paper edge
    comes down. No tone."""
    n = int(SR * 0.07)
    first = [a * b for a, b in zip(bp(noise(n, 2), 800, 4000), hit_env(n, 0.0, 0.004, 0.016))]
    edge = [a * b for a, b in zip(bp(noise(n, 3), 1200, 5000), hit_env(n, 0.014, 0.002, 0.008))]
    return write("stick", mix(first, scale(edge, 0.45)), 0.35)


def stick_b():
    """b: a paper settling. A tiny air 'fff' swells just before the contact, then one soft dry tick."""
    n = int(SR * 0.11)
    fff = [a * b for a, b in zip(bp(noise(n, 6), 1800, 6000), swell_env(n, 0.0, 0.06))]
    tick_ = [a * b for a, b in zip(bp(noise(n, 7), 600, 3000), hit_env(n, 0.045, 0.002, 0.012))]
    return write("stick_b", mix(scale(fff, 0.5), tick_), 0.35)


def stick_c():
    """c: a tape press. A soft 'pff' of filtered noise with a faint pat of low-mid noise under it, and a few tiny sticky crackles as the
    adhesive lets go. No pitched body."""
    n = int(SR * 0.12)
    pff = [a * b for a, b in zip(bp(noise(n, 8), 700, 3200), hit_env(n, 0.0, 0.004, 0.022))]
    pat = [a * b for a, b in zip(bp(noise(n, 9), 300, 900), hit_env(n, 0.0, 0.003, 0.006))]
    sticky = grains(n, 50, (0.022, 0.041, 0.053, 0.079), 2500, 6000, (0.5, 0.3, 0.45, 0.25))
    return write("stick_c", mix(scale(pff, 0.9), scale(pat, 0.5), scale(sticky, 0.5)), 0.35)


def stamp():
    """a: a rubber stamp hitting paper on a pad: a duller, heavier contact of low-mid noise (about 350-1800 Hz), then three little paper
    rattles settling after it. No tone."""
    n = int(SR * 0.26)
    hit = [a * b for a, b in zip(bp(noise(n, 11), 350, 1800), hit_env(n, 0.0, 0.004, 0.04))]
    parts = [hit]
    for k, (t0, amp) in enumerate(((0.03, 0.35), (0.06, 0.25), (0.095, 0.15))):
        parts.append(scale([a * b for a, b in zip(bp(noise(n, 12 + k), 1500, 5500), hit_env(n, t0, 0.004, 0.012))], amp))
    return write("stamp", mix(*parts))


def stamp_b():
    """b: press and lift. A dull pad contact, then the rubber peels up off the paper: a short sticky swell of brighter noise with a few
    tiny crackles."""
    n = int(SR * 0.24)
    press = [a * b for a, b in zip(bp(noise(n, 21), 400, 2000), hit_env(n, 0.0, 0.004, 0.04))]
    lift = [a * b for a, b in zip(bp(noise(n, 22), 1500, 5000), swell_env(n, 0.12, 0.07))]
    sticky = grains(n, 23, (0.125, 0.14, 0.152, 0.168), 2500, 6000, (0.4, 0.3, 0.4, 0.25))
    return write("stamp_b", mix(press, scale(lift, 0.4), scale(sticky, 0.5)))


def stamp_c():
    """c: a heavy thud that bounces: three quick dulled impacts a few ms apart, each softer, then a little paper crackle."""
    n = int(SR * 0.2)
    parts = []
    for k, (t0, amp) in enumerate(((0.0, 1.0), (0.028, 0.55), (0.052, 0.3))):
        parts.append(scale([a * b for a, b in zip(bp(noise(n, 31 + k), 300, 2200), hit_env(n, t0, 0.003, 0.02))], amp))
    parts.append(scale(grains(n, 35, (0.06, 0.082, 0.1, 0.13), 2000, 6000, (0.3, 0.4, 0.25, 0.2)), 0.4))
    return write("stamp_c", mix(*parts))


def dull(n, seed, lo, hi, t0, att, tau, order=2):
    """ONE soft, damped contact: noise low-passed at hi Hz (so no sparkle), a gentle high-pass at lo Hz (so the Switch's speaker can play it),
    a raised-sine attack of att seconds and an exponential decay of tau seconds. No ring, no second touch."""
    x = noise(n, seed)
    for _ in range(order):
        x = lowpass(x, a_of(hi))
    x = highpass(x, a_of(lo))
    return [a * b for a, b in zip(x, hit_env(n, t0, att, tau))]


def stick_box():
    """round 1, kept only for comparison in sounds-lab.html: a pure tone at 430-600 Hz with a puff of noise. It read as a hollow box on carpet."""
    n = int(SR * 0.12)
    thump = [math.sin(2 * math.pi * (430 + 170 * math.exp(-(i / SR) / 0.02)) * (i / SR)) * math.exp(-(i / SR) / 0.02)
             * min(1.0, (i / SR) / 0.002) for i in range(n)]
    puff = highpass(lowpass(noise(n, 2), 0.35), 0.1)
    pe = [math.exp(-(i / SR) / 0.012) * min(1.0, (i / SR) / 0.002) for i in range(n)]
    return write("stick_round1", [0.9 * a + 1.0 * b * c for a, b, c in zip(thump, puff, pe)], 0.35)


# Round 3: one soft contact each, no rattles, no crackle, no bounce, no ring. Heavily damped noise, low-passed so there is no sparkle.
def stickA():
    """felt: very dull, the band centred around 400-500 Hz, like a finger pressing felt"""
    n = int(SR * 0.08)
    return write("stickA", dull(n, 101, 260, 650, 0.0, 0.004, 0.02))


def stickB():
    """tup: low-passed at about 800 Hz, 35 ms, a dry little 'tup'"""
    n = int(SR * 0.05)
    return write("stickB", dull(n, 102, 220, 800, 0.0, 0.003, 0.009))


def stickC():
    """pat: low-passed at about 1.5 kHz, 60 ms, a lighter 'pat' of paper on cork"""
    n = int(SR * 0.09)
    return write("stickC", dull(n, 103, 250, 1500, 0.0, 0.003, 0.016))


def stickD():
    """press: a slower 6 ms attack and a long soft tail (about 90 ms), tape being pressed down with a thumb"""
    n = int(SR * 0.13)
    return write("stickD", dull(n, 104, 220, 1200, 0.0, 0.006, 0.03))


def stickE():
    """tup with air: a faint breath of air (noise below about 2 kHz) swelling for 40 ms, then the 800 Hz 'tup'"""
    n = int(SR * 0.09)
    air = [a * b for a, b in zip(bp(noise(n, 105), 500, 1800), swell_env(n, 0.0, 0.05))]
    tup = dull(n, 106, 220, 800, 0.04, 0.003, 0.009)
    return write("stickE", mix(scale(air, 0.35), tup))


def stickF():
    """thin tick: a soft, slightly brighter tick at about 0.9-1.9 kHz, 25 ms, still one contact"""
    n = int(SR * 0.04)
    return write("stickF", dull(n, 107, 900, 1900, 0.0, 0.002, 0.006, order=3))


def stampA():
    """felt thud: the heavier cousin of stickA, a very dull single thud around 250-650 Hz, 180 ms"""
    n = int(SR * 0.18)
    return write("stampA", dull(n, 111, 250, 600, 0.0, 0.005, 0.045))


def stampB():
    """tup, heavier: low-passed at about 700 Hz, a firm single thud, 150 ms"""
    n = int(SR * 0.15)
    return write("stampB", dull(n, 112, 220, 700, 0.0, 0.004, 0.03))


def stampC():
    """pat, heavier: low-passed at about 1.2 kHz, a rubber stamp meeting paper on a pad, 220 ms"""
    n = int(SR * 0.22)
    return write("stampC", dull(n, 113, 250, 1200, 0.0, 0.004, 0.045))


def stampD():
    """press, heavier: a 5 ms attack and a long soft tail, 250 ms, the stamp pushed down and held"""
    n = int(SR * 0.25)
    return write("stampD", dull(n, 114, 220, 1000, 0.0, 0.005, 0.07))


def swept(n, seed, fn, q=1.2, top=2200):
    """noise through a band-pass whose centre moves (fn(t) gives the centre in Hz at time t): the air of a 'fwoop'. A moving band of noise has no
    pitch of its own; q is low so the band is wide and never rings."""
    x = noise(n, seed)
    low = band = 0.0
    qi = 1.0 / q
    out = []
    for i in range(n):
        f = 2 * math.sin(math.pi * min(0.45 * SR, fn(i / SR)) / SR)
        low += f * band
        high = x[i] - low - qi * band
        band += f * high
        out.append(band)
    return lowpass(lowpass(out, a_of(top)), a_of(top))  # no sparkle: nothing much above top Hz


def fall(f0, f1, dur, t0=0.0):
    """centre frequency falling (or rising) exponentially from f0 to f1 over dur seconds starting at t0, then staying at f1"""
    return lambda t: f0 * (f1 / f0) ** min(1.0, max(0.0, (t - t0) / dur))


def shaped(x, env):
    return [a * b for a, b in zip(x, env)]


# Round 4: stamp as air being squeezed out from under a rubber stamp: a soft "fwoop" gesture around one dull contact, same world as stickE.
def stampF1():
    """fwoop: a quick downward sweep of air, the band falling from 1.6 kHz to 450 Hz over about 180 ms, soft attack"""
    n = int(SR * 0.2)
    return write("stampF1", shaped(swept(n, 201, fall(1600, 450, 0.18)), hit_env(n, 0.0, 0.004, 0.07)))


def stampF2():
    """fwoomp: a dull soft contact, then the air squeezed out right after it: a swell that rises and fades, 250 ms"""
    n = int(SR * 0.25)
    contact = dull(n, 202, 220, 700, 0.0, 0.004, 0.03)
    air = shaped(swept(n, 203, fall(1300, 450, 0.2, 0.01)), swell_env(n, 0.01, 0.23))
    return write("stampF2", mix(contact, scale(air, 0.9)))


def stampF3():
    """fwoo-oop: the air rises then falls, peaking about 60 ms in (the band goes up from 550 Hz to 1.3 kHz and back down to 450 Hz), 220 ms,
    and a very soft settle at the end"""
    n = int(SR * 0.24)
    up = fall(550, 1300, 0.06)
    down = fall(1300, 450, 0.15, 0.06)
    air = swept(n, 204, lambda t: up(t) if t < 0.06 else down(t))
    env = [math.sin(0.5 * math.pi * min(1.0, (i / SR) / 0.06)) ** 2 * (1.0 if i / SR < 0.06 else math.exp(-((i / SR) - 0.06) / 0.06))
           for i in range(n)]
    settle = dull(n, 205, 220, 700, 0.17, 0.005, 0.02)
    return write("stampF3", mix(shaped(air, env), scale(settle, 0.25)))


def stampF4():
    """fwip: a short, brighter burst of air, about 100 ms, the band falling from 2 kHz to 900 Hz"""
    n = int(SR * 0.1)
    return write("stampF4", shaped(swept(n, 206, fall(2000, 900, 0.07), top=3200), hit_env(n, 0.0, 0.003, 0.03)))


def stampF5():
    """heavy fwoop: the biggest one, a slower 8 ms attack and a longer fall (1.4 kHz to 420 Hz over 280 ms), about 300 ms"""
    n = int(SR * 0.3)
    return write("stampF5", shaped(swept(n, 207, fall(1400, 420, 0.28)), hit_env(n, 0.0, 0.008, 0.09)))


def stampF6():
    """fwoop and release: the air goes out, then one tiny soft 'pp' of suction as the stamp lifts. A single soft pop, not a rattle."""
    n = int(SR * 0.26)
    air = shaped(swept(n, 208, fall(1500, 480, 0.15)), hit_env(n, 0.0, 0.004, 0.06))
    pop = dull(n, 209, 400, 1500, 0.2, 0.004, 0.008)
    return write("stampF6", mix(air, scale(pop, 0.5)))


# Round 5: the stamp as air squeezed out under a stamp pressing into paper on a pad. The air is the sound; the contact is barely there (12+ dB under
# it) and has a slow attack, so nothing reads as a pop or a slap. Every envelope starts and ends at zero.
def lvl(x, db):
    """scale x so its RMS over the sounding part is 10**(db/20) (db relative to a unit-RMS reference), so contact-vs-air is set in dB"""
    m = max(abs(v) for v in x) or 1.0
    act = [v for v in x if abs(v) > 0.03 * m]
    r = math.sqrt(sum(v * v for v in act) / len(act))
    return [v * (10 ** (db / 20)) / r for v in x]


def squeeze_env(n, t0, att, tau, hold=0.0):
    """slow raised-sine attack of att seconds, optional hold, then an exponential tail"""
    out = []
    for i in range(n):
        t = i / SR - t0
        if t < 0:
            out.append(0.0)
        elif t < att:
            out.append(math.sin(0.5 * math.pi * t / att) ** 2)
        else:
            out.append(math.exp(-max(0.0, t - att - hold) / tau))
    return out


def air_g(n, seed, f0, f1, dur, t0, att, tau, hold=0.0, top=1600, q=0.9):
    return shaped(swept(n, seed, fall(f0, f1, dur, t0), q=q, top=top), squeeze_env(n, t0, att, tau, hold))


def stampG1():
    """air only: one slow squeeze of air, the band sliding from 1.3 kHz down to 500 Hz, 18 ms attack, no contact at all. 280 ms"""
    n = int(SR * 0.28)
    return write("stampG1", air_g(n, 301, 1300, 500, 0.22, 0.0, 0.018, 0.075, 0.03))


def stampG2():
    """air plus a soft thump: the same kind of squeeze, and 30 ms in a barely-there cushioned contact (300-800 Hz, 16 ms attack) 14 dB under it. 300 ms"""
    n = int(SR * 0.3)
    air = lvl(air_g(n, 302, 1300, 520, 0.24, 0.0, 0.02, 0.08, 0.03), 0)
    thump = lvl(dull(n, 303, 300, 800, 0.03, 0.016, 0.035), -14)
    return write("stampG2", mix(air, thump))


def stampG3():
    """two-stage squeeze: a first breath of air going out, a short pause, then a second, lower, softer one as the stamp settles; the contact sits
    between them, 15 dB under. 380 ms"""
    n = int(SR * 0.38)
    a1 = lvl(air_g(n, 304, 1400, 700, 0.13, 0.0, 0.016, 0.04, 0.02), 0)
    a2 = lvl(air_g(n, 305, 900, 400, 0.16, 0.16, 0.025, 0.06, 0.02, top=1200), -5)
    thump = lvl(dull(n, 306, 300, 700, 0.14, 0.02, 0.04), -15)
    return write("stampG3", mix(a1, a2, thump))


def stampG4():
    """longer sigh: a long, slow, low air, 25 ms attack, the band easing from 1.1 kHz to 380 Hz over 330 ms with a long gentle tail. No contact. 400 ms"""
    n = int(SR * 0.4)
    return write("stampG4", air_g(n, 307, 1100, 380, 0.33, 0.0, 0.025, 0.1, 0.05, top=1300, q=0.8))


def stampG5():
    """pad press: the air builds a little before it goes (rises 600 to 1.2 kHz in 70 ms, then sinks to 450 Hz), the cushion contact 18 dB under
    arrives at the peak with a 20 ms attack. 340 ms"""
    n = int(SR * 0.34)
    up, down = fall(600, 1200, 0.07), fall(1200, 450, 0.2, 0.07)
    air = swept(n, 308, lambda t: up(t) if t < 0.07 else down(t), q=0.9, top=1500)
    air = lvl(shaped(air, squeeze_env(n, 0.0, 0.022, 0.08, 0.06)), 0)
    thump = lvl(dull(n, 309, 300, 750, 0.06, 0.02, 0.045), -18)
    return write("stampG5", mix(air, thump))


def stampG6():
    """air then paper settling: a quick squeeze of air (200 ms, 12 ms attack, 1.5 kHz to 500 Hz), then the paper sinking into the pad: a very soft
    low-mid swell (500-1200 Hz) fading in as the air fades, 16 dB under. 330 ms"""
    n = int(SR * 0.33)
    air = lvl(air_g(n, 310, 1500, 500, 0.18, 0.0, 0.012, 0.06, 0.02, top=1700), 0)
    settle = lvl(shaped(bp(noise(n, 311), 500, 1200), swell_env(n, 0.1, 0.22)), -16)
    return write("stampG6", mix(air, settle))


# Round 6: G1-G6 (noise bursts with soft envelopes) all read as claps, so this round does not use shaped noise bursts. The sounds are glides: a
# sweeping resonance fed by faint breath, or a damped / gliding tone. Every one fades in over 30 ms or more with a smooth sin^2 ramp.
def glide_tone(n, f_of_t, env, noise_amt=0.0, seed=0, harm2=0.0):
    """a sine whose frequency follows f_of_t(t) (phase integrated, so no clicks), times env, with noise_amt of breath noise mixed in before the envelope"""
    ph, out = 0.0, []
    nz = noise(n, seed) if noise_amt else None
    for i in range(n):
        ph += 2 * math.pi * f_of_t(i / SR) / SR
        v = math.sin(ph) + harm2 * math.sin(2 * ph)
        if nz:
            v += noise_amt * nz[i]
        out.append(v * env[i])
    return out


def ramp_env(n, t0, att, tau, hold=0.0):
    """smooth sin^2 fade-in of att seconds from t0, a hold, then an exponential-ish fade that is itself smooth (cosine-shaped end, no knee)"""
    return squeeze_env(n, t0, att, tau, hold)


def stampH1():
    """breathy glide: faint breath noise through a very narrow resonance (Q 10) sliding 900 to 350 Hz over 200 ms, 40 ms fade-in. A pitched 'woop'. 300 ms"""
    n = int(SR * 0.3)
    x = swept(n, 401, fall(900, 350, 0.2), q=10, top=1500)
    return write("stampH1", shaped(x, squeeze_env(n, 0.0, 0.04, 0.07, 0.04)))


def stampH2():
    """soft bloop: a near-pure sine gliding 700 to 380 Hz over 220 ms with 6% breath noise, 35 ms fade-in, smooth tail. 300 ms"""
    n = int(SR * 0.3)
    f = fall(700, 380, 0.22)
    return write("stampH2", glide_tone(n, f, squeeze_env(n, 0.0, 0.035, 0.07, 0.03), 0.06, 402))


def stampH3():
    """'oo' to 'oh': two resonances (Q 8) sliding together, 1000 to 650 Hz and 600 to 420 Hz, fed by faint breath, 40 ms fade-in. 320 ms"""
    n = int(SR * 0.32)
    a = swept(n, 403, fall(1000, 650, 0.24), q=8, top=1600)
    b = swept(n, 404, fall(600, 420, 0.24), q=8, top=1600)
    return write("stampH3", shaped(mix(lvl(a, 0), lvl(b, 0)), squeeze_env(n, 0.0, 0.04, 0.08, 0.05)))


def stampH4():
    """pillow whump: a damped resonance gliding 440 to 300 Hz with a faint 2x partial (-14 dB), excited by a 30 ms ramp, no noise. 280 ms"""
    n = int(SR * 0.28)
    env = [min(1.0, (i / SR) / 0.03) for i in range(n)]
    env = [math.sin(0.5 * math.pi * e) ** 2 * math.exp(-(i / SR) / 0.07) for i, e in enumerate(env)]
    return write("stampH4", glide_tone(n, fall(440, 300, 0.2), env, 0.0, 0, harm2=0.2))


def stampH5():
    """air out of a squeezed bag: breath noise through a wide-ish sweeping band (Q 2.5, 1100 to 400 Hz) with a slow mid-envelope swell that peaks
    about 40% in (80 ms rise), 330 ms"""
    n = int(SR * 0.33)
    x = swept(n, 405, fall(1100, 400, 0.3), q=2.5, top=1400)
    env = [math.sin(math.pi * min(1.0, (i / n) ** 0.7)) ** 2 for i in range(n)]
    return write("stampH5", shaped(x, env))


def stampH6():
    """bubble: a different class, a soft water-bubble 'bloop' that rises 380 to 620 Hz over 110 ms then eases back to 540 Hz, 3% breath, 35 ms
    fade-in, 280 ms"""
    n = int(SR * 0.28)
    up, dn = fall(380, 620, 0.11), fall(620, 540, 0.17, 0.11)
    return write("stampH6", glide_tone(n, lambda t: up(t) if t < 0.11 else dn(t), squeeze_env(n, 0.0, 0.035, 0.07, 0.03), 0.03, 406, harm2=0.1))


# Round 7: built from the user's reference recording (a freesound.org "stamp" sample, decoded to ref/stamp-sample/ref.wav, mono 48 kHz, by
# `ffmpeg -i in.mp3 -ac 1 -ar 48000 ref.wav`). The reference is NOT committed or shipped; it only feeds these sound files (check its licence
# before shipping any J that still contains it). If the file is missing the J sounds are skipped.
# Measured: 648 ms; a quiet approach from 45 ms; the hard contact at 120-140 ms (peak 0.66); a body thump around 200 Hz fading by 200 ms (only
# 71% of the energy is above 250 Hz overall, the 150-300 ms part is mostly 100-250 Hz); a small tick at 375 ms and the lift at 420-460 ms
# (brighter, 1-4 kHz and more); silence from 630 ms.
REF = os.path.join(HERE, "ref", "stamp-sample", "ref.wav")


def load_ref():
    with wave.open(REF) as w:
        d = w.readframes(w.getnframes())
    return [v / 32768 for v in struct.unpack("<%dh" % (len(d) // 2), d)]


def seg(x, a_ms, b_ms):
    return x[int(a_ms * SR / 1000):int(b_ms * SR / 1000)]


def fades(x, fi, fo):
    """sin^2 fade-in of fi seconds and fade-out of fo seconds: nothing starts or ends with an edge"""
    n = len(x)
    ni, no = int(fi * SR), int(fo * SR)
    return [v * (math.sin(0.5 * math.pi * min(1.0, i / ni)) ** 2 if ni else 1.0) * (math.sin(0.5 * math.pi * min(1.0, (n - 1 - i) / no)) ** 2 if no else 1.0)
            for i, v in enumerate(x)]


def hp(x, fc, order=2):
    for _ in range(order):
        x = highpass(x, a_of(fc))
    return x


def lp(x, fc, order=2):
    for _ in range(order):
        x = lowpass(x, a_of(fc))
    return x


def resample(x, rate):
    """play x at `rate` times the speed (pitch and time together), linear interpolation"""
    out, i = [], 0.0
    while i < len(x) - 1:
        k = int(i)
        out.append(x[k] + (x[k + 1] - x[k]) * (i - k))
        i += rate
    return out


def place(n, x, t0):
    out = [0.0] * n
    i0 = int(t0 * SR)
    for j, v in enumerate(x):
        if 0 <= i0 + j < n:
            out[i0 + j] = v
    return out


def stampJ1():
    """the recording, enhanced: 85-480 ms (contact, body, small tick and lift) with the 100-250 Hz body thump filtered out (2-pole high-pass at
    300 Hz) so the console speaker has something to play, the 8 kHz-and-up hiss trimmed, faded in over 6 ms and out over 40 ms, levelled to the
    other sounds. Otherwise untouched."""
    x = lp(hp(seg(load_ref(), 85, 480), 320, 4), 9000, 1)
    return write("stampJ1", fades(x, 0.006, 0.04))


def stampJ2():
    """softened contact plus paper tail: only the contact (100-200 ms) with its click rounded (low-passed at 1.6 kHz, 12 ms fade-in) and the body
    removed (high-pass 300 Hz), then a faint rustle of paper settling after it (noise 700-2400 Hz, swelling in over 100 ms, 16 dB under)."""
    n = int(SR * 0.42)
    c = fades(hp(lp(seg(load_ref(), 100, 210), 1600), 320, 4), 0.012, 0.05)
    tail = lvl(shaped(bp(noise(n, 501), 700, 2400), swell_env(n, 0.1, 0.3)), -16)
    return write("stampJ2", mix(lvl(place(n, c, 0.0), 0), tail))


def stampJ3():
    """higher and quicker: the recording sped up 1.4x (pitch up a fourth, 290 ms), so the 200 Hz body moves to about 280 Hz and the contact is
    snappier; high-passed at 260 Hz, soft 8 ms fade-in"""
    x = lp(hp(resample(seg(load_ref(), 85, 480), 1.4), 280, 4), 7000, 1)
    return write("stampJ3", fades(x, 0.008, 0.04))


def stampJ4():
    """under an air squeeze: the recording's softened contact (low-passed at 2 kHz, high-passed at 300 Hz, 10 ms fade-in) 40 ms into a slow air
    squeeze (the G1 recipe) that is 5 dB louder than it"""
    n = int(SR * 0.4)
    c = fades(hp(lp(seg(load_ref(), 100, 230), 2000), 320, 4), 0.01, 0.06)
    air = lvl(air_g(n, 502, 1300, 520, 0.24, 0.0, 0.025, 0.08, 0.03), 0)
    return write("stampJ4", mix(air, lvl(place(n, c, 0.04), -5)))


def stampJ5():
    """resynthesis: the recording's own loudness curve (5 ms steps, smoothed so no edge is left) applied to our noise: 300-2500 Hz noise for the
    contact, 1.2-5 kHz noise for the lift. No sample audio in it, only its shape. 400 ms"""
    r = seg(load_ref(), 85, 485)
    n = len(r)
    wn = int(0.005 * SR)
    env = [0.0] * n
    for i in range(0, n - wn, wn):
        e = math.sqrt(sum(v * v for v in r[i:i + wn]) / wn)
        for j in range(wn):
            env[i + j] = e
    env = lowpass(lowpass(env, a_of(70)), a_of(70))
    split = int(0.27 * SR)
    a = bp(noise(n, 503), 300, 2500)
    b = bp(noise(n, 504), 1200, 5000)
    x = [(a[i] if i < split else 0.7 * b[i]) * env[i] for i in range(n)]
    return write("stampJ5", fades(x, 0.01, 0.04))


def stampJ6():
    """stamp, then a peel: the softened contact (as J2), then the recording's lift (365-470 ms) slowed to 0.8x, low-passed at 3.5 kHz and
    high-passed at 400 Hz, like tape coming up, 8 dB under the contact"""
    ref = load_ref()
    n = int(SR * 0.5)
    c = fades(hp(lp(seg(ref, 100, 210), 1700), 320, 4), 0.012, 0.05)
    lift = fades(hp(lp(resample(seg(ref, 365, 480), 0.8), 3500), 420, 3), 0.02, 0.05)
    return write("stampJ6", mix(lvl(place(n, c, 0.0), 0), lvl(place(n, lift, 0.17), -8)))


def _j3_core(end_ms):
    """J3's chain (sped up 1.4x, high-passed at 280 Hz, 7 kHz low-pass) on the reference from 85 ms to end_ms"""
    return lp(hp(resample(seg(load_ref(), 85, end_ms), 1.4), 280, 4), 7000, 1)


# Why J3 sounded cut off: it used 85-480 ms of the reference, but the lift there peaks at 445 ms and its decay runs on to about 560 ms (small
# ticks to 600 ms). The cut at 480 ms (200 ms after speeding up) landed on the lift's falling side and the 40 ms fade made it a clipped stop.
def stampJ3b():
    """J3 with the lift and its decay intact: reference 85-600 ms, same 1.4x speed-up and filters, 8 ms fade-in, 80 ms fade-out over the natural
    decay (it is already about 40 dB down by then)"""
    return write("stampJ3b", fades(_j3_core(600), 0.008, 0.08))


def stampJ3c():
    """J3b plus a slightly longer gentle tail: about 80 ms more of faint 1-4.5 kHz air fading out smoothly after the lift (starts about 30 dB under it)"""
    x = _j3_core(600)
    pad = int(0.08 * SR)
    n = len(x) + pad
    x = x + [0.0] * pad
    t0 = int(0.26 * SR)
    nz = bp(noise(n, 601), 1000, 4500)
    tail = [0.0] * n
    for i in range(t0, n):
        t = (i - t0) / SR
        tail[i] = nz[i] * math.exp(-t / 0.07) * min(1.0, t / 0.02)
    tail = lvl(tail, -30)
    m = max(abs(v) for v in x)
    act = [v for v in x if abs(v) > 0.03 * m]
    ref_rms = math.sqrt(sum(v * v for v in act) / len(act))
    return write("stampJ3c", fades(mix(x, scale(tail, ref_rms)), 0.008, 0.1))


def stampJ3d():
    """J3b with the contact about 3 dB softer (x0.7) and the lift about 1.5 dB fuller (x1.2), the gain moving smoothly between them"""
    x = _j3_core(600)
    out = []
    for i, v in enumerate(x):
        t = i / SR
        k = 0.5 - 0.5 * math.cos(math.pi * min(1.0, max(0.0, (t - 0.10) / 0.08)))
        out.append(v * (0.7 + 0.5 * k))
    return write("stampJ3d", fades(out, 0.008, 0.08))


def chime():
    """done, softened: two warm marimba notes a fifth apart, lower than before, a soft attack, almost no overtone"""
    n = int(SR * 0.8)
    out = [0.0] * n
    for start, f in ((0.0, 784.0), (0.13, 1174.7)):
        for i in range(int(start * SR), n):
            t = i / SR - start
            att = min(1.0, t / 0.012)
            out[i] += (math.sin(2 * math.pi * f * t) + 0.08 * math.sin(2 * math.pi * f * 2 * t)) * math.exp(-t / 0.17) * att
    return write("done", lowpass(out, 0.5), 0.5)


def tick():
    """one step of a bar filling: a tiny soft marimba blip (the page speeds it up and raises it with playbackRate)"""
    n = int(SR * 0.07)
    f = 660.0
    out = [(math.sin(2 * math.pi * f * (i / SR)) + 0.1 * math.sin(2 * math.pi * f * 2 * (i / SR))) * math.exp(-(i / SR) / 0.016)
           * min(1.0, (i / SR) / 0.003) for i in range(n)]
    return write("tick", lowpass(out, 0.55), 0.5)


def fill_end():
    """the last note of a filled bar: 'do!', a little longer and a fourth above the ticks"""
    n = int(SR * 0.4)
    f = 1046.5
    out = [(math.sin(2 * math.pi * f * (i / SR)) + 0.12 * math.sin(2 * math.pi * f * 2 * (i / SR))) * math.exp(-(i / SR) / 0.11)
           * min(1.0, (i / SR) / 0.006) for i in range(n)]
    return write("fillend", lowpass(out, 0.55), 0.55)


def slip(k):
    """one stick-slip of tape letting go of its roll: a tiny rounded burst of paper noise, about as bright as the peel but much shorter, with a
    faint low pat under it. The page plays a run of these (four variants, so it never machine-guns), faster as the roll speeds up, from a slow
    tap-tap to a rasp, and a little higher as the bar fills (playbackRate)."""
    n = int(SR * 0.016)
    nz = lowpass(highpass(noise(n, 20 + k), 0.07), 0.28)
    out = []
    for i in range(n):
        t = i / SR
        env = min(1.0, t / 0.001) * math.exp(-t / 0.003)
        out.append(nz[i] * env + 0.1 * math.sin(2 * math.pi * (170 + 10 * k) * t) * math.exp(-t / 0.005))
    return write("slip%d" % k, out, 0.4)


def tear():
    """the end of a strip, torn along its zigzag: a short quick rip with five or six teeth in it that opens up brighter and dies away"""
    n = int(SR * 0.17)
    nz = highpass(noise(n, 30), 0.06)
    out, y = [], 0.0
    for i in range(n):
        t = i / SR
        teeth = 0.5 + 0.5 * math.sin(2 * math.pi * 36 * t + 1.2) ** 2
        env = min(1.0, t / 0.004) * math.exp(-t / 0.055)
        y += (0.2 + 0.3 * min(1.0, t / 0.06)) * (nz[i] - y)
        out.append(y * teeth * env)
    return write("tear", lowpass(out, 0.7), 0.6)


def crinkle():
    """a small crumple where the tape was pinched: a handful of tiny uneven crackles, rounded off so it is a rustle and not a hiss"""
    n = int(SR * 0.13)
    r = random.Random(40)
    nz = lowpass(highpass(noise(n, 41), 0.08), 0.2)
    out = [0.0] * n
    t = 0.0
    while t < 0.11:
        i0, ln, amp = int(t * SR), int(SR * r.uniform(0.002, 0.005)), r.uniform(0.3, 1.0)
        for j in range(ln):
            if i0 + j < n:
                out[i0 + j] += nz[i0 + j] * amp * math.exp(-j / (SR * 0.0016)) * min(1.0, j / (SR * 0.0005))
        t += r.uniform(0.007, 0.022)
    return write("crinkle", out, 0.45)


def pop(k):
    """a key of the keypad or keyboard taking the focus: one small soft pop, far quieter and shorter than a tape peel and tap (there are many keys close
    together, and the peel and tap pair wore on the ear). A round little blip that falls about six semitones in 30 ms, with a whisper of paper
    noise in its first 3 ms so it has a click to it and no hiss. Two variants a little apart in pitch, so a run of them is not a machine."""
    n = int(SR * 0.055)
    f0, f1 = (660.0, 470.0) if k == 0 else (590.0, 420.0)
    nz = bp(noise(n, 50 + k), 1500, 4800)
    out, phase = [], 0.0
    for i in range(n):
        t = i / SR
        phase += (f1 + (f0 - f1) * math.exp(-t / 0.011)) / SR
        blip = math.sin(2 * math.pi * phase) * math.exp(-t / 0.012) * min(1.0, t / 0.0012)
        click = nz[i] * math.exp(-t / 0.0022) * min(1.0, t / 0.0004)
        out.append(blip + 0.45 * click)
    return write("pop%d" % k, lowpass(out, 0.6), 0.5)


def swish():
    """a page turning to the next screen: noise whose brightness sweeps up and back"""
    n = int(SR * 0.26)
    nz = noise(n, 5)
    out, y = [], 0.0
    for i in range(n):
        t = i / n
        a = 0.04 + 0.5 * math.sin(math.pi * t) ** 1.5
        y += a * (nz[i] - y)
        out.append(y * math.sin(math.pi * t) ** 0.8)
    return write("swish", out, 0.6)


def bonk():
    """a refusal: two soft low-mid taps (about 390 and 300 Hz; it used to be 150 and 112 Hz, which the Switch's speakers do not reproduce)"""
    n = int(SR * 0.3)
    out = [0.0] * n
    for start, f in ((0.0, 390), (0.1, 300)):
        for i in range(int(start * SR), n):
            t = i / SR - start
            out[i] += (math.sin(2 * math.pi * f * t) + 0.3 * math.sin(2 * math.pi * 2 * f * t)) * math.exp(-t / 0.04) * min(1.0, t / 0.002)
    return write("refuse", out, 0.7)


def riffle():
    """cards being shuffled (a list put in a new order): a quick run of soft paper flaps, quickening and then easing off, each a tiny burst of
    band-limited paper noise with a little low-mid body, uneven in loudness and spacing, then the cards squared up with one soft tap. About as
    long as the shuffle on screen (half a second)."""
    n = int(SR * 0.52)
    r = random.Random(70)
    nz = bp(noise(n, 71), 700, 5200)
    body = bp(noise(n, 72), 250, 1200)
    out = [0.0] * n
    t, k, N = 0.012, 0, 22
    while k < N and t < 0.40:
        u = k / (N - 1)
        gap = 0.024 - 0.013 * math.sin(math.pi * min(1.0, u * 1.15))  # quickens, then eases off
        amp = r.uniform(0.55, 1.0) * (0.6 + 0.4 * math.sin(math.pi * u))
        i0, ln = int(t * SR), int(SR * 0.012)
        tau = r.uniform(0.0016, 0.0032)
        for j in range(ln):
            if i0 + j < n:
                e = math.exp(-j / (SR * tau)) * min(1.0, j / (SR * 0.0005))
                out[i0 + j] += amp * e * (nz[i0 + j] + 0.5 * body[i0 + j])
        t += gap * r.uniform(0.8, 1.2)
        k += 1
    t0 = t + 0.035  # squared up
    for i in range(int(t0 * SR), n):
        tt = i / SR - t0
        out[i] += 0.9 * (0.7 * body[i] + 0.4 * nz[i]) * math.exp(-tt / 0.012) * min(1.0, tt / 0.0012)
    return write("riffle", lowpass(out, 0.55), 0.5)


if __name__ == "__main__":
    for fn in (peel, stick, stick_b, stick_c, stamp, stamp_b, stamp_c, stick_box, stickA, stickB, stickC, stickD, stickE, stickF, stampA, stampB, stampC, stampD, stampF1, stampF2, stampF3, stampF4, stampF5, stampF6, stampG1, stampG2, stampG3, stampG4, stampG5, stampG6, stampH1, stampH2, stampH3, stampH4, stampH5, stampH6, stampJ1, stampJ2, stampJ3, stampJ4, stampJ5, stampJ6, stampJ3b, stampJ3c, stampJ3d, chime, tick, fill_end, swish, bonk, tear, crinkle, riffle):
        d, r, p = fn()
        print(f"{fn.__name__:10s} {d:.2f}s  rms {r:.3f}  peak {p:.2f}")
    for k in range(4):
        d, r, p = slip(k)
        print(f"slip{k:<6d} {d:.3f}s  rms {r:.3f}  peak {p:.2f}")
    for k in range(2):
        d, r, p = pop(k)
        print(f"pop{k:<7d} {d:.3f}s  rms {r:.3f}  peak {p:.2f}")
