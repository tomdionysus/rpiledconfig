// The camera screen: VisionCamera frames go to the native LED reader
// (modules/led-reader), which finds and decodes the LED on the camera's
// thread. Its state is the UI.
//
// Exposure and focus: VisionCamera 4 has no lock, only `exposure` (a bias
// on top of auto-exposure) and `focus(point)`. focus() at the reticle is the
// nearest thing: on iOS it runs one autofocus and one auto-exposure at that
// point and then holds both until the scene changes a lot; on Android it is a
// CameraX focus-and-metering action at that point, which CameraX lets go
// after about five seconds. It is run once the camera starts and whenever the
// reticle is tapped.
import { useLocalSearchParams, useRouter } from '@/navigation';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Linking, Pressable, StyleSheet, Text, View, useWindowDimensions, type LayoutChangeEvent } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { Camera, getCameraFormat, useCameraDevices, useCameraPermission, useFrameProcessor, type CameraRuntimeError } from 'react-native-vision-camera';
import { useRunOnJS } from 'react-native-worklets-core';
import { chooseScanCamera } from '@/led/camera';
import { ScanProgress, type ScanView } from '@/led/scanProgress';
import { fingerprint } from '@/protocol/keys';
import { useApp } from '@/state/store';
import { Button } from '@/ui/Button';
import { Progress } from '@/ui/Progress';
import { colors, monospace, radius, space, type as typography } from '@/ui/theme';
import { DIAGNOSTICS } from '@/util/diagnostics';
import { hexToBytes } from '@/protocol/bytes';
import { createLedReader, type LedReaderState } from '../../modules/led-reader';

// The camera's frame rate and the Pi's symbol time: the protocol's 45 ms at 30 fps. A
// diagnostics build takes others from the deep link (rpiledconfig://find?symbolMs=..&fps=..)
// for the symbol-time experiment (TODO/ACTIVE.md), against a Pi with the same led.symbol_ms.
const DEFAULT_FPS = 30;
const DEFAULT_SYMBOL_MS = 45;

/** A positive number from a route parameter, in diagnostics builds only; otherwise the default. */
function setting(value: string | string[] | undefined, fallback: number): number {
  if (!DIAGNOSTICS || typeof value !== 'string') return fallback;
  const n = Number(value);
  return Number.isFinite(n) && n > 0 ? n : fallback;
}
/**
 * The viewfinder's share of the screen, fixed: when it took whatever the
 * panel left, a status line wrapping resized it and moved the measured spot
 * under a held phone.
 */
const VIEWFINDER_FRACTION = 0.5;

// The reader takes the Y plane of a yuv frame.
const PIXEL_FORMAT = 'yuv';

export default function ScanScreen() {
  const router = useRouter();
  const insets = useSafeAreaInsets();
  const window = useWindowDimensions();
  const { setMachineId, services, target } = useApp();
  // The experiment's settings, from a deep link such as rpiledconfig://find?symbolMs=25&fps=60.
  const params = useLocalSearchParams<{ symbolMs?: string; fps?: string }>();
  const FPS = setting(params.fps, DEFAULT_FPS);
  const SYMBOL_MS = setting(params.symbolMs, DEFAULT_SYMBOL_MS);
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
  // VisionCamera refuses an fps outside the format's range, so the rate asked for is
  // clamped to what the format gives; the log says both.
  const fps = Math.min(FPS, format?.maxFps ?? FPS);
  const [cameraError, setCameraError] = useState<string | undefined>(undefined);
  const camera = useRef<Camera>(null);
  const [view, setView] = useState({ width: 0, height: 0 });

  // The native reader (modules/led-reader): it finds the LED anywhere in the frame and
  // decodes it on the camera's thread; this screen only shows its state. One per visit.
  // eslint-disable-next-line react-hooks/exhaustive-deps
  const reader = useMemo(() => createLedReader(SYMBOL_MS), []);
  // The last second of frame times, for the measured frame rate in the log.
  const frameTimes = useRef<number[]>([]);
  const [state, setState] = useState<LedReaderState | undefined>(undefined);
  // What the screen says: the reader's frames folded into where the user is (led/scanProgress.ts).
  const progress = useRef(new ScanProgress());
  const [scan, setScan] = useState<ScanView>(() => progress.current.update({ t: 0, ledSeen: false, coverage: 0 }));
  const [active, setActive] = useState(true);
  const [frameError, setFrameError] = useState<string | undefined>(
    reader ? undefined : 'the LED reader is not built into this app on this platform',
  );
  const lastLog = useRef(0);

  const onState = useRunOnJS((next: LedReaderState) => {
    setState(next);
    if (DIAGNOSTICS) {
      const times = frameTimes.current;
      times.push(next.t);
      while (times.length > 0 && next.t - times[0] > 1000) times.shift();
    }
    const view = progress.current.update(next);
    setScan((current) => (current.phase === view.phase && current.progress === view.progress && current.hint === view.hint ? current : view));
    if (DIAGNOSTICS && (next.t - lastLog.current >= 1000 || next.idHex)) {
      // `adb logcat -s ReactNativeJS` on Android: what the reader measured, once a second.
      lastLog.current = next.t;
      const times = frameTimes.current;
      const measuredFps = times.length > 1 ? ((times.length - 1) * 1000) / (times[times.length - 1] - times[0]) : 0;
      console.log(
        `[reader] t=${(next.t / 1000).toFixed(1)} s, ${measuredFps.toFixed(1)} fps measured (asked ${FPS}, format max ${format?.maxFps ?? '?'}, using ${fps}, T ${SYMBOL_MS} ms), LED seen ${next.ledSeen}, ${Math.floor(next.coverage * 100)}% seen, contrast ${next.contrast.toFixed(0)}, ` +
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
        if (next) onState(next);
      } catch (error) {
        onFrameError(error instanceof Error ? error.message : String(error));
      }
    },
    [reader, onState, onFrameError],
  );

  const lockOnReticle = useCallback(async () => {
    if (!camera.current || view.width === 0 || (device && !device.supportsFocus)) return;
    try {
      await camera.current.focus({ x: view.width / 2, y: view.height / 2 });
    } catch {
      // A camera that cannot focus on a point reads the LED all the same.
    }
  }, [device, view]);

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
          fps={fps}
          isActive={active}
          pixelFormat={PIXEL_FORMAT}
          frameProcessor={frameProcessor}
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
          <Progress value={scan.progress} label={scan.label} detail={scan.detail} tone={scan.phase === 'lost' ? 'warn' : 'accent'} />
          <Text style={styles.check} numberOfLines={2}>
            {result ? `${result.errors} wrong and ${result.erasures} unreadable bytes repaired.` : scan.hint}
          </Text>
          {frameError ? <Text style={styles.error}>The camera frames cannot be read on this phone: {frameError}</Text> : null}
          {result ? (
            <View style={styles.result}>
              <Text style={styles.resultId}>{result.idHex}</Text>
              <Text style={styles.resultDetail}>Fingerprint {fingerprint(result.idBytes)}.</Text>
            </View>
          ) : null}
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
