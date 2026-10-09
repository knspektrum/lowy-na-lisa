// Fake Web Bluetooth for testing index.html without hardware (test/index.html).
// Behaves like Chrome with no flags: requestDevice() and GATT only, no
// requestLEScan, getDevices or watchAdvertisements. Devices speak the LisekOta
// protocol (software/libraries/LisekOta/src/LisekOta.cpp).
(() => {
  const SVC = "4c49534b-0001-4f54-8000-00805f9b34fb";
  const UUID = { info: "4c49534b-0002-4f54-8000-00805f9b34fb", ctrl: "4c49534b-0003-4f54-8000-00805f9b34fb", data: "4c49534b-0004-4f54-8000-00805f9b34fb" };
  const te = new TextEncoder(), td = new TextDecoder();
  const later = (f, ms = 5) => setTimeout(f, ms);

  class FakeDevice extends EventTarget {
    constructor(o) {
      super();
      Object.assign(this, { id: o.name, name: o.name, role: o.role, version: o.version, user: o.user ?? 0, devId: o.devId ?? 1,
        state: "ok", healthy: true, on: true, fast: o.fast ?? true, log: [] });
      this.gatt = new FakeGatt(this);
    }
    info() {
      return JSON.stringify({ role: this.role, version: this.version, state: this.state, user: this.user, healthy: this.healthy,
        partition: "app0", mtu: 517, uptime: 42, hide_after: 10, id: this.devId });
    }
    notify(s) { later(() => this.ctrl.dispatch(s)); }
    command(cmd) {
      this.log.push(cmd);
      if (cmd === "begin") { this.rx = []; this.got = 0; this.hdr = null; this.notify("ready"); }
      else if (cmd === "confirm") { if (this.state === "pending") this.state = "ok"; this.notify("confirmed"); }
      else if (cmd.startsWith("set ")) {
        const [k, v] = cmd.slice(4).split("=");
        if (this.role === "receiver" && k === "leds") this.user = (this.user & ~1) | (v === "binary" ? 1 : 0);
        else if (this.role === "receiver" && k === "channels") {
          if (+v > ((this.user & 1) ? 8 : 7)) return this.notify("error:rejected");
          this.user = (this.user & 1) | ((+v - 1) << 1);
        } else if (this.role === "transmitter" && k === "hide") this.user = [0, 10, 30, 60].indexOf(+v);
        else return this.notify("error:rejected");
        this.notify("set-ok");
      } else this.notify("error:unknown command");
    }
    data(bytes) {
      this.rx.push(...bytes);
      if (!this.hdr && this.rx.length >= 168) {
        const h = new Uint8Array(this.rx.slice(0, 168));
        const role = td.decode(h.slice(4, 16)).replace(/\0.*$/s, "");
        if (td.decode(h.slice(0, 4)) !== "LSK1") return this.notify("error:not a signed image");
        if (role !== this.role) return this.notify("error:image is for another device type");
        this.hdr = { version: td.decode(h.slice(16, 56)).replace(/\0.*$/s, ""), size: new DataView(h.buffer).getUint32(56, true) };
        this.notify("header-ok");
      }
      if (!this.hdr) return;
      const got = this.rx.length - 168, step = this.fast ? 8192 : 32768;
      if (Math.floor(got / step) > Math.floor(this.got / step) || got === this.hdr.size) this.notify(`progress:${got}/${this.hdr.size}`);
      this.got = got;
      if (got === this.hdr.size) {
        this.notify("done");
        later(() => {  // reboot into the new image, pending confirmation
          this.gatt.connected = false;
          this.on = false;
          this.dispatchEvent(new Event("gattserverdisconnected"));
          later(() => { this.on = true; this.version = this.hdr.version; this.state = "pending"; }, 1500);
        }, 300);
      }
    }
  }
  class FakeChar extends EventTarget {
    constructor(dev, kind) {
      super();
      this.dev = dev; this.kind = kind; this.uuid = UUID[kind];
      this.properties = { read: kind === "info", write: kind !== "info", notify: kind === "ctrl",
        writeWithoutResponse: kind === "data" && dev.fast };
    }
    check() { if (!this.dev.gatt.connected) throw new DOMException("GATT Server is disconnected", "NetworkError"); }
    async readValue() { this.check(); return new DataView(te.encode(this.dev.info()).buffer); }
    async startNotifications() { this.check(); return this; }
    dispatch(s) { this.value = new DataView(te.encode(s).buffer); this.dispatchEvent(new Event("characteristicvaluechanged")); }
    async writeValueWithResponse(v) {
      this.check();
      await new Promise(r => setTimeout(r, 1));
      const b = new Uint8Array(v.buffer ? v.buffer.slice(v.byteOffset, v.byteOffset + v.byteLength) : v);
      if (this.kind === "ctrl") this.dev.command(td.decode(b)); else this.dev.data(b);
    }
    async writeValueWithoutResponse(v) {
      if (!this.properties.writeWithoutResponse) throw new DOMException("not permitted", "NotSupportedError");
      return this.writeValueWithResponse(v);
    }
  }
  class FakeGatt {
    constructor(dev) { this.device = dev; this.connected = false; }
    async connect() {
      await new Promise(r => setTimeout(r, 30));
      if (!this.device.on) throw new DOMException("Connection failed for unknown reason.", "NetworkError");
      this.connected = true;
      return this;
    }
    disconnect() { this.connected = false; }
    async getPrimaryService(uuid) {
      if (!this.connected) throw new DOMException("GATT Server is disconnected", "NetworkError");
      const d = this.device;
      d.chars = d.chars || { info: new FakeChar(d, "info"), ctrl: new FakeChar(d, "ctrl"), data: new FakeChar(d, "data") };
      d.ctrl = d.chars.ctrl;
      return { getCharacteristic: async u => Object.values(d.chars).find(c => c.uuid === u) };
    }
  }

  const devices = [
    new FakeDevice({ name: "lisek-T-FDCC", role: "transmitter", version: "56302f2", user: 1, devId: 1, fast: false }),
    new FakeDevice({ name: "lisek-T-1A2B", role: "transmitter", version: "56302f2", user: 1, devId: 2, fast: true }),
    new FakeDevice({ name: "lisek-R-90C0", role: "receiver", version: "56302f2", user: 6 << 1, devId: 3, fast: true }),
  ];
  let next = 0;
  window.mockBluetooth = { devices, pick: name => { next = devices.findIndex(d => d.name === name); } };
  Object.defineProperty(navigator, "bluetooth", { configurable: true, value: {
    async requestDevice(opts) {
      const f = (opts.filters || [])[0] || {};
      const d = f.name ? devices.find(x => x.name === f.name) : devices[next++ % devices.length];
      if (!d || !d.on) throw new DOMException("User cancelled the requestDevice() chooser.", "NotFoundError");
      return d;
    },
  } });
})();
