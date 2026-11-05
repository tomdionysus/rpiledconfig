// What to tell a person about the Bluetooth adapter, in the words of its real state rather than
// assuming it is off. Kept apart from react-native-ble-plx so it can be tested.

/** States no amount of waiting will change. */
export function isFinalAdapterState(state: string): boolean {
  return state === 'Unauthorized' || state === 'Unsupported';
}

export function adapterMessage(state: string): string {
  switch (state) {
    case 'PoweredOff':
      return 'Bluetooth is off. Turn it on and try again.';
    case 'Unauthorized':
      return "This app is not allowed to use Bluetooth. Allow it in the app's settings and try again.";
    case 'Unsupported':
      return 'This phone does not support Bluetooth LE, which the app needs.';
    case 'Resetting':
      return 'Bluetooth is restarting. Try again in a moment.';
    default:
      return 'Bluetooth did not become ready. Try again in a moment.';
  }
}
