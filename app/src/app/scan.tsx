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
import { makeRecording, SampleRecorder, type CameraDetails } from '@/led/recording';
import { saveRecording } from '@/led/recordingStore';
import { fingerprint } from '@/protocol/keys';
import { useApp } from '@/state/store';
import { Button } from '@/ui/Button';
import { Progress } from '@/ui/Progress';
import { colors, monospace, radius, space, type as typography } from '@/ui/theme';
import { DIAGNOSTICS } from '@/util/diagnostics';
import { hexToBytes } from '@/protocol/bytes';
import { createLedReader, type LedReaderState } from '../../modules/led-reader';

const FPS = 30;
/**
 * The viewfinder's share of the screen, fixed: when it took whatever the
 * panel left, a status line wrapping resized it and moved the measured spot
 * under a held phone.
 */
const VIEWFINDER_FRACTION = 0.5;

const PLATFORM = Platform.OS;
// The reader takes the Y plane of a yuv frame.
const PIXEL_FORMAT = 'yuv';
const APP_VERSION = Constants.expoConfig?.version ?? 'unknown';

export default function ScanScreen() {
  const router = useRouter();
  const insets = useSafeAreaInsets();
  const window = useWindowDimensions();
  const { setMachineId, setLastScan, services, target } = useApp();
  // The fingerprint of an LED read that was not the chosen Pi's.
  const [otherPi, setOtherPi] = useState<string | undefined>(undefined);

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

  // The native reader (modules/led-reader): it finds the LED anywhere in the frame and
  // decodes it on the camera's thread; this screen only shows its state. One per visit.
  const reader = useMemo(() => createLedReader(), []);
  const recorder = useRef(new SampleRecorder());
  const [state, setState] = useState<LedReaderState | undefined>(undefined);
  const latest = useRef<LedReaderState | undefined>(undefined);
  const [active, setActive] = useState(true);
  const [frameError, setFrameError] = useState<string | undefined>(
    reader ? undefined : 'the LED reader is not built into this app on this platform',
  );
  const [locked, setLocked] = useState<'no' | 'yes' | 'unsupported'>('no');
  const [torch, setTorch] = useState(false);
  const [saved, setSaved] = useState<string | undefined>(undefined);
  const lastLog = useRef(0);

  const onState = useRunOnJS((next: LedReaderState, width: number, height: number, pixelFormat: string) => {
    latest.current = next;
    recorder.current.camera = { width, height, pixelFormat, regionFraction: 0, pixelStep: 2 };
    recorder.current.push({ t: next.t, y: next.sample });
    setState(next);
    if (DIAGNOSTICS && (next.t - lastLog.current >= 1000 || next.idHex)) {
      // `adb logcat -s ReactNativeJS` on Android: what the reader measured, once a second.
      lastLog.current = next.t;
      console.log(
        `[reader] t=${(next.t / 1000).toFixed(1)} s, LED seen ${next.ledSeen}, ${Math.floor(next.coverage * 100)}% seen, contrast ${next.contrast.toFixed(0)}, ` +
          `${next.candidates} candidate tiles, tile ${next.tileX ?? '-'},${next.tileY ?? '-'} of ${next.planeWidth}x${next.planeHeight}, tries ${next.tries}, ` +
          `${next.erasures} unsure${next.idHex ? ', read' : ''}`,
      );
    }
  }, []);
  const onFrameError = useRunOnJS((message: string) => setFrameError(message), []);

  const frameProcessor = useFrameProcessor(
    (frame) => {
      'worklet';
      if (!reader) return;
      try {
        const next = reader.call(frame) as unknown as LedReaderState | undefined;
        if (next) onState(next, frame.width, frame.height, frame.pixelFormat);
      } catch (error) {
        onFrameError(error instanceof Error ? error.message : String(error));
      }
    },
    [reader, onState, onFrameError],
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
    const last = latest.current;
    return makeRecording(recorder.current.snapshot(), {
      appVersion: APP_VERSION,
      platform: PLATFORM,
      fps: FPS,
      camera: recorder.current.camera as CameraDetails | undefined,
      decoded: last?.idHex ? { idHex: last.idHex, framesCombined: last.framesCombined ?? 1 } : null,
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
      // Another Pi's LED in view (docs/protocol.md, The idea): not the one asked to blink.
      if (target && fingerprint(idBytes) !== target.fingerprint) {
        setOtherPi(fingerprint(idBytes));
        return;
      }
      setMachineId(idBytes);
      router.replace('/pair');
    },
    [router, setMachineId, target],
  );

  const idHex = state?.idHex;
  const result = useMemo(
    () => (idHex ? { idHex, idBytes: hexToBytes(idHex), errors: state?.errors ?? 0, erasures: state?.filled ?? 0 } : undefined),
    // Only a new read makes a new result.
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [idHex],
  );
  useEffect(() => {
    // One frame that passes the CRC is a read.
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

  if (otherPi && target) {
    return (
      <View style={[styles.root, styles.prompt, { paddingTop: insets.top + space.xxl, paddingBottom: insets.bottom + space.xl }]}>
        <Text style={styles.promptTitle}>Another Pi's LED</Text>
        <Text style={styles.promptBody}>
          The LED read belongs to the Pi {otherPi}, not {target.fingerprint}, the one you chose. Point the camera at the Pi you chose.
        </Text>
        <Button label="Read again" onPress={() => router.replace('/scan')} />
        <Button label="Choose another Pi" variant="quiet" onPress={() => router.replace('/find')} />
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

  const boxSide = Math.round(Math.min(window.width, window.height) * 0.24);
  // Only what the reader has measured: whether a protocol-like blink is in view,
  // and how much of the repeating code it has seen.
  const ledSeen = state?.ledSeen ?? false;
  const coverage = state?.coverage ?? 0;
  const seenPercent = Math.floor(coverage * 100);
  const progressLabel = result ? 'Read' : ledSeen ? 'Reading the LED' : 'No blinking LED in view';
  const progressDetail = result ? '' : !ledSeen ? '' : seenPercent >= 100 ? 'All of the code seen; decoding' : `${seenPercent}% of the code seen`;
  const tileBox = state && view.width > 0 ? tileInView(state, view) : undefined;

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
        {tileBox ? (
          <View
            pointerEvents="none"
            style={[styles.tile, { left: tileBox.x, top: tileBox.y, width: tileBox.width, height: tileBox.height }, state?.level ? styles.tileLit : null]}
          />
        ) : null}
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
              <View key={corner} style={[styles.corner, styles[corner], state?.level ? styles.cornerLit : null]} />
            ))}
            <View style={[styles.centreDot, state?.level ? styles.centreDotLit : null]} />
          </Pressable>
        ) : null}
      </View>
      <View style={[styles.overlay, { paddingBottom: insets.bottom + space.md }]}>
        <View style={styles.panel}>
          <Progress value={result ? 1 : coverage} label={progressLabel} detail={progressDetail} />
          <Text style={styles.check} numberOfLines={2}>
            {result
              ? `Read, ${result.errors} wrong and ${result.erasures} unreadable bytes repaired.`
              : ledSeen
                ? 'Hold still: the bar is how much of the code the camera has seen.'
                : 'Bring the blinking LED into view; it is found anywhere in the picture.'}
          </Text>
          <View style={styles.counters}>
            <Text style={styles.counter}>Frames failed {state?.framesFailed ?? 0}</Text>
            <Text style={styles.counter}>Contrast {Math.round(state?.contrast ?? 0)}</Text>
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

/**
 * Where the tile being read lies in the viewfinder. The frame is the sensor's picture: on a
 * phone held upright it is landscape and turned a quarter turn from the screen (VisionCamera's
 * `orientation` says which way), and the preview covers the view, cropping the overflow.
 */
function tileInView(state: LedReaderState, view: { width: number; height: number }): { x: number; y: number; width: number; height: number } | undefined {
  if (state.tileX === undefined || state.tileY === undefined || !state.tileWidth || !state.tileHeight || !state.planeWidth || !state.planeHeight) return undefined;
  const turned = state.orientation === 'landscape-left' || state.orientation === 'landscape-right';
  const frameW = turned ? state.planeHeight : state.planeWidth;
  const frameH = turned ? state.planeWidth : state.planeHeight;
  const scale = Math.max(view.width / frameW, view.height / frameH);
  const offsetX = (view.width - frameW * scale) / 2;
  const offsetY = (view.height - frameH * scale) / 2;
  const cx = state.tileX + state.tileWidth / 2;
  const cy = state.tileY + state.tileHeight / 2;
  // A quarter turn clockwise for landscape-right, anticlockwise for landscape-left.
  let x = cx;
  let y = cy;
  if (state.orientation === 'landscape-right') {
    x = state.planeHeight - cy;
    y = cx;
  } else if (state.orientation === 'landscape-left') {
    x = cy;
    y = state.planeWidth - cx;
  }
  const side = state.tileWidth * scale;
  return { x: offsetX + x * scale - side / 2, y: offsetY + y * scale - side / 2, width: side, height: side };
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
  tile: {
    position: 'absolute',
    borderWidth: 2,
    borderColor: colors.text,
    borderRadius: radius.sm,
  },
  tileLit: {
    borderColor: colors.accent,
  },
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
