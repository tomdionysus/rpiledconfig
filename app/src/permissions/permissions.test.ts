import { afterEach, describe, expect, it, vi } from 'vitest';
import { PermissionsAndroid, Platform } from '@/test/react-native';
import { canContinue, checkNeeds, currentNeeds, needsFor, requestNeeds, showAtLaunch, statusesFrom } from './permissions';

const SCAN = 'android.permission.BLUETOOTH_SCAN';
const CONNECT = 'android.permission.BLUETOOTH_CONNECT';
const CAMERA = 'android.permission.CAMERA';
const FINE = 'android.permission.ACCESS_FINE_LOCATION';
const COARSE = 'android.permission.ACCESS_COARSE_LOCATION';

describe('needsFor', () => {
  it('on Android 12 and later: Bluetooth and camera required, location optional', () => {
    const needs = needsFor(31);
    expect(needs.map((need) => [need.id, need.required])).toEqual([
      ['bluetooth', true],
      ['camera', true],
      ['location', false],
    ]);
  });

  it('asks for coarse location with fine, which Android 12 needs, without counting on it', () => {
    const location = needsFor(31).find((need) => need.id === 'location');
    expect(location?.permissions).toEqual([FINE]);
    expect(location?.alsoRequest).toEqual([COARSE]);
  });

  it('on Android 11 and earlier: location is what Bluetooth scanning needs, so it is required', () => {
    const needs = needsFor(30);
    expect(needs.map((need) => [need.id, need.required])).toEqual([
      ['location', true],
      ['camera', true],
    ]);
  });
});

describe('statusesFrom', () => {
  const needs = needsFor(31);

  it('needs every permission of a need for it to count', () => {
    const statuses = statusesFrom(needs, { [SCAN]: 'granted', [CONNECT]: 'denied', [CAMERA]: 'granted', [FINE]: 'granted' });
    expect(statuses).toEqual({ bluetooth: 'denied', camera: 'granted', location: 'granted' });
  });

  it('tells refused for good apart: only Settings can undo that', () => {
    const statuses = statusesFrom(needs, { [SCAN]: 'never_ask_again', [CONNECT]: 'denied', [CAMERA]: 'never_ask_again', [FINE]: 'denied' });
    expect(statuses).toEqual({ bluetooth: 'blocked', camera: 'blocked', location: 'denied' });
  });

  it('counts approximate location as not granted: the network name needs precise', () => {
    const statuses = statusesFrom(needs, { [SCAN]: 'granted', [CONNECT]: 'granted', [CAMERA]: 'granted', [FINE]: 'denied', [COARSE]: 'granted' });
    expect(statuses.location).toBe('denied');
  });
});

describe('canContinue and showAtLaunch', () => {
  const needs = needsFor(31);
  const all = { bluetooth: 'granted', camera: 'granted', location: 'granted' } as const;

  it('goes on without the optional one, and not without a required one', () => {
    expect(canContinue(needs, { ...all, location: 'denied' })).toBe(true);
    expect(canContinue(needs, { ...all, camera: 'denied' })).toBe(false);
    expect(canContinue(needs, { ...all, bluetooth: 'blocked' })).toBe(false);
  });

  it('shows nothing once everything is granted', () => {
    expect(showAtLaunch(needs, all, false)).toBe(false);
    expect(showAtLaunch(needs, all, true)).toBe(false);
  });

  it('shows at first launch, for what has never been asked', () => {
    expect(showAtLaunch(needs, { ...all, location: 'denied' }, false)).toBe(true);
  });

  it('does not ask again at every launch for the optional one once declined', () => {
    expect(showAtLaunch(needs, { ...all, location: 'denied' }, true)).toBe(false);
  });

  it('always shows when something required is missing, such as one taken away in Settings', () => {
    expect(showAtLaunch(needs, { ...all, camera: 'denied' }, true)).toBe(true);
  });
});

describe('asking Android', () => {
  afterEach(() => {
    Platform.OS = 'ios';
    Platform.Version = 17;
    vi.restoreAllMocks();
  });

  it('needs nothing off Android, and what its version needs on it', () => {
    expect(currentNeeds()).toEqual([]);
    Platform.OS = 'android';
    Platform.Version = 30;
    expect(currentNeeds()).toEqual(needsFor(30));
    Platform.Version = '33';
    expect(currentNeeds()).toEqual(needsFor(33));
  });

  it('checks without asking, a need granted only with all its permissions', async () => {
    const granted = new Set([SCAN, CAMERA]);
    const check = vi.spyOn(PermissionsAndroid, 'check').mockImplementation(async (permission) => granted.has(permission));
    expect(await checkNeeds(needsFor(31))).toEqual({ bluetooth: 'denied', camera: 'granted', location: 'denied' });
    expect(check.mock.calls.map(([permission]) => permission).sort()).toEqual([CAMERA, CONNECT, FINE, SCAN].sort());
  });

  it('asks for everything in one go, coarse location included, and reads the answers', async () => {
    const requestMultiple = vi.spyOn(PermissionsAndroid, 'requestMultiple').mockResolvedValue({
      [SCAN]: 'granted',
      [CONNECT]: 'granted',
      [CAMERA]: 'never_ask_again',
      [FINE]: 'denied',
      [COARSE]: 'granted',
    });
    expect(await requestNeeds(needsFor(31))).toEqual({ bluetooth: 'granted', camera: 'blocked', location: 'denied' });
    expect(requestMultiple).toHaveBeenCalledWith([SCAN, CONNECT, CAMERA, FINE, COARSE]);
  });
});
