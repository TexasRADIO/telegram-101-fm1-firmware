// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// The FM-1 USB-MIDI update protocol over WebMIDI (after tools/fm1_install.py).
// Step 1: the running firmware (official
// or Felucca) reads parts of the package and stages the update loader. Step 2:
// the loader reads the whole image and writes it. Both steps are "the device
// asks, we answer": cmd 0x30 read requests on the logical image.

const HS_QUERY = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const HS_FULL = [0xF0, 0x00, 0x32, 0x45, 0x08, 0x00, 0x00, 0x40, 0x00, 0x7E, 0x01, 0xF7];
const BB1_PLAIN_ID = "FM-1_922";
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
const MAXDATA = 512;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const IS_OTA = (id) => /^ota-/i.test(id.model);

// errors carry a code the pages translate: notfound, model, stopped, noloader, lost, noreturn,
// restartUnconfirmed, mismatch (detail: the identity the device reports), badreq
function fail(code, msg, detail = "") { const e = new Error(msg); e.code = code; e.detail = detail; return e; }

// MIDIPort.close finishes untimestamped queued sends before resolving while
// connected. Close output first; never clear the final success/restart packet.
export async function releaseMidiPorts(input, output, {closeInput = true, closeOutput = true, timeoutMs = 3000} = {}) {
  let failure;
  for (const [port, owned] of [[output, closeOutput], [input, closeInput]]) {
    if (!owned || typeof port?.close !== "function" || port.connection === "closed") continue;
    let timer;
    try {
      await Promise.race([Promise.resolve().then(() => port.close()), new Promise((_, reject) => {
        timer = setTimeout(() => reject(fail("portclose", "The browser did not release the MIDI port.")), timeoutMs);
      })]);
    } catch (error) {
      // A disappearing device may reject close; the old port must still be
      // explicitly released, but absence is not a new transfer failure.
      if (port.state !== "disconnected" && !failure)
        failure = error?.code === "portclose" ? error : fail("portclose", "The browser could not release the MIDI port.");
    } finally { clearTimeout(timer); }
  }
  if (failure) throw failure;
}

export function pack7(data) {
  const out = []; let acc = 0, nb = 0;
  for (const b of data) {
    acc |= b << nb; nb += 8;
    while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7F);
  return out;
}

export function unpack7(s) {
  const out = []; let acc = 0, nb = 0;
  for (const b of s) {
    acc |= (b & 0x7F) << nb; nb += 7;
    while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
  }
  return out;
}

function response(addr, data, fl = 0) {
  const len = data.length;
  const body = [0x00, 0x59, 0x30, (len + 8) & 0xFF, ((len + 8) >> 8) & 0xFF, 0, fl,
    addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
    len & 0xFF, (len >> 8) & 0xFF, 0, ...data];
  let s = 0;
  for (let i = 6; i < body.length; i++) s += body[i];
  body.push(~s & 0xFF);
  return [0xF0, ...pack7(body), 0xF7];
}

function parseRequest(pkt) {
  if (pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const u = unpack7(pkt.slice(1, -1));
  if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  let s = 0;
  for (let i = 6; i < 14; i++) s += u[i];
  if ((~s & 0xFF) !== u[14]) return null;
  return { fl: u[6], addr: (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0, len: u[11] | (u[12] << 8) | (u[13] << 16) };
}

function parseIdentity(pkt) {
  const d = unpack7(pkt.slice(1, -1));
  if (d.length !== 34 || d[0] !== 0 || d[1] !== 0x59 || d[2] !== 0x11) return null;
  const body = d.slice(6, 33); const zero = body.indexOf(0);
  const txt = String.fromCharCode(...body.slice(0, zero < 0 ? body.length : zero));
  if (txt === BB1_PLAIN_ID) return { text: txt, model: "FM-1", version: null };
  const m = /^([^_]+)_(\d+)$/.exec(txt);
  return m ? { text: txt, model: m[1], version: parseInt(m[2], 10) } : null;
}

// one MIDI in/out pair with a SysEx queue
class Link {
  constructor(input, output) {
    this.input = input; this.output = output; this.q = []; this.waiter = null;
    this.onMessage = (ev) => {
      if (ev.data[0] !== 0xF0) return;
      this.q.push(Array.from(ev.data));
      if (this.waiter) { const w = this.waiter; this.waiter = null; w(); }
    };
    input.onmidimessage = this.onMessage;
  }
  get lost() { return this.input.state === "disconnected" || this.output.state === "disconnected"; }
  // false when the port is gone (unplugged): WebMIDI throws on a disconnected output
  send(bytes) {
    try { this.output.send(bytes); return true; } catch (_) { return false; }
  }
  drain() { this.q.length = 0; }
  async read(ms) {
    if (!this.q.length) {
      let tm;
      await Promise.race([new Promise((r) => { this.waiter = r; }), new Promise((r) => { tm = setTimeout(r, ms); })]);
      clearTimeout(tm);
      this.waiter = null;
    }
    return this.q.shift() || null;
  }
  // Detach only: callers handing these already-open ports to MidiConsole must
  // not race an asynchronous physical close against the new consumer.
  close() { if (this.input.onmidimessage === this.onMessage) this.input.onmidimessage = null; }
  async release() {
    this.close();
    if (!this.releasing) this.releasing = releaseMidiPorts(this.input, this.output);
    await this.releasing;
  }
}

async function ask(link, query, ms) {
  if (!link.send(query)) return false;
  const end = Date.now() + ms;
  while (Date.now() < end) {
    const p = await link.read(end - Date.now());
    if (!p) break;
    const id = parseIdentity(p);
    if (id) return id;
  }
  return null;
}

async function handshake(link, tries = 3, trace = () => {}) {
  link.drain();
  for (let t = 0; t < tries; t++) {
    trace(`identity query plain ${t + 1}/${tries}`);
    const id = await ask(link, HS_QUERY, 1000);
    if (id === false) { trace("identity query send failed"); return null; }
    if (!id) { trace("identity reply timeout"); continue; }
    trace(`identity reply ${id.text}`);
    const abbreviated = id.text === BB1_PLAIN_ID || /^[^_]+_\d{3}$/.test(id.text);
    if (IS_OTA(id) || !abbreviated) return id;
    trace("identity query full");
    const full = await ask(link, HS_FULL, 600);
    trace(full ? `identity full reply ${full.text}` : "identity full reply timeout");
    const agrees = full && (id.text === BB1_PLAIN_ID ? /^FM-1_922\d+$/.test(full.text) : full.text.startsWith(id.text));
    return full && full.model === id.model && agrees ? full : id;
  }
  return null;
}

export class Updater {
  constructor(access, log) {
    this.access = access;
    this.log = log || (() => {});
  }

  // every in/out pair with the same port name that answers the handshake
  async find(filter) {
    const outs = [...this.access.outputs.values()].filter((port) => port.state !== "disconnected");
    const inputs = [...this.access.inputs.values()].filter((port) => port.state !== "disconnected" &&
      /fm-1|big beat|compas|compás|felucca|ota|composite|sinco|usb-midi|choralroot|fm1|fm 1|m[- _]?vave|usb[ _]midi|minifm/i.test(port.name || ""));
    // Web MIDI names are not physical identities. With duplicate live ports
    // there is no safe way to pair input and output by name.
    const pairs = [];
    for (const input of inputs) {
      const matching = outs.filter((output) => output.name === input.name);
      if (!matching.length) continue;
      if (matching.length !== 1 || inputs.some((other) => other !== input && other.name === input.name))
        throw fail("ambiguous", "Multiple MIDI ports share the same name. Connect one FM-1 and retry.", input.name);
      pairs.push([input, matching[0]]);
    }
    const matches = [];
    for (const [input, output] of pairs) {
      const inputWasOpen = input.connection === "open" || input.connection === "pending";
      const outputWasOpen = output.connection === "open" || output.connection === "pending";
      const previousHandler = input.onmidimessage;
      const releaseProbe = () => releaseMidiPorts(input, output, {closeInput: !inputWasOpen, closeOutput: !outputWasOpen});
      try { await input.open(); await output.open(); } catch (_) { await releaseProbe(); continue; }
      const link = new Link(input, output);
      const discard = async () => {
        link.close();
        // Discovery borrows an already-open unrelated port; restore its owner
        // rather than closing that device's existing MIDI session.
        if (inputWasOpen && previousHandler) input.onmidimessage = previousHandler;
        await releaseProbe();
      };
      const id = await handshake(link, 2, (message) => this.log(`MIDI ${input.name || "(unnamed)"}: ${message}`));
      const compatible = !!(id && /^(ota-)?fm-1$/i.test(id.model));
      if (!compatible && id) {
        this.log(`MIDI ${input.name || "(unnamed)"}: rejected non-FM-1 responder ${id.text}`);
      }
      // The host-provided port label is only a discovery hint.  A stale label such as
      // "ChoralRoot FM-1" must never make another product eligible for an FM-1 write.
      if (compatible && (!filter || filter(id, { input, output, name: input.name })))
        matches.push({ link, id, name: input.name, discard });
      else await discard();
    }
    if (matches.length > 1) {
      for (const match of matches) await match.discard();
      throw fail("multiplefm1", "More than one device matches this install stage. Select the intended device and retry.",
        matches.map((match) => match.name).join(" / "));
    }
    if (!matches.length) return null;
    const selected = matches[0];
    return {link: selected.link, id: selected.id, name: selected.name};
  }

  async waitFor(filter, ms) {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      const dev = await this.find(filter);
      if (dev) return dev;
      await sleep(1000);
    }
    return null;
  }

  async confirmRestart(product, ms) {
    const [model] = product.split("_");
    let familySeen = false;
    const back = await this.waitFor(id => {
      if (id.model !== model) return false;
      if (id.text !== BB1_PLAIN_ID) return true;
      familySeen = true;
      return false; // A family reply can precede a working full identity query.
    }, ms);
    if (!back) {
      if (familySeen) throw fail("restartUnconfirmed", "The FM-1 returned on MIDI but did not confirm its full identity.");
      throw fail("noreturn", "The FM-1 did not return on MIDI.");
    }
    if (back.id.text !== product) {
      await back.link.release();
      throw fail("mismatch", `Expected ${product}; found ${back.id.text}`, back.id.text);
    }
    back.link.close();
    return {input: back.link.input, output: back.link.output};
  }

  // serve read requests until the device asks for one of the finish addresses; stops early
  // when the port disappears (lost: true) instead of waiting out idleMs
  async serve(link, image, finishAddr, idleMs, onProgress) {
    let served = 0, ignored = 0, last = Date.now();
    for (;;) {
      const pkt = await link.read(1000);
      if (!pkt) {
        if (link.lost) return { served, finished: false, lost: true };
        if (Date.now() - last > idleMs) return { served, finished: false };
        continue;
      }
      const r = parseRequest(pkt);
      if (!r) {
        if (ignored++ < 8) this.log(`MIDI update ignored packet (${finishAddr.toString(16)}): ${Array.from(pkt).slice(0, 48).map(value => value.toString(16).padStart(2, "0")).join(" ")}`);
        continue;
      }
      last = Date.now();
      if (r.addr === 0xE0000000 || r.addr === 0xF0000000) {
        if (r.addr !== finishAddr)
          throw fail("badreq", "The FM-1 reported completion for the wrong update stage.");
        if (!link.send(response(r.addr, [...new TextEncoder().encode("success"), 0])))
          return { served, finished: false, lost: true };
        return { served, finished: true };
      }
      if (r.len > MAXDATA || r.addr + r.len > image.length) throw fail("badreq", `bad request ${r.addr.toString(16)}+${r.len}`);
      await sleep(10);
      if (!link.send(response(r.addr, Array.from(image.subarray(r.addr, r.addr + r.len)), r.fl))) {
        return { served, finished: false, lost: true };
      }
      served++;
      if (onProgress) onProgress(served, r.addr);
    }
  }

  async findStartingFirmware(product, startingProducts = null) {
    const [model] = product.split("_");
    const dev = await this.find((id) => !IS_OTA(id) && id.model === model);
    if (!dev) throw fail("notfound", "FM-1 not found (USB cable, and no other app using it?)");
    if (dev.id.model !== model) { await dev.link.release(); throw fail("model", `the device is ${dev.id.text}, the package is for ${product}`); }
    // Check the actual device selected for this transaction, before UPGRADE.
    // Port names identify candidates only; they do not establish a firmware baseline.
    if (startingProducts !== null && (!Array.isArray(startingProducts) ||
        startingProducts.length === 0 || !startingProducts.includes(dev.id.text))) {
      await dev.link.release();
      throw fail("baseline", "Unsupported starting firmware. Nothing was written. Check the installer’s accepted starting versions.", dev.id.text);
    }
    return dev;
  }

  // full install: running firmware -> loader -> new firmware
  async install(image, product, onStep, startingProducts = null, beforeTransfer = null) {
    const step = onStep || (() => {});
    const dev = await this.findStartingFirmware(product, startingProducts);
    const model = dev.id.model;
    let s1;
    try {
      beforeTransfer?.();
      step("start", dev.id.text);
      if (!dev.link.send(UPGRADE)) throw fail("lost", "Could not start the firmware check.");
      await sleep(2000);
      s1 = await this.serve(dev.link, image, 0xE0000000, 8000, (n) => step("verify", n));
    } finally { await dev.link.release(); }
    if (!s1.finished && !s1.lost && s1.served === 0)
      throw fail("sourceidle", "The running firmware did not request the BB1 package. No BB1 data was transferred.", dev.id.text);
    if (!s1.finished) throw fail(s1.lost ? "lost" : "stopped", `the device ${s1.lost ? "was disconnected" : "stopped"} after ${s1.served} requests: nothing was written`);
    step("loader");
    await sleep(3000);
    let ota = await this.waitFor((id) => id.model === "ota-" + model, 30000);
    if (!ota) throw fail("noloader", "The MIDI update loader did not appear. Check MIDI/SysEx permission and close other MIDI apps. Retry only after this attempt has stopped. ROM-mode WL80UBOOT is not this loader.");
    let s2 = await this.write(ota, image, step);
    // CoreMIDI may replace the loader port while the write is in progress. Reacquire
    // that stage automatically instead of leaving a responsive device in update mode.
    for (let retry = 0; !s2.finished && retry < 2; retry++) {
      step("loader-reconnect", retry + 1);
      ota = await this.waitFor((id) => id.model === "ota-" + model, 15000);
      if (!ota) break;
      s2 = await this.write(ota, image, step);
    }
    if (!s2.finished) throw fail(s2.lost ? "lost" : "stopped", `the loader ${s2.lost ? "was disconnected" : "stopped"} after ${s2.served} requests. Replug and press Install again to resume.`);
    step("reboot");
    await sleep(3000);
    const ports = await this.confirmRestart(product, 40000);
    step("done", product, ports);
    return product;
  }

  // step 2 with the loader found by find()/waitFor()
  async write(ota, image, step) {
    try {
      if (!ota.link.send(UPGRADE)) throw fail("lost", "Could not start the firmware transfer.");
      await sleep(2000);
      return await this.serve(ota.link, image, 0xF0000000, 180000, (n) => step("write", n));
    } finally { await ota.link.release(); }
  }

  // resume: the device is already in update mode (loader) -> true when the write finished
  async resume(image, onStep, product = null, beforeTransfer = null) {
    const model = product && product.split("_")[0];
    const ota = await this.find((id) => model ? id.model === "ota-" + model : IS_OTA(id));
    if (!ota) return false;
    try { beforeTransfer?.(); } catch (error) { await ota.link.release(); throw error; }
    const step = onStep || (() => {});
    const result = await this.write(ota, image, step);
    if (!result.finished) return false;
    if (product) {
      step("reboot");
      await sleep(3000);
      const ports = await this.confirmRestart(product, 40000);
      step("done", product, ports);
    }
    return true;
  }
}
