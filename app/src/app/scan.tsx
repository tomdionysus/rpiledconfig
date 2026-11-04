// The camera screen: VisionCamera frames go through a worklet that averages
// the luminance of a centred square, and each (timestamp, luminance) sample
// feeds the decoder on the JS thread. The decoder's state is the UI.
//
// Exposure and focus: VisionCamera 4 has no lock, only `exposure` (a bias
// on top of auto-exposure) and `focus(point)`. focus() at the reticle is the
// nearest thing: on iOS it runs one autofocus and one auto-exposure at that
// point and then holds both until the scene changes a lot; on Android it is a
// CameraX focus-and-metering action at that point, which CameraX lets go
// after about five seconds. It is run once the camera starts and whenever the
// reticle is tapped.
import { useRouter } from 'expo-router';
import Constants from 'expo-constants';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Linking, Platform, Pressable, StyleSheet, Text, View, useWindowDimensions, type LayoutChangeEvent } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { Camera, getCameraFormat, useCameraDevices, useCameraPermission, useFrameProcessor, type CameraRuntimeError } from 'react-native-vision-camera';
import { useRunOnJS } from 'react-native-worklets-core';
import { chooseScanCamera } from '@/led/camera';
import { LedDecoder, type DecoderState } from '@/led/decoder';
import { brightLuminance, centredRegion, frameTimestampMs } from '@/led/luminance';
import { makeRecording, SampleRecorder, type CameraDetails } from '@/led/recording';
import { saveRecording } from '@/led/recordingStore';
import { fingerprint } from '@/protocol/keys';
import { useApp } from '@/state/store';
import { Button } from '@/ui/Button';
import { Progress } from '@/ui/Progress';
import { colors, monospace, radius, space, type as typography } from '@/ui/theme';
import { DIAGNOSTICS } from '@/util/diagnostics';

/** The sampled square, as a fraction of the frame's shorter side. */
const REGION_FRACTION = 0.15;
/** Every n-th pixel in each direction; the region is a blob of light. */
const PIXEL_STEP = 2;
const FPS = 30;
/** The most matching one camera frame may carry on the JS thread; about a quarter of a frame at 30 fps. */
const SLICE_MS = 8;
/**
 * The viewfinder's share of the screen, fixed: when it took whatever the
 * panel left, a status line wrapping resized it and moved the measured spot
 * under a held phone.
 */
const VIEWFINDER_FRACTION = 0.5;

const PLATFORM = Platform.OS;
// iOS's yuv frames are biplanar CVPixelBuffers, whose base address is not the
// Y plane, so toArrayBuffer() there is not a luma plane: iOS runs BGRA.
// Android's yuv buffer is the Y plane alone, rows bytesPerRow apart: measured
// on a Blackview A85 (Android 12) at 1280x720, bytesPerRow 1280 and a buffer
// of exactly 921600 bytes, with planesCount 3 (src/led/luminance.ts).
const PIXEL_FORMAT = PLATFORM === 'ios' ? 'rgb' : 'yuv';
const APP_VERSION = Constants.expoConfig?.version ?? 'unknown';

export default function ScanScreen() {
  const router = useRouter();
  const insets = useSafeAreaInsets();
  const window = useWindowDimensions();
  const { setMachineId, setLastScan, services } = useApp();

  // Look for Pis over Bluetooth while the LED is read, so pairing finds its Pi
  // at once rather than starting a scan after the read.
  useEffect(() => services.transport.prescan?.(), [services]);
  const { hasPermission, requestPermission } = useCameraPermission();
  // Not useCameraDevice('back'): it can pick a camera with no formats, and
  // useCameraFormat then throws during render (src/led/camera.ts).
  const devices = useCameraDevices();
  const device = useMemo(() => chooseScanCamera(devices, FPS), [devices]);
  const format = useMemo(
    () => (device && device.formats.length > 0 ? getCameraFormat(device, [{ fps: FPS }, { videoResolution: { width: 1280, height: 720 } }]) : undefined),
    [device],
  );
  const [cameraError, setCameraError] = useState<string | undefined>(undefined);
  const camera = useRef<Camera>(null);
  const [view, setView] = useState({ width: 0, height: 0 });

  // A try is spread over camera frames, at most SLICE_MS of matching each, so
  // the JS thread, which also draws this screen, is never held for long.
  const decoder = useRef(new LedDecoder({ sliceMs: SLICE_MS }));
  const recorder = useRef(new SampleRecorder());
  const [state, setState] = useState<DecoderState>(() => decoder.current.state());
  const [active, setActive] = useState(true);
  const [frameError, setFrameError] = useState<string | undefined>(undefined);
  const [locked, setLocked] = useState<'no' | 'yes' | 'unsupported'>('no');
  const [torch, setTorch] = useState(false);
  const [saved, setSaved] = useState<string | undefined>(undefined);
  const stats = useRef({ count: 0, firstRaw: 0, lastRaw: 0, lastLog: 0 });

  const onSample = useRunOnJS(
    (timestamp: number, luminance: number, width: number, height: number, bytesPerRow: number, bufferLength: number, planes: number, pixelFormat: string) => {
      const t = frameTimestampMs(timestamp, PLATFORM);
      recorder.current.camera = { width, height, pixelFormat, regionFraction: REGION_FRACTION, pixelStep: PIXEL_STEP };
      recorder.current.push({ t, y: luminance });
      const before = DIAGNOSTICS ? performance.now() : 0;
      let next: DecoderState;
      try {
        next = decoder.current.push({ t, y: luminance });
      } catch (error) {
        // Thrown here, an error would vanish inside the camera's callback and the scan would
        // stall without a word: say it, and carry on with the next frame.
        console.warn(`[decode] error: ${error instanceof Error ? (error.stack ?? error.message) : String(error)}`);
        return;
      }
      setState(next);
      if (DIAGNOSTICS) {
        // A push that ran a try: what it cost on this phone's JS thread.
        const cost = performance.now() - before;
        if (cost > 2) console.log(`[decode] push ${cost.toFixed(1)} ms at ${(t / 1000).toFixed(1)} s, LED seen ${next.ledSeen}, ${Math.floor(next.coverage * 100)}% seen, ${next.erasures} unsure${next.result ? ', read' : ''}`);
      }
      if (DIAGNOSTICS) {
        // The frame path as this phone delivers it, every two seconds:
        // `adb logcat -s ReactNativeJS` on Android.
        const s = stats.current;
        if (s.count === 0) s.firstRaw = timestamp;
        const rawDelta = timestamp - s.lastRaw;
        s.lastRaw = timestamp;
        s.count += 1;
        if (t - s.lastLog >= 2000) {
          s.lastLog = t;
          const elapsedMs = frameTimestampMs(timestamp - s.firstRaw, PLATFORM);
          console.log(
            `[frames] n=${s.count} fps=${elapsedMs > 0 ? (((s.count - 1) * 1000) / elapsedMs).toFixed(2) : '-'} raw=${timestamp} rawDelta=${rawDelta} ` +
              `${width}x${height} bytesPerRow=${bytesPerRow} buffer=${bufferLength} (w*h=${width * height}, bpr*h=${bytesPerRow * height}) planes=${planes} ` +
              `format=${pixelFormat} luma=${luminance.toFixed(1)}`,
          );
        }
      }
    },
    [],
  );
  const onFrameError = useRunOnJS((message: string) => setFrameError(message), []);

  const frameProcessor = useFrameProcessor(
    (frame) => {
      'worklet';
      let buffer: ArrayBuffer;
      try {
        buffer = frame.toArrayBuffer();
      } catch (error) {
        // Android below 9 has no HardwareBuffer for VisionCamera to copy from.
        onFrameError(error instanceof Error ? error.message : String(error));
        return;
      }
      const layout = frame.pixelFormat === 'rgb' ? 'bgra' : 'y';
      const plane = new Uint8Array(buffer);
      const region = centredRegion(frame.width, frame.height, REGION_FRACTION);
      const luminance = brightLuminance(plane, frame.width, frame.height, frame.bytesPerRow, region, layout, PIXEL_STEP);
      onSample(frame.timestamp, luminance, frame.width, frame.height, frame.bytesPerRow, plane.length, frame.planesCount, frame.pixelFormat);
    },
    [onSample, onFrameError],
  );

  const lockOnReticle = useCallback(async () => {
    if (!camera.current || view.width === 0) return;
    if (device && !device.supportsFocus) {
      setLocked('unsupported');
      return;
    }
    try {
      await camera.current.focus({ x: view.width / 2, y: view.height / 2 });
      setLocked('yes');
    } catch {
      setLocked('unsupported');
    }
  }, [device, view]);

  const snapshot = useCallback(() => {
    const result = decoder.current.state().result;
    return makeRecording(recorder.current.snapshot(), {
      appVersion: APP_VERSION,
      platform: PLATFORM,
      fps: FPS,
      camera: recorder.current.camera as CameraDetails | undefined,
      decoded: result ? { idHex: result.idHex, framesCombined: result.framesCombined } : null,
    });
  }, []);

  // Whatever happens to the scan, the development recordings screen can save it.
  useEffect(() => {
    return () => {
      if (recorder.current.count > 0) setLastScan(snapshot());
    };
  }, [setLastScan, snapshot]);

  const accept = useCallback(
    (idBytes: Uint8Array) => {
      setActive(false);
      setMachineId(idBytes);
      router.replace('/pair');
    },
    [router, setMachineId],
  );

  const result = state.result;
  useEffect(() => {
    // One frame that passes magic, version and CRC is a read.
    if (result) accept(result.idBytes);
  }, [result, accept]);

  if (!hasPermission) {
    return (
      <View style={[styles.root, styles.prompt, { paddingTop: insets.top + space.xxl, paddingBottom: insets.bottom + space.xl }]}>
        <Text style={styles.promptTitle}>Camera access</Text>
        <Text style={styles.promptBody}>Reading the LED needs the camera. Nothing is recorded, and no image leaves this phone.</Text>
        <Button
          label="Allow camera"
          onPress={() => {
            void requestPermission().then((granted) => {
              if (!granted) void Linking.openSettings();
            });
          }}
        />
        <Button label="Cancel" variant="quiet" onPress={() => router.back()} />
      </View>
    );
  }

  if (!device || cameraError) {
    return (
      <View style={[styles.root, styles.prompt]}>
        <Text style={styles.promptBody}>
          {cameraError ? `The camera stopped: ${cameraError}` : 'This phone has no back camera the app can use.'}
        </Text>
        <Text style={styles.promptBody}>The Pi's ID can be typed in instead; it is printed by rpiledconfig --show-id on the Pi.</Text>
        <Button label="Type the ID" variant="secondary" onPress={() => router.replace('/enter')} />
        <Button label="Back" variant="quiet" onPress={() => router.back()} />
      </View>
    );
  }

  const boxSide = Math.round(Math.min(window.width, window.height) * REGION_FRACTION * 1.6);
  // Only what the decoder has measured: whether a protocol-like blink is in the
  // square, and how much of the repeating code it has seen.
  const seenPercent = Math.floor(state.coverage * 100);
  const progressLabel = result ? 'Read' : state.ledSeen ? 'Reading the LED' : 'No blinking LED in the square';
  const progressDetail = result ? '' : !state.ledSeen ? '' : seenPercent >= 100 ? 'All of the code seen; decoding' : `${seenPercent}% of the code seen`;

  return (
    <View style={styles.root}>
      {/* The viewfinder has the top of the screen to itself and the panel sits below it, so
          the square at the viewfinder's centre is over the frame's centre (where the
          luminance is measured; a "cover" preview keeps the centres together) and covers no
          text. */}
      <View style={[styles.viewfinder, { height: Math.round(window.height * VIEWFINDER_FRACTION) }]} onLayout={(event: LayoutChangeEvent) => setView(event.nativeEvent.layout)}>
        <Camera
          ref={camera}
          style={StyleSheet.absoluteFill}
          device={device}
          format={format}
          fps={FPS}
          isActive={active}
          pixelFormat={PIXEL_FORMAT}
          frameProcessor={frameProcessor}
          torch={torch ? 'on' : 'off'}
          onInitialized={() => void lockOnReticle()}
          onError={(error: CameraRuntimeError) => {
            console.warn(`[camera] ${error.code}: ${error.message}`);
            setActive(false);
            setCameraError(error.message);
          }}
          resizeMode="cover"
        />
        <Text style={[styles.instruction, { top: insets.top + space.md }]}>Point the camera at the Pi's blinking main LED and hold still.</Text>
        {view.height > 0 ? (
          <Pressable
            accessibilityRole="button"
            accessibilityLabel="Lock focus and exposure on the square"
            onPress={() => void lockOnReticle()}
            style={[
              styles.reticle,
              { width: boxSide, height: boxSide, top: (view.height - boxSide) / 2, left: (view.width - boxSide) / 2 },
            ]}>
            {(['tl', 'tr', 'bl', 'br'] as const).map((corner) => (
              <View key={corner} style={[styles.corner, styles[corner], state.level ? styles.cornerLit : null]} />
            ))}
            <View style={[styles.centreDot, state.level ? styles.centreDotLit : null]} />
          </Pressable>
        ) : null}
      </View>
      <View style={[styles.overlay, { paddingBottom: insets.bottom + space.md }]}>
        <View style={styles.panel}>
          <Progress value={result ? 1 : state.coverage} label={progressLabel} detail={progressDetail} />
          <Text style={styles.check} numberOfLines={2}>
            {result
              ? `Read, ${result.errors} wrong and ${result.erasures} unreadable bytes repaired.`
              : state.ledSeen
                ? 'Hold still: the bar is how much of the code the camera has seen.'
                : 'Bring the blinking LED inside the square.'}
          </Text>
          <View style={styles.counters}>
            <Text style={styles.counter}>Frames failed {state.framesFailed}</Text>
            <Text style={styles.counter}>Contrast {Math.round(state.contrast)}</Text>
            <Text style={styles.counter}>{locked === 'yes' ? 'Locked' : locked === 'unsupported' ? 'No lock' : 'Auto'}</Text>
          </View>
          {frameError ? <Text style={styles.error}>The camera frames cannot be read on this phone: {frameError}</Text> : null}
          {result ? (
            <View style={styles.result}>
              <Text style={styles.resultId}>{result.idHex}</Text>
              <Text style={styles.resultDetail}>Fingerprint {fingerprint(result.idBytes)}.</Text>
            </View>
          ) : null}
          {DIAGNOSTICS ? (
            <View style={styles.devRow}>
              <Button
                label={saved ? 'Saved' : 'Save recording'}
                variant="quiet"
                onPress={() => {
                  try {
                    setSaved(saveRecording(snapshot()).name);
                  } catch (error) {
                    setSaved(undefined);
                    setFrameError(error instanceof Error ? error.message : String(error));
                  }
                }}
              />
              <Button label={torch ? 'Torch off' : 'Torch on'} variant="quiet" onPress={() => setTorch((on) => !on)} />
            </View>
          ) : null}
          {DIAGNOSTICS && saved ? <Text style={styles.counter}>{saved}</Text> : null}
          <Button label="Cancel" variant="quiet" onPress={() => router.back()} />
        </View>
      </View>
    </View>
  );
}

const CORNER = 22;

const styles = StyleSheet.create({
  root: {
    flex: 1,
    backgroundColor: colors.background,
  },
  prompt: {
    justifyContent: 'center',
    paddingHorizontal: space.xl,
    gap: space.md,
  },
  promptTitle: {
    ...typography.title,
    color: colors.text,
    textAlign: 'center',
  },
  promptBody: {
    ...typography.body,
    color: colors.textDim,
    textAlign: 'center',
    lineHeight: 21,
    marginBottom: space.lg,
  },
  viewfinder: {
    overflow: 'hidden',
  },
  overlay: {
    alignItems: 'center',
    paddingHorizontal: space.xl,
    paddingTop: space.md,
  },
  instruction: {
    position: 'absolute',
    left: space.xl,
    right: space.xl,
    ...typography.body,
    color: colors.text,
    textAlign: 'center',
    backgroundColor: colors.scrim,
    borderRadius: radius.md,
    overflow: 'hidden',
    paddingHorizontal: space.lg,
    paddingVertical: space.md,
  },
  reticle: {
    position: 'absolute',
    alignItems: 'center',
    justifyContent: 'center',
  },
  corner: {
    position: 'absolute',
    width: CORNER,
    height: CORNER,
    borderColor: colors.text,
  },
  cornerLit: {
    borderColor: colors.accent,
  },
  tl: { top: 0, left: 0, borderTopWidth: 3, borderLeftWidth: 3, borderTopLeftRadius: radius.sm },
  tr: { top: 0, right: 0, borderTopWidth: 3, borderRightWidth: 3, borderTopRightRadius: radius.sm },
  bl: { bottom: 0, left: 0, borderBottomWidth: 3, borderLeftWidth: 3, borderBottomLeftRadius: radius.sm },
  br: { bottom: 0, right: 0, borderBottomWidth: 3, borderRightWidth: 3, borderBottomRightRadius: radius.sm },
  centreDot: {
    width: 6,
    height: 6,
    borderRadius: radius.pill,
    backgroundColor: colors.textFaint,
  },
  centreDotLit: {
    backgroundColor: colors.accent,
  },
  panel: {
    alignSelf: 'stretch',
    gap: space.md,
    backgroundColor: colors.scrim,
    borderRadius: radius.lg,
    padding: space.lg,
  },
  checklist: {
    gap: space.xs,
  },
  check: {
    ...typography.caption,
    fontFamily: monospace,
    color: colors.textFaint,
  },
  checkDone: {
    color: colors.text,
  },
  counters: {
    flexDirection: 'row',
    justifyContent: 'space-between',
  },
  counter: {
    ...typography.caption,
    color: colors.textFaint,
  },
  devRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
  },
  error: {
    ...typography.caption,
    color: colors.danger,
  },
  result: {
    gap: space.sm,
    alignItems: 'center',
  },
  resultId: {
    ...typography.title,
    fontFamily: monospace,
    color: colors.accent,
    letterSpacing: 2,
  },
  resultDetail: {
    ...typography.caption,
    color: colors.textDim,
  },
});
