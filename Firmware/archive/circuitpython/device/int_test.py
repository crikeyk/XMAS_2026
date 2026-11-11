# int_test.py - SC7A20 INT1 / INT2 hardware check: which level is "active", and are the pins driven or released?
#
# HOW TO RUN (pick one):
#   1. Put   INT_TEST = yes   in settings.txt. code.py runs this test at boot, then carries on as normal.
#      Set it back to  no  afterwards, or it will run on every boot.
#   2. Or save this file on the drive as code.py (keep a copy of the real code.py first).
#
# Two trials. Each one: configure BOTH interrupt generators (INT1 and INT2) to fire on motion, take a "before"
# reading while the board is still, go into a soft "sleep" (LEDs off, both pins sampled every 20 ms), and when you
# shake the board take a "wake" reading, clear the latch, then watch the pins recover ("after").
#   Trial A: H_LACTIVE = 0 (datasheet: interrupt active HIGH)
#   Trial B: H_LACTIVE = 1 (datasheet: interrupt active LOW)
# For every reading each pin is read with no pull, with a pull-up and with a pull-down:
#   same value with both pulls  -> the chip is DRIVING the pin (push-pull / strongly driven)
#   value follows the pull      -> the pin is RELEASED (hi-Z / open-drain off)
# Every line it prints starts with INT>   Paste them all back.
import time
import digitalio
import microcontroller

THS = 2                 # INT threshold register value: 128 mg per step at +/-16 g, so 0.256 g
XYZ_THR = 40            # counts (about 0.3 g): independent check that the board really moved
SLEEP_TIMEOUT = 60.0    # seconds to wait for a shake in each trial


def run(leds, acc):
    spi = acc._spi
    cs = acc._cs
    buf1 = bytearray(1)
    buf6 = bytearray(6)
    cmd6 = bytes([0x28 | 0xC0])
    cath = leds._cath
    names = ("INT1 (GPIO23)", "INT2 (GPIO21)")

    pins = []
    try:
        for pn in (microcontroller.pin.GPIO23, microcontroller.pin.GPIO21):
            d = digitalio.DigitalInOut(pn)
            d.switch_to_input(pull=None)
            pins.append(d)
    except Exception as e:
        print("INT> PROBLEM: cannot open the INT pins (%s: %s)" % (type(e).__name__, e))
        for d in pins:
            d.deinit()
        return
    p1, p2 = pins

    def leds_off():
        for p in cath:
            p.value = True

    def leds_on():
        for p in cath:
            p.value = False

    def rr(reg):
        cs.value = False
        spi.write(bytes([0x80 | reg]))
        spi.readinto(buf1)
        cs.value = True
        return buf1[0]

    def wr(reg, val):
        acc._write(reg, val)

    def s16(lo, hi):
        v = (hi << 8) | lo
        if v & 0x8000:
            v -= 65536
        return v >> 4

    def xyz():
        cs.value = False
        spi.write(cmd6)
        spi.readinto(buf6)
        cs.value = True
        return (s16(buf6[0], buf6[1]), s16(buf6[2], buf6[3]), s16(buf6[4], buf6[5]))

    def levels():
        return (1 if p1.value else 0, 1 if p2.value else 0)

    def probe(p):
        p.pull = None
        time.sleep(0.002)
        n = 1 if p.value else 0
        p.pull = digitalio.Pull.UP
        time.sleep(0.002)
        u = 1 if p.value else 0
        p.pull = digitalio.Pull.DOWN
        time.sleep(0.002)
        d = 1 if p.value else 0
        p.pull = None
        return (n, u, d)

    def classify(t):
        if t[1] == t[2]:
            return ("DRV", t[1])
        return ("REL", None)

    def desc(t):
        k, lv = classify(t)
        if k == "DRV":
            return "%s, driven (none=%d up=%d down=%d)" % ("HIGH" if lv else "LOW", t[0], t[1], t[2])
        return "RELEASED/hi-Z (none=%d up=%d down=%d)" % (t[0], t[1], t[2])

    def src_text(v):
        t = ""
        for bit, nm in ((6, "IA"), (5, "ZH"), (4, "ZL"), (3, "YH"), (2, "YL"), (1, "XH"), (0, "XL")):
            if v & (1 << bit):
                t += nm + " "
        return "0x%02X [%s]" % (v, t.strip() or "none")

    def configure(active_low):
        wr(0x22, 0x00)                      # CTRL_REG3: nothing routed to INT1 while we set up
        wr(0x25, 0x00)                      # CTRL_REG6: nothing routed to INT2
        wr(0x20, 0x77)                      # 400 Hz, X/Y/Z on (same as code.py)
        wr(0x23, 0x88 | (3 << 4))           # block update, high-res, +/-16 g (same as code.py)
        wr(0x21, 0x03)                      # CTRL_REG2: HPIS1 + HPIS2 = high-pass filter feeds both comparators (readings unfiltered)
        wr(0x24, 0x0A)                      # CTRL_REG5: LIR_INT1 + LIR_INT2 = latch both until their SOURCE register is read
        wr(0x32, THS); wr(0x33, 1); wr(0x30, 0x2A)    # generator 1: OR of X/Y/Z high, 1 sample
        wr(0x36, THS); wr(0x37, 1); wr(0x34, 0x2A)    # generator 2: same
        wr(0x22, 0x40)                      # CTRL_REG3: I1_AOI1 -> INT1 pin
        # CTRL_REG6: I2_INT1 (0x40) + I2_INT2 (0x20) -> INT2 pin carries both generators (the datasheet text for these two
        # bits is ambiguous, so enable both); bit 1 = H_LACTIVE
        wr(0x25, 0x60 | (0x02 if active_low else 0x00))

    def arm():
        for _ in range(10):
            rr(0x26)                        # REFERENCE: reading it resets the high-pass filter
            time.sleep(0.03)
            rr(0x31)
            rr(0x35)                        # reading the SOURCE registers clears the latches
            time.sleep(0.005)
            a = rr(0x31)
            b = rr(0x35)
            if not (a & 0x40) and not (b & 0x40):
                return True
        return False

    def verdict(i, b_lv, b_pr, w_lv, w_pr):
        bk, bl = classify(b_pr[i])
        wk, wl = classify(w_pr[i])
        if b_pr[i][1:] == w_pr[i][1:]:
            return "%s: NO CHANGE between before and wake (before %s | wake %s)" % (names[i], desc(b_pr[i]), desc(w_pr[i]))
        if bk == "DRV" and wk == "DRV":
            return "%s: push-pull style. idle %s, active %s  => ACTIVE %s" % (
                names[i], "HIGH" if bl else "LOW", "HIGH" if wl else "LOW", "HIGH" if wl else "LOW")
        if bk == "REL" and wk == "DRV":
            return "%s: open-drain style. idle RELEASED, active driven %s  => ACTIVE %s (needs a pull-%s)" % (
                names[i], "HIGH" if wl else "LOW", "HIGH" if wl else "LOW", "down" if wl else "up")
        return "%s: idle %s | active %s" % (names[i], desc(b_pr[i]), desc(w_pr[i]))

    results = []

    def trial(label, active_low):
        print("INT> ===== TRIAL %s: H_LACTIVE=%d (datasheet says active %s) ====="
              % (label, 1 if active_low else 0, "LOW" if active_low else "HIGH"))
        configure(active_low)
        print("INT> hold the board STILL for the next 2 seconds")
        time.sleep(1.0)
        if not arm():
            print("INT> note: the interrupt flag (IA) would not clear at rest - continuing anyway")
        time.sleep(0.2)

        # ---------------- BEFORE ----------------
        b_lv = levels()
        b_pr = (probe(p1), probe(p2))
        print("INT> BEFORE (still): INT1=%d INT2=%d" % (b_lv[0], b_lv[1]))
        print("INT>   %s -> %s" % (names[0], desc(b_pr[0])))
        print("INT>   %s -> %s" % (names[1], desc(b_pr[1])))
        print("INT>   INT1_SOURCE=%s INT2_SOURCE=%s" % (src_text(rr(0x31)), src_text(rr(0x35))))
        sleep_pull = [None, None]
        for i in (0, 1):
            if classify(b_pr[i])[0] == "REL":
                sleep_pull[i] = digitalio.Pull.UP
                print("INT>   note: %s is released at idle, so it is sampled with a pull-up while asleep" % names[i])
        p1.pull = sleep_pull[0]
        p2.pull = sleep_pull[1]
        time.sleep(0.005)
        base = levels()
        x0, y0, z0 = xyz()

        # ---------------- DURING (soft sleep) ----------------
        leds_off()
        print("INT> SLEEPING (LEDs off, sampling both pins every 20 ms). SHAKE the board to wake it. Timeout %d s." % SLEEP_TIMEOUT)
        t0 = time.monotonic()
        t_move = None
        t_pin = None
        last_hb = t0
        why = "timeout"
        while True:
            time.sleep(0.02)
            now = time.monotonic()
            lv = levels()
            cx, cy, cz = xyz()
            if t_move is None and (abs(cx - x0) > XYZ_THR or abs(cy - y0) > XYZ_THR or abs(cz - z0) > XYZ_THR):
                t_move = now
                print("INT>   t=%.2fs: motion seen in x/y/z (%d/%d/%d counts)" % (now - t0, cx - x0, cy - y0, cz - z0))
            if lv != base and t_pin is None:
                t_pin = now
                print("INT>   t=%.2fs: PIN CHANGE  INT1 %d->%d  INT2 %d->%d" % (now - t0, base[0], lv[0], base[1], lv[1]))
                why = "pin change"
                break
            if t_move is not None and now - t_move > 0.5:
                why = "motion but NO pin change within 0.5 s"
                break
            if now - last_hb > 2.0:
                last_hb = now
                print("INT>   t=%.0fs still asleep: INT1=%d INT2=%d" % (now - t0, lv[0], lv[1]))
            if now - t0 > SLEEP_TIMEOUT:
                break
        print("INT> WAKE trigger: %s" % why)

        # fast timeline of both pins for 150 ms after the trigger
        tw = time.monotonic()
        last = levels()
        events = []
        while time.monotonic() - tw < 0.15:
            lv = levels()
            if lv != last:
                events.append(((time.monotonic() - tw) * 1000.0, lv))
                last = lv
        if events:
            for ms, lv in events:
                print("INT>   +%.1f ms: INT1=%d INT2=%d" % (ms, lv[0], lv[1]))
        else:
            print("INT>   no further pin changes in the 150 ms after the trigger")

        # ---------------- WAKE ----------------
        w_lv = levels()
        w_pr = (probe(p1), probe(p2))
        p1.pull = sleep_pull[0]
        p2.pull = sleep_pull[1]
        time.sleep(0.005)
        leds_on()
        time.sleep(0.1)
        leds_off()
        s1 = rr(0x31)                       # this read clears the INT1 latch
        s2 = rr(0x35)                       # this read clears the INT2 latch
        t_clr = time.monotonic()
        print("INT> WAKE (latch still set, before clearing): INT1=%d INT2=%d" % (w_lv[0], w_lv[1]))
        print("INT>   %s -> %s" % (names[0], desc(w_pr[0])))
        print("INT>   %s -> %s" % (names[1], desc(w_pr[1])))
        print("INT>   INT1_SOURCE=%s INT2_SOURCE=%s (read now = latch cleared)" % (src_text(s1), src_text(s2)))

        # ---------------- AFTER ----------------
        print("INT> AFTER clearing - keep the board still now")
        marks = (0, 2, 10, 50, 200, 1000)
        got = []
        for ms in marks:
            while (time.monotonic() - t_clr) * 1000.0 < ms:
                pass
            got.append((ms, levels()))
        for ms, lv in got:
            print("INT>   +%4d ms: INT1=%d INT2=%d" % (ms, lv[0], lv[1]))
        print("INT>   INT1_SOURCE=%s INT2_SOURCE=%s" % (src_text(rr(0x31)), src_text(rr(0x35))))
        a_pr = (probe(p1), probe(p2))
        print("INT>   settled: %s -> %s | %s -> %s" % (names[0], desc(a_pr[0]), names[1], desc(a_pr[1])))

        for i in (0, 1):
            results.append("TRIAL %s (H_LACTIVE=%d) %s" % (label, 1 if active_low else 0, verdict(i, b_lv, b_pr, w_lv, w_pr)))

    try:
        trial("A", False)
        print("INT> trial B in 4 seconds - put the board down and let it settle")
        time.sleep(4.0)
        trial("B", True)
    except Exception as e:
        print("INT> PROBLEM: test stopped (%s: %s)" % (type(e).__name__, e))

    # leave the sensor quiet so normal code starts from a clean state
    try:
        wr(0x22, 0x00)
        wr(0x25, 0x00)
        wr(0x24, 0x00)
        wr(0x21, 0x00)
        wr(0x30, 0x00)
        wr(0x34, 0x00)
        rr(0x31)
        rr(0x35)
    except Exception:
        pass
    leds_off()
    for d in pins:
        d.deinit()
    print("INT> ===== SUMMARY =====")
    for r in results:
        print("INT> " + r)
    print("INT> ===== end of INT test =====")


if __name__ == "__main__":
    from leds import Leds
    from accel import Accel
    _l = Leds("gpio")
    _a = Accel()
    run(_l, _a)
