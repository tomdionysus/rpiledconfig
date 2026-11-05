// The native LED reader: a VisionCamera frame-processor plugin (android/, cpp/) that finds the
// blinking LED anywhere in the frame and decodes frame version 7 on the camera's thread. The
// application registers the plugin at start (LedReaderRegistration.kt); here it is only looked
// up. Android only for now; elsewhere `createLedReader` returns undefined.
import { Platform } from 'react-native';
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
  /** Present once read: the key and what the code repaired. */
  idHex?: string;
  errors?: number;
  filled?: number;
  framesCombined?: number;
  /** Where the LED is, in the plane's pixels, once seen both lit and dark. */
  spotX?: number;
  spotY?: number;
  /** The tile being read, in the plane's pixels, while there is one. */
  tileX?: number;
  tileY?: number;
  tileWidth?: number;
  tileHeight?: number;
  planeWidth: number;
  planeHeight: number;
  /** Tiles the LED is seen in now. */
  candidates: number;
  /** This frame's brightness of the tile read (or the centre tile). */
  sample: number;
  orientation: string;
}

/**
 * A new reader with nothing seen yet, or undefined where the plugin is not built in.
 * `symbolMs` is the T of the rate the Pi was asked for (src/led/rates.ts).
 */
export function createLedReader(symbolMs: number): FrameProcessorPlugin | undefined {
  if (Platform.OS !== 'android') return undefined;
  try {
    return VisionCameraProxy.initFrameProcessorPlugin('ledReader', { symbolMs }) ?? undefined;
  } catch {
    return undefined;
  }
}
