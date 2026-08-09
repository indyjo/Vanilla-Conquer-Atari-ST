import { assembleMix } from '../web/src/lib/mix-format.ts';
import { harvestTransitAuds, injectAudsIntoMix, looksLikeAud } from '../web/src/lib/transit-aud.ts';

function pcmAud(samples) {
  const hdr = new Uint8Array(12 + samples);
  hdr[0] = 0x11;
  hdr[1] = 0x2b;
  const n = samples;
  hdr[2] = n & 0xff;
  hdr[3] = (n >> 8) & 0xff;
  hdr[4] = (n >> 16) & 0xff;
  hdr[5] = (n >> 24) & 0xff;
  hdr[6] = n & 0xff;
  hdr[7] = (n >> 8) & 0xff;
  hdr[8] = (n >> 16) & 0xff;
  hdr[9] = (n >> 24) & 0xff;
  hdr[10] = 0;
  hdr[11] = 0;
  for (let i = 0; i < samples; i++) hdr[12 + i] = 128;
  return hdr;
}

const wsa = new Uint8Array(100);
wsa[0] = 0x57;
const aud1 = pcmAud(64);
const aud2 = pcmAud(128);
const record = new Uint8Array([1, 2, 3, 4]);

if (!looksLikeAud(aud1)) throw new Error('aud1 should look like AUD');
if (looksLikeAud(wsa)) throw new Error('wsa should not look like AUD');

const transit = assembleMix([
  { crc: 0x11111111, payload: aud1 },
  { crc: 0x22222222, payload: wsa },
  { crc: 0x33333333, payload: aud2 },
  { crc: 0x44444444, payload: record },
]);

const h = harvestTransitAuds(transit);
if (h.auds.length !== 2) throw new Error(`expected 2 auds, got ${h.auds.length}`);
if (h.audBytes !== 76 + 140) throw new Error(`audBytes ${h.audBytes}`);

const sounds = assembleMix([{ crc: 0x55555555, payload: pcmAud(16) }]);
const inj = injectAudsIntoMix(sounds, h.auds);
if (inj.added !== 2) throw new Error(`added ${inj.added}`);

const again = harvestTransitAuds(h.stripped);
if (again.auds.length !== 0) throw new Error('stripped should have no AUDs');

console.log('ok', {
  harvested: h.auds.length,
  stripped: h.stripped.length,
  soundsOut: inj.mix.length,
});
