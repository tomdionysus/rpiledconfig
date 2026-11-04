// react-native-vision-camera under test: the camera is native and no test
// renders it; the luminance logic it feeds is tested on plain arrays.
export const Camera = () => null;
export function useCameraDevice(): undefined {
  return undefined;
}
export function useCameraFormat(): undefined {
  return undefined;
}
export function useCameraDevices(): [] {
  return [];
}
export function getCameraFormat(): undefined {
  return undefined;
}
export type CameraRuntimeError = Error & { code: string };
export function useCameraPermission() {
  return { hasPermission: false, requestPermission: async () => false };
}
export function useFrameProcessor<T>(fn: T): T {
  return fn;
}
export type Frame = unknown;
