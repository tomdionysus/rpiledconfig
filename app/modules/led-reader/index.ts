// The native LED reader: a VisionCamera frame-processor plugin (android/, cpp/) that finds the
// blinking LED anywhere in the frame and decodes frame version 6 on the camera's thread.
// Android only for now; on iOS `createLedReader` returns undefined.
import { requireOptionalNativeModule } from 'expo-modules-core';
import { VisionCameraProxy, type FrameProcessorPlugin } from 'react-native-vision-camera';

/** What the plugin returns for each frame. */
export interface LedReaderState {
  /** The frame's time, in milliseconds. */
  t: number;
  ledSeen: boolean;
  /** The share of the frame's symbol slots seen while the LED was, 0..1. */
  coverage: number;
  contrast: number;
  level: boolean;
  tries: number;
  framesFailed: number;
  erasures: number;
  /** Present once read: the machine ID and what the code repaired. */
  idHex?: string;
  errors?: number;
  filled?: number;
  framesCombined?: number;
  /** The tile being read, in the plane's pixels, while there is one. */
  tileX?: number;
  tileY?: number;
  tileWidth?: number;
  tileHeight?: number;
  planeWidth: number;
  planeHeight: number;
  /** Tiles the LED is seen in now. */
  candidates: number;
  /** This frame's brightness of the tile read (or the centre tile): what a recording keeps. */
  sample: number;
  orientation: string;
}

/**
 * A new reader with nothing seen yet, or undefined where the plugin is not built in.
 * `symbolMs` is the Pi's symbol time: the protocol's 50 unless both sides are built for
 * an experiment (TODO/ACTIVE.md).
 */
export function createLedReader(symbolMs = 50): FrameProcessorPlugin | undefined {
  const module = requireOptionalNativeModule<{ install(): boolean }>('LedReader');
  if (!module) return undefined;
  module.install();
  return VisionCameraProxy.initFrameProcessorPlugin('ledReader', { symbolMs });
}
