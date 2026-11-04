import { describe, expect, it } from 'vitest';
import { chooseScanCamera, type CameraCandidate } from './camera';

function camera(id: string, over: Partial<CameraCandidate> = {}): CameraCandidate {
  return {
    id,
    position: 'back',
    hardwareLevel: 'limited',
    physicalDevices: ['wide-angle-camera'],
    formats: [{ maxFps: 30 }],
    ...over,
  };
}

describe('chooseScanCamera', () => {
  it('skips a back camera with no formats, as the A85 reports camera 2', () => {
    // The A85's list: VisionCamera's own scoring picked "2", which has no formats.
    const devices = [
      camera('0', { hardwareLevel: 'limited' }),
      camera('1', { position: 'front' }),
      camera('2', { hardwareLevel: 'full', formats: [] }),
    ];
    expect(chooseScanCamera(devices, 30)?.id).toBe('0');
  });

  it('answers undefined when no back camera has a format', () => {
    expect(chooseScanCamera([camera('0', { formats: [] }), camera('1', { position: 'front' })], 30)).toBeUndefined();
    expect(chooseScanCamera([], 30)).toBeUndefined();
  });

  it('prefers a camera that reaches the frame rate', () => {
    const devices = [camera('0', { formats: [{ maxFps: 15 }], hardwareLevel: 'full' }), camera('3', { formats: [{ maxFps: 30 }] })];
    expect(chooseScanCamera(devices, 30)?.id).toBe('3');
  });

  it('prefers a plain wide-angle camera over a logical or other lens', () => {
    const devices = [
      camera('0', { physicalDevices: ['ultra-wide-angle-camera', 'wide-angle-camera'], hardwareLevel: 'full' }),
      camera('4', { physicalDevices: ['telephoto-camera'] }),
      camera('5', { physicalDevices: ['wide-angle-camera'] }),
    ];
    expect(chooseScanCamera(devices, 30)?.id).toBe('5');
  });

  it('then the higher hardware level, then more formats, then the lower id', () => {
    expect(chooseScanCamera([camera('0', { hardwareLevel: 'legacy' }), camera('1', { hardwareLevel: 'full' })], 30)?.id).toBe('1');
    expect(chooseScanCamera([camera('0'), camera('1', { formats: [{ maxFps: 30 }, { maxFps: 60 }] })], 30)?.id).toBe('1');
    expect(chooseScanCamera([camera('7'), camera('2'), camera('10')], 30)?.id).toBe('2');
  });
});
