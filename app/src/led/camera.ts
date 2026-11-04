// Which camera reads the LED. VisionCamera's own choice (useCameraDevice) scores
// back cameras by hardware level and lens type and never asks whether a camera
// has any formats; on a Blackview A85 it picks camera 2, an auxiliary back
// camera with none, and useCameraFormat then throws during render. So the
// choice is made here, from the list, and only among cameras that can stream.
import type { CameraDevice } from 'react-native-vision-camera';

/** What the choice reads of a device; a CameraDevice has all of it. */
export type CameraCandidate = Pick<CameraDevice, 'id' | 'position' | 'hardwareLevel' | 'physicalDevices'> & {
  formats: ReadonlyArray<Pick<CameraDevice['formats'][number], 'maxFps'>>;
};

const HARDWARE_RANK: Record<CameraCandidate['hardwareLevel'], number> = { legacy: 0, limited: 1, full: 2 };

/**
 * The back camera to scan with, or undefined when no back camera has a format.
 * In order: it must have formats; one that reaches `fps` beats one that does
 * not; a plain wide-angle camera beats anything else (a logical multi-camera
 * starts slower, and an ultra-wide or telephoto alone is a poorer view of a
 * small LED); then the higher hardware level; then more formats; then the
 * lower id, which on Android is the main camera.
 */
export function chooseScanCamera<T extends CameraCandidate>(devices: readonly T[], fps: number): T | undefined {
  const usable = devices.filter((d) => d.position === 'back' && d.formats.length > 0);
  const key = (d: T): number[] => [
    d.formats.some((f) => f.maxFps >= fps) ? 1 : 0,
    d.physicalDevices.length === 1 && d.physicalDevices[0] === 'wide-angle-camera' ? 1 : 0,
    HARDWARE_RANK[d.hardwareLevel] ?? 0,
    d.formats.length,
    -idRank(d.id),
  ];
  let best: T | undefined;
  let bestKey: number[] = [];
  for (const device of usable) {
    const k = key(device);
    if (best === undefined || compare(k, bestKey) > 0) {
      best = device;
      bestKey = k;
    }
  }
  return best;
}

function idRank(id: string): number {
  const n = Number(id);
  return Number.isFinite(n) ? n : Number.MAX_SAFE_INTEGER;
}

function compare(a: number[], b: number[]): number {
  for (let i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) return a[i] - b[i];
  }
  return 0;
}
