// The frame-processor side of the LED reader: the mean brightness of a region
// of a camera frame. Kept to plain loops over a byte array so the worklet can
// call it and vitest can test it; nothing here knows about cameras.
//
// Two layouts, because VisionCamera 4.7's `toArrayBuffer()` differs by
// platform (read from its source):
// - Android: it locks the frame's AHardwareBuffer and copies height x stride
//   bytes from its base address, which is one plane's worth; for a `yuv`
//   (YUV_420_888) frame that is the Y plane alone, rows `bytesPerRow` (plane
//   0's row stride) apart. Measured on a Blackview A85 (Android 12): 1280x720,
//   bytesPerRow 1280, a buffer of exactly 921600 bytes (w x h), planesCount
//   3, 29.98 fps, timestamps about 33.3e6 apart (nanoseconds). It needs Android 9: below
//   that VisionCamera has no HardwareBuffer and throws.
// - iOS: it copies bytesPerRow x height from CVPixelBufferGetBaseAddress. For
//   a biplanar `yuv` buffer that address is not the Y plane, so the camera is
//   run in `rgb` (BGRA, four bytes per pixel, rows bytesPerRow apart, which
//   may be padded) and luma is computed from the channels.

export interface Region {
  x: number;
  y: number;
  width: number;
  height: number;
}

export type PlaneLayout = 'y' | 'bgra';

/**
 * Mean brightness (0..255) of `region` in a frame of `width` x `height`
 * pixels whose rows are `bytesPerRow` apart. `step` samples every n-th pixel
 * in both directions: the region is a blob of LED light, not detail, and a
 * 1080p frame at 30 fps is not read in full. Returns 0 for an empty region or
 * a buffer too short to hold it.
 */
export function meanLuminance(
  plane: Uint8Array,
  width: number,
  height: number,
  bytesPerRow: number,
  region: Region,
  layout: PlaneLayout = 'y',
  step = 1,
): number {
  'worklet';
  const bytesPerPixel = layout === 'bgra' ? 4 : 1;
  const x0 = Math.max(0, Math.floor(region.x));
  const y0 = Math.max(0, Math.floor(region.y));
  const x1 = Math.min(width, Math.floor(region.x + region.width));
  const y1 = Math.min(height, Math.floor(region.y + region.height));
  const stride = Math.max(1, Math.floor(step));
  let sum = 0;
  let count = 0;
  for (let row = y0; row < y1; row += stride) {
    const base = row * bytesPerRow;
    if (base + x1 * bytesPerPixel > plane.length) break;
    for (let col = x0; col < x1; col += stride) {
      const at = base + col * bytesPerPixel;
      if (layout === 'bgra') {
        // Rec. 601 luma from B, G, R, in integer weights summing to 256.
        sum += (plane[at] * 29 + plane[at + 1] * 150 + plane[at + 2] * 77) >> 8;
      } else {
        sum += plane[at];
      }
      count += 1;
    }
  }
  return count === 0 ? 0 : sum / count;
}

/**
 * Mean brightness (0..255) of the brightest `top` fraction of the sampled
 * pixels in `region`, at least four of them. What the scan screen measures:
 * an LED held back from the camera is a dot of a few dozen pixels in a square
 * of thousands, and the plain mean diluted it to a contrast of 12 to 18 on a
 * Blackview A85, too little to read. The brightest pixels are the LED
 * wherever it sits in the square, and when it is off they are merely the
 * brightest background, which the decoder's adaptive threshold allows for.
 */
export function brightLuminance(
  plane: Uint8Array,
  width: number,
  height: number,
  bytesPerRow: number,
  region: Region,
  layout: PlaneLayout = 'y',
  step = 1,
  top = 0.02,
): number {
  'worklet';
  const bytesPerPixel = layout === 'bgra' ? 4 : 1;
  const x0 = Math.max(0, Math.floor(region.x));
  const y0 = Math.max(0, Math.floor(region.y));
  const x1 = Math.min(width, Math.floor(region.x + region.width));
  const y1 = Math.min(height, Math.floor(region.y + region.height));
  const stride = Math.max(1, Math.floor(step));
  const histogram = new Uint32Array(256);
  let count = 0;
  for (let row = y0; row < y1; row += stride) {
    const base = row * bytesPerRow;
    if (base + x1 * bytesPerPixel > plane.length) break;
    for (let col = x0; col < x1; col += stride) {
      const at = base + col * bytesPerPixel;
      const value = layout === 'bgra' ? (plane[at] * 29 + plane[at + 1] * 150 + plane[at + 2] * 77) >> 8 : plane[at];
      histogram[value] += 1;
      count += 1;
    }
  }
  if (count === 0) return 0;
  const wanted = Math.min(count, Math.max(4, Math.round(count * top)));
  let taken = 0;
  let sum = 0;
  for (let value = 255; value >= 0 && taken < wanted; value -= 1) {
    const take = Math.min(histogram[value], wanted - taken);
    sum += take * value;
    taken += take;
  }
  return sum / taken;
}

/** A square of `fraction` of the shorter side, centred in the frame. */
export function centredRegion(width: number, height: number, fraction: number): Region {
  'worklet';
  const side = Math.max(1, Math.floor(Math.min(width, height) * fraction));
  return { x: Math.floor((width - side) / 2), y: Math.floor((height - side) / 2), width: side, height: side };
}

/**
 * A frame's timestamp in milliseconds. VisionCamera reports iOS presentation
 * time as CMTimeGetSeconds x 1000, so milliseconds, but passes Android's
 * `ImageInfo.getTimestamp()` through as is, which is nanoseconds; the decoder
 * wants one unit.
 */
export function frameTimestampMs(timestamp: number, platform: 'ios' | 'android' | string): number {
  return platform === 'android' ? timestamp / 1e6 : timestamp;
}
