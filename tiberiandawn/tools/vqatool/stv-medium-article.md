# STV - a video codec for the Atari ST

---

I like porting DOS-era games to the Atari ST — a home computer launched in 1985 and mostly obsolete by 1995, the year Westwood’s *Command & Conquer* arrived. When I started a port of that RTS classic, cutscenes were not on the critical path. The game core came first. Experienced players often skip the videos anyway, and it was not obvious an ST port even needed them.

What *was* obvious: Westwood’s video format (VQA, for Vector Quantized Animation) was never going to work out of the box on a low-end ST. Designed for the VGA graphics of its day, VQA videos are 320×200 pixels, 256 colors, and run at 15 frames per second. The ST, on the other hand, supports only 16 colors and uses planar video memory. I wrote more about that while [porting DOOM to the ST](https://medium.com/@jonas.eschenburg/how-i-stopped-worrying-and-started-loving-the-assembly-4fd00e786c60). It can’t display VQA videos directly; they’d have to be converted using a "chunky-to-planar" (c2p) process. Achieving 15 fps would be out of reach for an 8 MHz Atari ST, so I did the wise thing and decided not to embark on such a foolish endeavor.

Except I'm really bad at resisting temptations. The idea of FMV on an ST kept creeping into my thoughts until I finally gave in. That is how **STV** started — a backronym for *ST* and *TV*, and less a product requirement than an excuse to squeeze an old CPU the way people used to.

## Borrow what fits, redesign what doesn’t

I studied Westwood's VQA and kept the useful idea: a **codebook** of tiles, with frames mostly sending indices into that dictionary. Beyond that, the ST wanted different geometry.

VQA’s small tiles (4×2) looked like a poor fit. Two observations pushed me toward **8×8**.

From earlier experiments I already knew that eight horizontal pixels are a natural unit on this machine. The 68000 has an instruction called `movep` that can update those eight pixels as four bytes, without the bit twiddling a naive planar write would need. Height eight made sense out of practical considerations: a 200-pixel column holds 25 tiles, which fits in a single 32-bit skip mask. Fitting things into a single register is good if you want performance on an old hardware.

Each codebook entry is 32 bytes of planar data, ready to be copied directly into video RAM using the **movep** instruction. Encoding happens on a modern host; the Atari's job is simply to throw these tiles on screen as fast as possible.

## How a frame looks (skip if you only want the story)

*Gory details. Jump ahead if you prefer the narrative.*

Let's start with the main idea: A frame is drawn column by column. For each column the stream carries a **32-bit skip mask**, then 16-bit **codebook indices** only for the tiles that change, compared to the previous frame:

```text
column 0      column 1         ...    column 39
┌───────────┐ ┌───────────┐           ┌───────────┐
│ skip mask │ │ skip mask │    ...    │ skip mask │
│  (uint32) │ │  (uint32) │           │  (uint32) │
├───────────┤ ├───────────┤           ├───────────┤
│ idx,…     │ │ idx,…     │    ...    │ idx,…     │
│ (uint16;  │ │ (uint16;  │           │ (uint16;  │
│  changed  │ │  changed  │           │  changed  │
│  only)    │ │  only)    │           │  only)    │
└───────────┘ └───────────┘           └───────────┘
```

Fullscreen video has no spare cycles for copying a whole framebuffer every frame. So the player uses ping-pong buffering: two screens — front and back — flipped on vertical blank. A skipped tile keeps what is already in the back buffer, which is not the previous frame but the one before that — frame **N−2**. The encoder needs to take this into account but the player can work very efficiently.

I didn’t know at first what the real bottleneck would be. At the bit rates I used, disk streaming turned out not to be an issue — I wasn’t streaming from floppy. What mattered was drawing tiles as cheaply as possible. The hot path is mostly moving memory, and bandwidth is tight: avoid redundant reads and writes, and keep as much as you can in registers. It’s one of those times where smart algorithms lose to dumb ones well-adapted to the hardware.

## A codebook that adapts

A static codebook would not survive a cutscene. Faces move, lighting shifts, logos slam onto the screen — the dictionary has to learn new tiles as the clip unfolds. I knew a **dynamic codebook** was necessary. By default it holds **2048** tiles (about 64 KB at 32 bytes each). What I did not know was the update budget: how many of those entries could the player afford to replace each frame?

On the ST, patching the codebook turned out to be cheap. Copying a handful of 32-byte tiles into RAM barely shows up next to drawing the frame. So the choice to allow about **32 updated tiles per frame** was driven less by CPU fear and more by **compression**: every replace costs bitstream space, and the encoder has to spend that budget where it helps quality most.

## Audio: stop transcoding on the target

Sound went through a similar reality check. While porting C&C I first tried Westwood’s ADPCM-compressed audio. Decoding it on the ST was possible, but it maxed out the low-end machines.

The easier path won: **12.5 kHz, 8-bit mono PCM**, played straight through the Atari STE’s DMA sound hardware with no transcoding on the target. The encoder does the work; the player mostly shoves samples into a DMA buffer and lets the hardware run.

## Keep the player dumb, make the encoder smart

The format and the player have to stay dead simple. That does not mean the encoder has to be. Most of the interesting engineering lives on the host side, where you can afford YUV math, frequency-domain metrics, and codebook policy that would be absurd at 8 MHz.

A few of the tricks:

- **Work in YUV, not RGB.** Errors that matter to the eye are easier to reason about when luma and chroma are separated.
- **Compare tiles in the frequency domain.** Each 8×8 block is turned into a short DCT (Discrete Cosine Transform) feature vector; distance is measured there instead of raw pixels. Low frequencies can be weighted more heavily, and chroma can use fewer coefficients than luma — faces and logos care more about structure than about every chroma wiggle.
- **Maintain the codebook with a utility function.** New tiles are accepted and old ones evicted according to how much they help the current (and nearby) frames, not by blind FIFO replacement. A little lookahead and a dash of randomness keep the dictionary from getting stuck.
- **Judge quality against the original source, store what the ST can draw.** The codebook holds dithered 16-color tiles; the error metric still looks at the richer source picture.
- **Be smart about choosing the right palette.**
  The encoder uses a [palette optimization / color reduction](https://youtube.com/shorts/dUC-d_GMXFY?si=7HxnI4N2-s3BlYHh) algorithm to select 16 colors that best fit each scene and minimize color error.
- **After a palette change, prefer evicting tiles that belonged to the old palette.** The dictionary has to turn over when the colors do.

None of that complexity shows up in the player. It only sees indices, skip bits, and the occasional codebook patch.

<!-- Medium: paste the URL below alone on its own line so it auto-embeds as a video player. -->

https://youtube.com/shorts/dUC-d_GMXFY

## What is still hard

Coming up with good **16-color palettes** is still not ideal. Cutscenes want more color than the hardware has, and no amount of clever tiling fully hides a weak palette.

**Palette switches between frames** are another sharp edge. The codebook is suddenly invalidated and has to catch up over the next few frames. Transitions appear a little blurry.

And the encoder has to be **fast enough to run inside the remix-web app**, in the browser, where people convert their game data.

## The first clip that felt real

The breakthrough was the intro: Westwood logo, metallic *Command & Conquer* mark, a couple of explosions. When that held together on the ST, I posted it on Twitter. After that, the rest felt less like a gamble.

## What surprised me

I expected this to be hard. With a simple player — planar tiles, skip masks, ping-pong buffers, DMA PCM — and a smarter encoder behind it, STV was more approachable than I had feared.

Imagine a slightly different 1990s: CD-based, 16-color Atari ST games with real FMV. The hardware was never the natural home for that genre. With the right split between a dumb player and a clever encoder, it gets surprisingly close.

STV exists because a C&C port left a gap I could not stop poking at. If you like algorithms and old stories about wringing performance out of thin silicon, that gap is a fun place to visit.

If you want to see where this is headed — or just play some 16-color *Command & Conquer* on an Atari ST or in an emulator like Hatari — the work-in-progress port lives on itch.io: [Command & Conquer for Atari ST](https://indyjo.itch.io/commandconquer).
