// What the app needs of the phone, why, and where each stands. The decisions are plain functions
// over answers, so they can be tested; the screen (screens/Permissions.tsx) only asks Android and
// shows them.
import { PermissionsAndroid, Platform } from 'react-native';

export type NeedId = 'bluetooth' | 'camera' | 'location';

export interface Need {
  id: NeedId;
  title: string;
  /** Why, in a sentence, for someone deciding. */
  why: string;
  /** Without it the app cannot set up a Pi. */
  required: boolean;
  /** All of these make it granted. */
  permissions: string[];
  /** Asked for with the others, and not needed for it to count as granted. */
  alsoRequest?: string[];
}

/** Granted; refused and can be asked again; or refused for good, which only Settings can undo. */
export type NeedStatus = 'granted' | 'denied' | 'blocked';

const P = {
  scan: 'android.permission.BLUETOOTH_SCAN',
  connect: 'android.permission.BLUETOOTH_CONNECT',
  camera: 'android.permission.CAMERA',
  fine: 'android.permission.ACCESS_FINE_LOCATION',
  coarse: 'android.permission.ACCESS_COARSE_LOCATION',
};

const CAMERA: Need = {
  id: 'camera',
  title: 'Camera',
  why: 'To read the blinking LED of the Pi you choose. Frames are read on this phone as they arrive; nothing is recorded, and no image leaves the phone.',
  required: true,
  permissions: [P.camera],
};

/** Android 12 brought Bluetooth permissions of its own; before it, scanning needs location. */
export function needsFor(androidVersion: number): Need[] {
  if (androidVersion >= 31) {
    return [
      {
        id: 'bluetooth',
        title: 'Nearby devices',
        why: 'To find the Pi over Bluetooth and talk to it. This is how the WiFi details reach it.',
        required: true,
        permissions: [P.scan, P.connect],
      },
      CAMERA,
      {
        id: 'location',
        title: 'Location (optional)',
        why: 'Android tells an app the name of the WiFi network the phone is on only with this. It is used to mark that network in the list, and your location is never read. Choose Precise; everything else works without it.',
        required: false,
        permissions: [P.fine],
        // Android 12 and later ignore a request for fine location made without coarse.
        alsoRequest: [P.coarse],
      },
    ];
  }
  return [
    {
      id: 'location',
      title: 'Location',
      why: 'This version of Android allows Bluetooth scanning, which is how the app finds the Pi, only with this. Your location is never read.',
      required: true,
      permissions: [P.fine],
    },
    CAMERA,
  ];
}

/** Where each need stands after a request's answers, by permission. */
export function statusesFrom(needs: Need[], answers: Record<string, string>): Record<string, NeedStatus> {
  const statuses: Record<string, NeedStatus> = {};
  for (const need of needs) {
    const results = need.permissions.map((permission) => answers[permission]);
    if (results.every((result) => result === 'granted')) statuses[need.id] = 'granted';
    else statuses[need.id] = results.includes('never_ask_again') ? 'blocked' : 'denied';
  }
  return statuses;
}

/** Whether the app can go on: every required need granted. */
export function canContinue(needs: Need[], statuses: Record<string, NeedStatus>): boolean {
  return needs.every((need) => !need.required || statuses[need.id] === 'granted');
}

/**
 * Whether to show the screen at launch: anything required is missing, or anything optional has
 * never been asked for. An optional need once declined is not asked for at every launch.
 */
export function showAtLaunch(needs: Need[], statuses: Record<string, NeedStatus>, askedBefore: boolean): boolean {
  if (!canContinue(needs, statuses)) return true;
  return !askedBefore && needs.some((need) => statuses[need.id] !== 'granted');
}

export function currentNeeds(): Need[] {
  return Platform.OS === 'android' ? needsFor(Number(Platform.Version)) : [];
}

/** Where each need stands now, without asking anything. */
export async function checkNeeds(needs: Need[]): Promise<Record<string, NeedStatus>> {
  const statuses: Record<string, NeedStatus> = {};
  for (const need of needs) {
    const granted = await Promise.all(need.permissions.map((permission) => PermissionsAndroid.check(permission as never)));
    statuses[need.id] = granted.every(Boolean) ? 'granted' : 'denied';
  }
  return statuses;
}

/** Asks Android for everything not yet granted, in one go. */
export async function requestNeeds(needs: Need[]): Promise<Record<string, NeedStatus>> {
  const permissions = needs.flatMap((need) => [...need.permissions, ...(need.alsoRequest ?? [])]);
  const answers = await PermissionsAndroid.requestMultiple(permissions as never[]);
  return statusesFrom(needs, answers as Record<string, string>);
}
